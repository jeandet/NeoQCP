# Async Data Pipeline for NeoQCP

**Date:** 2026-03-14
**Status:** Draft
**Scope:** Generic async data transformation pipeline for QCPGraph2 and QCPColorMap2

## Problem

NeoQCP has two async resampling needs that share the same pattern:
- **QCPGraph2**: hierarchical two-level min/max resampling for large datasets (>10M points)
- **QCPColorMap2**: 2D grid resampling to screen resolution

Currently QCPColorMap2 has a bespoke `QCPColormapResampler` + `QCPResamplerScheduler`. QCPGraph2 has no async path at all. Beyond resampling, users may want arbitrary transforms (filtering, unit conversion, FFT) running off the GUI thread.

## Design

### Layer 1: QCPPipelineScheduler

Owned by `QCustomPlot`, shared by all pipelines.

- Wraps a `QThreadPool` with configurable max thread count (`setMaxPipelineThreads(int)`)
- Default: `std::max(1, QThread::idealThreadCount() / 2)`
- Two priority levels: `Heavy` and `Fast`
- Fast jobs always get priority over heavy jobs in the queue
- The scheduler is intentionally dumb — just a prioritized thread pool. All smart logic (coalescing, generation tracking, caching) lives in the pipeline.

### Layer 2: QCPAsyncPipelineBase

Non-templated base class. Owns all the pipeline machinery.

**State:**
- `mGeneration: std::atomic<uint64_t>` — incremented on every invalidation
- `mDisplayedGeneration: uint64_t` — generation of currently displayed result
- `mRunningGeneration: uint64_t` — generation currently executing
- `mPending: std::function<void()>` — coalesced next job (guarded by mutex)
- `mCache: std::any` — opaque, owned by the pipeline, moved into job lambdas (see Cache Lifecycle)
- `mKind: TransformKind` — `ViewportIndependent` or `ViewportDependent`
- `mScheduler: QCPPipelineScheduler*` — borrowed pointer

**Invalidation methods:**
- `onDataChanged()` — callable from any thread. Increments generation, resets cache to empty, submits heavy job.
- `onViewportChanged(ViewportParams)` — GUI thread. If `ViewportIndependent`: no-op. Otherwise increments generation, submits fast job (cache preserved).

**Coalescing:**
- At most one pending + one running job per pipeline. This invariant is enforced by the pipeline, not the pool — the pipeline only submits a new job to the pool when the previous one completes.
- `submit(priority, work)`: if a job is running, overwrite `mPending`. If idle, submit directly to scheduler.
- When a job completes: if `mPending` exists, run it immediately. Otherwise go idle.

**Result delivery:**
- Job completion emits `finished(uint64_t generation)` via `Qt::QueuedConnection` to GUI thread
- If `generation > mDisplayedGeneration`: swap result in, update displayed generation, trigger replot
- Stale but newer-than-displayed results are still displayed as interim (gives immediate visual feedback while latest job runs)
- Null result from transform means "no output for this generation" — pipeline does not update `mDisplayedGeneration` or trigger replot

**Replot trigger:**
- The plottable connects the pipeline's `finished` signal to its own slot, which calls `layer()->replot()`. The pipeline itself has no back-pointer to `QCustomPlot`.

**Activity signal:**
- `busyChanged(bool)` — emitted on idle↔working transitions (backlog: user-facing activity indicator)

### Layer 3: QCPAsyncPipeline<In, Out>

Thin typed template over `QCPAsyncPipelineBase`.

```cpp
template<typename In, typename Out>
class QCPAsyncPipeline : public QCPAsyncPipelineBase
```

**Transform signature:**
```cpp
using TransformFn = std::function<
    std::shared_ptr<Out>(const In& source,
                         const ViewportParams& viewport,
                         std::any& cache)>;
```

**Typed state:**
- `mSource: const In*` — raw pointer, user owns lifetime. Captured by value into job lambdas at submission time (no ownership transfer — user guarantees source stays alive while pipeline may read it).
- `mResult: std::shared_ptr<Out>` — latest displayed result
- `mPendingResult: std::shared_ptr<Out>` — awaiting swap on GUI thread
- `mTransform: TransformFn` — defaults to none (passthrough mode)

**API:**
- `setTransform(TransformKind, TransformFn)` — set transform and its kind
- `setSource(const In*)` — sets source, calls `onDataChanged()`
- `result() -> const Out*` — if no transform is set, returns `mSource` directly (passthrough, zero overhead). Otherwise returns `mResult.get()`.

**Concrete aliases:**
```cpp
using QCPGraphPipeline = QCPAsyncPipeline<QCPAbstractDataSource, QCPAbstractDataSource>;
using QCPColormapPipeline = QCPAsyncPipeline<QCPAbstractDataSource2D, QCPColorMapData>;
```

### ViewportParams

```cpp
struct ViewportParams {
    QCPRange keyRange;
    QCPRange valueRange;
    int plotWidthPx;
    int plotHeightPx;
    bool keyLogScale;
    bool valueLogScale;
};
```

Lightweight value type, captured by value on job submission.

### Cache Lifecycle

The cache (`std::any`) is owned by the pipeline but **moved** into the job lambda at submission time to avoid concurrent access. Sequence:

1. **Submit:** pipeline moves `mCache` into the lambda → `mCache` is now empty
2. **Transform runs:** the lambda owns the cache exclusively, no contention
3. **Job completes:** the job lambda bundles the cache into a result struct alongside the output and generation number. This struct is delivered to the GUI thread via the `finished` signal (`Qt::QueuedConnection`). The GUI thread's slot moves the cache back into `mCache` (under the mutex) before checking for pending jobs. If a pending job exists, it is submitted with the now-restored cache.
4. **`onDataChanged()`:** if no job is running, cache is simply reset to empty. If a job is running, the running job owns its copy — pipeline's `mCache` is already empty, and the pending job will start with an empty cache.

The transform casts the cache to whatever internal structure it needs. Example for the hierarchical graph resampler:

```cpp
struct GraphResamplerCache {
    QCPRange cachedKeyRange;
    std::shared_ptr<QCPAbstractDataSource> level1;  // 1M-point intermediate
};
```

### Lane Selection

The framework determines job priority based on what triggered submission:
- `onDataChanged()` → `Heavy` priority
- `onViewportChanged()` → `Fast` priority

The transform function itself is always the same — it uses its cache internally to decide how much work to do (full recompute vs fast re-extract).

## Data Flow

```
External thread              Pool thread              GUI thread
     │                            │                       │
 setData() #1                     │                       │
   │ gen=1, submit(heavy)──→ transform(gen=1)             │
     │                            │ running...            │
 setData() #2                     │                       │
   │ gen=2, mark pending          │                       │
     │                            │                       │
 setData() #3                     │                       │
   │ gen=3, overwrite pending     │                       │
     │                            │                       │
                              transform(gen=1) done       │
                              │ gen 1 < 3 but > displayed  │
                              └──finished(gen=1)──→ display as interim
                              │ pending exists, run it     │  replot()
                              transform(gen=3)─────────────│
                              │ done                       │
                              └──finished(gen=3)──→ swap final result
                                                     replot()
```

Viewport changes follow the same flow but originate in the GUI thread and submit with `Fast` priority. For `ViewportIndependent` transforms, viewport changes are no-ops.

## Thread Safety

### Mutex Scope

A single `QMutex mMutex` guards all mutable pipeline state: `mSource`, `mCache`, `mPending`, `mRunningGeneration`. The lock is held only for brief bookkeeping (pointer assignment, cache move, job submission/completion) — never during transform execution.

`mGeneration` is `std::atomic<uint64_t>` and does not require the mutex. `mDisplayedGeneration` is only accessed on the GUI thread (in the `finished` slot) and does not require synchronization.

### Contracts

- `setData()` / `setSource()` is callable from any thread. It acquires the mutex, stores the data pointer, resets the cache, and submits to the pool — no GUI thread round-trip. It must not touch Qt widgets, trigger repaints, or modify any GUI-thread state.
- **Zero-copy contract:** The pipeline never copies or owns the data source. The user guarantees the source remains valid and safe to read for the duration of any in-flight transform. The source pointer is captured by value into the job lambda at submission time.
- The pipeline never touches Qt widgets or GUI-thread state from the pool thread. Only `finished()` via `Qt::QueuedConnection` lands on the GUI thread.
- The cache is moved into the job lambda at submission time under the mutex — no concurrent access (see Cache Lifecycle).

### Destruction

When a plottable (and its pipeline) is destroyed while a job is in-flight on the pool:
- The pipeline destructor sets a `std::atomic<bool> mDestroyed` flag (shared with the job lambda via `std::shared_ptr<std::atomic<bool>>`)
- The job's completion callback checks the flag before delivering results — if set, it silently discards the result
- The destructor does not block waiting for the pool thread — it detaches cleanly

## Plottable Integration

### QCPGraph2
- Owns a `QCPGraphPipeline`
- `setData()` / `setDataSource()` → `mPipeline.setSource()`
- `draw()` reads from `mPipeline.result()`
- Synchronous adaptive sampling (`optimizedLineData`) remains — runs at render time on already-reduced pipeline output
- Built-in hierarchical resampling available via `graph->setResampling(true)`

### QCPColorMap2
- Owns a `QCPColormapPipeline`
- Replaces `QCPColormapResampler` + `QCPResamplerScheduler` — pipeline subsumes both
- Existing `qcp::algo2d::resample` becomes the default transform
- `draw()` reads from `mPipeline.result()`

### QCustomPlot (core)
- Owns the `QCPPipelineScheduler`
- Passes scheduler pointer to plottables on construction
- Exposes `setMaxPipelineThreads(int)`

### Viewport Notification

Each plottable connects to its key and value axes' `rangeChanged` signals. In its slot, it constructs a `ViewportParams` from current axis state and calls `mPipeline.onViewportChanged(params)`. The pipeline is decoupled from the axis system.

### Legacy Classes
- `QCPGraph`, `QCPColorMap`: untouched, no pipeline.

## Identity / Passthrough Default

When no transform is set, the pipeline is a no-op:
- `result()` returns the source directly
- No jobs are ever submitted to the pool
- Zero overhead for simple use cases

## Backlog

- **Activity indicator**: `busyChanged(bool)` signal is available. User-facing widget/overlay (spinner, progress bar) to show when a plot is computing. Not in initial scope.
- **Cancellation token**: Pass an `std::atomic<bool>` to the transform so it can early-exit when its generation is superseded. Currently, superseded jobs run to completion and their results are discarded — wasteful for very heavy transforms.
- **Append-only data sources**: `TransformKind` could gain a third value for data sources that only append (cache is extended, not cleared). Deferred until a concrete use case requires it.

## Migration

`QCPColormapResampler` and `QCPResamplerScheduler` become obsolete once `QCPColorMap2` is migrated to the pipeline. They can be removed after migration.

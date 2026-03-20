# QCPMultiGraph Async Resampling Design

## Goal

Add two-level async min/max resampling to QCPMultiGraph, matching QCPGraph2's performance for large datasets. All columns are resampled in a single pass over the shared key array.

## Background

QCPMultiGraph currently uses synchronous per-draw decimation via `getOptimizedLineData()`. With N columns, this does N traversals of the visible data range on every frame. For large datasets (>1M points with many columns), this blocks the UI thread.

QCPGraph2 solves this with a two-phase async pipeline:
- **L1**: bins the full dataset into ~100K min/max pairs (async, viewport-independent)
- **L2**: re-bins L1 over the current viewport into ~4x screen-width bins (sync at draw time)

QCPMultiGraph should use the same approach, but exploit the shared key array to bin all columns in a single pass.

## Architecture

### Pipeline

A single `QCPMultiGraphPipeline` (alias for `QCPAsyncPipeline<QCPAbstractMultiDataSource, QCPAbstractMultiDataSource>`) handles all columns. The pipeline transform is `ViewportIndependent` — it builds L1 over the full key range. L2 is computed lazily at draw time.

The L1 transform signature:
```cpp
[](const QCPAbstractMultiDataSource& src,
   const ViewportParams& vp,
   std::any& cache) -> std::shared_ptr<QCPAbstractMultiDataSource> {
    return buildL1CacheMulti(src, vp, cache); // returns nullptr, stores result in cache
}
```

Like QCPGraph2, the transform returns `nullptr` — L1 results are stored in the `std::any& cache`, not in the pipeline's `result()` pointer. `pipeline.result()` is unused; L1 cache is extracted via `pipeline.cache()` in `onL1Ready()`, and L2 is built manually at draw time.

### Threshold

The async pipeline activates when `sourceSize * columnCount >= kResampleThreshold` (i.e., total value reads exceed 10M). A minimum source size floor of 100K prevents pipeline overhead from exceeding the benefit for tiny datasets with many columns.

Below threshold: no transform installed, pipeline passes through the raw source. Draw uses `getOptimizedLineData()` for per-draw decimation (existing behavior).

### Two-Phase Resampling

**L1 (async, thread pool):**
- `binMinMaxMulti()` bins all columns in one pass over the shared key array
- For each source point: compute bin index from key once, then scatter min/max into all N column accumulators
- Parallel variant splits by bin ranges (disjoint writes, zero synchronization) — same strategy as existing `binMinMaxParallel()`. Partitioning uses `findBegin()`/`findEnd()` on the shared key array (identical to single-column case since keys are shared).
- Output: `MultiColumnBinResult` stored in pipeline cache via `std::any`
- ~100K bins, producing 200K key entries and 200K value entries per column
- **Log-scale keys**: L1 binning assumes uniform bin widths. If `keyLogScale` is true, L1 is skipped (returns nullptr). Draw falls back to raw data with `getOptimizedLineData()`.

**L2 (sync, at draw time):**
- Triggered when `mL2Dirty` is set (by viewport changes)
- Re-bins L1 cache per column into viewport-sized output (~4x screen width bins)
- Returns nullptr if `keyLogScale` is true (falls back to raw data)
- Produces a `QCPResampledMultiDataSource` implementing `QCPAbstractMultiDataSource`
- Draw iterates per component and calls `getLines(column, begin, end, ...)` on the resampled source for each visible column

### Data Flow

```
setDataSource(source)
  → pipeline.setSource(source)
  → if size * N >= threshold && size >= 100K: install L1 transform, trigger async job
  → else: no transform, pipeline passes through raw source

L1 async job (thread pool):
  → binMinMaxMulti(src, 0, srcSize, fullKeyRange, 100K bins, N columns)
  → one key-array traversal, N value reads per point
  → stores MultiColumnBinResult in pipeline cache
  → signals finished()

onL1Ready():
  → extracts cache from pipeline → mL1Cache
  → marks mL2Dirty = true
  → queues replot

draw():
  → if mL2Dirty && mL1Cache: rebuildL2(viewport)
  → picks data source: mL2Result ?? mDataSource
  → if no L2 result: uses getOptimizedLineData(column, ...) per component (sub-threshold fallback)
  → else: uses getLines(column, ...) on resampled source per component
  → iterates components, applies line style, draws

Viewport change (key axis rangeChanged only):
  → sets mL2Dirty = true (no async work)

dataChanged():
  → resets L1 cache, triggers new async L1 build
```

## New Types

### `MultiColumnBinResult` (in `graph-resampler.h`)

Flat memory layout for cache-friendly access during the scatter-min/max inner loop:

```cpp
struct MultiColumnBinResult {
    std::vector<double> keys;    // 2 * numBins (shared across columns)
    std::vector<double> values;  // N * 2 * numBins, column-major: values[col * stride + bin]
    int numColumns = 0;
    int stride() const { return static_cast<int>(keys.size()); }  // 2 * numBins
};
```

Column `c`, bin entry `i` is at `values[c * stride() + i]`. This keeps all bins for one column contiguous (good for L2 re-binning) while avoiding N separate heap allocations.

### `MultiGraphResamplerCache` (in `graph-resampler.h`)

```cpp
struct MultiGraphResamplerCache {
    MultiColumnBinResult level1;
    QCPRange cachedKeyRange;
    int sourceSize = 0;
    int columnCount = 0;
};
```

### `binMinMaxMulti()` / `binMinMaxMultiParallel()` (in `graph-resampler.h`)

Free functions alongside existing `binMinMax()`. Single pass over keys, scatter to N column accumulators. Parallel variant splits by bin ranges with disjoint writes — partitioning uses `findBegin()`/`findEnd()` on the shared key array.

### `QCPResampledMultiDataSource` (new file: `datasource/resampled-multi-datasource.h`)

Header-only wrapper around `MultiColumnBinResult` implementing `QCPAbstractMultiDataSource`. Provides `columnCount()`, `size()`, `keyAt()`, `valueAt(column, i)`, `getLines(column, ...)`, `findBegin()`/`findEnd()`, `keyRange()`, `valueRange()`.

### Pipeline alias (in `async-pipeline.h`)

```cpp
using QCPMultiGraphPipeline = QCPAsyncPipeline<QCPAbstractMultiDataSource, QCPAbstractMultiDataSource>;
```

## Changes to QCPMultiGraph

### New members

- `QCPMultiGraphPipeline mPipeline`
- `std::shared_ptr<MultiGraphResamplerCache> mL1Cache`
- `std::shared_ptr<QCPAbstractMultiDataSource> mL2Result`
- `bool mL2Dirty = false`
- `bool mNeedsResampling = false` — tracks whether source crossed the threshold

### Modified methods

- **`setDataSource()`**: passes source to pipeline, installs L1 transform if above threshold, resets L1 cache
- **`dataChanged()`**: re-checks threshold (source size may have changed), resets L1 cache, triggers pipeline rebuild if needed
- **`draw()`**: if `mL2Dirty`, rebuilds L2. Picks `mL2Result ?? mDataSource`. Below threshold or log-scale uses `getOptimizedLineData(column, ...)` per component, above threshold uses `getLines(column, ...)` on resampled source per component. Rest of draw loop unchanged.

### New private/protected methods

- **`onL1Ready()`**: extracts cache from pipeline, marks L2 dirty, queues replot
- **`rebuildL2(ViewportParams)`**: re-bins L1 per column into viewport-sized output. Returns nullptr if `keyLogScale` is true.
- **`onViewportChanged()`**: sets `mL2Dirty = true`
- **`pipelineBusy() const override`**: returns `mPipeline.isBusy()` (for busy indicator support)

### Constructor additions

- Creates pipeline with `parentPlot()->pipelineScheduler()`
- Connects key axis `rangeChanged` → `onViewportChanged()` (key axis only — value axis range does not affect min/max binning)
- Connects `pipeline.finished` → `onL1Ready()`
- Connects `pipeline.busyChanged` → `updateEffectiveBusy()` (busy indicator)

### Kept unchanged

- `mAdaptiveSampling` flag — used as sub-threshold draw-time fallback via `getOptimizedLineData()`
- Component visibility — all columns resampled always, visibility only affects draw
- Line style transforms, scatter drawing, selection, legend

## Export Support

PDF/pixmap export uses the same `runSynchronously()` pattern added for QCPColorMap2. If `draw()` detects export mode (`pmNoCaching`) and has no L1 cache, it runs the L1 transform synchronously, builds L2 inline, then proceeds with drawing.

## Testing

### Unit tests
- `binMinMaxMulti` produces correct per-column min/max for known data
- `binMinMaxMultiParallel` matches single-threaded results
- `QCPResampledMultiDataSource` correctly implements `QCPAbstractMultiDataSource` (columnCount, size, keyAt, valueAt, getLines, findBegin/End)
- L2 rebuild produces correct viewport-scoped output from L1 cache
- Log-scale keys: L1 and L2 return nullptr, draw falls back to raw data

### Integration tests
- Small dataset (below threshold): draw uses raw source with `getOptimizedLineData`
- Large dataset (above threshold): L1 builds async, L2 rebuilds on viewport change
- Threshold scales with column count: `size * N >= 10M`
- `dataChanged()` invalidates L1 and triggers rebuild
- Rapid `setDataSource()` / `setDataSource()` sequence: stale L1 from first source is discarded (pipeline generation counter)
- Hidden components: L1 includes all columns, visibility only affects draw
- Export path: `runSynchronously()` works for PDF/pixmap export

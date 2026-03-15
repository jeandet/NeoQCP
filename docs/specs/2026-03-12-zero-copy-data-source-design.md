# Zero-Copy Data Source for QCPGraph2

**Date:** 2026-03-12
**Status:** Draft
**Scope:** QCPGraph only, sorted data only

## Problem

NeoQCP (inherited from QCustomPlot) requires all plot data to flow through `QCPDataContainer<QCPGraphData>`, which stores `double key` + `double value` in an Array-of-Structures layout. Every `setData()` call copies user data into this internal format. This has two costs:

1. **Allocation + copy overhead** for large datasets (millions of points from sensors/instruments)
2. **API friction** — users must convert their containers (`std::vector<float>`, raw `float*`, `QVector<int>`, etc.) into `QVector<double>` pairs

## Goals

- Plot directly from user-owned containers without copying or converting data
- Support `std::vector`, `QVector`, `std::span`, raw pointers for any numeric type (`double`, `float`, `int`, etc.)
- Keep algorithms in native types (e.g., `float` math) until the final pixel-coordinate conversion
- Support both owning (library takes data) and non-owning (zero-copy view) modes
- Coexist with legacy `QCPGraph` — no breaking changes

## Non-Goals

- Unsorted data support (deferred — would need index building)
- Plottables other than graph (curves, bars, etc. — future work)
- GPU-side typed buffers (current RHI path receives `QPointF` output)
- Line styles other than `lsLine` (step, impulse — future work, will require revisiting the rendering method signatures)
- Incremental `addData()` appends (deferred — users create a new data source for now)

## Design

### Architecture Overview

Three layers:

```
QCPGraph2 (QObject, non-templated)
    |
    v
QCPAbstractDataSource (virtual interface, non-templated)
    |
    v
QCPSoADataSource<K,V> (template, holds/views typed data)
    |
    v
qcp::algo::* (free function templates, native-type algorithms)
```

The QObject boundary is non-templated. Template machinery lives entirely in the data source and algorithm layers. Virtual dispatch happens once per render frame (not per data point).

### Layer 1: Abstract Data Source

`QCPAbstractDataSource` — non-templated, non-QObject base class.

```cpp
class QCPAbstractDataSource {
public:
    virtual ~QCPAbstractDataSource() = default;

    virtual int size() const = 0;
    virtual bool empty() const { return size() == 0; }

    // Range queries (for axis auto-scaling)
    virtual QCPRange keyRange(bool& foundRange,
                              QCP::SignDomain sd = QCP::sdBoth) const = 0;
    virtual QCPRange valueRange(bool& foundRange,
                                QCP::SignDomain sd = QCP::sdBoth,
                                const QCPRange& inKeyRange = QCPRange()) const = 0;

    // Binary search on sorted keys (for visible range).
    // expandedRange=true includes one extra point beyond the boundary
    // (needed for correct line rendering at viewport edges).
    virtual int findBegin(double sortKey, bool expandedRange = true) const = 0;
    virtual int findEnd(double sortKey, bool expandedRange = true) const = 0;

    // Per-element access (slow path: selection, tooltips)
    virtual double keyAt(int i) const = 0;
    virtual double valueAt(int i) const = 0;

    // Processed outputs — graph calls these, never touches raw data.
    // Implementations run native-type algorithms internally,
    // cast to double/QPointF only at the pixel-coordinate output step.
    virtual QVector<QPointF> getOptimizedLineData(
        int begin, int end, int pixelWidth,
        QCPAxis* keyAxis, QCPAxis* valueAxis) const = 0;

    virtual QVector<QPointF> getLines(
        int begin, int end,
        QCPAxis* keyAxis, QCPAxis* valueAxis) const = 0;
};
```

Key point: the virtual methods for rendering (`getOptimizedLineData`, `getLines`) return `QVector<QPointF>`. The concrete template subclass implements them by calling free function templates with native types. No visitor pattern, no type enumeration — any `QCPSoADataSource<K,V>` that compiles is automatically supported.

### Layer 2: Typed SoA Data Source

`QCPSoADataSource<KeyContainer, ValueContainer>` — template, final, parameterized on container types.
Ownership and memory layout are properties of the container types themselves:
`std::vector<double>` owns, `std::span<const float>` views, `std::deque<int>` owns but is non-contiguous.

**Concepts:**

```cpp
template <typename C>
concept IndexableNumericRange = std::ranges::random_access_range<C>
    && std::is_arithmetic_v<std::ranges::range_value_t<C>>;

template <typename C>
concept ContiguousNumericRange = IndexableNumericRange<C>
    && std::ranges::contiguous_range<C>;
```

**Data source template:**

```cpp
template <IndexableNumericRange KeyContainer, IndexableNumericRange ValueContainer>
class QCPSoADataSource final : public QCPAbstractDataSource {
public:
    using K = std::ranges::range_value_t<KeyContainer>;
    using V = std::ranges::range_value_t<ValueContainer>;

    QCPSoADataSource(KeyContainer keys, ValueContainer values);

    // Typed access for algorithms
    const KeyContainer& keys() const;
    const ValueContainer& values() const;

    // QCPAbstractDataSource overrides
    int size() const override;
    double keyAt(int i) const override { return static_cast<double>(mKeys[i]); }
    double valueAt(int i) const override { return static_cast<double>(mValues[i]); }
    QCPRange keyRange(bool&, QCP::SignDomain sd = QCP::sdBoth) const override;
    QCPRange valueRange(bool&, QCP::SignDomain sd = QCP::sdBoth,
                        const QCPRange& inKeyRange = QCPRange()) const override;
    int findBegin(double sortKey, bool expandedRange = true) const override;
    int findEnd(double sortKey, bool expandedRange = true) const override;

    QVector<QPointF> getOptimizedLineData(
        int begin, int end, int pixelWidth,
        QCPAxis* keyAxis, QCPAxis* valueAxis) const override;

    QVector<QPointF> getLines(
        int begin, int end,
        QCPAxis* keyAxis, QCPAxis* valueAxis) const override;

private:
    KeyContainer mKeys;
    ValueContainer mValues;
};
```

One constructor, takes containers by value. Users move owning containers (`std::vector`, `QVector`) or copy cheap views (`std::span`). No special ownership logic — the container type determines semantics.

**Examples:**

```cpp
// Owning (move)
QCPSoADataSource<std::vector<double>, std::vector<float>> src(std::move(times), std::move(vals));

// Non-owning view
QCPSoADataSource<std::span<const double>, std::span<const float>> src(std::span{times}, std::span{vals});

// Non-contiguous (works, just no SIMD fast path)
QCPSoADataSource<std::deque<double>, std::deque<float>> src(std::move(keys), std::move(vals));
```

### Layer 3: Free Function Templates (Algorithms)

Header-only, in `qcp::algo` namespace. Operate on container references via concepts.
Cast to `double` only at the pixel-coordinate output step.

Algorithms are parameterized on container types. The compiler selects the best
overload via concept subsumption: contiguous containers get raw-pointer fast paths,
random-access containers get index-based paths.

```cpp
namespace qcp::algo {

// Binary search on sorted keys
template <IndexableNumericRange KC>
int findBegin(const KC& keys, double sortKey, bool expandedRange = true);

template <IndexableNumericRange KC>
int findEnd(const KC& keys, double sortKey, bool expandedRange = true);

// Range queries
template <IndexableNumericRange KC>
QCPRange keyRange(const KC& keys, QCP::SignDomain sd = QCP::sdBoth);

template <IndexableNumericRange KC, IndexableNumericRange VC>
QCPRange valueRange(const KC& keys, const VC& values,
                    QCP::SignDomain sd = QCP::sdBoth,
                    const QCPRange& inKeyRange = QCPRange());

// Key/value to pixel coords (the cast-to-double boundary)
template <IndexableNumericRange KC, IndexableNumericRange VC>
QVector<QPointF> linesToPixels(
    const KC& keys, const VC& values,
    int begin, int end,
    QCPAxis* keyAxis, QCPAxis* valueAxis);

// Adaptive sampling — reduces point count while preserving visual shape
template <IndexableNumericRange KC, IndexableNumericRange VC>
QVector<QPointF> optimizedLineData(
    const KC& keys, const VC& values,
    int begin, int end,
    int pixelWidth,
    QCPAxis* keyAxis, QCPAxis* valueAxis);

} // namespace qcp::algo
```

**Contiguous fast paths:** For algorithms where contiguity matters (e.g., range scans),
additional overloads constrained with `ContiguousNumericRange` can extract raw pointers
and enable auto-vectorization. The compiler picks the most constrained match automatically
via concept subsumption.

**Optimization opportunities** to explore during extraction from `QCPGraph`:
- Contiguous overloads: raw pointer access enables SIMD auto-vectorization for scans
- Pre-allocated output buffers instead of `QVector<QPointF>` temporaries
- Branchless binary search for large datasets
- Template-based NaN elimination: `int` types can skip NaN checks entirely (`if constexpr`)
- Adaptive sampling in pixel space to reduce per-point key→pixel mapping

### Layer 4: QCPGraph2

QObject-based plottable. Holds a `shared_ptr<QCPAbstractDataSource>`. No template code in the hot path.

```cpp
class QCPGraph2 : public QCPAbstractPlottable, public QCPPlottableInterface1D {
    Q_OBJECT
public:
    explicit QCPGraph2(QCPAxis* keyAxis, QCPAxis* valueAxis);

    // Data source (owns or shares)
    void setDataSource(std::unique_ptr<QCPAbstractDataSource> source);
    void setDataSource(std::shared_ptr<QCPAbstractDataSource> source);
    QCPAbstractDataSource* dataSource() const;

    // Convenience: owning, from any movable container pair.
    // Constructs QCPSoADataSource<std::decay_t<KC>, std::decay_t<VC>>.
    // Constrained: both KC and VC must satisfy IndexableNumericRange.
    template <IndexableNumericRange KC, IndexableNumericRange VC>
    void setData(KC&& keys, VC&& values);

    // Convenience: non-owning view from raw pointers
    template <typename K, typename V>
    void viewData(const K* keys, const V* values, int count);

    // Convenience: non-owning view from spans
    template <typename K, typename V>
    void viewData(std::span<const K> keys, std::span<const V> values);

    // Notify that viewed data has been mutated externally
    void dataChanged();

    // QCPPlottableInterface1D — delegates to mDataSource
    int dataCount() const override;
    double dataMainKey(int index) const override;
    double dataMainValue(int index) const override;
    double dataSortKey(int index) const override;
    QCPRange dataValueRange(int index) const override;
    QPointF dataPixelPosition(int index) const override;
    bool sortKeyIsMainKey() const override { return true; }
    QCPDataSelection selectTestRect(const QRectF& rect, bool onlySelectable) const override;
    int findBegin(double sortKey, bool expandedRange) const override;
    int findEnd(double sortKey, bool expandedRange) const override;

    // QCPAbstractPlottable overrides
    QCPPlottableInterface1D* interface1D() override { return this; }
    double selectTest(const QPointF& pos, bool onlySelectable, QVariant* details) const override;

protected:
    void draw(QCPPainter* painter) override;
    void drawLegendIcon(QCPPainter* painter, const QRectF& rect) const override;
    QCPRange getKeyRange(bool& foundRange, QCP::SignDomain inSignDomain) const override;
    QCPRange getValueRange(bool& foundRange, QCP::SignDomain inSignDomain,
                           const QCPRange& inKeyRange) const override;

private:
    std::shared_ptr<QCPAbstractDataSource> mDataSource;
};
```

- `shared_ptr` allows multiple graphs to share one data source
- `setDataSource(unique_ptr)` converts to shared
- Template convenience methods construct the right `QCPSoADataSource` and call `setDataSource()`
- `draw()` calls `getOptimizedLineData()` when adaptive sampling is enabled (default for large datasets) or `getLines()` when disabled, receives `QVector<QPointF>`, draws lines/scatters
- `dataChanged()` triggers replot for non-owning views where user mutates data in place
- Implements `QCPPlottableInterface1D` for selection support, delegating to `mDataSource->keyAt()`/`valueAt()`/`findBegin()`/`findEnd()`/`size()`

### Ownership & Lifetime

Ownership is determined by the container type, not by special constructors:

1. **Owning** — `graph->setData(std::move(vec_keys), std::move(vec_values))` — containers are moved into the data source
2. **Shared** — `graph->setDataSource(shared_source)` — multiple graphs share one data source via `shared_ptr`
3. **Non-owning view** — `graph->viewData(std::span{keys}, std::span{vals})` — user manages lifetime, must call `dataChanged()` after mutation, must not free data while the graph exists

No automatic dirty-tracking of external memory. Same philosophy as `std::span`.

**Thread safety:** Non-owning views provide no synchronization. Concurrent modification of the viewed data during `replot()` is undefined behavior. Users requiring thread-safe updates should use the owning mode with `setData()` from the GUI thread.

### File Layout

```
src/
  datasource/
    abstract-datasource.h        # QCPAbstractDataSource
    soa-datasource.h             # QCPSoADataSource<K,V> (header-only)
    algorithms.h                 # qcp::algo free function templates (header-only)
  plottables/
    plottable-graph2.h           # QCPGraph2 declaration + template methods
    plottable-graph2.cpp         # QCPGraph2 non-template implementation
```

Umbrella header `src/qcp.h` includes the new files. `meson.build` adds `plottable-graph2.cpp` to sources.

### What Stays Untouched

- `QCPGraph`, `QCPDataContainer`, `QCPGraphData` — fully preserved
- All existing plottables, layout system, selection system
- RHI compositing pipeline, paint buffers, layers

### Testing

- **Algorithm unit tests**: feed raw `double[]`, `float[]`, `int[]` arrays to `qcp::algo` functions, verify outputs against known results
- **Data source unit tests**: ownership semantics (owning vs view), range queries, binary search correctness, span constructor
- **QCPGraph2 integration tests**: create plot with various container/type combinations, replot, verify axis ranges and no crashes
- **Benchmarks**: compare `QCPGraph` vs `QCPGraph2` with large `float` datasets — measure `setData` time and render time

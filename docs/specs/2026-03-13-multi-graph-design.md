# QCPMultiGraph — Multi-Component Graph Plottable

**Date:** 2026-03-13
**Status:** Draft
**Scope:** Multi-component line graphs with shared key axis, per-component selection, grouped legend

## Problem

Scientific time series data is often multi-component — a magnetic field vector has Bx, By, Bz sharing the same timestamps. In SciQLopPlots, this is handled by `SciQLopLineGraph` creating N separate `QCPGraph` instances. This has three costs:

1. **N copies of the key array** — each QCPGraph stores its own timestamps
2. **N independent adaptive sampling passes** over identical keys
3. **N legend entries** with no grouping — the legend doesn't reflect that these components belong together

The current SciQLopPlots wrapper also forces a copy when splitting a 2D numpy array into per-component 1D arrays.

## Goals

- Single plottable that renders N value columns against one shared key axis
- Zero-copy from contiguous multi-column data (e.g. 2D numpy array with stride)
- One adaptive sampling pass shared across all components
- Per-component style (pen, scatter, visibility)
- Per-component selection (click Bx → only Bx highlights)
- Grouped legend item (collapsible, both group and component rows selectable)

## Non-Goals

- GPU-batched rendering (natural follow-up, not in initial implementation)
- Channel fill between components (future work)
- Mixed line styles per component (shared line style for now — step/impulse apply to all)
- Incremental append (users create a new data source, same as QCPGraph2)

## Required Base Class Changes

Two methods on `QCPAbstractPlottable` must become virtual:

- **`addToLegend(QCPLegend*)`** — currently non-virtual, hardcodes `new QCPPlottableLegendItem`. Must be virtual so `QCPMultiGraph` can create a `QCPGroupLegendItem` instead.
- **`removeFromLegend(QCPLegend*)`** — currently non-virtual, uses `itemWithPlottable()` which only finds `QCPPlottableLegendItem`. Must be virtual so `QCPMultiGraph` can find and remove its `QCPGroupLegendItem`.

These are source-compatible changes — no existing code breaks, just gains a vtable entry.

## Design

### Architecture Overview

```
QCPMultiGraph (QObject, non-templated plottable)
    |
    v
QCPAbstractMultiDataSource (virtual interface, non-templated)
    |
    v
QCPSoAMultiDataSource<KC, VC> (template, holds/views typed data)
    |
    v
qcp::algo::* (free function templates, native-type algorithms)
```

Same layering as QCPGraph2's data source design. The QObject boundary is non-templated. Template machinery lives in the data source layer. Virtual dispatch happens once per column per render frame.

### Layer 1: Abstract Multi Data Source

`QCPAbstractMultiDataSource` — mirrors `QCPAbstractDataSource2D` pattern, minus the y-axis dimension.

```cpp
class QCPAbstractMultiDataSource {
public:
    virtual ~QCPAbstractMultiDataSource() = default;

    virtual int columnCount() const = 0;
    virtual int size() const = 0;
    virtual bool empty() const { return size() == 0; }

    // Shared key axis
    virtual double keyAt(int i) const = 0;
    virtual QCPRange keyRange(bool& found, QCP::SignDomain sd = QCP::sdBoth) const = 0;
    virtual int findBegin(double sortKey, bool expandedRange = true) const = 0;
    virtual int findEnd(double sortKey, bool expandedRange = true) const = 0;

    // Per-column value access
    virtual double valueAt(int column, int i) const = 0;
    virtual QCPRange valueRange(int column, bool& found,
                                QCP::SignDomain sd = QCP::sdBoth,
                                const QCPRange& inKeyRange = QCPRange()) const = 0;

    // Bulk output for drawing
    virtual QVector<QPointF> getOptimizedLineData(
        int column, int begin, int end, int pixelWidth,
        QCPAxis* keyAxis, QCPAxis* valueAxis) const = 0;

    virtual QVector<QPointF> getLines(
        int column, int begin, int end,
        QCPAxis* keyAxis, QCPAxis* valueAxis) const = 0;
};
```

### Layer 2: Template Multi Data Source

`QCPSoAMultiDataSource<KC, VC>` — one key container + N value containers of the same type.

```cpp
template <IndexableNumericRange KC, IndexableNumericRange VC>
class QCPSoAMultiDataSource final : public QCPAbstractMultiDataSource {
public:
    using K = std::ranges::range_value_t<KC>;
    using V = std::ranges::range_value_t<VC>;

    QCPSoAMultiDataSource(KC keys, std::vector<VC> valueColumns);

    int columnCount() const override { return static_cast<int>(mValues.size()); }
    int size() const override { return static_cast<int>(std::ranges::size(mKeys)); }

    // Key methods delegate to qcp::algo::findBegin/findEnd/keyRange on mKeys
    // Value methods delegate to qcp::algo per column
    // getOptimizedLineData/getLines reuse qcp::algo::optimizedLineData/linesToPixels

private:
    KC mKeys;
    std::vector<VC> mValues;  // one container per column
};
```

Supports the same owning/non-owning patterns as `QCPSoADataSource`:
- Owning: `QCPSoAMultiDataSource<std::vector<double>, std::vector<double>>`
- Non-owning: `QCPSoAMultiDataSource<std::span<const double>, std::span<const float>>`

**Zero-copy from numpy:** Column-major (Fortran-order) 2D arrays map directly to `std::span` per column — true zero-copy. Row-major (C-order) arrays have non-contiguous columns, so each column requires either a copy or a strided view type (not plain `std::span`). In practice, SciQLopPlots can request column-major layout from numpy (`np.asfortranarray`) or accept the column-copy cost, which is still cheaper than the current approach of copying both keys and values N times.

### Layer 3: Component Descriptors

Each value column has associated style and selection state:

```cpp
struct QCPGraphComponent {
    QString name;
    QPen pen;
    QPen selectedPen;
    QCPScatterStyle scatterStyle;
    QCPDataSelection selection;
    bool visible = true;
};
```

`selectedPen` defaults to a derived version of `pen` (e.g. brighter + thicker) but can be set explicitly.

### QCPMultiGraph

```cpp
class QCPMultiGraph : public QCPAbstractPlottable, public QCPPlottableInterface1D {
    Q_OBJECT
public:
    enum LineStyle { lsNone, lsLine, lsStepLeft, lsStepRight, lsStepCenter, lsImpulse };

    explicit QCPMultiGraph(QCPAxis* keyAxis, QCPAxis* valueAxis);
    ~QCPMultiGraph() override;

    // --- Data source ---
    void setDataSource(std::unique_ptr<QCPAbstractMultiDataSource> source);
    void setDataSource(std::shared_ptr<QCPAbstractMultiDataSource> source);
    QCPAbstractMultiDataSource* dataSource() const;
    void dataChanged();

    // Convenience: owning
    template <IndexableNumericRange KC, IndexableNumericRange VC>
    void setData(KC&& keys, std::vector<VC>&& valueColumns);

    // Convenience: non-owning spans
    template <typename K, typename V>
    void viewData(std::span<const K> keys, std::vector<std::span<const V>> valueColumns);

    // --- Components ---
    int componentCount() const;
    QCPGraphComponent& component(int index);
    const QCPGraphComponent& component(int index) const;

    void setComponentNames(const QStringList& names);
    void setComponentColors(const QList<QColor>& colors);
    void setComponentPens(const QList<QPen>& pens);

    // Per-component value access (for tracers/tooltips)
    double componentValueAt(int column, int index) const;

    // --- Shared style ---
    LineStyle lineStyle() const;
    void setLineStyle(LineStyle style);
    bool adaptiveSampling() const;
    void setAdaptiveSampling(bool enabled);
    int scatterSkip() const;
    void setScatterSkip(int skip);

    // --- Per-component selection ---
    QCPDataSelection componentSelection(int index) const;
    void setComponentSelection(int index, const QCPDataSelection& sel);

    // --- QCPPlottableInterface1D ---
    int dataCount() const override;
    double dataMainKey(int index) const override;
    double dataSortKey(int index) const override;
    double dataMainValue(int index) const override;  // always column 0
    QCPRange dataValueRange(int index) const override;
    QPointF dataPixelPosition(int index) const override;
    bool sortKeyIsMainKey() const override { return true; }
    QCPDataSelection selectTestRect(const QRectF& rect, bool onlySelectable) const override;
    int findBegin(double sortKey, bool expandedRange) const override;
    int findEnd(double sortKey, bool expandedRange) const override;

    // --- QCPAbstractPlottable ---
    QCPPlottableInterface1D* interface1D() override { return this; }
    double selectTest(const QPointF& pos, bool onlySelectable,
                      QVariant* details = nullptr) const override;
    QCPRange getKeyRange(bool& foundRange, QCP::SignDomain sd) const override;
    QCPRange getValueRange(bool& foundRange, QCP::SignDomain sd,
                           const QCPRange& inKeyRange) const override;

    // --- Legend (requires base class virtual addToLegend/removeFromLegend) ---
    bool addToLegend(QCPLegend* legend) override;
    bool removeFromLegend(QCPLegend* legend) const override;

protected:
    void draw(QCPPainter* painter) override;
    void drawLegendIcon(QCPPainter* painter, const QRectF& rect) const override;

    // Selection events (override QCPLayerable virtuals)
    void selectEvent(QMouseEvent* event, bool additive, const QVariant& details,
                     bool* selectionStateChanged) override;
    void deselectEvent(bool* selectionStateChanged) override;

private:
    std::shared_ptr<QCPAbstractMultiDataSource> mDataSource;
    QVector<QCPGraphComponent> mComponents;
    LineStyle mLineStyle = lsLine;
    bool mAdaptiveSampling = true;
    int mScatterSkip = 0;
};
```

**Component lifecycle:** `setDataSource()` resizes `mComponents` to match `columnCount()`. If growing, new components get default pens from the theme's color cycle. If shrinking, excess components are dropped. If unchanged, styles are preserved.

**`getValueRange()`**: returns the union of all visible components' value ranges — needed for axis auto-scaling.

**`dataMainValue(int index)`**: always returns column 0's value, regardless of component visibility. This is a stable contract for generic `QCPPlottableInterface1D` consumers. For per-component values, use `componentValueAt(column, index)`.

**Inherited `mSelection` vs per-component selection:** The authoritative selection state lives in `mComponents[i].selection`. The inherited `QCPAbstractPlottable::mSelection` is kept as the union of all component selections (updated in `selectEvent`/`deselectEvent`) so that base-class code like `selected()` and the `selectionChanged` signal work correctly. Code that only needs "is anything selected?" uses the base `mSelection`; code that needs per-component granularity uses `componentSelection(i)`.

### Rendering

**`draw()` — QPainter path:**

1. Compute visible range once: `begin = findBegin(keyRange.lower)`, `end = findEnd(keyRange.upper)`
2. For each visible component:
   a. Get line data via `dataSource->getOptimizedLineData(column, begin, end, pixelWidth, ...)` — adaptive sampling is applied per column but shares the same key range
   b. Apply step/impulse transform if `mLineStyle != lsLine`
   c. Split into selected/unselected segments using the component's `QCPDataSelection`
   d. Draw unselected segments with `component.pen`, selected with `component.selectedPen`
   e. Draw scatter points if scatter style is set

The key-to-pixel transform is computed once and reused. The adaptive sampling operates on the same key range for all columns.

**Export path (PDF/SVG/pixmap):** Same `draw()` method, no special handling.

**GPU path (future):** All N polylines could be batched into a single vertex buffer with per-vertex color. One draw call. Not in scope for initial implementation.

### Selection

**`selectTest(pos, onlySelectable, details)`:**
- Iterates all visible components
- For each, computes distance from `pos` to nearest line segment
- Returns the minimum distance across all components
- Encodes `{componentIndex, dataIndex}` as a QVariantMap in `details`

**`selectTestRect(rect, onlySelectable)`:**
- For each visible component, finds data points within `rect`
- Stores per-component results in `mComponents[i].selection`
- Returns the union (for QCPPlottableInterface1D compatibility)

**`selectEvent(event, additive, details)`:**
- Reads component index from `details`
- Updates only `mComponents[componentIndex].selection`
- If `additive`, adds to existing selection; otherwise replaces

**`deselectEvent()`:** Clears selection on all components.

**Tracer/tooltip support:** `componentValueAt(column, index)` provides explicit per-component value access. The caller reads the component index from `selectTest()`'s details QVariant, then queries the value directly.

### Legend — `QCPGroupLegendItem`

```cpp
class QCPGroupLegendItem : public QCPAbstractLegendItem {
    Q_OBJECT
public:
    QCPGroupLegendItem(QCPLegend* parent, QCPMultiGraph* multiGraph);

    QCPMultiGraph* multiGraph() const;
    bool expanded() const;
    void setExpanded(bool expanded);

protected:
    void draw(QCPPainter* painter) override;
    QSize minimumOuterSizeHint() const override;
    void selectEvent(QMouseEvent* event, bool additive,
                     const QVariant& details, bool* selectionStateChanged) override;
    void deselectEvent(bool* selectionStateChanged) override;

private:
    QCPMultiGraph* mMultiGraph;
    bool mExpanded = false;
};
```

**Collapsed** (default): one row — short horizontal line segments in each component's color + group name. Example: `[——][——][——] B field`

**Expanded**: group name on first row, then one indented sub-row per component with its color swatch and name:
```
▾ B field
   —— Bx
   —— By
   —— Bz
```

**Interactions:**
- Disclosure triangle (▸/▾) toggles expanded/collapsed
- Group row and component sub-rows are all selectable via the inherited `selectable` property
- Click on group row: selects/deselects all components
- Click on component sub-row: selects/deselects that component
- `selectEvent()` encodes what was hit (group vs component index) in the details QVariant

**Layout approach:** `QCPGroupLegendItem` is a single "fat" legend item that occupies one cell in the `QCPLegend` grid. When expanded, it internally renders multiple sub-rows within its own cell — the grid itself does not gain additional items. `minimumOuterSizeHint()` returns a larger size when expanded, triggering a legend relayout. This is simpler than dynamically adding/removing child items to the legend grid.

`QCPMultiGraph::addToLegend()` creates a `QCPGroupLegendItem` instead of the default `QCPPlottableLegendItem`. `removeFromLegend()` searches legend items by type (`QCPGroupLegendItem`) and matching `multiGraph()` pointer.

### Manual Test

Added to the existing manual test application. Two plots side by side:

- **Left plot**: 10 separate `QCPGraph2` instances, each with 100k points, different colors, standard legend entries
- **Right plot**: 1 `QCPMultiGraph` with 10 components, same 100k points, same colors, grouped legend entry

Both use the same synthetic data (10 sine waves with different phases and amplitudes). Interactive: drag to pan, scroll to zoom, click to select.

Provides visual correctness check (should look identical) and a qualitative feel for the performance difference.

## File Layout

```
src/datasource/abstract-multi-datasource.h     — QCPAbstractMultiDataSource
src/datasource/soa-multi-datasource.h           — QCPSoAMultiDataSource<KC,VC>
src/plottables/plottable-multigraph.h            — QCPMultiGraph, QCPGraphComponent
src/plottables/plottable-multigraph.cpp          — QCPMultiGraph implementation
src/layoutelements/layoutelement-legend-group.h  — QCPGroupLegendItem
src/layoutelements/layoutelement-legend-group.cpp — QCPGroupLegendItem implementation
tests/manual/mainwindow.cpp                      — side-by-side comparison test
```

## Dependencies

- `QCPAbstractDataSource` concepts (`IndexableNumericRange`, etc.) from the zero-copy data source design
- `qcp::algo::*` free function templates from QCPGraph2
- Existing `QCPAbstractPlottable`, `QCPPlottableInterface1D`, `QCPAbstractLegendItem` base classes

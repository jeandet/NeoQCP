# Interactive Items & Utilities — Design Spec

Features migrated/inspired from SciQLopPlots into NeoQCP as generic, reusable primitives.

## 1. Span Items

### QCPItemVSpan

Interactive vertical span marking a key-axis range, spanning the full value-axis height.

**Positions (2):**
- `lowerEdge` — left edge, `setTypeX(ptPlotCoords)` on key axis, `setTypeY(ptAxisRectRatio)` fixed at 0
- `upperEdge` — right edge, `setTypeX(ptPlotCoords)` on key axis, `setTypeY(ptAxisRectRatio)` fixed at 1

Positions inherit `QCPItemAnchor`, so they also serve as anchors. The Y coordinates are fixed internally and not user-settable.

**Anchors (1):** `center` — midpoint between edges, computed in `anchorPixelPosition()`

**Convenience API:**
- `setRange(QCPRange)` / `range() const` — set/get both edges at once
- `setMovable(bool)` / `movable() const` — own property on the span item (not inherited, QCPAbstractItem has no such flag)

**Rendering (`draw()`):**
- Filled rectangle between the two key-axis coordinates, full axis rect height
- Two vertical border lines at the edges

**Hit testing (`selectTest()`):**
- Checks border proximity first (priority over fill)
- Stores a `QCPSpanSelectionDetails` struct in `QVariant* details` containing the hit edge index (0 = lowerEdge, 1 = upperEdge, -1 = fill)
- Fill hit only if `brush` is non-`Qt::NoBrush`

**Interaction:**
- Drag via `mousePressEvent`/`mouseMoveEvent`/`mouseReleaseEvent` overrides on the span item
- `mousePressEvent` accepts the event if hit, stores which edge/fill was grabbed (from `selectTest` details)
- `mouseMoveEvent` converts pixel delta to axis-coord delta via `pixelToCoord`, moves the grabbed edge or whole span
- Respects `movable()` flag — if false, press events are ignored
- Inverted ranges (lower > upper) are allowed, `draw()` normalizes for rendering

**Delete key handling:**
- Add `deleteRequested()` signal to `QCPAbstractItem` (generic, any item can use it)
- Override `QCustomPlot::keyPressEvent()` to emit `deleteRequested()` on all selected items when Delete/Backspace is pressed
- No span-specific coupling in core — the signal lives on the base class

**Styling:**
- `pen` / `selectedPen` — fill rectangle outline
- `brush` / `selectedBrush` — fill
- `borderPen` / `selectedBorderPen` — edge lines (defaults to `pen` if unset)

**Signals:**
- `rangeChanged(QCPRange newRange)`
- `deleteRequested()`

**Files:** `src/items/item-vspan.h`, `src/items/item-vspan.cpp`

### QCPItemHSpan

Identical to QCPItemVSpan but swaps key/value axis. Marks a value-axis range, full key-axis width.

**Positions (2):** `lowerEdge`, `upperEdge` — `setTypeY(ptPlotCoords)` on value axis, `setTypeX(ptAxisRectRatio)` fixed at 0/1

**Anchors (1):** `center`

**Signals:** same as QCPItemVSpan

**Files:** `src/items/item-hspan.h`, `src/items/item-hspan.cpp`

### QCPItemRSpan

Rectangle span — both axes constrained, 4 draggable edges.

**Positions (4):** `leftEdge`, `rightEdge`, `topEdge`, `bottomEdge` — all `ptPlotCoords`

Each position stores one meaningful coordinate (the constrained axis), the other is derived from the axis rect extent.

**Anchors (5):** `topLeft`, `topRight`, `bottomRight`, `bottomLeft`, `center`

Corner anchors are computed from the intersection of adjacent edge positions. No corner positions — corners are not independently draggable (deferred).

**Hit testing:** checks all 4 edges first, then fill. Details struct reports which edge was hit.

**Interaction:**
- Single edge grabbed → that edge moves along its axis
- Fill grabbed → whole rectangle translates on both axes

**Convenience API:**
- `setKeyRange(QCPRange)` / `keyRange() const`
- `setValueRange(QCPRange)` / `valueRange() const`
- `setMovable(bool)` / `movable() const`

**Signals:**
- `keyRangeChanged(QCPRange newRange)`
- `valueRangeChanged(QCPRange newRange)`
- `deleteRequested()`

**Files:** `src/items/item-rspan.h`, `src/items/item-rspan.cpp`

### Implementation notes (all spans)

- Inherit directly from `QCPAbstractItem` — no shared base class
- Edges drawn directly in `draw()`, not composed from separate item objects
- One item = one layerable = one layer assignment
- Constructor auto-registers with QCustomPlot (standard QCP pattern)
- Clipping to axis rect via inherited `setClipToAxisRect()` / `setClipAxisRect()`

## 2. QCPItemRichText

Extends `QCPItemText` with HTML rendering via `QTextDocument`.

**Inherits:** `QCPItemText` (1 position, 8 anchors — all inherited)

**Additional API:**
- `setHtml(const QString&)` — sets HTML content via `QTextDocument`, recomputes bounding rect
- `html() const` — accessor

**Interaction with inherited `setText()`:** `setHtml()` and `setText()` are independent paths. `draw()` uses the `QTextDocument` if HTML has been set (non-empty), otherwise falls back to `QCPItemText::draw()`. Calling `setText()` clears the HTML and vice versa.

**Rendering:**
- `draw()` override uses `QTextDocument::drawContents()` when HTML is active
- Background rect, padding, rotation inherited from `QCPItemText`

**`anchorPixelPosition()` override:** accounts for bounding rect differences from text document sizing.

No new signals or interaction — purely a richer text renderer.

**Files:** `src/items/item-richtext.h`, `src/items/item-richtext.cpp`

## 3. QCPDataLocator

Utility class (not a QObject, not an item) that finds the nearest data point on any plottable at a given pixel position.

**API:**
```cpp
class QCPDataLocator {
public:
    void setPlottable(QCPAbstractPlottable* plottable);
    bool locate(const QPointF& pixelPos);

    bool isValid() const;
    double key() const;
    double value() const;
    double data() const;  // z-value for colormaps, NaN otherwise
    int dataIndex() const;
    QCPAbstractPlottable* plottable() const;
    QCPAbstractPlottable* hitPlottable() const;  // for QCPMultiGraph: which sub-graph was hit
};
```

**Supported plottable types** (detected via `qobject_cast`):
- `QCPGraph` — legacy `QCPDataContainer<QCPGraphData>` iterator access
- `QCPGraph2` — `QCPAbstractDataSource` interface (`keyAt(i)`, `valueAt(i)`)
- `QCPCurve` — legacy iterator access
- `QCPColorMap` — returns key, value, and z (cell data)
- `QCPColorMap2` — same as above via new data source
- `QCPMultiGraph` — locates on nearest constituent graph, `hitPlottable()` reports which sub-graph

**Strategy:** prefers `QCPAbstractDataSource` when available, falls back to `QCPDataContainer` iterators for legacy plottables. Uses plottable `selectTest()` with details to get data index.

**Edge cases:**
- Empty plottable → `locate()` returns false, `isValid()` returns false
- NaN data → skipped during nearest-point search

**Files:** `src/data-locator.h`, `src/data-locator.cpp`

## 4. QCPAxisRect::calculateAutoMargin() visibility

Change `calculateAutoMargin(QCP::MarginSide)` from `protected` to `public`.

One-line change in `src/layoutelements/layoutelement-axisrect.h`. Eliminates the `UnProtectedQCPAxisRect` static_cast hack in SciQLopPlots' `VPlotsAlign`.

## File summary

| File | Type |
|---|---|
| `src/items/item-vspan.h/.cpp` | New |
| `src/items/item-hspan.h/.cpp` | New |
| `src/items/item-rspan.h/.cpp` | New |
| `src/items/item-richtext.h/.cpp` | New |
| `src/data-locator.h/.cpp` | New |
| `src/layoutelements/layoutelement-axisrect.h` | Modified (1 line) |
| `src/qcp.h` | Modified (add new includes) |
| `meson.build` | Modified (add new sources) |

## Testing

- Unit tests for each span type: create, set range, verify `range()`, move edges, delete
- Inverted range (lower > upper) — verify draw normalizes, `range()` preserves order as-given
- Span on log-scale axis — verify coord conversion works correctly
- Unit test for QCPItemRichText: create, set HTML, verify rendering produces non-empty pixmap; verify `setText()` clears HTML
- Unit test for QCPDataLocator: create graph with known data, locate at known pixel, verify key/value; test with empty plottable
- Manual test: visual verification of span interaction (drag edges, drag body, delete key)

# QCPOverlay Design Spec

## Summary

A widget-level overlay for displaying status messages, errors, and informational text on top of a QCustomPlot widget. Paints above all plot content, survives `clear()`, and supports collapsing.

## Motivation

SciQLop's virtual product debugger needs to show status/error overlays on plots. The current approach uses child QWidgets, which fight Qt's widget stacking order and get destroyed by plot lifecycle operations. A native overlay in NeoQCP solves these issues by painting within the existing layer compositing pipeline.

## Class Hierarchy and Ownership

`QCPOverlay` inherits `QCPLayerable` directly — not a layout element, not an item.

```
QCPLayerable
  └── QCPOverlay
```

Owned by `QCustomPlot` as a dedicated member (`mOverlay`), created lazily on first `overlay()` call. Lives on a dedicated `"notification"` layer added as the topmost `lmBuffered` layer. Not stored in `mItems` or `mPlotLayout`, so `clear()` has no effect on it.

> Note: The layer is named `"notification"` rather than `"overlay"` because QCustomPlot already creates a default `"overlay"` layer used by items like tracers. The `"notification"` layer sits above it.

**Layer ordering guarantee**: The `"notification"` layer is re-ordered to the top during `replot()` if user code has added layers after it via `addLayer()`. This ensures the overlay always composites above everything.

**Ownership and destruction**: `mOverlay` is QObject-parented to `QCustomPlot`. During `~QCustomPlot()`, `qDeleteAll(mLayers)` destroys the layer and the overlay removes itself from it via `QCPLayerable`'s destructor. The QObject parent-child cleanup handles the rest — no explicit `delete mOverlay` needed.

## API

```cpp
class QCPOverlay : public QCPLayerable
{
    Q_OBJECT
public:
    enum Level { Info, Warning, Error };
    enum SizeMode { Compact, FitContent, FullWidget };
    enum Position { Top, Bottom, Left, Right };

    void showMessage(const QString& text, Level level = Info,
                     SizeMode sizeMode = Compact, Position position = Top);
    void clearMessage();

    void setCollapsible(bool enabled);
    bool isCollapsible() const;
    void setCollapsed(bool collapsed);
    bool isCollapsed() const;

    void setOpacity(qreal opacity);  // 0.0–1.0, default 1.0
    qreal opacity() const;

    void setFont(const QFont& font);
    QFont font() const;

signals:
    void messageChanged(const QString& text, QCPOverlay::Level level);
    void collapsedChanged(bool collapsed);
};
```

- `showMessage()` is the single entry point — sets text, level, size mode, and position. Triggers `replot(rpQueuedReplot)`.
- `clearMessage()` hides the overlay (named to avoid confusion with `QCustomPlot::clear()`).
- Default colors derived from Level: Info=green, Warning=amber, Error=red.
- Plain text only, with level-based coloring (no rich text / inline formatting).

## Rendering

`QCPOverlay::draw(QCPPainter*)`:

1. Bail early if no message text or if exporting (see Export section).
2. Compute rect based on size mode and position:
   - `Compact`: single line height + padding, anchored to the chosen edge.
   - `FitContent`: text bounding rect with word wrap within available widget dimension, anchored to edge.
   - `FullWidget`: entire `mParentPlot->rect()`.
3. Draw background: filled rounded rect with level-derived color, multiplied by opacity.
4. Draw text: white text, `AlignLeft | AlignVCenter` for Compact, `TextWordWrap` for FitContent/FullWidget.
5. Draw collapse handle (if collapsible): chevron icon (▼/▲ or ◀/▶ depending on position) at the trailing edge. When collapsed, only chevron + first line shown regardless of SizeMode.

For Left/Right positions, text is rotated: Left = bottom-to-top (+90°), Right = top-to-bottom (-90°).

All pixel sizes (padding, collapse handle) are in logical pixels, scaled by `mParentPlot->bufferDevicePixelRatio()` for HiDPI displays.

`applyDefaultAntialiasingHint(QCPPainter*)`: enables text antialiasing.

`clipRect()`: returns `mParentPlot->viewport()` (default behavior, correct for all size modes since the overlay draws within widget bounds).

No custom shaders — pure QPainter on the overlay's paint buffer, composited by the existing RHI pipeline.

## Export Behavior

The overlay is a debug/status UI element and is **excluded from exports**. `draw()` bails early when the painter mode is `pmVectorized` or `pmNoCaching` (the modes used by `savePdf()`, `savePng()`, `toPixmap()`, etc.).

## Mouse Interaction

**Click pass-through behavior depends on size mode:**

- `Compact` / `FitContent`: `selectTest()` returns 0 only on the collapse handle rect (~20×20 logical px). All other clicks pass through to the plot beneath (returns -1).
- `FullWidget`: `selectTest()` returns 0 on the entire overlay rect, blocking all interaction with content behind it. This is intentional — a full-widget error overlay should prevent accidental interaction with the plot.

`mousePressEvent()`: if hit is on the collapse handle, toggles collapsed state, emits `collapsedChanged()`, triggers `replot(rpQueuedReplot)`.

No dragging, no selection, no other interaction. The overlay does not participate in QCustomPlot's selection system.

## Integration with QCustomPlot

Changes to `QCustomPlot`:

- `core.h`: add `QCPOverlay* mOverlay{nullptr}` member and `QCPOverlay* overlay()` accessor.
- `core.cpp`: `overlay()` lazily constructs `QCPOverlay`, creates the `"notification"` layer as topmost `lmBuffered`, and assigns the overlay to it.

No changes to `clear()`, `initialize()`, or `render()`. Minor addition in `replot()`: ensure the overlay layer is topmost (re-order if needed).

New files:

- `src/overlay.h` / `src/overlay.cpp` — QCPOverlay class.
- Added to meson build and `qcp.h` umbrella header.

## Testing

Qt Test-based tests in `tests/auto/`:

- `testOverlayShowClear`: `showMessage()` → visible with correct text/level, `clearMessage()` → hidden.
- `testOverlaySurvivesClear`: `showMessage()` → `plot->clear()` → overlay still has its message.
- `testOverlayCollapse`: set collapsible, toggle collapsed, verify `collapsedChanged` signal fires and state toggles.
- `testOverlaySizeModes`: show message with each size mode, verify computed rect (Compact = single line height, FitContent = text bounding rect, FullWidget = widget rect).
- `testOverlayPositions`: show message at each edge (Top/Bottom/Left/Right), verify rect is anchored correctly.
- `testOverlayOpacity`: verify getter/setter roundtrip.
- `testOverlayStaysTopmost`: create overlay, call `addLayer()`, verify overlay layer index is still highest after `replot()`.
- `testOverlayClickPassThrough`: in Compact mode, verify clicks outside the collapse handle reach plottables beneath.
- `testOverlayBlocksClicksFullWidget`: in FullWidget mode, verify clicks are blocked from reaching plottables.
- `testOverlayShowMessageTriggersReplot`: verify `showMessage()` triggers a replot.

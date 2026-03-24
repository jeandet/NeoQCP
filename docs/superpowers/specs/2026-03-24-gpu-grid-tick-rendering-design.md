# GPU Grid Line & Tick Mark Rendering

## Problem

During pan/zoom on macOS, the axes layer repaints every frame via QPainter→CoreGraphics. Grid lines and tick marks account for ~120 `drawLine()` calls per frame (10 major ticks + 50 subticks × 2 axes). On Retina displays, this generates a ~8MB staging buffer upload per frame. Since tick positions rarely change between consecutive pan frames, this work is almost entirely wasted.

## Solution

Render grid lines, zero-lines, subtick lines, and tick marks via QRhi shaders instead of QPainter. Reuse the existing span shader infrastructure (`span.vert` + `plottable.frag`). Store tick positions as data-space coordinates in a vertex buffer; the shader maps data→pixel using axis range UBO parameters. During pan, only the UBO updates (64 bytes per axis rect) — geometry rebuilds only when the tick set changes.

## Architecture

### New class: `QCPGridRhiLayer`

Located in `src/painting/grid-rhi-layer.h/.cpp`. Follows the `QCPSpanRhiLayer` pattern:

- One instance per `QCustomPlot`, lazily created (`mGridRhiLayer`)
- Groups geometry by `QCPAxisRect` for scissor clipping
- Reuses `span.vert` + `plottable.frag` (same vertex format, UBO layout, blending)
- Renders in **two passes** at different layers (see Render Loop Integration)

### Vertex format

Same as spans: 11 floats per vertex (dataCoord, color, extrudeDir, extrudeWidth, isPixel).

**Grid lines** (horizontal axis example — vertical line at tick value `t`):
- `dataCoord = (t, pixTop)` / `(t, pixBot)` — X is data space, Y is pixel space
- `isPixel = (0, 1)` — shader maps X via `coordToPixel()`, passes Y through
- `extrudeDir = (1, 0)`, `extrudeWidth = halfPenWidth`
- Generated via `appendBorder()` (6 vertices per line)

**Zero-line**: same as grid lines but with `mZeroLinePen` color. Emitted only when `range.lower < 0 && range.upper > 0` and `mZeroLinePen.style() != Qt::NoPen`. The zero-line is at data coordinate 0.

**Tick marks** (horizontal axis, bottom example — short line at tick value `t`):
- `dataCoord = (t, baseline - tickLengthOut)` / `(t, baseline + tickLengthIn)` — X is data space, Y is pixel space
- `isPixel = (0, 1)` — same mapping as grid lines
- `extrudeDir = (1, 0)`, `extrudeWidth = halfPenWidth`

Subticks use the same layout with different Y extents and potentially different color.

### Draw groups

Three draw group types per axis rect:
1. **Grid lines + zero-line** — scissored to axis rect interior, rendered on `"grid"` layer
2. **Tick marks + subtick marks** — unscissored, rendered on `"axes"` layer

Grid lines and tick marks live on different layers in the current QPainter path (`QCPGrid` draws on `"grid"`, `QCPAxisPainterPrivate` draws ticks on `"axes"`). The GPU path preserves this ordering by splitting the draw groups into two render passes. Each draw group stores its target layer name so `render()` can filter by layer.

### Dirty detection

Cache the tick value set (`mTickVector` + `mSubTickVector` values, not pixel positions) and visual properties (pen colors, pen widths, subgrid visibility, zero-line pen) after each geometry rebuild. Mark geometry dirty when:
- Tick values change (detected by comparison during `uploadResources()`)
- Any grid visual property changes (call `markGeometryDirty()` from property setters in `QCPGrid` and `QCPAxis`)
- Axis rect bounds change (detected by comparing cached bounds, same as `QCPSpanRhiLayer`)

### UBO

Identical to span UBO (64 bytes, std140):
```
width, height, yFlip, dpr,
keyRangeLower, keyRangeUpper, keyAxisOffset, keyAxisLength, keyLogScale,
valRangeLower, valRangeUpper, valAxisOffset, valAxisLength, valLogScale,
_pad0, _pad1
```
Updated every frame with current axis ranges (64-byte dynamic buffer update per axis rect).

**Axis selection**: each draw group's UBO is parameterized from the actual parent axis of the grid, not hardcoded to `atBottom`/`atLeft`. A horizontal-axis grid line's UBO uses that axis as key and the perpendicular axis as value. This avoids the hardcoding limitation present in `QCPSpanRhiLayer`.

### Exclusions

- **Polar grids** (`QCPPolarGrid`): excluded from the GPU path. Polar grids use angular/radial axes that don't inherit from `QCPAxis` and have no `QCPAxisRect`. The `rebuildGeometry()` function only processes `QCPGrid` instances attached to standard `QCPAxis` objects.
- **Log scale constraint**: the span shader clamps coordinates to `max(coord, 1e-30)` before `log()`. This is safe because `QCPAxisTickerLog` only produces positive tick values. Custom tickers on log axes that produce zero/negative values will be silently clamped — this matches the span shader behavior.

## Data flow

### Per-frame during pan (tick set unchanged — common case)

```
setRange() → replot() → setupTickVectors()
  → compare tick values to cached set → MATCH
  → skip geometry rebuild
  → uploadResources(): update UBO only (64 bytes per axis rect)
  → render(): setGraphicsPipeline, draw vertices (already on GPU)
```

### When a tick scrolls in/out (rare)

```
setRange() → replot() → setupTickVectors()
  → compare tick values to cached set → MISMATCH
  → rebuildGeometry(): fill staging vertex array
  → uploadResources(): upload vertex buffer (~32 KB) + update UBO
  → render(): draw
```

## Render loop integration

In `core.cpp render()`, the layer loop iterates layers in order (background → grid → main → axes → legend → overlay). The grid RHI layer renders at **two points** to match the QPainter draw ordering:

```
for each layer:
    1. composite paint buffer texture (existing)
    2. draw colormap RHI layers (existing)
    3. draw plottable RHI layers (existing)
    4. IF layer == "main": draw span RHI layer (existing)
    5. IF layer == "grid": draw grid RHI layer — grid lines only (NEW)
    6. IF layer == "axes": draw grid RHI layer — tick marks only (NEW)
```

This preserves the current Z-ordering: grid lines behind plottables, tick marks in front.

### Lifecycle

- Created lazily on first access (like `mSpanRhiLayer`)
- `ensurePipeline()` + `uploadResources()` called in `render()` before the layer loop
- `releaseResources()` deletes it
- `resize()` calls `invalidatePipeline()`

## QPainter skip logic

### `QCPGrid::draw()`

Early-return when RHI grid layer is active and not exporting:

```cpp
if (mParentAxis->parentPlot()->gridRhiLayer()
    && !painter->modes().testFlag(QCPPainter::pmVectorized)
    && !painter->modes().testFlag(QCPPainter::pmNoCaching))
    return;
```

### `QCPAxisPainterPrivate::draw()`

Skip tick mark and subtick `drawLine()` loops when RHI grid layer is active (same guard). Baseline, line endings, tick labels, and axis label remain QPainter-drawn.

## Export path

PDF/SVG/pixmap export uses QPainter directly (`pmVectorized` / `pmNoCaching` modes). The QPainter skip guards ensure `QCPGrid::draw()` and tick mark drawing still execute for export. No changes to export behavior.

## Performance

### Typical 2-axis plot (10 major ticks, 50 subticks per axis)

| Metric | Before (QPainter) | After (GPU) |
|---|---|---|
| Per-frame draw calls | ~120 `drawLine()` | 2 GPU draw calls + 64-byte UBO |
| Per-frame CPU→GPU upload | ~8 MB staging buffer | 0 (texture eliminated for grid) |
| Geometry rebuild (when ticks change) | N/A (always full) | ~32 KB vertex buffer |
| CoreGraphics overhead | ~120 CG path operations | 0 |

## Files changed

| File | Change |
|---|---|
| `src/painting/grid-rhi-layer.h` | New — `QCPGridRhiLayer` class declaration |
| `src/painting/grid-rhi-layer.cpp` | New — implementation |
| `src/core.h` | Add `mGridRhiLayer` member, `gridRhiLayer()` accessor |
| `src/core.cpp` | Lifecycle, render loop integration at `"grid"` and `"axes"` layers |
| `src/axis/axis.cpp` | `QCPGrid::draw()` early-return, `QCPAxisPainterPrivate::draw()` skip ticks, `markGeometryDirty()` from property setters |
| `meson.build` | Add new source files |

## Files NOT changed

- Shaders (`span.vert`, `plottable.frag`) — reused as-is
- `QCPSpanRhiLayer` — independent, no shared state
- Export paths — guarded by painter mode flags
- Tick label rendering — stays QPainter
- Polar plot code — explicitly excluded

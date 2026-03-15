# GPU-Accelerated Plottable Rendering

## Problem

NeoQCP's rendering pipeline rasterizes all plot content via QPainter into CPU-side QImage buffers. The GPU only composites the pre-rendered layer textures. QPainter's raster engine is slow for thick lines and large fills — the cost is proportional to the number of pixels covered, with no optimization path. This is the dominant bottleneck when curves fill most of the screen.

## Solution

Add a GPU rendering path for `QCPGraph` and `QCPCurve` line strokes and fills. Plottable geometry is extruded into triangle strips on the CPU, uploaded as vertex buffers, and rendered directly via QRhi shaders — bypassing QPainter entirely for the hot path.

Other plottable types (bars, financial, statistical box), scatter symbols, and text remain on QPainter. Export paths (PDF, PNG, etc.) are unaffected and continue using QPainter.

## Scope and Fallbacks

The GPU path applies only when ALL of these conditions are met:
- Parent `QCustomPlot` has an active QRhi (not an export path)
- Pen style is `Qt::SolidLine` (dashed/dotted lines fall back to QPainter)
- Fill type is baseline fill via `getFillPolygon()` (channel fills via `getChannelFillPolygon()` fall back to QPainter)
- Plottable is `QCPGraph` or `QCPCurve`

`QCPCurve` fills (inline in its `draw()`, not routed through a `drawFill()` method) remain on QPainter for this iteration.

When falling back to QPainter, the plottable draws into the layer's staging QImage as before — no behavior change.

## Architecture

### Rendering Paths

```
Existing layers (axes, grid, legend, text):
  QPainter → QImage staging → GPU texture → composite quad

Plottable GPU geometry (NEW):
  dataToLines() → CPU triangle extrusion → vertex buffer → GPU draw
```

The plottable GPU geometry renders directly into the QRhiWidget render target during `render()`, interleaved in the correct Z-order with the texture-composited QPainter layers.

### Triangle Extrusion (Lines)

Given `QVector<QPointF>` pixel coordinates from the existing `dataToLines()` pipeline:
- For each segment, compute the perpendicular normal
- Offset left/right by `penWidth / 2` to produce quad corners
- At joins, use miter (extend normals to intersection), with a miter limit that falls back to bevel
- Line caps: flat (truncate at endpoints), matching QPainter default
- NaN gaps produce degenerate triangles to break the strip

Output: triangle strip, 2 vertices per input point (left and right edge).

### Fill Tessellation

`getFillPolygon()` returns a closed polygon: baseline point → curve data points → baseline point. This has a known structure: one flat edge (the baseline) and one irregular edge (the curve).

Tessellate using **trapezoid decomposition**: for each pair of consecutive curve points, emit a quad (2 triangles) connecting those points to their projections on the baseline. This is always correct regardless of curve shape — no convexity requirement, no self-intersection risk.

Channel fills (`getChannelFillPolygon()`) are excluded from GPU rendering and fall back to QPainter.

### Draw Order Per Plottable

Two draw calls per plottable: fill triangles first, then stroke strip on top (matches current QPainter behavior). Each draw call is tracked as `(bufferOffset, vertexCount)`.

### Vertex Format

`(x, y, r, g, b, a)` — 24 bytes per vertex. Position in pixel coordinates (logical, pre-DPR), color per-vertex allows different plottables in the same buffer without pipeline state changes.

### Clipping

GPU-rendered geometry must be clipped to the plottable's axis rect (matching the QPainter `clipRect()` behavior). This is done via **scissor rect**: the pipeline is created with `QRhiGraphicsPipeline::UsesScissor`, and `cb->setScissor()` is called per-plottable with the axis rect bounds (scaled by DPR to physical pixels).

### Shaders

**`plottable.vert`**:
- Input attributes: `(x, y)` pixel position + `(r, g, b, a)` color
- Push constant: `(width, height, yFlip, dpr)` — physical viewport size, Y-direction sign, device pixel ratio
- NDC transform:
  ```glsl
  float ndcX = (position.x * dpr / width) * 2.0 - 1.0;
  float ndcY = yFlip * ((position.y * dpr / height) * 2.0 - 1.0);
  gl_Position = vec4(ndcX, ndcY, 0.0, 1.0);
  ```
  Where `yFlip = -1.0` for Y-up NDC (OpenGL), `1.0` for Y-down (Metal/D3D)
- Passes color to fragment shader

**`plottable.frag`**:
- Receives interpolated color, outputs directly (premultiplied alpha)

### Pipeline

- Premultiplied alpha blending: `src=One, dst=OneMinusSrcAlpha`
- `UsesScissor` flag enabled
- Dynamic vertex buffer, re-uploaded only when geometry changes
- Triangle strip topology (for lines), triangle list topology (for fills) — or both packed as triangle list for simplicity
- Push constant block: `{float width, height, yFlip, dpr}` (16 bytes)
- MSAA via `QRhiWidget::setSampleCount()` for antialiasing (global — affects composite pipeline too, but harmlessly)

### New Class: `QCPPlottableRhiLayer`

One instance per `QCPLayer` that contains GPU-renderable plottables. Manages:
- Vertex staging buffer (CPU-side `QVector<float>`)
- `QRhiBuffer*` dynamic vertex buffer (GPU-side)
- Per-plottable draw entries: `{offset, fillVertexCount, strokeVertexCount, scissorRect}`
- Pipeline and push constant state (shared across plottables on the same layer)
- Dirty flag for upload tracking

Lifecycle: created lazily when a layer first contains a GPU-eligible plottable, destroyed in `releaseResources()`.

### Integration Points

**`drawPolyline()`** in `plottable1d.h` — the shared bottleneck for Graph and Curve. When GPU mode is active and pen is solid, writes extruded triangles to the layer's `QCPPlottableRhiLayer` geometry buffer instead of calling `painter->drawLine/drawPolyline`.

**`drawFill()`** in `plottable-graph.cpp` — when GPU mode is active and fill is baseline type, tessellates and writes to the same buffer instead of calling `painter->drawPolygon`. Channel fills fall back to QPainter.

**GPU mode detection**: plottable checks `parentPlot()->rhi() != nullptr` AND `pen().style() == Qt::SolidLine`. Export paths (where `QCPPainter` has `pmVectorized` mode) always use QPainter.

**Selection**: selected plottables use the selection decorator's pen/brush colors, which map to different per-vertex RGBA values in the GPU path. No structural change — just different color values during extrusion.

**Scatters and text** remain on QPainter in the staging QImage.

## Data Flow

### During `replot()`

```
1. setupPaintBuffers()              — existing, no change
2. for each layer:
     for each GPU-eligible plottable on this layer:
       plottable->getLines()        — existing, produces QVector<QPointF>
       extrudeLines()               — NEW, produces triangle strip vertices
       tessellateFill()             — NEW, produces fill triangles
       → vertices accumulated in layer's QCPPlottableRhiLayer staging buffer
     layer->drawToPaintBuffer()     — existing, draws scatters/text/fallback plottables
3. update()                         — schedule render
```

### During `render()`

```
1. Upload dirty QPainter staging textures       — existing
2. Upload dirty plottable vertex buffers        — NEW
3. Begin render pass
4. For each layer in order:
     composite QPainter texture quad            — existing
     if layer has QCPPlottableRhiLayer:
       bind plottable pipeline
       set push constants (viewport, yFlip, dpr)
       for each plottable draw entry:
         set scissor rect (plottable's clipRect)
         draw fill (offset, fillVertexCount)
         draw stroke (offset, strokeVertexCount)
5. End render pass
```

### Dirty Tracking

Plottable vertex buffer is only re-uploaded when `replot()` regenerates geometry. Between frames with no data change, no upload occurs.

### Resize

Vertex data is in pixel coordinates — regenerated on resize. Already handled because `resizeEvent` triggers `replot()`.

### Memory

One dynamic vertex buffer per GPU layer. For line strokes: 2 vertices per input point × 24 bytes. For fills: ~2N vertices for N data points (trapezoid decomposition). Typical graph with 10k visible points: ~10k × 4 vertices × 24 bytes ≈ ~1MB. Negligible.

## Files

### New

| File | Purpose |
|------|---------|
| `src/painting/plottable-rhi-layer.h/.cpp` | `QCPPlottableRhiLayer`: per-layer GPU resources, geometry accumulation, vertex buffer, pipeline, rendering |
| `src/painting/line-extruder.h/.cpp` | Polyline → triangle strip (miter joins, bevel fallback). Baseline polygon → trapezoid triangles for fills |
| `src/painting/shaders/plottable.vert` | Position + color vertex shader with pixel-to-NDC transform and DPR scaling |
| `src/painting/shaders/plottable.frag` | Passthrough premultiplied-alpha color fragment shader |

### Modified

| File | Change |
|------|--------|
| `src/plottables/plottable1d.h` | `drawPolyline()`: branch on GPU mode (solid pen + RHI active), call extruder |
| `src/plottables/plottable-graph.cpp` | `drawFill()`: branch on GPU mode for baseline fills only. `drawLinePlot()`: delegates to `drawPolyline()` change |
| `src/plottables/plottable-curve.cpp` | `drawCurveLine()`: branch on GPU mode for solid-pen lines only |
| `src/core.h` | Add per-layer `QCPPlottableRhiLayer` management |
| `src/core.cpp` | `initialize()`: create plottable pipeline. `render()`: draw plottable geometry with scissor rects. `releaseResources()`: cleanup. `replot()`: coordinate geometry accumulation and upload |
| `src/painting/shaders/embed_shaders.py` | Handle two new shader files |
| `meson.build` | Add new source files and shader build targets |

### Not Modified

Data containers, plottable base class, axis code, layout code, items, legend — all unchanged.

## Future Work

- If CPU triangle extrusion becomes a bottleneck (millions of points), move extrusion to vertex shader (approach B) or use instanced rendering (approach C)
- Shader-based antialiasing as alternative to MSAA
- GPU path for dashed/dotted pen styles (gap insertion in triangle strip)
- GPU path for channel fills (requires general polygon tessellation)
- GPU path for QCPCurve fills
- GPU-accelerated scatter rendering
- GPU-accelerated colormap rendering
- Integration with Qt Canvas Painter (Qt 6.11+) if it stabilizes

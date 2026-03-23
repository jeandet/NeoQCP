# GPU-Native Span Rendering — Design Spec

**Date:** 2026-03-23
**Status:** Draft
**Scope:** QCPItemVSpan, QCPItemHSpan, QCPItemRSpan

## Problem

Span items (VSpan, HSpan, RSpan) currently render via QPainter into CPU-side QImage staging buffers. Every pan/zoom triggers a full layer repaint and multi-megapixel texture re-upload to the GPU. On macOS Metal with Retina displays, this texture upload is a known bottleneck (~7 MB per layer at 2x DPR).

Spans are geometrically trivial (axis-aligned filled rectangles + border lines) and typically 10–100 in count. Their pixel positions are a pure function of axis ranges, making them ideal candidates for GPU-native rendering where pan/zoom only requires a uniform buffer update (a few floats) instead of a full texture re-upload.

## Goals

- **Zero texture upload during pan/zoom** — span positions computed entirely on the GPU via axis range uniforms
- **Support all three span types** — VSpan, HSpan, RSpan from day one
- **Support linear and log axes** — coord-to-pixel transform in vertex shader handles both
- **Preserve mouse interaction** — hit testing, drag, resize stay CPU-side (pure math on data coordinates)
- **Preserve export paths** — `savePdf()`, `toPixmap()`, etc. use existing QPainter fallback
- **Semi-transparent fill** — premultiplied alpha blending for overlapping spans over curves
- **Automatic layer assignment** — no API change; spans are GPU-native transparently

## Non-Goals

- Custom axis types beyond linear/log (can be added later)
- GPU-native rendering for other item types (text, line, rect, etc.)
- Animated spans or gradient fills

## Architecture

### New class: `QCPSpanRhiLayer`

A new RHI layer class that manages all GPU-native span rendering, analogous to `QCPPlottableRhiLayer` (line geometry) and `QCPColormapRhiLayer` (textured quads).

Lives in `src/painting/span-rhi-layer.h/.cpp`.

**Ownership:** `QCustomPlot` owns a single `QCPSpanRhiLayer` instance, lazily created when the first span is constructed.

**Responsibilities:**
- Accumulates span geometry (fill quads + border line quads) in a vertex buffer
- Maintains per-axis-rect uniform buffers with axis range parameters
- Manages QRhi pipeline, shader resource bindings, and draw calls
- Rebuilds vertex buffer only when span geometry changes (add/remove/move/restyle)
- Updates uniform buffer every frame with current axis ranges (cheap)

### Data flow

```
Span items (VSpan/HSpan/RSpan)
  │
  │  registerSpan()/unregisterSpan() on construct/destruct
  │  markGeometryDirty() on edge move or color change
  │
  ▼
QCPSpanRhiLayer
  │  owns: vertex buffer (data-space coords), uniform buffers, pipeline
  │
  │  on render():
  │    if geometryDirty → rebuild vertex buffer, upload
  │    always → update uniform buffer with current axis ranges
  │    per axis rect → set scissor, bind uniforms, draw
  │
  ▼
Framebuffer
```

### Span registration

Span constructors call `mParentPlot->spanRhiLayer()->registerSpan(this)`. The span RHI layer maintains a list of registered spans grouped by parent axis rect. Spans remain `QCPLayerable` objects in the layer system for export path compatibility.

### Render order

The existing `render()` loop iterates layers and interleaves plottable, colormap, and paint buffer composite draws per layer. There is no clean insertion point between GPU content and paint buffers within this loop.

The span draw call goes **after the layer loop ends**, before `cb->endPass()`:

```
1. Per-layer loop: plottable draws, colormap draws, paint buffer composites (existing)
2. Span RHI layer — on top of everything (NEW)
```

Spans render on top of all content including axes/labels/legend. Scissor clipping to the axis rect prevents spans from covering axis decorations outside the plot area. This is the natural visual for overlay/highlight items.

**Screen vs export Z-order**: The QPainter export path respects layer assignment, while the GPU path renders all spans at a fixed Z-order. This is acceptable since spans are conventionally on top of data.

### Export fallback

For export paths (`savePdf`, `toPixmap`, `saveRastered`), spans use their existing `draw(QCPPainter*)` QPainter implementation. The span RHI layer is not involved.

Detection: `draw()` checks `painter->modes().testFlag(QCPPainter::pmVectorized) || !mParentPlot->spanRhiLayer()` — if true, run the QPainter path; otherwise, no-op (GPU handles it).

## Vertex Format

```cpp
struct SpanVertex {
    float coordX;       // data-space X or logical pixel X (see isPixelX/Y)
    float coordY;       // data-space Y or logical pixel Y
    float r, g, b, a;   // premultiplied alpha color
    float extrudeDirX;  // border extrusion direction in pixel space (0 for fill)
    float extrudeDirY;
    float extrudeWidth;  // border half-width in logical pixels (0 for fill)
    float isPixelX;     // 0.0 = coordX is data-space, 1.0 = coordX is logical pixels
    float isPixelY;     // 0.0 = coordY is data-space, 1.0 = coordY is logical pixels
};
```

**Stride:** 11 floats = 44 bytes per vertex.

### Position types: data-space vs pixel-space

VSpan and HSpan have one "infinite" dimension that spans the full axis rect:

- **VSpan**: X edges are `ptPlotCoords` (data-space), Y edges are `ptAxisRectRatio` (full axis rect height)
- **HSpan**: Y edges are `ptPlotCoords` (data-space), X edges are `ptAxisRectRatio` (full axis rect width)
- **RSpan**: all four edges are `ptPlotCoords` (data-space)

At vertex buffer build time, the infinite dimension is resolved to **logical pixel coordinates** (axis rect top/bottom or left/right). These are stored in the vertex with `isPixelX=1.0` or `isPixelY=1.0`, telling the vertex shader to skip `coordToPixel` and use the value directly.

Data-space coordinates are stored with `isPixelX/Y=0.0` and transformed by the shader's `coordToPixel` function.

Pixel-space vertices only change on widget **resize** (axis rect bounds change), not on pan/zoom. Resize sets `mGeometryDirty`, triggering a rebuild — this is infrequent.

### Geometry per span

| Span type | Fill triangles | Border lines | Total vertices |
|-----------|---------------|--------------|----------------|
| VSpan     | 6 (1 quad)    | 2 × 6 = 12  | 18             |
| HSpan     | 6 (1 quad)    | 2 × 6 = 12  | 18             |
| RSpan     | 6 (1 quad)    | 4 × 6 = 24  | 30             |

For 100 spans (worst case all RSpan): 3000 vertices × 44 bytes = ~132 KB. Trivially small.

### Fill quad geometry

2 triangles covering the span rectangle. For VSpan, X coordinates are data-space and Y coordinates are axis rect pixel bounds. For HSpan, Y coordinates are data-space and X coordinates are axis rect pixel bounds. For RSpan, all four edges are data coordinates.

### Border line geometry

Each border is an axis-aligned line extruded into a quad (2 triangles, 6 vertices). The extrusion happens in pixel space via the `extrudeDir` and `extrudeWidth` vertex attributes:

- Vertical border (VSpan edges, RSpan left/right): `extrudeDir = (±1, 0)`, perpendicular to Y axis
- Horizontal border (HSpan edges, RSpan top/bottom): `extrudeDir = (0, ±1)`, perpendicular to X axis

The vertex shader applies the extrusion after the coord-to-pixel transform, ensuring constant pixel-width borders regardless of zoom level.

## Shader Design

### Uniform buffer (per axis rect)

```glsl
layout(std140, binding = 0) uniform SpanParams {
    // Viewport
    float width;          // output physical pixel width
    float height;         // output physical pixel height
    float yFlip;          // -1.0 or +1.0 (NDC convention)
    float dpr;            // device pixel ratio

    // Key axis (X) transform
    float keyRangeLower;
    float keyRangeUpper;
    float keyAxisOffset;  // logical pixel position of axis start
    float keyAxisLength;  // logical pixel length of axis
    float keyLogScale;    // 0.0 = linear, 1.0 = log

    // Value axis (Y) transform
    float valRangeLower;
    float valRangeUpper;
    float valAxisOffset;  // logical pixel position of axis start
    float valAxisLength;  // logical pixel length of axis
    float valLogScale;

    float _pad;           // std140 requires total size multiple of 16 bytes (16 floats = 64 bytes)
};
```

16 floats × 4 bytes = 64 bytes, aligned to std140 requirements.

### Vertex shader (`span.vert`)

All positions (both data-space and pixel-space) are in **logical pixels** after the coord-to-pixel transform. The single `* dpr / width` in the NDC conversion handles the logical→physical→NDC mapping, matching the convention in `plottable.vert`.

```glsl
#version 440

layout(location = 0) in vec2 dataCoord;
layout(location = 1) in vec4 color;
layout(location = 2) in vec2 extrudeDir;
layout(location = 3) in float extrudeWidth;
layout(location = 4) in vec2 isPixel;      // (isPixelX, isPixelY): 0.0 = data-space, 1.0 = pixel-space

layout(location = 0) out vec4 v_color;

layout(std140, binding = 0) uniform SpanParams {
    float width;
    float height;
    float yFlip;
    float dpr;
    float keyRangeLower;
    float keyRangeUpper;
    float keyAxisOffset;
    float keyAxisLength;
    float keyLogScale;
    float valRangeLower;
    float valRangeUpper;
    float valAxisOffset;
    float valAxisLength;
    float valLogScale;
};

float coordToPixel(float coord, float lower, float upper,
                   float offset, float length, float isLog) {
    float t;
    if (isLog > 0.5) {
        // Clamp to small positive value to avoid log(0) or log(negative)
        float safeCoord = max(coord, 1e-30);
        float safeLower = max(lower, 1e-30);
        float safeUpper = max(upper, 1e-30);
        t = (log(safeCoord) - log(safeLower)) / (log(safeUpper) - log(safeLower));
    } else {
        t = (coord - lower) / (upper - lower);
    }
    return t * length + offset;
}

void main() {
    // For data-space coords: transform via coordToPixel to logical pixels
    // For pixel-space coords: use directly (already logical pixels)
    float px = isPixel.x > 0.5
        ? dataCoord.x
        : coordToPixel(dataCoord.x, keyRangeLower, keyRangeUpper,
                        keyAxisOffset, keyAxisLength, keyLogScale);
    float py = isPixel.y > 0.5
        ? dataCoord.y
        : coordToPixel(dataCoord.y, valRangeLower, valRangeUpper,
                        valAxisOffset, valAxisLength, valLogScale);

    // Extrude border lines in logical pixel space
    px += extrudeDir.x * extrudeWidth;
    py += extrudeDir.y * extrudeWidth;

    // Logical pixels → NDC (matches plottable.vert convention)
    float ndcX = (px * dpr / width) * 2.0 - 1.0;
    float ndcY = yFlip * ((py * dpr / height) * 2.0 - 1.0);
    gl_Position = vec4(ndcX, ndcY, 0.0, 1.0);
    v_color = color;
}
```

### Fragment shader

Reuses the existing `plottable.frag` (pass-through premultiplied alpha color). No new fragment shader file needed.

## Dirty tracking

A single `mGeometryDirty` boolean flag on `QCPSpanRhiLayer`.

**Set dirty when:**
- `registerSpan()` / `unregisterSpan()` called (span added/removed)
- Span edge positions change (drag interaction)
- Span colors/pen/brush change via setters (`setPen`, `setBrush`, `setBorderPen`, `setSelectedPen`, `setSelectedBrush`, `setSelectedBorderPen` — 6 setters × 3 span types = 18 callsites)
- Selection state changes (switches between normal/selected colors)
- Widget resize (pixel-space vertices for infinite edges need rebuilding)

All span property setters must call `mParentPlot->spanRhiLayer()->markGeometryDirty()`.

**On render:**
- If dirty: iterate registered spans, rebuild vertex buffer, upload, clear flag
- If clean: skip vertex buffer — only update uniform buffer with current axis ranges

The uniform buffer is always updated (axis ranges change on every pan/zoom). This is 64 bytes per axis rect — negligible.

## Hit testing and interaction

No changes to the existing CPU-side hit testing. Spans keep their `selectTest()`, `mousePressEvent()`, `mouseMoveEvent()`, `mouseReleaseEvent()` implementations, which operate on data coordinates via `coordToPixel()` math.

When a drag moves a span edge, the span calls `mParentPlot->spanRhiLayer()->markGeometryDirty()` to trigger vertex buffer rebuild on next render.

## Span lifecycle

**Construction:** Span constructor calls `mParentPlot->spanRhiLayer()->registerSpan(this)`.

**Destruction:** Span destructor calls `mParentPlot->spanRhiLayer()->unregisterSpan(this)`. Must be safe during `QCustomPlot` destruction (check `mParentPlot` and span RHI layer are still valid). The existing pattern: if `mParentPlot` is being destroyed, it clears items before destroying the span RHI layer.

## Axis rect grouping and scissor clipping

Spans are grouped by their parent axis rect in the vertex buffer. On render, the span RHI layer iterates axis rects:

1. Set scissor rect to the axis rect bounds (physical pixels, Y-flipped for Y-up backends)
2. Update uniform buffer with that axis rect's key/value axis ranges
3. Draw the vertex range for that axis rect's spans

This handles:
- VSpan/HSpan infinite edges (±FLT_MAX coords produce pixels far outside viewport, clipped by scissor)
- Multiple axis rects with independent axis ranges
- Spans not bleeding into neighboring axis rects

## QCustomPlot integration

### New members on `QCustomPlot`

```cpp
QCPSpanRhiLayer* mSpanRhiLayer = nullptr;  // lazily created
QCPSpanRhiLayer* spanRhiLayer();            // accessor, creates on first call
```

### Changes to `QCustomPlot::render()`

The span RHI layer is a single global object, not per-layer. It renders once, after the layer loop ends but before `cb->endPass()`. All spans share a single Z-order (on top of everything). Span layer assignment in the `QCPLayer` system is ignored for on-screen rendering (only matters for the export QPainter path).

Resource upload phase (before `beginPass`):
```cpp
if (mSpanRhiLayer && mSpanRhiLayer->hasSpans()) {
    mSpanRhiLayer->ensurePipeline(rpDesc, sampleCount);
    mSpanRhiLayer->uploadResources(updates, outputSize, dpr, isYUpInNDC);
}
```

Draw phase (after the per-layer loop, before `cb->endPass()`):
```cpp
if (mSpanRhiLayer && mSpanRhiLayer->hasSpans()) {
    mSpanRhiLayer->render(cb, outputSize);
}
```

Also add `releaseResources()` cleanup:
```cpp
delete mSpanRhiLayer;
mSpanRhiLayer = nullptr;
```

### Changes to span constructors

Register with span RHI layer:

```cpp
QCPItemVSpan::QCPItemVSpan(QCustomPlot* parentPlot)
    : QCPAbstractItem(parentPlot)
{
    // ... existing position/anchor initialization ...
    parentPlot->spanRhiLayer()->registerSpan(this);
}
```

### Changes to `QCPAbstractItem::draw()` dispatch

Span `draw(QCPPainter*)` checks context:
- Export (vectorized painter or no span RHI layer): run existing QPainter code
- On-screen: no-op, GPU handles rendering

## File inventory

| File | Action |
|------|--------|
| `src/painting/span-rhi-layer.h` | **New** — `QCPSpanRhiLayer` class |
| `src/painting/span-rhi-layer.cpp` | **New** — implementation |
| `src/painting/shaders/span.vert` | **New** — vertex shader |
| `src/painting/shaders/span.frag` | **Not needed** — reuses existing `plottable.frag` |
| `src/painting/shaders/embed_shaders.py` | **Modify** — add span shader compilation |
| `src/painting/shaders/embedded_shaders.h` | **Regenerated** — includes span shaders |
| `src/core.h` | **Modify** — add `mSpanRhiLayer`, `spanRhiLayer()` |
| `src/core.cpp` | **Modify** — integrate span RHI layer in `render()`, `releaseResources()` |
| `src/items/item-vspan.h/.cpp` | **Modify** — register/unregister, export check in `draw()` |
| `src/items/item-hspan.h/.cpp` | **Modify** — same |
| `src/items/item-rspan.h/.cpp` | **Modify** — same |
| `src/qcp.h` | **Modify** — include `span-rhi-layer.h` |
| `meson.build` | **Modify** — add new source files |

## Testing

- **Unit test:** Create spans, verify they render (pixel comparison against QPainter reference)
- **Log axis test:** Spans on log-scale axes position correctly
- **Pan/zoom test:** Verify no visual artifacts during rapid panning
- **Export test:** `toPixmap()` produces correct output (QPainter fallback works)
- **Multi-axis-rect test:** Spans in separate axis rects don't bleed across
- **Add/remove test:** Dynamic span lifecycle, geometry dirty tracking

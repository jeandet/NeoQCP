# Layer-Level GPU Translation Fast Path

## Goal

Extend the GPU translation fast path from per-plottable (Graph2/MultiGraph only) to the entire composited layer, so that all QPainter-rendered plottables (Curve, Bars, Financial, ErrorBars, StatisticalBox, etc.) also slide smoothly during pan while async resampling is busy.

## Background

NeoQCP's async plottables resample data in a background thread. During the resampling window, stale content is displayed. For GPU-rendered plottables (Graph2, MultiGraph), the vertex shader already applies a pixel-space translation to shift stale geometry to match the current viewport. QPainter-rendered plottables on the same layer have no such fast path — their rasterized image stays frozen until fresh data arrives.

### Layer and paint buffer structure

NeoQCP creates six default layers: `background`, `grid`, `main`, `axes`, `legend`, `overlay`. Only `overlay` is `lmBuffered`; the rest are `lmLogical` and **share a single paint buffer**. Plottables are registered to `main`, axes to `axes`, grid to `grid`, etc. — but at the GPU level, all logical layers render into the same `QCPPaintBufferRhi` texture.

This means translating the shared buffer would shift everything (axes, grid, background) — not just plottable content. To isolate plottable content for translation, the `main` layer must be promoted to `lmBuffered`.

## Architecture

### Prerequisite: buffered main layer

Set the `main` layer to `lmBuffered` in the `QCustomPlot` constructor so it gets its own `QCPPaintBufferRhi`. This gives the compositing step a dedicated texture containing only plottable content, which can be translated independently.

```cpp
layer(QLatin1String("main"))->setMode(QCPLayer::lmBuffered);
```

Impact: one additional GPU texture and composite draw call. Negligible cost — the `overlay` layer already uses `lmBuffered`.

### Composite shader translation

The `composite.vert` shader currently renders a hardcoded fullscreen quad in NDC with no uniforms. Add a `LayerParams` uniform buffer:

```glsl
layout(std140, binding = 1) uniform LayerParams {
    float translateX;  // pixel offset
    float translateY;
    float viewportW;   // for pixel → NDC conversion
    float viewportH;
    float yFlip;       // -1.0 for Y-up NDC (Metal/Vulkan), +1.0 for OpenGL
} lp;

void main() {
    float dx = (lp.translateX / lp.viewportW) * 2.0;
    float dy = lp.yFlip * (lp.translateY / lp.viewportH) * 2.0;
    v_texcoord = texcoord;
    gl_Position = vec4(position.x + dx, position.y + dy, 0.0, 1.0);
}
```

The `yFlip` value is determined at runtime via `rhi->isYUpInNDC()`, following the same pattern as `plottable.vert`.

For layers with zero offset (axes, grid, legend, background), the shader behaves identically to today. For the `main` layer during busy panning, the texture shifts by the pixel delta.

### SRB and pipeline changes

The current composite pipeline's SRB layout declares only one binding (texture sampler at slot 0). Adding the UBO at slot 1 requires:

1. **Layout SRB** (`mLayoutSrb`): Add a `uniformBuffer(1, ...)` binding alongside the existing `sampledTexture(0, ...)`.
2. **Per-buffer SRBs**: Each `QCPPaintBufferRhi::srb()` must also bind the UBO at slot 1. The UBO itself is shared (one allocation, updated per draw call).
3. **`srbMatchesTexture()` check**: Currently validates only the texture. The UBO is a single stable object shared across all SRBs, so it never changes identity. SRBs are always recreated when null (after resize/init), at which point they get both bindings. The texture is the only varying binding, so `srbMatchesTexture()` remains the correct staleness signal — no change needed.
4. **Pipeline recreation**: The pipeline must be rebuilt with the new layout SRB. Since the pipeline is lazily created in `render()`, this happens automatically on the first frame after the shader change.

The UBO is a single `QRhiBuffer` (20 bytes / padded to 32 for std140, `Dynamic` usage) created in `initialize()` **before** any paint buffer SRBs are built, and updated via `updateDynamicBuffer()` before each composite draw call.

### Plottable offset API

`QCPAbstractPlottable` gains a new virtual method:

```cpp
virtual QPointF stallPixelOffset() const { return {}; }
```

Async plottables (`QCPGraph2`, `QCPMultiGraph`) override this to return the pixel offset computed from their `mRenderedRange` and `computeViewportOffset()` when their pipeline is busy, or `{0,0}` otherwise. Non-async plottables inherit the default (always zero).

This keeps `mRenderedRange` private and gives `QCPLayer` a clean interface without `dynamic_cast`.

### Layer offset computation

`QCPLayer` exposes `pixelOffset()`:

```cpp
QPointF QCPLayer::pixelOffset() const {
    for (auto* child : mChildren) {
        if (auto* plottable = qobject_cast<QCPAbstractPlottable*>(child)) {
            QPointF offset = plottable->stallPixelOffset();
            if (!offset.isNull())
                return offset;
        }
    }
    return {};
}
```

First busy plottable's offset wins. All plottables on the same layer share the same axis rect in the common case, so the offset is identical.

**Multiple axis rects on the same layer**: If two async plottables belong to different axis rects with independent pan states, a single layer offset cannot represent both correctly. In this case, `pixelOffset()` returns `{0,0}` — no translation for the layer. This is a graceful degradation to the current behavior (stale content stays frozen). The per-plottable GPU offset (Graph2/MultiGraph) still works independently.

**Non-async plottables on the same layer**: A non-async plottable (e.g. plain `QCPGraph`) rendered fresh in the current replot will be translated along with the stale async content. This is acceptable: the non-async plottable was drawn at the current viewport (correct position), and the translation shifts it by the same delta as the async plottable. The visual result is that both appear to slide together, which is the expected behavior during a fast pan. When the async pipeline finishes and the layer is repainted fresh, everything snaps to the correct position.

### Render-time plumbing

In `QCustomPlot::render()`, when compositing each paint buffer:

1. Determine the layer associated with this paint buffer (for `lmBuffered` layers, there's a 1:1 mapping).
2. Call `layer->pixelOffset()` to get the translation.
3. Write `translateX`, `translateY`, `viewportW`, `viewportH`, `yFlip` to the composite UBO via `updateDynamicBuffer()`.
4. If offset is nonzero, enable scissor clipping to the axis rect.
5. Issue the draw call.

For shared paint buffers (logical layers), offset is always zero (no async plottables live on `grid`, `axes`, etc.).

### Scissor clipping

When a layer has a nonzero pixel offset, the translated texture may extend beyond the axis rect. Enable scissor clipping on the composite draw call, using the same axis rect bounds that `QCPPlottableRhiLayer` uses.

For layers with zero offset, no scissor is needed (current behavior preserved).

Edge case: a layer with plottables from multiple axis rects uses the union of all axis rects — conservative but correct.

### Interaction with existing per-plottable offsets

- **Graph2/MultiGraph**: Already have vertex shader translation via `QCPPlottableRhiLayer::setPixelOffset()`. These render as separate GPU geometry on top of the composited paint buffer texture. No double-translation: the layer offset shifts the QPainter texture, the per-plottable offset shifts the GPU geometry. Both compute the same delta from `computeViewportOffset()`.
- **ColorMap2/Histogram2D**: Quad rect is computed from data coords via `coordToPixel()` each draw — naturally repositions. No offset needed at any level.

### Shader recompilation

The modified `composite.vert` must be recompiled with `qsb` and re-embedded via `embed_shaders.py`, following the existing shader build process.

## Plottables affected

**Gains smooth panning (new)**:
- QCPCurve, QCPBars, QCPStatisticalBox, QCPFinancial, QCPErrorBars
- Any future QPainter-rendered plottable on the `main` layer

**Already has fast path (unchanged)**:
- QCPGraph2, QCPMultiGraph — per-plottable vertex shader offset
- QCPColorMap2, QCPHistogram2D — natural repositioning via `coordToPixel()`

**Not translated (by design)**:
- Background fill (`background` layer)
- Grid lines (`grid` layer)
- Axes, ticks, labels (`axes` layer)
- Legend (`legend` layer)
- Selection rect, overlays (`overlay` layer)

## API surface

One new virtual method on `QCPAbstractPlottable`:

```cpp
virtual QPointF stallPixelOffset() const { return {}; }
```

No other public API changes. The layer translation is automatic — any plottable with an async pipeline that reports busy state gets the fast path for free.

## Testing

- Unit test: verify `QCPLayer::pixelOffset()` returns correct offset when child plottable is busy, zero when idle
- Unit test: verify `stallPixelOffset()` returns correct values for Graph2 (busy vs idle)
- Integration test: replot during pan with mixed async/non-async plottables on `main` layer doesn't crash
- Scissor test: verify translated content doesn't bleed outside axis rect
- Regression: existing GPU translation tests for Graph2/MultiGraph still pass
- Verify `main` layer being `lmBuffered` doesn't break existing rendering (axes, grid, legend still render correctly)

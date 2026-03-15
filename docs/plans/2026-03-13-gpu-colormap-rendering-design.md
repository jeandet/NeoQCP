# GPU-Accelerated Colormap Rendering for QCPColorMap2

**Date:** 2026-03-13
**Status:** Approved

## Goals

1. **Performance** — skip the QPainter→staging buffer→layer texture round-trip for colormaps
2. **Pixel-accurate** — nearest-neighbor sampling by default (no interpolation/smoothing)
3. **Apple Silicon** — Metal backend via QRhi, must work well on M-series Macs

## Current Flow (CPU)

```
resampler (background thread)
  → QCPColorMapData (resampled grid)
  → updateMapImage() produces mMapImage (QImage ARGB32_Premultiplied)
  → draw() calls painter->drawImage() into layer staging buffer
  → staging buffer uploaded as layer texture
  → composited as fullscreen quad
```

The bottleneck is the QPainter drawImage into the staging buffer (CPU compositing) followed by re-uploading the entire layer texture on every pan/zoom.

## GPU Flow

```
resampler (background thread)
  → QCPColorMapData (resampled grid)
  → updateMapImage() produces mMapImage (QImage RGBA8888_Premultiplied)
  → draw() pushes image + quad rect to QCPColormapRhiLayer
  → render() uploads image directly as GPU texture
  → renders as textured quad with nearest-neighbor sampling
```

The colormap image is uploaded as its own GPU texture and rendered as a quad clipped to the axis rect. No QPainter intermediary, no layer staging buffer involvement.

## Architecture

### New Class: QCPColormapRhiLayer

Analogous to `QCPPlottableRhiLayer` but for textured quads instead of vertex-colored triangles.

**GPU Resources:**
- `QRhiTexture*` — colormap image (RGBA8, recreated on size change)
- `QRhiSampler*` — nearest-neighbor filtering (`Nearest, Nearest, None, ClampToEdge`)
- `QRhiBuffer*` — vertex buffer for a single quad (4 vertices: position float2 + texcoord float2)
- `QRhiBuffer*` — uniform buffer (viewport params: width, height, yFlip, dpr — 16 bytes)
- `QRhiShaderResourceBindings*` — binds uniform + texture + sampler
- `QRhiGraphicsPipeline*` — premultiplied alpha blend, triangle strip topology

**Dirty Flags:**
- `mTextureDirty` — set when mMapImage changes, triggers texture re-upload
- `mGeometryDirty` — set when quad corners change (axis range or widget resize)

**Methods:**
- `setImage(const QImage& image)` — stores image reference, sets texture dirty
- `setQuadRect(QRectF pixelRect)` — quad corners in pixel coordinates, sets geometry dirty
- `setScissorRect(QRect scissor)` — axis rect clipping
- `ensurePipeline(QRhi*, QRhiRenderPassDescriptor*)` — lazy pipeline creation
- `uploadResources(QRhiResourceUpdateBatch*)` — upload dirty texture/vertices
- `render(QRhiCommandBuffer*)` — draw the quad with scissor
- `releaseResources()` — cleanup

### Shader

Reuse existing **composite shader** (`composite.vert`/`composite.frag`). It already does:
- Vertex: position + texcoord passthrough
- Fragment: sample texture at UV, output color

The only difference from compositing is the quad isn't fullscreen — it covers the axis rect area. The vertex positions are in pixel coordinates, transformed to NDC by the same viewport uniform.

### Nearest-Neighbor Guarantee

`QRhiSampler` with `Nearest` min/mag filter ensures no interpolation between texels. Each data cell maps to exact pixel boundaries. This is the Metal/Vulkan/D3D equivalent of `GL_NEAREST`.

## Integration

### Render Loop Order

In `QCustomPlot::render()`, per layer:

1. Composite paint buffer texture (axes, grids, text, legend)
2. **Render colormap RHI layers** (textured quads)
3. Render plottable RHI layer (GPU lines/fills)

Colormaps render after the paint buffer (so axes are behind) and before line plottables (so lines can overlay colormaps).

### QCPColorMap2::draw() Branching

```
if (GPU path available — mParentPlot has RHI):
    compute quad rect from axis coordinates → pixel coordinates
    push mMapImage + quad rect to QCPColormapRhiLayer
else (export path — PDF, SVG, pixmap):
    painter->drawImage() as before
```

### Ownership

- `QCPColorMap2` creates its `QCPColormapRhiLayer` lazily on first GPU render
- `QCustomPlot` tracks active colormap RHI layers per `QCPLayer` (map, similar to `mPlottableRhiLayers`)
- `releaseResources()` deletes all colormap RHI layers
- `initialize()` invalidates pipelines on resize (render pass descriptor may change)

### Image Format

`mMapImage` must be `QImage::Format_RGBA8888_Premultiplied` for direct upload as `QRhiTexture::RGBA8`. The current `ARGB32_Premultiplied` format needs a conversion — change `updateMapImage()` to produce RGBA8888 directly.

## Files Changed

| File | Change |
|------|--------|
| `src/painting/colormap-rhi-layer.h` | **New** — QCPColormapRhiLayer class declaration |
| `src/painting/colormap-rhi-layer.cpp` | **New** — implementation |
| `src/plottables/plottable-colormap2.h` | Add `QCPColormapRhiLayer*` member, forward declare |
| `src/plottables/plottable-colormap2.cpp` | `draw()` branches GPU vs QPainter; `updateMapImage()` produces RGBA8888 |
| `src/core.h` | Add `QHash<QCPLayer*, QCPColormapRhiLayer*> mColormapRhiLayers` |
| `src/core.cpp` | Track colormap RHI layers in render loop, clean up in releaseResources, invalidate in initialize |
| `src/meson.build` | Add new source files |

## Export Fallback

`savePdf()`, `toPixmap()`, etc. bypass RHI entirely — they call `draw(QCPPainter*)` which uses `painter->drawImage()`. No change needed. The GPU path is only active when rendering to the widget.

## Testing

- Existing QCPColorMap2 manual test: visual verification that rendering is identical
- Auto-test: render a colormap, verify no crash, verify texture upload path when RHI is active
- Export test: verify `toPixmap()` still produces correct output (QPainter fallback)
- Apple Silicon: CI already covers macOS ARM, ensure Metal backend works

## Future Extensions (Not In Scope)

- **Shader-side gradient mapping**: upload z-values as R32F texture + gradient as 1D LUT, change gradient without resampling
- **Bilinear filtering option**: `QRhiSampler` with `Linear` for smooth interpolation when requested
- **Streaming tiles**: progressive texture upload during resampling

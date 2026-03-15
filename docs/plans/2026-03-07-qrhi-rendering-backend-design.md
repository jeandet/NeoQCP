# QRhiWidget Rendering Backend Design

## Problem

NeoQCP's OpenGL rendering path drew into FBO paint buffers, then read them back to the CPU via `QOpenGLFramebufferObject::toImage()` before compositing onto the QWidget surface with QPainter. This GPU-to-CPU readback was the main performance bottleneck, especially on macOS where OpenGL is deprecated and runs through a Metal translation layer.

## Solution

Replace the QWidget + OpenGL FBO rendering path with QRhiWidget (Qt 6.7+). QRhiWidget uses Qt's Rendering Hardware Interface, which maps to the native graphics API on each platform (Metal on macOS, Vulkan on Linux, D3D on Windows). Rendering composites layer textures directly to the screen with no GPU-to-CPU readback.

## Architecture

### Rendering flow

```
replot()
  -> layers draw via QPainter into QImage staging buffers (QCPPaintBufferRhi)
  -> calls update() to schedule render
QRhiWidget::render()
  -> uploads dirty staging images to GPU textures
  -> composites layer textures as fullscreen quads with premultiplied alpha blending
```

### Export flow (unchanged)

```
savePdf() / saveSvg()
  -> creates QPainter on QPdfWriter / QSvgGenerator
  -> toPainter() redraws all layerables directly
  -> fully vectorial output, never touches paint buffers
```

## Class Changes

### QCustomPlot

**Inheritance:** `QCustomPlot : public QRhiWidget` (QRhiWidget extends QWidget, so no downstream API breakage)

**RHI compositing members:**
- `QRhiGraphicsPipeline* mCompositePipeline` — textured-quad pipeline for compositing layers
- `QRhiShaderResourceBindings* mLayoutSrb` — layout-only SRB for pipeline compatibility
- `QRhiSampler* mSampler` — shared texture sampler
- `QRhiBuffer* mQuadVertexBuffer`, `mQuadIndexBuffer` — fullscreen quad geometry

**QRhiWidget overrides:**
- `initialize(QRhiCommandBuffer*)` — creates pipeline, sampler, quad buffer; skips on resize (resources remain valid)
- `render(QRhiCommandBuffer*)` — uploads dirty textures, composites layers to screen
- `releaseResources()` — cleans up all RHI resources including paint buffers

### Paint Buffer Hierarchy

```
QCPAbstractPaintBuffer
  +-- QCPPaintBufferPixmap        (software, kept for export)
  +-- QCPPaintBufferRhi           (QRhiTexture-backed, used for screen rendering)
```

### QCPPaintBufferRhi

Each layer gets a `QImage` staging buffer and a `QRhiTexture`. Layers paint via QPainter into the staging image; the texture is uploaded lazily in `render()` only when the buffer is dirty.

- `startPainting()` — returns a QCPPainter wrapping a QPainter on the staging QImage
- `donePainting()` — marks the buffer as needing upload
- `clear(QColor)` — fills the staging image, marks as needing upload
- `texture()` — returns the `QRhiTexture*` for compositing
- `srb()` / `setSrb()` — cached shader resource bindings for the texture
- `needsUpload()` / `setUploaded()` — dirty tracking to skip redundant uploads

### Compositing Shaders

Embedded at build time via `embed_shaders.py` (precompiled `.qsb` → C array header).

- Vertex shader: passthrough quad (position + texcoord, UV orientation adjusted for `isYUpInNDC()`)
- Fragment shader: `texture(sampler, texcoord)` with premultiplied alpha blending

## Files

### Removed (from OpenGL path)
- `src/painting/paintbuffer-glfbo.h/.cpp` — QCPPaintBufferGlFbo and NeoQCPBatchDrawingHelper

### Added
- `src/painting/paintbuffer-rhi.h/.cpp` — QCPPaintBufferRhi
- `src/painting/shaders/composite.vert` — compositing vertex shader
- `src/painting/shaders/composite.frag` — compositing fragment shader
- `src/painting/shaders/embed_shaders.py` — build-time shader embedding script

### Modified
- `src/core.h/.cpp` — inheritance change, RHI compositing implementation
- `src/global.h` — removed OpenGL conditionals
- `meson.build` — removed OpenGL dependency, added shader compilation and embedding
- `meson_options.txt` — removed `with_opengl` and `enable_batch_drawing` options

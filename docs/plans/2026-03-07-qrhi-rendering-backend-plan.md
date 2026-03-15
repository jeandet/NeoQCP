# QRhiWidget Rendering Backend Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the OpenGL FBO rendering path with QRhiWidget to eliminate GPU-to-CPU readback and use native graphics APIs (Metal/Vulkan/D3D).

**Architecture:** QCustomPlot inherits QRhiWidget instead of QWidget. Each layer's paint buffer becomes a QRhiTexture. Compositing happens directly on screen in `render()` via a simple textured-quad shader. No readback for screen rendering. Export paths (PDF/SVG/PNG) unchanged.

**Tech Stack:** Qt 6.10+ QRhi API, GLSL/SPIR-V shaders, Meson build system.

**Design doc:** `docs/plans/2026-03-07-qrhi-rendering-backend-design.md`

---

### Task 1: Remove OpenGL FBO code and batch drawing helper

Remove the old rendering backend. This will break compilation until subsequent tasks add the replacement.

**Files:**
- Delete: `src/painting/paintbuffer-glfbo.h`
- Delete: `src/painting/paintbuffer-glfbo.cpp`

**Step 1: Delete the OpenGL FBO files**

```bash
git rm src/painting/paintbuffer-glfbo.h src/painting/paintbuffer-glfbo.cpp
```

**Step 2: Remove references from meson.build**

In `meson.build`, remove these entries:
- From `neoqcp_moc_headers`: `'src/painting/paintbuffer-glfbo.h'`
- From `NeoQCP` static_library sources: `'src/painting/paintbuffer-glfbo.cpp'`

**Step 3: Remove OpenGL-related meson options**

In `meson_options.txt`, remove:
```
option('with_opengl', ...)
option('enable_batch_drawing', ...)
```

In `meson.build`, remove the `if get_option('with_opengl')` block (lines 23-27) and the `if get_option('enable_batch_drawing')` block (lines 39-41) and the `if get_option('tracy_enable')` / `with_opengl` config_data blocks (lines 42-47).

Remove the `GL` dependency and `qtopengl_dep` from the build.

**Step 4: Clean up config_data**

In `meson.build`, remove these `config_data.set()` calls:
- `NEOQCP_BATCH_DRAWING`
- `NEOQCP_USE_OPENGL`
- `QCUSTOMPLOT_USE_OPENGL`
- `QCP_OPENGL_FBO`
- `QCP_OPENGL_OFFSCREENSURFACE`

**Step 5: Commit**

```bash
git add -u
git commit -m "Remove OpenGL FBO rendering backend and batch drawing helper

Preparation for QRhiWidget migration. Removes QCPPaintBufferGlFbo,
NeoQCPBatchDrawingHelper, and all OpenGL-specific meson options and
compile flags. Compilation is expected to be broken until the RHI
backend is added."
```

---

### Task 2: Clean up QCustomPlot from OpenGL members and methods

Remove all OpenGL-specific code from `core.h` and `core.cpp`, and from `global.h`.

**Files:**
- Modify: `src/core.h`
- Modify: `src/core.cpp`
- Modify: `src/global.h`

**Step 1: Clean core.h**

In `src/core.h`:
- Remove the `#ifdef NEOQCP_BATCH_DRAWING` forward declaration of `NeoQCPBatchDrawingHelper`
- Remove the `#include "neoqcp_config.h"` if it's only used for OpenGL flags (check first — it's also used for version, keep it if so)
- Remove member variables:
  - `bool mOpenGl`
  - `int mOpenGlMultisamples`
  - `QCP::AntialiasedElements mOpenGlAntialiasedElementsBackup`
  - `bool mOpenGlCacheLabelsBackup`
  - The entire `#ifdef QCP_OPENGL_FBO` block containing `mGlContext`, `mGlSurface`, `mGlPaintDevice`, `mBatchDrawingHelper`
- Remove method declarations:
  - `void setOpenGl(bool enabled, int multisampling = 16)`
  - `bool openGl() const`
  - `bool setupOpenGl()`
  - `void freeOpenGl()`
- Remove the `Q_PROPERTY(bool openGl ...)` line

**Step 2: Clean core.cpp**

In `src/core.cpp`:
- Remove `#include "painting/paintbuffer-glfbo.h"`
- Remove the `QCustomPlot::setOpenGl()` method entirely
- Remove the `QCustomPlot::setupOpenGl()` method entirely
- Remove the `QCustomPlot::freeOpenGl()` method entirely
- In `QCustomPlot::QCustomPlot()` constructor:
  - Remove `mOpenGl(false)` from initializer list
  - Remove `mOpenGlMultisamples(4)` from initializer list
  - Remove `mOpenGlAntialiasedElementsBackup(QCP::aeNone)` from initializer list
  - Remove `mOpenGlCacheLabelsBackup(true)` from initializer list
  - Remove `mOpenGlAntialiasedElementsBackup = mAntialiasedElements;` in body
  - Remove `mOpenGlCacheLabelsBackup = ...` in body
- In `QCustomPlot::createPaintBuffer()`:
  - Remove the `if (mOpenGl)` branch with `QCPPaintBufferGlFbo`. Keep only the `QCPPaintBufferPixmap` return (for now — Task 4 will add RHI path)
- In `QCustomPlot::setupPaintBuffers()`:
  - Remove the entire `#ifdef NEOQCP_BATCH_DRAWING` block at the end
- In `QCustomPlot::paintEvent()`:
  - Remove the `#ifdef NEOQCP_BATCH_DRAWING` branches. Keep only the simple loop: `for (const auto& buffer : mPaintBuffers) buffer->draw(&painter);`
  - Remove `PROFILE_PASS_VALUE(this->openGl());`
- In `QCustomPlot::replot()`:
  - Remove `PROFILE_PASS_VALUE_N("OpenGL", this->mOpenGl);`

**Step 3: Clean global.h**

In `src/global.h`:
- Remove the `#ifdef QCP_OPENGL_FBO` block that includes OpenGL headers (`QOpenGLFramebufferObject`, `QOpenGLPaintDevice`, `QOffscreenSurface`, `QOpenGLContext`)

**Step 4: Verify the project builds with pixmap-only rendering**

```bash
meson setup --wipe build && meson compile -C build
```

**Step 5: Run tests to verify nothing is broken**

```bash
meson test --print-errorlogs -C build
```

Expected: All existing tests pass (they don't test OpenGL rendering).

**Step 6: Commit**

```bash
git add -u
git commit -m "Remove all OpenGL members and methods from QCustomPlot

Strips mOpenGl, setupOpenGl(), freeOpenGl(), setOpenGl() and all
related members. QCustomPlot now uses pixmap-only rendering.
All tests pass."
```

---

### Task 3: Create compositing shaders

Write the vertex and fragment shaders for compositing layer textures onto the screen.

**Files:**
- Create: `src/painting/shaders/composite.vert`
- Create: `src/painting/shaders/composite.frag`

**Step 1: Create the vertex shader**

Create `src/painting/shaders/composite.vert`:

```glsl
#version 440

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texcoord;

layout(location = 0) out vec2 v_texcoord;

void main()
{
    v_texcoord = texcoord;
    gl_Position = vec4(position, 0.0, 1.0);
}
```

**Step 2: Create the fragment shader**

Create `src/painting/shaders/composite.frag`:

```glsl
#version 440

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D tex;

void main()
{
    fragColor = texture(tex, v_texcoord);
}
```

**Step 3: Add shader compilation to meson.build**

Add after the `neoqcp_moc_files` line in `meson.build`:

```meson
neoqcp_shaders = qtmod.compile_shaders(
    sources: ['src/painting/shaders/composite.vert',
              'src/painting/shaders/composite.frag'],
    output_dir: 'shaders')
```

Add `neoqcp_shaders` to the `NeoQCP` static_library sources list.

Note: If `qtmod.compile_shaders()` is not available (it requires the `ShaderTools` module), fall back to precompiling shaders with `qsb` manually and embedding the `.qsb` files. Check Qt 6.10 documentation for the meson integration.

**Step 4: Verify shaders compile**

```bash
meson setup --wipe build && meson compile -C build
```

**Step 5: Commit**

```bash
git add src/painting/shaders/ meson.build
git commit -m "Add compositing shaders for QRhi rendering

Simple vertex+fragment shader pair for drawing textured quads.
Used to composite layer textures onto the screen in render()."
```

---

### Task 4: Implement QCPPaintBufferRhi

Create the RHI-backed paint buffer that replaces QCPPaintBufferGlFbo.

**Files:**
- Create: `src/painting/paintbuffer-rhi.h`
- Create: `src/painting/paintbuffer-rhi.cpp`
- Modify: `meson.build` (add new source files)

**Step 1: Create paintbuffer-rhi.h**

```cpp
#pragma once

#include "paintbuffer.h"
#include <rhi/qrhi.h>

class QCP_LIB_DECL QCPPaintBufferRhi : public QCPAbstractPaintBuffer
{
public:
    explicit QCPPaintBufferRhi(const QSize& size, double devicePixelRatio,
                                const QString& layerName, QRhi* rhi);
    virtual ~QCPPaintBufferRhi() override;

    // reimplemented virtual methods:
    virtual QCPPainter* startPainting() override;
    virtual void donePainting() override;
    virtual void draw(QCPPainter* painter) const override;
    void clear(const QColor& color) override;

    // RHI-specific:
    QRhiTexture* texture() const { return mTexture; }

protected:
    virtual void reallocateBuffer() override;

private:
    QRhi* mRhi;
    QRhiTexture* mTexture = nullptr;
    QImage mStagingImage;  // CPU-side image for QPainter drawing, uploaded to texture
};
```

Note: The initial implementation uses a hybrid approach — QPainter draws into a QImage (`mStagingImage`), which is then uploaded to the QRhiTexture. This is CPU-to-GPU (fast) rather than the old GPU-to-CPU readback (slow). A future optimization could render QPainter directly into the RHI texture via `QRhi::beginOffscreenFrame()`.

**Step 2: Create paintbuffer-rhi.cpp**

```cpp
#include "paintbuffer-rhi.h"
#include "painter.h"
#include "Profiling.hpp"

QCPPaintBufferRhi::QCPPaintBufferRhi(const QSize& size, double devicePixelRatio,
                                       const QString& layerName, QRhi* rhi)
    : QCPAbstractPaintBuffer(size, devicePixelRatio, layerName)
    , mRhi(rhi)
{
    QCPPaintBufferRhi::reallocateBuffer();
}

QCPPaintBufferRhi::~QCPPaintBufferRhi()
{
    delete mTexture;
}

QCPPainter* QCPPaintBufferRhi::startPainting()
{
    PROFILE_HERE_N("QCPPaintBufferRhi::startPainting");
    QCPPainter* result = new QCPPainter(&mStagingImage);
    return result;
}

void QCPPaintBufferRhi::donePainting()
{
    PROFILE_HERE_N("QCPPaintBufferRhi::donePainting");
    // Upload the staging image to the GPU texture.
    // This happens during the next beginOffscreenFrame/render pass.
    // The actual upload is deferred to render() in QCustomPlot.
}

void QCPPaintBufferRhi::draw(QCPPainter* painter) const
{
    // Fallback for export paths: draw the staging image via QPainter
    PROFILE_HERE_N("QCPPaintBufferRhi::draw");
    if (painter && painter->isActive())
    {
        const int targetWidth = mStagingImage.width() / mDevicePixelRatio;
        const int targetHeight = mStagingImage.height() / mDevicePixelRatio;
        painter->drawImage(QRect(0, 0, targetWidth, targetHeight),
                           mStagingImage, mStagingImage.rect());
    }
    else
        qDebug() << Q_FUNC_INFO << "invalid or inactive painter passed";
}

void QCPPaintBufferRhi::clear(const QColor& color)
{
    mStagingImage.fill(color);
}

void QCPPaintBufferRhi::reallocateBuffer()
{
    setInvalidated();

    QSize pixelSize = mSize * mDevicePixelRatio;

    mStagingImage = QImage(pixelSize, QImage::Format_RGBA8888_Premultiplied);
    mStagingImage.setDevicePixelRatio(mDevicePixelRatio);
    mStagingImage.fill(Qt::transparent);

    delete mTexture;
    mTexture = nullptr;
    if (mRhi)
    {
        mTexture = mRhi->newTexture(QRhiTexture::RGBA8, pixelSize, 1,
                                     QRhiTexture::UsedAsTransferSource);
        if (!mTexture->create())
        {
            qDebug() << Q_FUNC_INFO << "Failed to create RHI texture";
            delete mTexture;
            mTexture = nullptr;
        }
    }
}
```

**Step 3: Add to meson.build**

Add `'src/painting/paintbuffer-rhi.h'` to `neoqcp_moc_headers` (if needed for MOC — check if there are Q_OBJECT macros; if not, just add to `extra_files`).

Add `'src/painting/paintbuffer-rhi.cpp'` to the `NeoQCP` static_library sources.

**Step 4: Verify compilation**

```bash
meson setup --wipe build && meson compile -C build
```

**Step 5: Commit**

```bash
git add src/painting/paintbuffer-rhi.h src/painting/paintbuffer-rhi.cpp meson.build
git commit -m "Add QCPPaintBufferRhi: RHI-backed paint buffer

Uses a staging QImage for QPainter drawing with CPU-to-GPU upload to
QRhiTexture. Replaces the old QCPPaintBufferGlFbo which did expensive
GPU-to-CPU readback."
```

---

### Task 5: Change QCustomPlot to inherit QRhiWidget

This is the core migration: change inheritance and implement the QRhiWidget virtual methods.

**Files:**
- Modify: `src/core.h`
- Modify: `src/core.cpp`

**Step 1: Change inheritance in core.h**

Replace:
```cpp
#include "painting/paintbuffer.h"
```
with:
```cpp
#include "painting/paintbuffer.h"
#include <QRhiWidget>
```

Change:
```cpp
class QCP_LIB_DECL QCustomPlot : public QWidget
```
to:
```cpp
class QCP_LIB_DECL QCustomPlot : public QRhiWidget
```

Add new private members:
```cpp
    // RHI compositing resources:
    QRhi* mRhi = nullptr;
    QRhiGraphicsPipeline* mCompositePipeline = nullptr;
    QRhiSampler* mSampler = nullptr;
    QRhiBuffer* mQuadVertexBuffer = nullptr;
    QRhiBuffer* mQuadIndexBuffer = nullptr;
    QRhiShaderResourceBindings* mCompositeBindings = nullptr;
    bool mRhiInitialized = false;
```

Add new protected method declarations (QRhiWidget overrides):
```cpp
    void initialize(QRhiCommandBuffer* cb) override;
    void render(QRhiCommandBuffer* cb) override;
    void releaseResources() override;
```

Remove `paintEvent(QPaintEvent*)` declaration.

**Step 2: Update constructor in core.cpp**

Change:
```cpp
QCustomPlot::QCustomPlot(QWidget* parent)
    : QWidget(parent)
```
to:
```cpp
QCustomPlot::QCustomPlot(QWidget* parent)
    : QRhiWidget(parent)
```

**Step 3: Implement initialize()**

```cpp
void QCustomPlot::initialize(QRhiCommandBuffer* cb)
{
    Q_UNUSED(cb)
    PROFILE_HERE_N("QCustomPlot::initialize");

    mRhi = rhi();
    if (!mRhi)
    {
        qDebug() << Q_FUNC_INFO << "No QRhi instance available";
        return;
    }

    // Create sampler
    mSampler = mRhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                 QRhiSampler::None,
                                 QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    mSampler->create();

    // Fullscreen quad vertices: position (x,y) + texcoord (u,v)
    static const float quadData[] = {
        // x,    y,   u, v
        -1.0f, -1.0f, 0.0f, 1.0f,  // bottom-left
         1.0f, -1.0f, 1.0f, 1.0f,  // bottom-right
         1.0f,  1.0f, 1.0f, 0.0f,  // top-right
        -1.0f,  1.0f, 0.0f, 0.0f,  // top-left
    };
    mQuadVertexBuffer = mRhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                         sizeof(quadData));
    mQuadVertexBuffer->create();

    static const quint16 indices[] = { 0, 1, 2, 0, 2, 3 };
    mQuadIndexBuffer = mRhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer,
                                        sizeof(indices));
    mQuadIndexBuffer->create();

    // Upload vertex/index data
    QRhiResourceUpdateBatch* updates = mRhi->nextResourceUpdateBatch();
    updates->uploadStaticBuffer(mQuadVertexBuffer, quadData);
    updates->uploadStaticBuffer(mQuadIndexBuffer, indices);
    cb->resourceUpdate(updates);

    // Pipeline will be created on first render (needs render target format)
    mRhiInitialized = true;
}
```

**Step 4: Implement render()**

```cpp
void QCustomPlot::render(QRhiCommandBuffer* cb)
{
    PROFILE_FRAME_MARK;
    PROFILE_HERE_N("QCustomPlot::render");

    if (!mRhiInitialized || !mRhi)
        return;

    // Detect DPR changes
    if (const auto newDpr = devicePixelRatioF(); !qFuzzyCompare(mBufferDevicePixelRatio, newDpr))
    {
        setBufferDevicePixelRatio(newDpr);
        replot(QCustomPlot::rpQueuedRefresh);
        return;
    }

    const QSize outputSize = renderTarget()->pixelSize();
    QRhiResourceUpdateBatch* updates = mRhi->nextResourceUpdateBatch();

    // Upload staging images from paint buffers to GPU textures
    for (const auto& buffer : mPaintBuffers)
    {
        if (auto* rhiBuffer = dynamic_cast<QCPPaintBufferRhi*>(buffer.data()))
        {
            if (rhiBuffer->texture())
            {
                QRhiTextureSubresourceUploadDescription subDesc(rhiBuffer->stagingImage());
                QRhiTextureUploadDescription uploadDesc(
                    QRhiTextureUploadEntry(0, 0, subDesc));
                updates->uploadTexture(rhiBuffer->texture(), uploadDesc);
            }
        }
    }

    // Begin render pass on the widget's render target
    cb->beginPass(renderTarget(), QColor(Qt::white), { 1.0f, 0 }, updates);

    // Create pipeline on first use (needs renderPassDescriptor)
    if (!mCompositePipeline)
    {
        QShader vertShader = QShader::fromSerialized(
            /* load from compiled .qsb resource */);
        QShader fragShader = QShader::fromSerialized(
            /* load from compiled .qsb resource */);

        mCompositeBindings = mRhi->newShaderResourceBindings();
        // Bindings will be set per-layer texture

        mCompositePipeline = mRhi->newGraphicsPipeline();
        mCompositePipeline->setShaderStages({
            { QRhiShaderStage::Vertex, vertShader },
            { QRhiShaderStage::Fragment, fragShader }
        });

        QRhiVertexInputLayout inputLayout;
        inputLayout.setBindings({ { 4 * sizeof(float) } });
        inputLayout.setAttributes({
            { 0, 0, QRhiVertexInputAttribute::Float2, 0 },
            { 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) }
        });
        mCompositePipeline->setVertexInputLayout(inputLayout);

        // Enable alpha blending (premultiplied)
        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::One;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        mCompositePipeline->setTargetBlends({ blend });

        mCompositePipeline->setShaderResourceBindings(mCompositeBindings);
        mCompositePipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        mCompositePipeline->create();
    }

    // Draw each layer texture as a fullscreen quad
    cb->setGraphicsPipeline(mCompositePipeline);
    cb->setViewport({ 0, 0, float(outputSize.width()), float(outputSize.height()) });
    const QRhiCommandBuffer::VertexInput vbufBinding(mQuadVertexBuffer, 0);
    cb->setVertexInput(0, 1, &vbufBinding, mQuadIndexBuffer, 0, QRhiCommandBuffer::IndexUInt16);

    for (const auto& buffer : mPaintBuffers)
    {
        if (auto* rhiBuffer = dynamic_cast<QCPPaintBufferRhi*>(buffer.data()))
        {
            if (rhiBuffer->texture())
            {
                // Create per-draw shader resource bindings for this texture
                auto* srb = mRhi->newShaderResourceBindings();
                srb->setBindings({
                    QRhiShaderResourceBinding::sampledTexture(
                        1, QRhiShaderResourceBinding::FragmentStage,
                        rhiBuffer->texture(), mSampler)
                });
                srb->create();
                cb->setShaderResources(srb);
                cb->drawIndexed(6);
                // Note: srb ownership/caching needs refinement
            }
        }
    }

    cb->endPass();
}
```

Note: The `render()` implementation above is a starting scaffold. The shader loading, SRB caching, and resource management will need refinement during implementation. The key architectural pattern is correct: upload staging images, then composite each layer as a textured quad.

**Step 5: Implement releaseResources()**

```cpp
void QCustomPlot::releaseResources()
{
    delete mCompositePipeline;
    mCompositePipeline = nullptr;
    delete mCompositeBindings;
    mCompositeBindings = nullptr;
    delete mSampler;
    mSampler = nullptr;
    delete mQuadVertexBuffer;
    mQuadVertexBuffer = nullptr;
    delete mQuadIndexBuffer;
    mQuadIndexBuffer = nullptr;
    mRhi = nullptr;
    mRhiInitialized = false;
}
```

**Step 6: Remove paintEvent()**

Delete the `QCustomPlot::paintEvent()` method from `core.cpp`. The `render()` method replaces it.

**Step 7: Update createPaintBuffer()**

Change `QCustomPlot::createPaintBuffer()` to create `QCPPaintBufferRhi` when RHI is available:

```cpp
QCPAbstractPaintBuffer* QCustomPlot::createPaintBuffer(const QString& layerName)
{
    if (mRhi)
        return new QCPPaintBufferRhi(viewport().size(), mBufferDevicePixelRatio, layerName, mRhi);
    else
        return new QCPPaintBufferPixmap(viewport().size(), mBufferDevicePixelRatio, layerName);
}
```

Add `#include "painting/paintbuffer-rhi.h"` to core.cpp.

**Step 8: Verify compilation**

```bash
meson setup --wipe build && meson compile -C build
```

**Step 9: Run tests**

```bash
meson test --print-errorlogs -C build
```

**Step 10: Commit**

```bash
git add -u
git commit -m "Migrate QCustomPlot from QWidget to QRhiWidget

QCustomPlot now inherits QRhiWidget. Rendering composites layer
textures directly on screen via RHI without GPU-to-CPU readback.
Uses Metal on macOS, Vulkan on Linux, D3D on Windows."
```

---

### Task 6: Add staging image accessor and refine paint buffer API

The `render()` method needs access to the staging image for texture upload.

**Files:**
- Modify: `src/painting/paintbuffer-rhi.h`

**Step 1: Add accessor**

Add to `QCPPaintBufferRhi` public section:

```cpp
const QImage& stagingImage() const { return mStagingImage; }
```

**Step 2: Commit**

```bash
git add src/painting/paintbuffer-rhi.h
git commit -m "Add stagingImage() accessor to QCPPaintBufferRhi"
```

---

### Task 7: Update CI workflow

**Files:**
- Modify: `.github/workflows/tests.yml`

**Step 1: Update Qt version requirement**

Ensure `QT_VERSION` is >= 6.10. Currently set to 6.10.2, so this should already be fine.

**Step 2: Remove OpenGL-specific dependencies if no longer needed**

In the Linux install steps, `libgl-dev`, `libglx-dev`, `libegl-dev` may still be needed by Qt's RHI OpenGL fallback. Keep them for now.

**Step 3: Verify CI build passes**

Push to a branch and check the CI run.

**Step 4: Commit (if changes needed)**

```bash
git add .github/workflows/tests.yml
git commit -m "Update CI for QRhiWidget rendering backend"
```

---

### Task 8: Manual testing and profiling

This is a manual verification task, not automated.

**Step 1: Build with Tracy profiling**

```bash
meson setup -Dtracy_enable=true --buildtype=debugoptimized build-profile
meson compile -C build-profile
```

**Step 2: Run the manual test application**

```bash
./build-profile/tests/manual/manual
```

Verify:
- Plot renders correctly on screen
- Panning and zooming work
- No visual artifacts
- Tracy shows no `toImage` or `glGetTexImage` calls in the hot path

**Step 3: Run the big_data_for_tracy test**

```bash
./build-profile/tests/big_data_for_tracy/big_data_for_tracy
```

Compare frame times with the old OpenGL backend.

**Step 4: Test export paths**

In the manual test app or via code:
- `savePdf()` produces correct vectorized output
- `savePng()` produces correct raster output
- `toPixmap()` returns correct image

**Step 5: Test on macOS (if available)**

This is the platform where the biggest improvement is expected. Verify smooth rendering without the Metal-to-OpenGL translation overhead.

---

### Task 9: Clean up residual OpenGL references

**Files:**
- Modify: `src/doc-performance.dox` (update documentation references to OpenGL)
- Modify: `src/doc-mainpage.dox` (update references to `setOpenGl`)
- Modify: any other files still referencing `setOpenGl`, `QCP_OPENGL_FBO`, etc.

**Step 1: Search for remaining references**

```bash
grep -rn "OpenGl\|OPENGL\|openGl\|setOpenGl\|QCP_OPENGL\|NEOQCP_BATCH\|NEOQCP_USE_OPENGL\|QCUSTOMPLOT_USE_OPENGL\|NEOQCP_MANUAL_GL" src/
```

**Step 2: Update or remove each reference**

Update documentation to reference QRhiWidget instead of OpenGL. Remove any dead `#ifdef` branches.

**Step 3: Verify build and tests**

```bash
meson setup --wipe build && meson compile -C build && meson test --print-errorlogs -C build
```

**Step 4: Commit**

```bash
git add -u
git commit -m "Clean up residual OpenGL references in docs and code"
```

#include "colormap-rhi-layer.h"
#include "embedded_shaders.h"
#include "rhi-utils.h"
#include "Profiling.hpp"

QCPColormapRhiLayer::QCPColormapRhiLayer(QRhi* rhi)
    : mRhi(rhi)
{
}

QCPColormapRhiLayer::~QCPColormapRhiLayer()
{
    delete mPipeline;
    delete mSrb;
    delete mSampler;
    delete mVertexBuffer;
    delete mTexture;
}

void QCPColormapRhiLayer::clear()
{
    mStagingImage = QImage();
}

void QCPColormapRhiLayer::invalidatePipeline()
{
    delete mPipeline;
    mPipeline = nullptr;
    delete mSrb;
    mSrb = nullptr;
}

void QCPColormapRhiLayer::setImage(const QImage& image)
{
    mStagingImage = image;
    mTextureDirty = true;
}

void QCPColormapRhiLayer::setQuadRect(const QRectF& pixelRect)
{
    mQuadPixelRect = pixelRect;
    mGeometryDirty = true;
}

void QCPColormapRhiLayer::setScissorRect(const QRect& scissor)
{
    mScissorRect = scissor;
}

bool QCPColormapRhiLayer::ensurePipeline(QRhiRenderPassDescriptor* rpDesc,
                                          int sampleCount,
                                          QRhiBuffer* compositeUbo)
{
    PROFILE_HERE_N("QCPColormapRhiLayer::ensurePipeline");
    if (mPipeline && mLastSampleCount == sampleCount)
        return true;

    invalidatePipeline();

    // Reuse composite shaders: position(float2) + texcoord(float2).
    // The shader requires a LayerParams UBO at binding 1 (always zero
    // translation for colormaps since they pre-compute NDC on the CPU).
    auto vertShader = qcp::rhi::loadEmbeddedShader(composite_vert_qsb_data, composite_vert_qsb_data_len);
    auto fragShader = qcp::rhi::loadEmbeddedShader(composite_frag_qsb_data, composite_frag_qsb_data_len);

    if (!vertShader.isValid() || !fragShader.isValid())
    {
        qDebug() << "Failed to load composite shaders for colormap";
        return false;
    }

    if (!mSampler)
    {
        mSampler = mRhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest,
                                     QRhiSampler::None,
                                     QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
        mSampler->create();
    }

    // Layout-only SRB for pipeline creation (nullptr texture is valid for layout)
    // Actual texture binding created in uploadResources() once texture exists
    auto* layoutSrb = mRhi->newShaderResourceBindings();
    layoutSrb->setBindings({
        QRhiShaderResourceBinding::sampledTexture(
            0, QRhiShaderResourceBinding::FragmentStage,
            nullptr, mSampler),
        QRhiShaderResourceBinding::uniformBuffer(
            1, QRhiShaderResourceBinding::VertexStage,
            compositeUbo)
    });
    layoutSrb->create();

    mPipeline = mRhi->newGraphicsPipeline();
    mPipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vertShader},
        {QRhiShaderStage::Fragment, fragShader}
    });

    // Vertex layout: position(float2) + texcoord(float2) — same as composite
    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({{4 * sizeof(float)}});
    inputLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},
        {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)}
    });
    mPipeline->setVertexInputLayout(inputLayout);

    mPipeline->setTargetBlends({qcp::rhi::premultipliedAlphaBlend()});

    mPipeline->setFlags(QRhiGraphicsPipeline::UsesScissor);
    mPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    mPipeline->setSampleCount(sampleCount);
    mPipeline->setRenderPassDescriptor(rpDesc);
    mPipeline->setShaderResourceBindings(layoutSrb);

    if (!mPipeline->create())
    {
        qDebug() << "Failed to create colormap pipeline";
        delete mPipeline;
        mPipeline = nullptr;
        delete layoutSrb;
        return false;
    }

    delete layoutSrb;
    mLastSampleCount = sampleCount;
    return true;
}

bool QCPColormapRhiLayer::ensureTexture(QRhiBuffer* compositeUbo)
{
    QSize imgSize = mStagingImage.size();
    bool textureRecreated = false;
    if (!mTexture || mTextureSize != imgSize)
    {
        delete mTexture;
        const auto fmt = qcp::rhi::preferredTextureFormat(mRhi);
        mTexture = mRhi->newTexture(fmt, imgSize);
        if (!mTexture->create())
        {
            qDebug() << "Failed to create colormap RHI texture";
            delete mTexture;
            mTexture = nullptr;
            delete mSrb;
            mSrb = nullptr;
            return false;
        }
        mTextureSize = imgSize;
        mTextureDirty = true;
        textureRecreated = true;
    }

    if ((!mSrb || textureRecreated) && mTexture)
    {
        delete mSrb;
        mSrb = mRhi->newShaderResourceBindings();
        mSrb->setBindings({
            QRhiShaderResourceBinding::sampledTexture(
                0, QRhiShaderResourceBinding::FragmentStage,
                mTexture, mSampler),
            QRhiShaderResourceBinding::uniformBuffer(
                1, QRhiShaderResourceBinding::VertexStage,
                compositeUbo)
        });
        mSrb->create();
    }
    return true;
}

void QCPColormapRhiLayer::updateQuadGeometry(QRhiResourceUpdateBatch* updates,
                                              const QSize& outputSize, float dpr,
                                              bool isYUpInNDC)
{
    if (!mGeometryDirty && mVertexBuffer && mLastOutputSize == outputSize)
        return;

    mLastOutputSize = outputSize;
    const float w = float(outputSize.width());
    const float h = float(outputSize.height());
    const float yFlip = isYUpInNDC ? -1.0f : 1.0f;

    auto toNDC = [&](float px, float py) -> std::pair<float, float> {
        float ndcX = (px * dpr / w) * 2.0f - 1.0f;
        float ndcY = yFlip * ((py * dpr / h) * 2.0f - 1.0f);
        return {ndcX, ndcY};
    };

    auto [x0, y0] = toNDC(mQuadPixelRect.left(), mQuadPixelRect.top());
    auto [x1, y1] = toNDC(mQuadPixelRect.right(), mQuadPixelRect.bottom());

    const float verts[] = {
        x0, y0,  0.0f, 0.0f,
        x1, y0,  1.0f, 0.0f,
        x0, y1,  0.0f, 1.0f,
        x1, y1,  1.0f, 1.0f,
    };

    if (!mVertexBuffer)
    {
        mVertexBuffer = mRhi->newBuffer(QRhiBuffer::Dynamic,
                                         QRhiBuffer::VertexBuffer,
                                         sizeof(verts));
        mVertexBuffer->create();
    }

    updates->updateDynamicBuffer(mVertexBuffer, 0, sizeof(verts), verts);
    mGeometryDirty = false;
}

void QCPColormapRhiLayer::uploadResources(QRhiResourceUpdateBatch* updates,
                                            const QSize& outputSize, float dpr,
                                            bool isYUpInNDC,
                                            QRhiBuffer* compositeUbo)
{
    PROFILE_HERE_N("QCPColormapRhiLayer::uploadResources");
    if (mStagingImage.isNull())
        return;

    if (!ensureTexture(compositeUbo))
        return;

    if (mTextureDirty)
    {
        QRhiTextureSubresourceUploadDescription subDesc(mStagingImage);
        updates->uploadTexture(mTexture, QRhiTextureUploadDescription(
            QRhiTextureUploadEntry(0, 0, subDesc)));
        mTextureDirty = false;
    }

    updateQuadGeometry(updates, outputSize, dpr, isYUpInNDC);
}

void QCPColormapRhiLayer::render(QRhiCommandBuffer* cb, const QSize& outputSize)
{
    PROFILE_HERE_N("QCPColormapRhiLayer::render");

    if (!mPipeline || !mVertexBuffer || !mSrb || !mTexture)
        return;

    cb->setGraphicsPipeline(mPipeline);
    cb->setViewport({0, 0, float(outputSize.width()), float(outputSize.height())});
    cb->setShaderResources(mSrb);

    const QRhiCommandBuffer::VertexInput vbufBinding(mVertexBuffer, 0);
    cb->setVertexInput(0, 1, &vbufBinding);

    cb->setScissor({mScissorRect.x(), mScissorRect.y(),
                    mScissorRect.width(), mScissorRect.height()});
    cb->draw(4);
}

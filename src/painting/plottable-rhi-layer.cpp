#include "plottable-rhi-layer.h"
#include "Profiling.hpp"
#include "embedded_shaders.h"

QCPPlottableRhiLayer::QCPPlottableRhiLayer(QRhi* rhi)
    : mRhi(rhi)
{
}

QCPPlottableRhiLayer::~QCPPlottableRhiLayer()
{
    delete mPipeline;
    delete mSrb;
    delete mUniformBuffer;
    delete mVertexBuffer;
}

void QCPPlottableRhiLayer::invalidatePipeline()
{
    delete mPipeline;
    mPipeline = nullptr;
    delete mSrb;
    mSrb = nullptr;
    delete mUniformBuffer;
    mUniformBuffer = nullptr;
}

void QCPPlottableRhiLayer::clear()
{
    mStagingVertices.clear();
    mDrawEntries.clear();
    mDirty = true;
}

QCPPlottableRhiLayer::DrawEntry
QCPPlottableRhiLayer::addPlottable(const QVector<float>& fillVerts,
                                    const QVector<float>& strokeVerts,
                                    const QRect& clipRect, double dpr,
                                    int outputHeight, bool isYUpInNDC)
{
    PROFILE_HERE_N("QCPPlottableRhiLayer::addPlottable");
    DrawEntry entry;
    int sx = clipRect.x() * dpr;
    int sy = clipRect.y() * dpr;
    int sw = clipRect.width() * dpr;
    int sh = clipRect.height() * dpr;
    // QRhi scissor Y origin is bottom for Y-up backends (OpenGL)
    if (isYUpInNDC)
        sy = outputHeight - sy - sh;
    entry.scissorRect = QRect(sx, sy, sw, sh);

    if (!fillVerts.isEmpty())
    {
        entry.fillOffset = mStagingVertices.size() / 6; // vertex index
        entry.fillVertexCount = fillVerts.size() / 6;
        mStagingVertices.append(fillVerts);
    }

    if (!strokeVerts.isEmpty())
    {
        entry.strokeOffset = mStagingVertices.size() / 6;
        entry.strokeVertexCount = strokeVerts.size() / 6;
        mStagingVertices.append(strokeVerts);
    }

    mDrawEntries.append(entry);
    mDirty = true;
    return entry;
}

bool QCPPlottableRhiLayer::ensurePipeline(QRhiRenderPassDescriptor* rpDesc,
                                           int sampleCount)
{
    PROFILE_HERE_N("QCPPlottableRhiLayer::ensurePipeline");
    if (mPipeline && mLastSampleCount == sampleCount)
        return true;

    // Rebuild pipeline if sample count changed or first creation
    invalidatePipeline();

    // Load shaders from embedded data
    QShader vertShader = QShader::fromSerialized(QByteArray::fromRawData(
        reinterpret_cast<const char*>(plottable_vert_qsb_data), plottable_vert_qsb_data_len));
    QShader fragShader = QShader::fromSerialized(QByteArray::fromRawData(
        reinterpret_cast<const char*>(plottable_frag_qsb_data), plottable_frag_qsb_data_len));

    if (!vertShader.isValid() || !fragShader.isValid())
    {
        qDebug() << "Failed to load plottable shaders";
        return false;
    }

    // Create uniform buffer for viewport params (24 bytes: width, height, yFlip, dpr, translateX, translateY)
    delete mUniformBuffer;
    mUniformBuffer = mRhi->newBuffer(QRhiBuffer::Dynamic,
                                      QRhiBuffer::UniformBuffer, 24);
    mUniformBuffer->create();

    // Create SRB binding the uniform buffer at binding 0
    delete mSrb;
    mSrb = mRhi->newShaderResourceBindings();
    mSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage, mUniformBuffer)
    });
    mSrb->create();

    mPipeline = mRhi->newGraphicsPipeline();
    mPipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vertShader},
        {QRhiShaderStage::Fragment, fragShader}
    });

    // Vertex layout: (x, y) float2 + (r, g, b, a) float4
    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({{6 * sizeof(float)}});
    inputLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},
        {0, 1, QRhiVertexInputAttribute::Float4, 2 * sizeof(float)}
    });
    mPipeline->setVertexInputLayout(inputLayout);

    // Premultiplied alpha blending
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::One;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    mPipeline->setTargetBlends({blend});

    mPipeline->setFlags(QRhiGraphicsPipeline::UsesScissor);
    mPipeline->setTopology(QRhiGraphicsPipeline::Triangles);
    mPipeline->setSampleCount(sampleCount);
    mPipeline->setRenderPassDescriptor(rpDesc);
    mPipeline->setShaderResourceBindings(mSrb);

    if (!mPipeline->create())
    {
        qDebug() << "Failed to create plottable pipeline";
        delete mPipeline;
        mPipeline = nullptr;
        return false;
    }

    mLastSampleCount = sampleCount;
    return true;
}

void QCPPlottableRhiLayer::uploadResources(QRhiResourceUpdateBatch* updates,
                                            const QSize& outputSize, float dpr,
                                            bool isYUpInNDC)
{
    PROFILE_HERE_N("QCPPlottableRhiLayer::uploadResources");
    // Upload uniform buffer (viewport params) — always, since output size may change
    if (mUniformBuffer)
    {
        struct { float width, height, yFlip, dpr, translateX, translateY; } params = {
            float(outputSize.width()),
            float(outputSize.height()),
            isYUpInNDC ? -1.0f : 1.0f,
            dpr,
            float(mPixelOffset.x()),
            float(mPixelOffset.y())
        };
        updates->updateDynamicBuffer(mUniformBuffer, 0, 24, &params);
    }

    if (!mDirty || mStagingVertices.isEmpty())
        return;

    int requiredSize = mStagingVertices.size() * sizeof(float);

    if (!mVertexBuffer || mVertexBufferSize < requiredSize)
    {
        delete mVertexBuffer;
        mVertexBuffer = mRhi->newBuffer(QRhiBuffer::Dynamic,
                                         QRhiBuffer::VertexBuffer,
                                         requiredSize);
        mVertexBuffer->create();
        mVertexBufferSize = requiredSize;
    }

    updates->updateDynamicBuffer(mVertexBuffer, 0, requiredSize,
                                  mStagingVertices.constData());
    mDirty = false;
}

void QCPPlottableRhiLayer::render(QRhiCommandBuffer* cb,
                                   const QSize& outputSize)
{
    PROFILE_HERE_N("QCPPlottableRhiLayer::render");

    if (!mPipeline || !mVertexBuffer || !mSrb || mDrawEntries.isEmpty())
        return;

    cb->setGraphicsPipeline(mPipeline);
    cb->setViewport({0, 0, float(outputSize.width()), float(outputSize.height())});
    cb->setShaderResources(mSrb);

    const QRhiCommandBuffer::VertexInput vbufBinding(mVertexBuffer, 0);
    cb->setVertexInput(0, 1, &vbufBinding);

    for (const auto& entry : mDrawEntries)
    {
        cb->setScissor({entry.scissorRect.x(), entry.scissorRect.y(),
                        entry.scissorRect.width(), entry.scissorRect.height()});

        if (entry.fillVertexCount > 0)
            cb->draw(entry.fillVertexCount, 1, entry.fillOffset, 0);
        if (entry.strokeVertexCount > 0)
            cb->draw(entry.strokeVertexCount, 1, entry.strokeOffset, 0);
    }
}

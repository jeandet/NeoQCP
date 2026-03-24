#include "grid-rhi-layer.h"
#include "Profiling.hpp"
#include "embedded_shaders.h"
#include "../axis/axis.h"
#include "../layoutelements/layoutelement-axisrect.h"

#include <array>
#include <cmath>

static constexpr int kFloatsPerVertex = 11;
static constexpr int kUniformBufferSize = 64;

QCPGridRhiLayer::QCPGridRhiLayer(QRhi* rhi)
    : mRhi(rhi)
{
}

QCPGridRhiLayer::~QCPGridRhiLayer()
{
    delete mPipeline;
    delete mLayoutSrb;
    delete mLayoutUbo;
    delete mVertexBuffer;
    cleanupDrawGroups();
}

void QCPGridRhiLayer::markGeometryDirty() { mGeometryDirty = true; }

void QCPGridRhiLayer::cleanupDrawGroups()
{
    for (auto& group : mDrawGroups)
    {
        delete group.uniformBuffer;
        delete group.srb;
    }
    mDrawGroups.clear();
}

void QCPGridRhiLayer::invalidatePipeline()
{
    delete mPipeline;
    mPipeline = nullptr;
    delete mLayoutSrb;
    mLayoutSrb = nullptr;
    delete mLayoutUbo;
    mLayoutUbo = nullptr;
    cleanupDrawGroups();
}

bool QCPGridRhiLayer::ensurePipeline(QRhiRenderPassDescriptor* rpDesc, int sampleCount)
{
    PROFILE_HERE_N("QCPGridRhiLayer::ensurePipeline");

    if (mPipeline && mLastSampleCount == sampleCount)
        return true;

    invalidatePipeline();

    QShader vertShader = QShader::fromSerialized(QByteArray::fromRawData(
        reinterpret_cast<const char*>(span_vert_qsb_data), span_vert_qsb_data_len));
    QShader fragShader = QShader::fromSerialized(QByteArray::fromRawData(
        reinterpret_cast<const char*>(plottable_frag_qsb_data), plottable_frag_qsb_data_len));

    if (!vertShader.isValid() || !fragShader.isValid())
        return false;

    mLayoutUbo = mRhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUniformBufferSize);
    mLayoutUbo->create();

    mLayoutSrb = mRhi->newShaderResourceBindings();
    mLayoutSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage, mLayoutUbo)
    });
    mLayoutSrb->create();

    mPipeline = mRhi->newGraphicsPipeline();
    mPipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vertShader},
        {QRhiShaderStage::Fragment, fragShader}
    });

    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({{kFloatsPerVertex * sizeof(float)}});
    inputLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},
        {0, 1, QRhiVertexInputAttribute::Float4, 2 * sizeof(float)},
        {0, 2, QRhiVertexInputAttribute::Float2, 6 * sizeof(float)},
        {0, 3, QRhiVertexInputAttribute::Float,  8 * sizeof(float)},
        {0, 4, QRhiVertexInputAttribute::Float2, 9 * sizeof(float)},
    });
    mPipeline->setVertexInputLayout(inputLayout);

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
    mPipeline->setShaderResourceBindings(mLayoutSrb);

    if (!mPipeline->create())
    {
        delete mPipeline;
        mPipeline = nullptr;
        return false;
    }

    mLastSampleCount = sampleCount;
    return true;
}

void QCPGridRhiLayer::rebuildGeometry(float /*dpr*/, int /*outputHeight*/, bool /*isYUpInNDC*/)
{
    // Stub — filled in later
    mStagingVertices.clear();
    cleanupDrawGroups();
}

void QCPGridRhiLayer::uploadResources(QRhiResourceUpdateBatch* /*updates*/,
                                       const QSize& /*outputSize*/, float /*dpr*/,
                                       bool /*isYUpInNDC*/)
{
    // Stub — filled in later
}

void QCPGridRhiLayer::renderGroups(QRhiCommandBuffer* cb, const QSize& outputSize, bool gridLines)
{
    if (!mPipeline || !mVertexBuffer || mDrawGroups.isEmpty())
        return;

    cb->setGraphicsPipeline(mPipeline);
    cb->setViewport({0, 0, float(outputSize.width()), float(outputSize.height())});

    const QRhiCommandBuffer::VertexInput vbufBinding(mVertexBuffer, 0);
    cb->setVertexInput(0, 1, &vbufBinding);

    for (const auto& group : mDrawGroups)
    {
        if (group.isGridLines != gridLines)
            continue;
        if (!group.scissorRect.isNull())
            cb->setScissor({group.scissorRect.x(), group.scissorRect.y(),
                            group.scissorRect.width(), group.scissorRect.height()});
        else
            cb->setScissor({0, 0, outputSize.width(), outputSize.height()});
        cb->setShaderResources(group.srb);
        cb->draw(group.vertexCount, 1, group.vertexOffset, 0);
    }
}

void QCPGridRhiLayer::renderGridLines(QRhiCommandBuffer* cb, const QSize& outputSize)
{
    PROFILE_HERE_N("QCPGridRhiLayer::renderGridLines");
    renderGroups(cb, outputSize, true);
}

void QCPGridRhiLayer::renderTickMarks(QRhiCommandBuffer* cb, const QSize& outputSize)
{
    PROFILE_HERE_N("QCPGridRhiLayer::renderTickMarks");
    renderGroups(cb, outputSize, false);
}

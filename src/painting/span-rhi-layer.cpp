#include "span-rhi-layer.h"
#include "Profiling.hpp"
#include "embedded_shaders.h"

#include "../items/item-vspan.h"
#include "../items/item-hspan.h"
#include "../items/item-rspan.h"
#include "../layoutelements/layoutelement-axisrect.h"
#include "../axis/axis.h"

#include <array>
#include <cmath>

static constexpr int kFloatsPerVertex = 11;
// Uniform buffer size: 15 floats padded to 16-byte alignment = 64 bytes
static constexpr int kUniformBufferSize = 64;

static auto premultiply(const QColor& c) -> std::array<float, 4>
{
    float a = c.alphaF();
    return {float(c.redF() * a), float(c.greenF() * a), float(c.blueF() * a), a};
}

static void appendVertex(QVector<float>& buf, float x, float y,
                          const std::array<float, 4>& rgba,
                          float extX, float extY, float extW,
                          float isPixelX, float isPixelY)
{
    buf.append(x);
    buf.append(y);
    buf.append(rgba[0]);
    buf.append(rgba[1]);
    buf.append(rgba[2]);
    buf.append(rgba[3]);
    buf.append(extX);
    buf.append(extY);
    buf.append(extW);
    buf.append(isPixelX);
    buf.append(isPixelY);
}

// Emit 6 vertices (2 triangles) for a quad defined by 4 corners:
//   TL--TR
//   |  / |
//   BL--BR
static void appendQuad(QVector<float>& buf,
                        float tlX, float tlY, float trX, float trY,
                        float blX, float blY, float brX, float brY,
                        const std::array<float, 4>& rgba,
                        float extX, float extY, float extW,
                        float isPixelX, float isPixelY)
{
    // Triangle 1: TL, BL, TR
    appendVertex(buf, tlX, tlY, rgba, extX, extY, extW, isPixelX, isPixelY);
    appendVertex(buf, blX, blY, rgba, extX, extY, extW, isPixelX, isPixelY);
    appendVertex(buf, trX, trY, rgba, extX, extY, extW, isPixelX, isPixelY);
    // Triangle 2: TR, BL, BR
    appendVertex(buf, trX, trY, rgba, extX, extY, extW, isPixelX, isPixelY);
    appendVertex(buf, blX, blY, rgba, extX, extY, extW, isPixelX, isPixelY);
    appendVertex(buf, brX, brY, rgba, extX, extY, extW, isPixelX, isPixelY);
}

// Emit a border line as a quad with extrude direction.
// The border runs between (x0,y0) and (x1,y1). extrudeDir is perpendicular.
// We emit 6 vertices: 3 with +extrudeWidth, 3 with -extrudeWidth.
static void appendBorder(QVector<float>& buf,
                          float x0, float y0, float x1, float y1,
                          const std::array<float, 4>& rgba,
                          float extDirX, float extDirY, float halfWidth,
                          float isPixelX, float isPixelY)
{
    // TL = (x0,y0) +extrude, TR = (x1,y1) +extrude
    // BL = (x0,y0) -extrude, BR = (x1,y1) -extrude
    appendVertex(buf, x0, y0, rgba, extDirX, extDirY, halfWidth, isPixelX, isPixelY);
    appendVertex(buf, x0, y0, rgba, -extDirX, -extDirY, halfWidth, isPixelX, isPixelY);
    appendVertex(buf, x1, y1, rgba, extDirX, extDirY, halfWidth, isPixelX, isPixelY);

    appendVertex(buf, x1, y1, rgba, extDirX, extDirY, halfWidth, isPixelX, isPixelY);
    appendVertex(buf, x0, y0, rgba, -extDirX, -extDirY, halfWidth, isPixelX, isPixelY);
    appendVertex(buf, x1, y1, rgba, -extDirX, -extDirY, halfWidth, isPixelX, isPixelY);
}

QCPSpanRhiLayer::QCPSpanRhiLayer(QRhi* rhi)
    : mRhi(rhi)
{
}

QCPSpanRhiLayer::~QCPSpanRhiLayer()
{
    delete mPipeline;
    delete mLayoutSrb;
    delete mLayoutUbo;
    delete mVertexBuffer;
    cleanupDrawGroups();
}

void QCPSpanRhiLayer::registerSpan(QCPAbstractItem* span)
{
    if (!mSpans.contains(span))
    {
        mSpans.append(span);
        mGeometryDirty = true;
    }
}

void QCPSpanRhiLayer::unregisterSpan(QCPAbstractItem* span)
{
    if (mSpans.removeOne(span))
        mGeometryDirty = true;
}

void QCPSpanRhiLayer::markGeometryDirty()
{
    mGeometryDirty = true;
}

void QCPSpanRhiLayer::cleanupDrawGroups()
{
    for (auto& group : mDrawGroups)
    {
        delete group.uniformBuffer;
        delete group.srb;
    }
    mDrawGroups.clear();
}

void QCPSpanRhiLayer::invalidatePipeline()
{
    delete mPipeline;
    mPipeline = nullptr;
    delete mLayoutSrb;
    mLayoutSrb = nullptr;
    delete mLayoutUbo;
    mLayoutUbo = nullptr;
    cleanupDrawGroups();
}

bool QCPSpanRhiLayer::ensurePipeline(QRhiRenderPassDescriptor* rpDesc, int sampleCount)
{
    PROFILE_HERE_N("QCPSpanRhiLayer::ensurePipeline");

    if (mPipeline && mLastSampleCount == sampleCount)
        return true;

    invalidatePipeline();

    QShader vertShader = QShader::fromSerialized(QByteArray::fromRawData(
        reinterpret_cast<const char*>(span_vert_qsb_data), span_vert_qsb_data_len));
    QShader fragShader = QShader::fromSerialized(QByteArray::fromRawData(
        reinterpret_cast<const char*>(plottable_frag_qsb_data), plottable_frag_qsb_data_len));

    if (!vertShader.isValid() || !fragShader.isValid())
    {
        qDebug() << "Failed to load span shaders";
        return false;
    }

    // Layout-only UBO + SRB: the SRB defines the binding layout for the pipeline.
    // Per-group SRBs with real data are used at draw time.
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

    // Vertex layout: stride = 11 * sizeof(float) = 44 bytes
    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({{kFloatsPerVertex * sizeof(float)}});
    inputLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},                        // dataCoord
        {0, 1, QRhiVertexInputAttribute::Float4, 2 * sizeof(float)},        // color
        {0, 2, QRhiVertexInputAttribute::Float2, 6 * sizeof(float)},        // extrudeDir
        {0, 3, QRhiVertexInputAttribute::Float,  8 * sizeof(float)},        // extrudeWidth
        {0, 4, QRhiVertexInputAttribute::Float2, 9 * sizeof(float)},        // isPixel
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
    mPipeline->setShaderResourceBindings(mLayoutSrb);

    if (!mPipeline->create())
    {
        qDebug() << "Failed to create span pipeline";
        delete mPipeline;
        mPipeline = nullptr;
        return false;
    }

    mLastSampleCount = sampleCount;
    return true;
}

void QCPSpanRhiLayer::rebuildGeometry(float dpr, int outputHeight, bool isYUpInNDC)
{
    PROFILE_HERE_N("QCPSpanRhiLayer::rebuildGeometry");

    mStagingVertices.clear();
    cleanupDrawGroups();

    // Group spans by axis rect
    QMap<QCPAxisRect*, QVector<QCPAbstractItem*>> groupedSpans;
    for (auto* span : mSpans)
    {
        QCPAxisRect* ar = span->clipAxisRect();
        if (!ar)
            continue;
        groupedSpans[ar].append(span);
    }

    for (auto it = groupedSpans.constBegin(); it != groupedSpans.constEnd(); ++it)
    {
        QCPAxisRect* ar = it.key();
        const auto& spans = it.value();

        int groupVertexStart = mStagingVertices.size() / kFloatsPerVertex;

        for (auto* span : spans)
        {
            if (auto* vspan = qobject_cast<QCPItemVSpan*>(span))
            {
                float dataX0 = float(vspan->lowerEdge->coords().x());
                float dataX1 = float(vspan->upperEdge->coords().x());
                float pixTop = float(ar->top());
                float pixBot = float(ar->top() + ar->height());

                // Fill quad: X = data coords, Y = pixel coords
                auto fillColor = premultiply(vspan->brush().color());
                if (vspan->brush().style() != Qt::NoBrush && fillColor[3] > 0.0f)
                {
                    appendQuad(mStagingVertices,
                               dataX0, pixTop, dataX1, pixTop,
                               dataX0, pixBot, dataX1, pixBot,
                               fillColor, 0, 0, 0, 0, 1);
                }

                // Border lines (vertical edges)
                auto borderColor = premultiply(vspan->borderPen().color());
                float halfW = float(vspan->borderPen().widthF()) / 2.0f;
                if (vspan->borderPen().style() != Qt::NoPen && halfW > 0.0f)
                {
                    // Left border (at dataX0): vertical line from pixTop to pixBot
                    appendBorder(mStagingVertices,
                                 dataX0, pixTop, dataX0, pixBot,
                                 borderColor, 1, 0, halfW, 0, 1);
                    // Right border (at dataX1)
                    appendBorder(mStagingVertices,
                                 dataX1, pixTop, dataX1, pixBot,
                                 borderColor, 1, 0, halfW, 0, 1);
                }
            }
            else if (auto* hspan = qobject_cast<QCPItemHSpan*>(span))
            {
                float dataY0 = float(hspan->lowerEdge->coords().y());
                float dataY1 = float(hspan->upperEdge->coords().y());
                float pixLeft = float(ar->left());
                float pixRight = float(ar->left() + ar->width());

                // Fill quad: Y = data coords, X = pixel coords
                auto fillColor = premultiply(hspan->brush().color());
                if (hspan->brush().style() != Qt::NoBrush && fillColor[3] > 0.0f)
                {
                    appendQuad(mStagingVertices,
                               pixLeft, dataY0, pixRight, dataY0,
                               pixLeft, dataY1, pixRight, dataY1,
                               fillColor, 0, 0, 0, 1, 0);
                }

                // Border lines (horizontal edges)
                auto borderColor = premultiply(hspan->borderPen().color());
                float halfW = float(hspan->borderPen().widthF()) / 2.0f;
                if (hspan->borderPen().style() != Qt::NoPen && halfW > 0.0f)
                {
                    // Top border (at dataY0): horizontal line from pixLeft to pixRight
                    appendBorder(mStagingVertices,
                                 pixLeft, dataY0, pixRight, dataY0,
                                 borderColor, 0, 1, halfW, 1, 0);
                    // Bottom border (at dataY1)
                    appendBorder(mStagingVertices,
                                 pixLeft, dataY1, pixRight, dataY1,
                                 borderColor, 0, 1, halfW, 1, 0);
                }
            }
            else if (auto* rspan = qobject_cast<QCPItemRSpan*>(span))
            {
                float dataLeft = float(rspan->leftEdge->coords().x());
                float dataRight = float(rspan->rightEdge->coords().x());
                float dataTop = float(rspan->topEdge->coords().y());
                float dataBot = float(rspan->bottomEdge->coords().y());

                // Fill quad: all data coords
                auto fillColor = premultiply(rspan->brush().color());
                if (rspan->brush().style() != Qt::NoBrush && fillColor[3] > 0.0f)
                {
                    appendQuad(mStagingVertices,
                               dataLeft, dataTop, dataRight, dataTop,
                               dataLeft, dataBot, dataRight, dataBot,
                               fillColor, 0, 0, 0, 0, 0);
                }

                // 4 border lines
                auto borderColor = premultiply(rspan->borderPen().color());
                float halfW = float(rspan->borderPen().widthF()) / 2.0f;
                if (rspan->borderPen().style() != Qt::NoPen && halfW > 0.0f)
                {
                    // Left border: vertical line
                    appendBorder(mStagingVertices,
                                 dataLeft, dataTop, dataLeft, dataBot,
                                 borderColor, 1, 0, halfW, 0, 0);
                    // Right border
                    appendBorder(mStagingVertices,
                                 dataRight, dataTop, dataRight, dataBot,
                                 borderColor, 1, 0, halfW, 0, 0);
                    // Top border: horizontal line
                    appendBorder(mStagingVertices,
                                 dataLeft, dataTop, dataRight, dataTop,
                                 borderColor, 0, 1, halfW, 0, 0);
                    // Bottom border
                    appendBorder(mStagingVertices,
                                 dataLeft, dataBot, dataRight, dataBot,
                                 borderColor, 0, 1, halfW, 0, 0);
                }
            }
        }

        int groupVertexCount = mStagingVertices.size() / kFloatsPerVertex - groupVertexStart;
        if (groupVertexCount == 0)
            continue;

        // Compute scissor rect in physical pixels
        int sx = int(ar->left() * dpr);
        int sy = int(ar->top() * dpr);
        int sw = int(ar->width() * dpr);
        int sh = int(ar->height() * dpr);
        if (isYUpInNDC)
            sy = outputHeight - sy - sh;

        // Create per-group uniform buffer and SRB
        auto* ubo = mRhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUniformBufferSize);
        ubo->create();

        auto* srb = mRhi->newShaderResourceBindings();
        srb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage, ubo)
        });
        srb->create();

        DrawGroup group;
        group.axisRect = ar;
        group.vertexOffset = groupVertexStart;
        group.vertexCount = groupVertexCount;
        group.scissorRect = QRect(sx, sy, sw, sh);
        group.uniformBuffer = ubo;
        group.srb = srb;
        mDrawGroups.append(group);
    }
}

void QCPSpanRhiLayer::uploadResources(QRhiResourceUpdateBatch* updates,
                                        const QSize& outputSize, float dpr,
                                        bool isYUpInNDC)
{
    PROFILE_HERE_N("QCPSpanRhiLayer::uploadResources");

    if (mGeometryDirty)
    {
        rebuildGeometry(dpr, outputSize.height(), isYUpInNDC);
        mGeometryDirty = false;

        if (!mStagingVertices.isEmpty())
        {
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
        }
    }

    // Always update uniform buffers with current axis ranges
    for (auto& group : mDrawGroups)
    {
        QCPAxisRect* ar = group.axisRect;
        QCPAxis* keyAxis = ar->axis(QCPAxis::atBottom);
        QCPAxis* valAxis = ar->axis(QCPAxis::atLeft);
        if (!keyAxis || !valAxis)
            continue;

        float keyOffset, keyLength;
        if (!keyAxis->rangeReversed())
        {
            keyOffset = float(ar->left());
            keyLength = float(ar->width());
        }
        else
        {
            keyOffset = float(ar->right());
            keyLength = float(-ar->width());
        }

        float valOffset, valLength;
        if (!valAxis->rangeReversed())
        {
            valOffset = float(ar->bottom());
            valLength = float(-ar->height());
        }
        else
        {
            valOffset = float(ar->top());
            valLength = float(ar->height());
        }

        struct {
            float width, height, yFlip, dpr;
            float keyRangeLower, keyRangeUpper, keyAxisOffset, keyAxisLength, keyLogScale;
            float valRangeLower, valRangeUpper, valAxisOffset, valAxisLength, valLogScale;
            float _pad;
        } params = {
            float(outputSize.width()),
            float(outputSize.height()),
            isYUpInNDC ? -1.0f : 1.0f,
            dpr,
            float(keyAxis->range().lower),
            float(keyAxis->range().upper),
            keyOffset,
            keyLength,
            (keyAxis->scaleType() == QCPAxis::stLogarithmic) ? 1.0f : 0.0f,
            float(valAxis->range().lower),
            float(valAxis->range().upper),
            valOffset,
            valLength,
            (valAxis->scaleType() == QCPAxis::stLogarithmic) ? 1.0f : 0.0f,
            0.0f
        };

        updates->updateDynamicBuffer(group.uniformBuffer, 0, kUniformBufferSize, &params);
    }
}

void QCPSpanRhiLayer::render(QRhiCommandBuffer* cb, const QSize& outputSize)
{
    PROFILE_HERE_N("QCPSpanRhiLayer::render");

    if (!mPipeline || !mVertexBuffer || mDrawGroups.isEmpty())
        return;

    cb->setGraphicsPipeline(mPipeline);
    cb->setViewport({0, 0, float(outputSize.width()), float(outputSize.height())});

    const QRhiCommandBuffer::VertexInput vbufBinding(mVertexBuffer, 0);
    cb->setVertexInput(0, 1, &vbufBinding);

    for (const auto& group : mDrawGroups)
    {
        cb->setScissor({group.scissorRect.x(), group.scissorRect.y(),
                        group.scissorRect.width(), group.scissorRect.height()});
        cb->setShaderResources(group.srb);
        cb->draw(group.vertexCount, 1, group.vertexOffset, 0);
    }
}

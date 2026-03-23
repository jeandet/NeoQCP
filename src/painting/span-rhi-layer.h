#pragma once

#include <QColor>
#include <QPointF>
#include <QRect>
#include <QVector>
#include <QMap>
#include <rhi/qrhi.h>

class QCPAbstractItem;
class QCPAxisRect;

class QCPSpanRhiLayer
{
public:
    struct DrawGroup
    {
        QCPAxisRect* axisRect = nullptr;
        int vertexOffset = 0;
        int vertexCount = 0;
        QRect scissorRect;
        QRhiBuffer* uniformBuffer = nullptr;
        QRhiShaderResourceBindings* srb = nullptr;
    };

    explicit QCPSpanRhiLayer(QRhi* rhi);
    ~QCPSpanRhiLayer();

    void registerSpan(QCPAbstractItem* span);
    void unregisterSpan(QCPAbstractItem* span);
    void markGeometryDirty();

    bool hasSpans() const { return !mSpans.isEmpty(); }

    void invalidatePipeline();
    bool ensurePipeline(QRhiRenderPassDescriptor* rpDesc, int sampleCount);
    void uploadResources(QRhiResourceUpdateBatch* updates,
                          const QSize& outputSize, float dpr, bool isYUpInNDC);
    void render(QRhiCommandBuffer* cb, const QSize& outputSize);

private:
    void rebuildGeometry(float dpr, int outputHeight, bool isYUpInNDC);
    void cleanupDrawGroups();

    QRhi* mRhi;
    QVector<QCPAbstractItem*> mSpans;

    QVector<float> mStagingVertices;
    QVector<DrawGroup> mDrawGroups;
    bool mGeometryDirty = true;

    QRhiBuffer* mVertexBuffer = nullptr;
    QRhiGraphicsPipeline* mPipeline = nullptr;
    QRhiShaderResourceBindings* mLayoutSrb = nullptr;
    QRhiBuffer* mLayoutUbo = nullptr;
    int mVertexBufferSize = 0;
    int mLastSampleCount = 0;
};

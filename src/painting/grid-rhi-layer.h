#pragma once

#include <QColor>
#include <QPointF>
#include <QRect>
#include <QVector>
#include <QMap>
#include <rhi/qrhi.h>

class QCPAxis;
class QCPAxisRect;

class QCPGridRhiLayer
{
public:
    struct DrawGroup
    {
        QCPAxisRect* axisRect = nullptr;
        int vertexOffset = 0;
        int vertexCount = 0;
        QRect scissorRect;             // empty = no scissor (tick marks)
        QRhiBuffer* uniformBuffer = nullptr;
        QRhiShaderResourceBindings* srb = nullptr;
        bool isGridLines = true;       // false = tick marks
    };

    explicit QCPGridRhiLayer(QRhi* rhi);
    ~QCPGridRhiLayer();

    void markGeometryDirty();
    bool hasContent() const { return !mDrawGroups.isEmpty(); }

    void invalidatePipeline();
    bool ensurePipeline(QRhiRenderPassDescriptor* rpDesc, int sampleCount);
    void uploadResources(QRhiResourceUpdateBatch* updates,
                         const QSize& outputSize, float dpr, bool isYUpInNDC);
    void renderGridLines(QRhiCommandBuffer* cb, const QSize& outputSize);
    void renderTickMarks(QRhiCommandBuffer* cb, const QSize& outputSize);

private:
    void rebuildGeometry(float dpr, int outputHeight, bool isYUpInNDC);
    void renderGroups(QRhiCommandBuffer* cb, const QSize& outputSize, bool gridLines);
    void cleanupDrawGroups();

    QRhi* mRhi;

    QVector<float> mStagingVertices;
    QVector<DrawGroup> mDrawGroups;
    bool mGeometryDirty = true;

    QRhiBuffer* mVertexBuffer = nullptr;
    QRhiGraphicsPipeline* mPipeline = nullptr;
    QRhiShaderResourceBindings* mLayoutSrb = nullptr;
    QRhiBuffer* mLayoutUbo = nullptr;
    int mVertexBufferSize = 0;
    int mLastSampleCount = 0;
    QMap<QCPAxisRect*, QRect> mLastAxisRectBounds;
};

#pragma once

#include <QColor>
#include <QPointF>
#include <QRect>
#include <QVector>
#include <rhi/qrhi.h>

class QCPPlottableRhiLayer
{
public:
    struct DrawEntry
    {
        int fillOffset = 0;
        int fillVertexCount = 0;
        int strokeOffset = 0;
        int strokeVertexCount = 0;
        QRect scissorRect; // in physical pixels, Y-flipped for Y-up backends
    };

    explicit QCPPlottableRhiLayer(QRhi* rhi);
    ~QCPPlottableRhiLayer();

    // Geometry accumulation (called during replot)
    void clear();
    void addPlottable(const QVector<float>& fillVerts,
                      const QVector<float>& strokeVerts,
                      const QRect& clipRect, double dpr,
                      int outputHeight, bool isYUpInNDC);

    // GPU resource management
    void invalidatePipeline(); // call on resize (render pass descriptor change)
    bool ensurePipeline(QRhiRenderPassDescriptor* rpDesc, int sampleCount);
    void uploadResources(QRhiResourceUpdateBatch* updates,
                          const QSize& outputSize, float dpr, bool isYUpInNDC);
    void render(QRhiCommandBuffer* cb, const QSize& outputSize);

    void setPixelOffset(QPointF offset) { mPixelOffset = offset; }
    QPointF pixelOffset() const { return mPixelOffset; }

    bool isDirty() const { return mDirty; }
    bool hasGeometry() const { return !mDrawEntries.isEmpty(); }

private:
    QRhi* mRhi; // non-owned; lifetime managed by QRhiWidget
    QVector<float> mStagingVertices;
    QVector<DrawEntry> mDrawEntries;

    QRhiBuffer* mVertexBuffer = nullptr;
    QRhiBuffer* mUniformBuffer = nullptr;
    QRhiShaderResourceBindings* mSrb = nullptr;
    QRhiGraphicsPipeline* mPipeline = nullptr;
    int mVertexBufferSize = 0;
    int mLastSampleCount = 0;
    bool mDirty = false;
    QPointF mPixelOffset;
};

#pragma once

#include <QImage>
#include <QRect>
#include <QRectF>
#include <rhi/qrhi.h>

class QCPLayer;

class QCPColormapRhiLayer
{
public:
    explicit QCPColormapRhiLayer(QRhi* rhi);
    ~QCPColormapRhiLayer();

    // Called during replot (CPU side)
    void setImage(const QImage& image);
    void setQuadRect(const QRectF& pixelRect);
    void setScissorRect(const QRect& scissor);
    void setLayer(QCPLayer* layer) { mLayer = layer; }
    QCPLayer* layer() const { return mLayer; }

    // Called during render (GPU side)
    void invalidatePipeline();
    bool ensurePipeline(QRhiRenderPassDescriptor* rpDesc, int sampleCount,
                        QRhiBuffer* compositeUbo);
    void uploadResources(QRhiResourceUpdateBatch* updates,
                         const QSize& outputSize, float dpr, bool isYUpInNDC,
                         QRhiBuffer* compositeUbo);
    void render(QRhiCommandBuffer* cb, const QSize& outputSize);

    bool hasContent() const { return !mStagingImage.isNull(); }
    void clear();

private:
    QRhi* mRhi; // non-owned; lifetime managed by QRhiWidget
    QCPLayer* mLayer = nullptr;

    // CPU staging
    QImage mStagingImage;
    QRectF mQuadPixelRect;
    QRect mScissorRect;
    bool mTextureDirty = false;
    bool mGeometryDirty = false;

    // GPU resources
    QRhiTexture* mTexture = nullptr;
    QRhiSampler* mSampler = nullptr;
    QRhiBuffer* mVertexBuffer = nullptr;
    QRhiShaderResourceBindings* mSrb = nullptr;
    QRhiGraphicsPipeline* mPipeline = nullptr;
    int mLastSampleCount = 0;
    QSize mTextureSize;
    QSize mLastOutputSize;

    bool ensureTexture(QRhiBuffer* compositeUbo);
    void updateQuadGeometry(QRhiResourceUpdateBatch* updates,
                            const QSize& outputSize, float dpr, bool isYUpInNDC);
};

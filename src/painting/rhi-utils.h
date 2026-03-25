#pragma once

#include <rhi/qrhi.h>

namespace qcp::rhi {

inline QRhiTexture::Format preferredTextureFormat(QRhi* rhi)
{
    return rhi->isTextureFormatSupported(QRhiTexture::BGRA8)
        ? QRhiTexture::BGRA8
        : QRhiTexture::RGBA8;
}

inline QShader loadEmbeddedShader(const unsigned char* data, unsigned int length)
{
    return QShader::fromSerialized(QByteArray::fromRawData(
        reinterpret_cast<const char*>(data), length));
}

inline QRhiGraphicsPipeline::TargetBlend premultipliedAlphaBlend()
{
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::One;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    return blend;
}

} // namespace qcp::rhi

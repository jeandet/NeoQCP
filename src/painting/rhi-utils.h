#pragma once

#include <rhi/qrhi.h>

namespace qcp::rhi {

inline QRhiTexture::Format preferredTextureFormat(QRhi* rhi)
{
    return rhi->isTextureFormatSupported(QRhiTexture::BGRA8)
        ? QRhiTexture::BGRA8
        : QRhiTexture::RGBA8;
}

} // namespace qcp::rhi

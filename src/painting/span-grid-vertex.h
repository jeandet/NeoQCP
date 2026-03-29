#pragma once

#include <QVector>
#include <array>

// Shared vertex helpers for span and grid RHI layers.
// Vertex layout: 11 floats (x, y, r, g, b, a, extX, extY, extW, isPixelX, isPixelY)

namespace qcp::rhi::span_grid {

constexpr int kFloatsPerVertex = 11;
// Uniform buffer size: 14 data floats + 2 padding = 16 floats = 64 bytes (std140)
constexpr int kUniformBufferSize = 64;

inline void appendVertex(QVector<float>& buf, float x, float y,
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

// Emit 6 vertices (2 triangles) for a quad:
//   TL--TR
//   |  / |
//   BL--BR
inline void appendQuad(QVector<float>& buf,
                        float tlX, float tlY, float trX, float trY,
                        float blX, float blY, float brX, float brY,
                        const std::array<float, 4>& rgba,
                        float extX, float extY, float extW,
                        float isPixelX, float isPixelY)
{
    appendVertex(buf, tlX, tlY, rgba, extX, extY, extW, isPixelX, isPixelY);
    appendVertex(buf, blX, blY, rgba, extX, extY, extW, isPixelX, isPixelY);
    appendVertex(buf, trX, trY, rgba, extX, extY, extW, isPixelX, isPixelY);
    appendVertex(buf, trX, trY, rgba, extX, extY, extW, isPixelX, isPixelY);
    appendVertex(buf, blX, blY, rgba, extX, extY, extW, isPixelX, isPixelY);
    appendVertex(buf, brX, brY, rgba, extX, extY, extW, isPixelX, isPixelY);
}

// Emit a border line as a quad with extrude direction.
inline void appendBorder(QVector<float>& buf,
                          float x0, float y0, float x1, float y1,
                          const std::array<float, 4>& rgba,
                          float extDirX, float extDirY, float halfWidth,
                          float isPixelX, float isPixelY)
{
    appendVertex(buf, x0, y0, rgba, extDirX, extDirY, halfWidth, isPixelX, isPixelY);
    appendVertex(buf, x0, y0, rgba, -extDirX, -extDirY, halfWidth, isPixelX, isPixelY);
    appendVertex(buf, x1, y1, rgba, extDirX, extDirY, halfWidth, isPixelX, isPixelY);
    appendVertex(buf, x1, y1, rgba, extDirX, extDirY, halfWidth, isPixelX, isPixelY);
    appendVertex(buf, x0, y0, rgba, -extDirX, -extDirY, halfWidth, isPixelX, isPixelY);
    appendVertex(buf, x1, y1, rgba, -extDirX, -extDirY, halfWidth, isPixelX, isPixelY);
}

} // namespace qcp::rhi::span_grid

#pragma once

#include <QVector>
#include <array>
#include <cstring>
#include <span>

// Vertex layout: 11 floats (x, y, r, g, b, a, extX, extY, extW, isPixelX, isPixelY)

namespace qcp::rhi::span_grid {

constexpr int kFloatsPerVertex = 11;
// 14 data floats + 2 padding = 16 floats = 64 bytes (std140)
constexpr int kUniformBufferSize = 64;

struct Vertex
{
    float x, y;
    float r, g, b, a;
    float extX, extY, extW;
    float isPixelX, isPixelY;
};
static_assert(sizeof(Vertex) == kFloatsPerVertex * sizeof(float));

inline void append(QVector<float>& buf, std::span<const Vertex> vertices)
{
    const auto offset = buf.size();
    buf.resize(offset + int(vertices.size()) * kFloatsPerVertex);
    std::memcpy(buf.data() + offset, vertices.data(),
                vertices.size() * sizeof(Vertex));
}

inline auto makeVertex(float x, float y,
                       const std::array<float, 4>& rgba,
                       float extX, float extY, float extW,
                       float isPixelX, float isPixelY) -> Vertex
{
    return {x, y, rgba[0], rgba[1], rgba[2], rgba[3],
            extX, extY, extW, isPixelX, isPixelY};
}

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
    const std::array<Vertex, 6> verts = {{
        {tlX, tlY, rgba[0], rgba[1], rgba[2], rgba[3], extX, extY, extW, isPixelX, isPixelY},
        {blX, blY, rgba[0], rgba[1], rgba[2], rgba[3], extX, extY, extW, isPixelX, isPixelY},
        {trX, trY, rgba[0], rgba[1], rgba[2], rgba[3], extX, extY, extW, isPixelX, isPixelY},
        {trX, trY, rgba[0], rgba[1], rgba[2], rgba[3], extX, extY, extW, isPixelX, isPixelY},
        {blX, blY, rgba[0], rgba[1], rgba[2], rgba[3], extX, extY, extW, isPixelX, isPixelY},
        {brX, brY, rgba[0], rgba[1], rgba[2], rgba[3], extX, extY, extW, isPixelX, isPixelY},
    }};
    append(buf, verts);
}

inline void appendBorder(QVector<float>& buf,
                         float x0, float y0, float x1, float y1,
                         const std::array<float, 4>& rgba,
                         float extDirX, float extDirY, float halfWidth,
                         float isPixelX, float isPixelY)
{
    const std::array<Vertex, 6> verts = {{
        {x0, y0, rgba[0], rgba[1], rgba[2], rgba[3],  extDirX,  extDirY, halfWidth, isPixelX, isPixelY},
        {x0, y0, rgba[0], rgba[1], rgba[2], rgba[3], -extDirX, -extDirY, halfWidth, isPixelX, isPixelY},
        {x1, y1, rgba[0], rgba[1], rgba[2], rgba[3],  extDirX,  extDirY, halfWidth, isPixelX, isPixelY},
        {x1, y1, rgba[0], rgba[1], rgba[2], rgba[3],  extDirX,  extDirY, halfWidth, isPixelX, isPixelY},
        {x0, y0, rgba[0], rgba[1], rgba[2], rgba[3], -extDirX, -extDirY, halfWidth, isPixelX, isPixelY},
        {x1, y1, rgba[0], rgba[1], rgba[2], rgba[3], -extDirX, -extDirY, halfWidth, isPixelX, isPixelY},
    }};
    append(buf, verts);
}

} // namespace qcp::rhi::span_grid

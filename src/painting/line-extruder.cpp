#include "line-extruder.h"
#include "rhi-utils.h"
#include "Profiling.hpp"
#include <QtMath>
#include <array>

namespace QCPLineExtruder
{

namespace
{

constexpr float MITER_LIMIT = 4.0f;

struct Vertex
{
    float x, y, r, g, b, a;
};


void pushVertex(QVector<float>& out, float x, float y, const std::array<float, 4>& rgba)
{
    out.append(x);
    out.append(y);
    out.append(rgba[0]);
    out.append(rgba[1]);
    out.append(rgba[2]);
    out.append(rgba[3]);
}

void pushQuad(QVector<float>& out,
              QPointF tl, QPointF tr, QPointF br, QPointF bl,
              const std::array<float, 4>& rgba)
{
    pushVertex(out, tl.x(), tl.y(), rgba);
    pushVertex(out, tr.x(), tr.y(), rgba);
    pushVertex(out, br.x(), br.y(), rgba);
    pushVertex(out, tl.x(), tl.y(), rgba);
    pushVertex(out, br.x(), br.y(), rgba);
    pushVertex(out, bl.x(), bl.y(), rgba);
}

bool isNan(const QPointF& p) { return qIsNaN(p.x()) || qIsNaN(p.y()); }

QPointF perp(const QPointF& dir)
{
    return {-dir.y(), dir.x()};
}

QPointF normalized(const QPointF& p)
{
    double len = qSqrt(p.x() * p.x() + p.y() * p.y());
    if (len < 1e-10) return {0, 1};
    return p / len;
}

QPointF miterOffset(const QPointF& n0, const QPointF& n1, float halfWidth)
{
    QPointF tangent = normalized(n0 + n1);
    double dot = tangent.x() * n0.x() + tangent.y() * n0.y();
    if (qAbs(dot) < 1e-6)
        return {};
    double miterLen = halfWidth / dot;
    if (qAbs(miterLen) > MITER_LIMIT * halfWidth)
        return {};
    return tangent * miterLen;
}

struct SegmentData
{
    int startIdx;
    int endIdx;
};

QVector<SegmentData> splitByNaN(const QVector<QPointF>& points)
{
    QVector<SegmentData> segments;
    int start = 0;
    for (int i = 0; i < points.size(); ++i)
    {
        if (isNan(points[i]))
        {
            if (i - start >= 2)
                segments.append({start, i});
            start = i + 1;
        }
    }
    if (points.size() - start >= 2)
        segments.append({start, int(points.size())});
    return segments;
}

void extrudeSegment(QVector<float>& out, const QVector<QPointF>& points,
                    int start, int end, float halfWidth, const std::array<float, 4>& rgba)
{
    int count = end - start;
    if (count < 2) return;

    // Sliding window: only previous and current normal/left/right needed.
    QPointF prevNormal = perp(normalized(points[start + 1] - points[start]));
    QPointF prevLeft = points[start] + prevNormal * halfWidth;
    QPointF prevRight = points[start] - prevNormal * halfWidth;

    for (int i = 1; i < count - 1; ++i)
    {
        QPointF curNormal = perp(normalized(points[start + i + 1] - points[start + i]));
        QPointF mo = miterOffset(prevNormal, curNormal, halfWidth);

        QPointF curLeft, curRight;
        if (mo.isNull())
        {
            QPointF p = points[start + i];
            QPointF leftPrev = p + prevNormal * halfWidth;
            QPointF rightPrev = p - prevNormal * halfWidth;
            QPointF leftNext = p + curNormal * halfWidth;
            QPointF rightNext = p - curNormal * halfWidth;

            pushQuad(out, prevLeft, leftPrev, rightPrev, prevRight, rgba);

            double cross = prevNormal.x() * curNormal.y() - prevNormal.y() * curNormal.x();
            if (cross > 0)
            {
                pushVertex(out, rightPrev.x(), rightPrev.y(), rgba);
                pushVertex(out, rightNext.x(), rightNext.y(), rgba);
                pushVertex(out, p.x(), p.y(), rgba);
            }
            else
            {
                pushVertex(out, leftPrev.x(), leftPrev.y(), rgba);
                pushVertex(out, leftNext.x(), leftNext.y(), rgba);
                pushVertex(out, p.x(), p.y(), rgba);
            }

            curLeft = leftNext;
            curRight = rightNext;
        }
        else
        {
            curLeft = points[start + i] + mo;
            curRight = points[start + i] - mo;
            pushQuad(out, prevLeft, curLeft, curRight, prevRight, rgba);
        }

        prevLeft = curLeft;
        prevRight = curRight;
        prevNormal = curNormal;
    }

    QPointF lastLeft = points[start + count - 1] + prevNormal * halfWidth;
    QPointF lastRight = points[start + count - 1] - prevNormal * halfWidth;
    pushQuad(out, prevLeft, lastLeft, lastRight, prevRight, rgba);
}

} // anonymous namespace

QVector<float> extrudePolyline(const QVector<QPointF>& points, float penWidth,
                                const QColor& color)
{
    PROFILE_HERE_N("extrudePolyline");
    if (points.size() < 2 || penWidth <= 0.0f)
        return {};
    PROFILE_PASS_VALUE(points.size());

    auto rgba = qcp::rhi::premultipliedColor(color);
    float halfWidth = penWidth / 2.0f;

    auto segments = splitByNaN(points);
    QVector<float> out;
    out.reserve(points.size() * 6 * 6);

    for (const auto& seg : segments)
        extrudeSegment(out, points, seg.startIdx, seg.endIdx, halfWidth, rgba);

    return out;
}

QVector<float> tessellateFillPolygon(const QPolygonF& polygon, const QColor& color)
{
    PROFILE_HERE_N("tessellateFillPolygon");
    if (polygon.size() < 4)
        return {};
    PROFILE_PASS_VALUE(polygon.size());

    auto rgba = qcp::rhi::premultipliedColor(color);
    QVector<float> out;

    const QPointF base0 = polygon.first();
    const QPointF base1 = polygon.last();
    int curveCount = polygon.size() - 2;

    if (curveCount < 2)
    {
        pushVertex(out, base0.x(), base0.y(), rgba);
        pushVertex(out, polygon[1].x(), polygon[1].y(), rgba);
        pushVertex(out, base1.x(), base1.y(), rgba);
        return out;
    }

    out.reserve((curveCount - 1) * 36 + 2 * 18);

    bool horizontalBaseline = qFuzzyCompare(base0.y(), base1.y());

    auto projectToBaseline = [&](const QPointF& pt) -> QPointF {
        if (horizontalBaseline)
            return {pt.x(), base0.y()};
        else
            return {base0.x(), pt.y()};
    };

    QPointF proj0 = projectToBaseline(polygon[1]);
    pushVertex(out, base0.x(), base0.y(), rgba);
    pushVertex(out, polygon[1].x(), polygon[1].y(), rgba);
    pushVertex(out, proj0.x(), proj0.y(), rgba);

    for (int i = 1; i < curveCount; ++i)
    {
        QPointF c0 = polygon[i];
        QPointF c1 = polygon[i + 1];
        QPointF p0 = projectToBaseline(c0);
        QPointF p1 = projectToBaseline(c1);

        pushQuad(out, c0, c1, p1, p0, rgba);
    }

    QPointF projLast = projectToBaseline(polygon[polygon.size() - 2]);
    pushVertex(out, polygon[polygon.size() - 2].x(), polygon[polygon.size() - 2].y(), rgba);
    pushVertex(out, base1.x(), base1.y(), rgba);
    pushVertex(out, projLast.x(), projLast.y(), rgba);

    return out;
}

} // namespace QCPLineExtruder

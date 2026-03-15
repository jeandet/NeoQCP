#pragma once

#include <QColor>
#include <QPointF>
#include <QPolygonF>
#include <QVector>

namespace QCPLineExtruder
{

// Extrude a polyline into a triangle-list vertex buffer.
// Each vertex: (x, y, r, g, b, a) — 6 floats.
// NaN points in the input create gaps.
// Miter joins with bevel fallback at sharp angles.
QVector<float> extrudePolyline(const QVector<QPointF>& points, float penWidth,
                                const QColor& color);

// Tessellate a baseline fill polygon into a triangle-list vertex buffer.
// The polygon structure is: basePoint0, curvePoint0..N, basePoint1.
// Uses trapezoid decomposition between consecutive curve points.
QVector<float> tessellateFillPolygon(const QPolygonF& polygon, const QColor& color);

} // namespace QCPLineExtruder

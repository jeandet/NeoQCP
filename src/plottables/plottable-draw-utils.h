#pragma once

#include <QPointF>
#include <QPen>
#include <QRect>
#include <QVector>

class QCustomPlot;
class QCPPainter;
class QCPLayer;

namespace qcp {

/// Draw a polyline using the GPU path if available, otherwise QPainter.
/// The GPU path is disabled during export (pmVectorized, pmNoCaching).
/// When gpuOffset is non-null, points are pre-translated so other plottables
/// on the same shared layer are unaffected.
/// @param clipRect the plottable's clip rect (passed explicitly to avoid protected access)
void drawPolylineWithGpuFallback(QCPPainter* painter,
                                  QCustomPlot* parentPlot,
                                  QCPLayer* layer,
                                  const QVector<QPointF>& pts,
                                  const QPen& pen,
                                  const QPointF& gpuOffset,
                                  const QRect& clipRect);

} // namespace qcp

#pragma once

#include <QPointF>
#include <QPen>
#include <QRect>
#include <QVector>

class QCustomPlot;
class QCPPainter;
class QCPLayer;

namespace qcp {

/// Cached extruded vertices for a single polyline.
/// Stores the untranslated GPU vertices so they can be reused across frames
/// with only a cheap translation applied.
struct ExtrusionCache {
    QVector<float> vertices;   // untranslated extruded verts (6 floats per vertex)
    float penWidth = 0;
    QRgb penColor = 0;

    void clear() { vertices.clear(); }
    [[nodiscard]] bool isEmpty() const { return vertices.isEmpty(); }
};

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

/// Same as above but with cached extrusion.
/// When @a freshLines is true, re-extrudes and stores in @a cache.
/// When false, translates cached vertices by gpuOffset.
void drawPolylineCached(QCPPainter* painter,
                         QCustomPlot* parentPlot,
                         QCPLayer* layer,
                         const QVector<QPointF>& pts,
                         const QPen& pen,
                         const QPointF& gpuOffset,
                         const QRect& clipRect,
                         bool freshLines,
                         ExtrusionCache& cache);

} // namespace qcp

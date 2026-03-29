#include "plottable-draw-utils.h"

#include "../core.h"
#include "../painting/line-extruder.h"
#include "../painting/painter.h"
#include "../painting/plottable-rhi-layer.h"

namespace qcp {

void drawPolylineWithGpuFallback(QCPPainter* painter,
                                  QCustomPlot* parentPlot,
                                  QCPLayer* layer,
                                  const QVector<QPointF>& pts,
                                  const QPen& pen,
                                  const QPointF& gpuOffset,
                                  const QRect& clipRect)
{
    if (auto* rhi = parentPlot ? parentPlot->rhi() : nullptr;
        rhi && !painter->modes().testFlag(QCPPainter::pmVectorized)
            && !painter->modes().testFlag(QCPPainter::pmNoCaching)
            && pen.style() == Qt::SolidLine)
    {
        if (auto* prl = parentPlot->plottableRhiLayer(layer))
        {
            const double dpr = parentPlot->bufferDevicePixelRatio();
            const float penWidth = (pen.isCosmetic() || qFuzzyIsNull(pen.widthF()))
                ? static_cast<float>(1.0 / dpr)
                : qMax(1.0f, static_cast<float>(pen.widthF()));
            auto strokeVerts = QCPLineExtruder::extrudePolyline(pts, penWidth, pen.color());
            if (!strokeVerts.isEmpty())
            {
                const QSize outputSize = parentPlot->rhiOutputSize();
                prl->addPlottable({}, strokeVerts, clipRect, dpr,
                                   outputSize.height(), rhi->isYUpInNDC(),
                                   static_cast<float>(gpuOffset.x()),
                                   static_cast<float>(gpuOffset.y()));
                return;
            }
        }
    }
    // Software fallback
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    if (!gpuOffset.isNull())
    {
        painter->translate(gpuOffset);
        painter->drawPolyline(pts.constData(), pts.size());
        painter->translate(-gpuOffset);
    }
    else
    {
        painter->drawPolyline(pts.constData(), pts.size());
    }
}

void drawPolylineCached(QCPPainter* painter,
                         QCustomPlot* parentPlot,
                         QCPLayer* layer,
                         const QVector<QPointF>& pts,
                         const QPen& pen,
                         const QPointF& gpuOffset,
                         const QRect& clipRect,
                         bool freshLines,
                         ExtrusionCache& cache)
{
    if (auto* rhi = parentPlot ? parentPlot->rhi() : nullptr;
        rhi && !painter->modes().testFlag(QCPPainter::pmVectorized)
            && !painter->modes().testFlag(QCPPainter::pmNoCaching)
            && pen.style() == Qt::SolidLine)
    {
        if (auto* prl = parentPlot->plottableRhiLayer(layer))
        {
            const double dpr = parentPlot->bufferDevicePixelRatio();
            const float penWidth = (pen.isCosmetic() || qFuzzyIsNull(pen.widthF()))
                ? static_cast<float>(1.0 / dpr)
                : qMax(1.0f, static_cast<float>(pen.widthF()));

            if (freshLines || cache.isEmpty()
                || cache.penWidth != penWidth || cache.penColor != pen.color().rgba())
            {
                QCPLineExtruder::extrudePolyline(pts, penWidth, pen.color(), cache.vertices);
                cache.penWidth = penWidth;
                cache.penColor = pen.color().rgba();
            }

            if (cache.isEmpty())
                return;

            const QSize outputSize = parentPlot->rhiOutputSize();
            prl->addPlottable({}, cache.vertices, clipRect, dpr,
                               outputSize.height(), rhi->isYUpInNDC(),
                               static_cast<float>(gpuOffset.x()),
                               static_cast<float>(gpuOffset.y()));
            return;
        }
    }
    // Software fallback — no caching benefit, delegate to regular path
    drawPolylineWithGpuFallback(painter, parentPlot, layer, pts, pen, gpuOffset, clipRect);
}

} // namespace qcp

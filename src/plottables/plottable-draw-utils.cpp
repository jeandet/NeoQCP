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
            // Translate extruded vertices instead of re-extruding translated input —
            // miter joins are translation-invariant, so the geometry is identical.
            if (!gpuOffset.isNull())
            {
                const float dx = static_cast<float>(gpuOffset.x());
                const float dy = static_cast<float>(gpuOffset.y());
                for (int i = 0; i < strokeVerts.size(); i += 6)
                {
                    strokeVerts[i + 0] += dx;
                    strokeVerts[i + 1] += dy;
                }
            }
            if (!strokeVerts.isEmpty())
            {
                const QSize outputSize = parentPlot->rhiOutputSize();
                prl->addPlottable({}, strokeVerts, clipRect, dpr,
                                   outputSize.height(), rhi->isYUpInNDC());
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
                cache.vertices = QCPLineExtruder::extrudePolyline(pts, penWidth, pen.color());
                cache.penWidth = penWidth;
                cache.penColor = pen.color().rgba();
            }

            if (cache.isEmpty())
                return;

            // Translate cached vertices by gpuOffset
            const QVector<float>* src = &cache.vertices;
            QVector<float> translated;
            if (!gpuOffset.isNull())
            {
                translated = cache.vertices;
                const float dx = static_cast<float>(gpuOffset.x());
                const float dy = static_cast<float>(gpuOffset.y());
                for (int i = 0; i < translated.size(); i += 6)
                {
                    translated[i + 0] += dx;
                    translated[i + 1] += dy;
                }
                src = &translated;
            }

            const QSize outputSize = parentPlot->rhiOutputSize();
            prl->addPlottable({}, *src, clipRect, dpr,
                               outputSize.height(), rhi->isYUpInNDC());
            return;
        }
    }
    // Software fallback — no caching benefit, delegate to regular path
    drawPolylineWithGpuFallback(painter, parentPlot, layer, pts, pen, gpuOffset, clipRect);
}

} // namespace qcp

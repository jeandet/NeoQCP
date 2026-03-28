#pragma once

#include <QPointF>
#include <QSize>
#include <axis/axis.h>
#include <layoutelements/layoutelement-axisrect.h>

namespace qcp {

inline QPointF computeViewportOffset(
    const QCPAxis* keyAxis, const QCPAxis* valueAxis,
    const QCPRange& oldKeyRange, const QCPRange& oldValueRange)
{
    if (!keyAxis || !valueAxis)
        return {};

    double dKey = keyAxis->coordToPixel(oldKeyRange.lower)
                - keyAxis->coordToPixel(keyAxis->range().lower);
    double dVal = valueAxis->coordToPixel(oldValueRange.lower)
                - valueAxis->coordToPixel(valueAxis->range().lower);

    if (keyAxis->orientation() == Qt::Horizontal)
        return {dKey, dVal};
    else
        return {dVal, dKey};
}

struct LineCacheResult {
    bool needFreshLines;
    QPointF gpuOffset;
};

inline LineCacheResult evaluateLineCache(
    bool cacheDirty, bool cacheEmpty,
    const QSize& currentPlotSize, const QSize& cachedPlotSize,
    bool hasRenderedRange,
    const QCPRange& renderedKeyRange, const QCPRange& renderedValueRange,
    const QCPAxis* keyAxis, const QCPAxis* valueAxis,
    bool isExportMode)
{
    bool needFresh = cacheDirty || cacheEmpty;

    if (currentPlotSize != cachedPlotSize)
        needFresh = true;

    if (!needFresh && hasRenderedRange)
    {
        double keyRatio = keyAxis->range().size() / renderedKeyRange.size();
        double valRatio = valueAxis->range().size() / renderedValueRange.size();
        if (qAbs(keyRatio - 1.0) > 0.01 || qAbs(valRatio - 1.0) > 0.01)
            needFresh = true;
    }

    QPointF gpuOffset;
    if (!needFresh && hasRenderedRange)
    {
        gpuOffset = computeViewportOffset(keyAxis, valueAxis,
                                          renderedKeyRange, renderedValueRange);
        const bool keyIsVertical = keyAxis->orientation() == Qt::Vertical;
        const double keyDim = keyIsVertical
            ? keyAxis->axisRect()->height()
            : keyAxis->axisRect()->width();
        const double valDim = keyIsVertical
            ? keyAxis->axisRect()->width()
            : keyAxis->axisRect()->height();
        const double keyOff = qAbs(keyIsVertical ? gpuOffset.y() : gpuOffset.x());
        const double valOff = qAbs(keyIsVertical ? gpuOffset.x() : gpuOffset.y());
        if (keyOff > keyDim * 1.0 || valOff > valDim * 1.0)
            needFresh = true;
    }

    if (isExportMode)
        needFresh = true;

    return {needFresh, gpuOffset};
}

} // namespace qcp

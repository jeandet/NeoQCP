#pragma once

#include <QPointF>
#include <axis/axis.h>

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

} // namespace qcp

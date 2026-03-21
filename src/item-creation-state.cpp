#include "item-creation-state.h"
#include "core.h"
#include "layoutelements/layoutelement-axisrect.h"
#include "axis/axis.h"
#include "items/item.h"

QCPItemCreationState::QCPItemCreationState(QCustomPlot* plot)
    : QObject(plot)
    , mPlot(plot)
{
}

bool QCPItemCreationState::handleMousePress(QMouseEvent* event)
{
    Q_UNUSED(event);
    return false;
}

bool QCPItemCreationState::handleMouseMove(QMouseEvent* event)
{
    Q_UNUSED(event);
    return false;
}

bool QCPItemCreationState::handleKeyPress(QKeyEvent* event)
{
    Q_UNUSED(event);
    return false;
}

void QCPItemCreationState::cancel()
{
    if (mState == Drawing)
        cancelItem();
}

void QCPItemCreationState::initItemPosition(double key, double value)
{
    Q_UNUSED(key);
    Q_UNUSED(value);
}

void QCPItemCreationState::commitItem()
{
}

void QCPItemCreationState::cancelItem()
{
}

void QCPItemCreationState::updateItemPosition(const QPointF& pixelPos)
{
    Q_UNUSED(pixelPos);
}

QCPAxisRect* QCPItemCreationState::axisRectAt(const QPointF& pos) const
{
    return mPlot->axisRectAt(pos);
}

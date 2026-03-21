#include "item-creation-state.h"
#include "core.h"
#include "layoutelements/layoutelement-axisrect.h"
#include "axis/axis.h"
#include "items/item.h"
#include "items/item-vspan.h"
#include "items/item-hspan.h"
#include "items/item-rspan.h"

QCPItemCreationState::QCPItemCreationState(QCustomPlot* plot)
    : QObject(plot)
    , mPlot(plot)
{
}

bool QCPItemCreationState::handleMousePress(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return false;

    const auto& creator = mPlot->itemCreator();
    if (!creator)
        return false;

    if (mState == Idle)
    {
        auto* axisRect = axisRectAt(event->pos());
        if (!axisRect)
            return false;

        mKeyAxis = axisRect->axis(QCPAxis::atBottom);
        mValueAxis = axisRect->axis(QCPAxis::atLeft);
        if (!mKeyAxis) mKeyAxis = axisRect->axis(QCPAxis::atTop);
        if (!mValueAxis) mValueAxis = axisRect->axis(QCPAxis::atRight);
        if (!mKeyAxis || !mValueAxis)
            return false;

        mCurrentItem = creator(mPlot, mKeyAxis, mValueAxis);
        if (!mCurrentItem)
            return false;

        double key = mKeyAxis->pixelToCoord(event->pos().x());
        double value = mValueAxis->pixelToCoord(event->pos().y());
        initItemPosition(key, value);

        mState = Drawing;
        mPlot->setCursor(Qt::CrossCursor);
        mPlot->replot(QCustomPlot::rpQueuedReplot);
        return true;
    }
    else if (mState == Drawing)
    {
        updateItemPosition(event->pos());
        commitItem();
        return true;
    }

    return false;
}

bool QCPItemCreationState::handleMouseMove(QMouseEvent* event)
{
    if (mState != Drawing || !mCurrentItem)
        return false;

    updateItemPosition(event->pos());
    mPlot->replot(QCustomPlot::rpQueuedReplot);
    return true;
}

bool QCPItemCreationState::handleKeyPress(QKeyEvent* event)
{
    if (mState != Drawing)
        return false;

    if (event->key() == Qt::Key_Escape) {
        cancelItem();
        return true;
    }
    return false;
}

void QCPItemCreationState::cancel()
{
    if (mState == Drawing)
        cancelItem();
}

void QCPItemCreationState::initItemPosition(double key, double value)
{
    mAnchorKey = key;
    mAnchorValue = value;

    if (auto* vs = qobject_cast<QCPItemVSpan*>(mCurrentItem)) {
        vs->setRange(QCPRange(key, key));
    } else if (auto* hs = qobject_cast<QCPItemHSpan*>(mCurrentItem)) {
        hs->setRange(QCPRange(value, value));
    } else if (auto* rs = qobject_cast<QCPItemRSpan*>(mCurrentItem)) {
        rs->setKeyRange(QCPRange(key, key));
        rs->setValueRange(QCPRange(value, value));
    } else {
        auto positions = mCurrentItem->positions();
        if (positions.size() == 2) {
            positions[0]->setType(QCPItemPosition::ptPlotCoords);
            positions[0]->setCoords(key, value);
            positions[1]->setType(QCPItemPosition::ptPlotCoords);
            positions[1]->setCoords(key, value);
        }
    }
}

void QCPItemCreationState::commitItem()
{
    auto* item = mCurrentItem;
    mCurrentItem = nullptr;
    mKeyAxis = nullptr;
    mValueAxis = nullptr;
    mState = Idle;
    if (!mPlot->creationModeEnabled())
        mPlot->setCursor(Qt::ArrowCursor);
    mPlot->replot(QCustomPlot::rpQueuedReplot);
    emit itemCreated(item);
}

void QCPItemCreationState::cancelItem()
{
    if (mCurrentItem) {
        mPlot->removeItem(mCurrentItem);
        mCurrentItem = nullptr;
    }
    mKeyAxis = nullptr;
    mValueAxis = nullptr;
    mState = Idle;
    if (!mPlot->creationModeEnabled())
        mPlot->setCursor(Qt::ArrowCursor);
    mPlot->replot(QCustomPlot::rpQueuedReplot);
    emit itemCanceled();
}

void QCPItemCreationState::updateItemPosition(const QPointF& pixelPos)
{
    if (!mCurrentItem || !mKeyAxis || !mValueAxis) return;

    double key = mKeyAxis->pixelToCoord(pixelPos.x());
    double value = mValueAxis->pixelToCoord(pixelPos.y());

    if (auto* vs = qobject_cast<QCPItemVSpan*>(mCurrentItem)) {
        vs->setRange(QCPRange(mAnchorKey, key));
    } else if (auto* hs = qobject_cast<QCPItemHSpan*>(mCurrentItem)) {
        hs->setRange(QCPRange(mAnchorValue, value));
    } else if (auto* rs = qobject_cast<QCPItemRSpan*>(mCurrentItem)) {
        rs->setKeyRange(QCPRange(mAnchorKey, key));
        rs->setValueRange(QCPRange(mAnchorValue, value));
    } else {
        auto positions = mCurrentItem->positions();
        if (positions.size() == 2) {
            positions[1]->setCoords(key, value);
        }
    }
}

QCPAxisRect* QCPItemCreationState::axisRectAt(const QPointF& pos) const
{
    return mPlot->axisRectAt(pos);
}

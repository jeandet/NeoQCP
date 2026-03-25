#include "item-spanbase.h"

#include "../core.h"
#include "../painting/painter.h"
#include "../painting/span-rhi-layer.h"

QCPAbstractSpanItem::~QCPAbstractSpanItem()
{
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->unregisterSpan(this);
}

void QCPAbstractSpanItem::setPen(const QPen& pen)
{
    mPen = pen;
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->markGeometryDirty();
}

void QCPAbstractSpanItem::setSelectedPen(const QPen& pen)
{
    mSelectedPen = pen;
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->markGeometryDirty();
}

void QCPAbstractSpanItem::setBrush(const QBrush& brush)
{
    mBrush = brush;
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->markGeometryDirty();
}

void QCPAbstractSpanItem::setSelectedBrush(const QBrush& brush)
{
    mSelectedBrush = brush;
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->markGeometryDirty();
}

void QCPAbstractSpanItem::setBorderPen(const QPen& pen)
{
    mBorderPen = pen;
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->markGeometryDirty();
}

void QCPAbstractSpanItem::setSelectedBorderPen(const QPen& pen)
{
    mSelectedBorderPen = pen;
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->markGeometryDirty();
}

bool QCPAbstractSpanItem::tryRhiDraw(QCPPainter* painter)
{
    if (auto* layer = mParentPlot ? mParentPlot->spanRhiLayer() : nullptr)
    {
        layer->registerSpan(this);
        if (!painter->modes().testFlag(QCPPainter::pmVectorized)
            && !painter->modes().testFlag(QCPPainter::pmNoCaching))
            return true;
    }
    return false;
}

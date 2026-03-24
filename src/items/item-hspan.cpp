#include "item-hspan.h"

#include "../core.h"
#include "../painting/painter.h"
#include "../painting/span-rhi-layer.h"

QCPItemHSpan::QCPItemHSpan(QCustomPlot* parentPlot)
    : QCPAbstractItem(parentPlot)
    , lowerEdge(createPosition(QLatin1String("lowerEdge")))
    , upperEdge(createPosition(QLatin1String("upperEdge")))
    , center(createAnchor(QLatin1String("center"), aiCenter))
{
    lowerEdge->setTypeY(QCPItemPosition::ptPlotCoords);
    lowerEdge->setTypeX(QCPItemPosition::ptAxisRectRatio);
    lowerEdge->setCoords(0, 0);

    upperEdge->setTypeY(QCPItemPosition::ptPlotCoords);
    upperEdge->setTypeX(QCPItemPosition::ptAxisRectRatio);
    upperEdge->setCoords(1, 1);

    setPen(QPen(Qt::black));
    setSelectedPen(QPen(Qt::blue, 2));
    setBrush(QBrush(QColor(0, 0, 255, 50)));
    setSelectedBrush(QBrush(QColor(0, 0, 255, 80)));
    setBorderPen(QPen(Qt::black, 2));
    setSelectedBorderPen(QPen(Qt::blue, 2));

    if (parentPlot->spanRhiLayer())
    {
        parentPlot->spanRhiLayer()->registerSpan(this);
        connect(this, &QCPAbstractItem::selectionChanged, this, [this](bool) {
            if (mParentPlot && mParentPlot->spanRhiLayer())
                mParentPlot->spanRhiLayer()->markGeometryDirty();
        });
    }
}

QCPItemHSpan::~QCPItemHSpan()
{
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->unregisterSpan(this);
}

QCPRange QCPItemHSpan::range() const
{
    return QCPRange(lowerEdge->coords().y(), upperEdge->coords().y());
}

void QCPItemHSpan::setRange(const QCPRange& range)
{
    lowerEdge->setCoords(0, range.lower);
    upperEdge->setCoords(1, range.upper);
    emit rangeChanged(range);
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->markGeometryDirty();
}

void QCPItemHSpan::setPen(const QPen& pen)
{
    mPen = pen;
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->markGeometryDirty();
}

void QCPItemHSpan::setSelectedPen(const QPen& pen)
{
    mSelectedPen = pen;
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->markGeometryDirty();
}

void QCPItemHSpan::setBrush(const QBrush& brush)
{
    mBrush = brush;
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->markGeometryDirty();
}

void QCPItemHSpan::setSelectedBrush(const QBrush& brush)
{
    mSelectedBrush = brush;
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->markGeometryDirty();
}

void QCPItemHSpan::setBorderPen(const QPen& pen)
{
    mBorderPen = pen;
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->markGeometryDirty();
}

void QCPItemHSpan::setSelectedBorderPen(const QPen& pen)
{
    mSelectedBorderPen = pen;
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->markGeometryDirty();
}

void QCPItemHSpan::setMovable(bool movable) { mMovable = movable; }

QPen QCPItemHSpan::mainPen() const { return mSelected ? mSelectedPen : mPen; }
QBrush QCPItemHSpan::mainBrush() const { return mSelected ? mSelectedBrush : mBrush; }
QPen QCPItemHSpan::mainBorderPen() const { return mSelected ? mSelectedBorderPen : mBorderPen; }

double QCPItemHSpan::selectTest(const QPointF& pos, bool onlySelectable, QVariant* details) const
{
    if (onlySelectable && !mSelectable)
        return -1;

    auto* valAxis = lowerEdge->valueAxis();
    if (!valAxis)
        return -1;

    const double lowerPx = valAxis->coordToPixel(lowerEdge->coords().y());
    const double upperPx = valAxis->coordToPixel(upperEdge->coords().y());
    const QRectF axisRect = clipRect();
    const double top = qMin(lowerPx, upperPx);
    const double bottom = qMax(lowerPx, upperPx);

    const double tolerance = mParentPlot->selectionTolerance();

    // check edges first (priority)
    const double distLower = qAbs(pos.y() - lowerPx);
    const double distUpper = qAbs(pos.y() - upperPx);

    if (distLower <= tolerance && axisRect.left() <= pos.x() && pos.x() <= axisRect.right())
    {
        if (details)
            details->setValue(static_cast<int>(hpLowerEdge));
        return distLower;
    }
    if (distUpper <= tolerance && axisRect.left() <= pos.x() && pos.x() <= axisRect.right())
    {
        if (details)
            details->setValue(static_cast<int>(hpUpperEdge));
        return distUpper;
    }

    // check fill
    bool filledRect = mBrush.style() != Qt::NoBrush && mBrush.color().alpha() != 0;
    if (filledRect && pos.y() >= top && pos.y() <= bottom
        && pos.x() >= axisRect.left() && pos.x() <= axisRect.right())
    {
        if (details)
            details->setValue(static_cast<int>(hpFill));
        return 0;
    }

    return -1;
}

void QCPItemHSpan::draw(QCPPainter* painter)
{
    if (mParentPlot && mParentPlot->spanRhiLayer()
        && !painter->modes().testFlag(QCPPainter::pmVectorized)
        && !painter->modes().testFlag(QCPPainter::pmNoCaching))
        return;

    auto* valAxis = lowerEdge->valueAxis();
    if (!valAxis)
        return;

    const double lowerPx = valAxis->coordToPixel(lowerEdge->coords().y());
    const double upperPx = valAxis->coordToPixel(upperEdge->coords().y());
    const QRectF axRect = clipRect();

    const double top = qMin(lowerPx, upperPx);
    const double bottom = qMax(lowerPx, upperPx);
    QRectF spanRect(axRect.left(), top, axRect.width(), bottom - top);

    if (!spanRect.intersects(axRect))
        return;

    // fill
    painter->setPen(mainPen());
    painter->setBrush(mainBrush());
    painter->drawRect(spanRect);

    // border lines
    painter->setPen(mainBorderPen());
    painter->drawLine(QPointF(axRect.left(), lowerPx), QPointF(axRect.right(), lowerPx));
    painter->drawLine(QPointF(axRect.left(), upperPx), QPointF(axRect.right(), upperPx));
}

QPointF QCPItemHSpan::anchorPixelPosition(int anchorId) const
{
    if (anchorId == aiCenter)
    {
        auto* valAxis = lowerEdge->valueAxis();
        if (!valAxis)
            return {};
        const double midY = (valAxis->coordToPixel(lowerEdge->coords().y())
                             + valAxis->coordToPixel(upperEdge->coords().y())) * 0.5;
        const QRectF axRect = clipRect();
        return QPointF((axRect.left() + axRect.right()) * 0.5, midY);
    }
    qDebug() << Q_FUNC_INFO << "invalid anchorId" << anchorId;
    return {};
}

void QCPItemHSpan::mousePressEvent(QMouseEvent* event, const QVariant& details)
{
    if (!mMovable)
    {
        event->ignore();
        return;
    }
    mDragPart = static_cast<HitPart>(details.toInt());
    mDragStartLower = lowerEdge->coords().y();
    mDragStartUpper = upperEdge->coords().y();
    event->accept();
}

void QCPItemHSpan::mouseMoveEvent(QMouseEvent* event, const QPointF& startPos)
{
    auto* valAxis = lowerEdge->valueAxis();
    if (!valAxis || mDragPart == hpNone)
        return;

    const double startCoord = valAxis->pixelToCoord(startPos.y());
    const double currentCoord = valAxis->pixelToCoord(event->pos().y());
    const double delta = currentCoord - startCoord;

    switch (mDragPart)
    {
        case hpLowerEdge:
            lowerEdge->setCoords(0, mDragStartLower + delta);
            break;
        case hpUpperEdge:
            upperEdge->setCoords(1, mDragStartUpper + delta);
            break;
        case hpFill:
            lowerEdge->setCoords(0, mDragStartLower + delta);
            upperEdge->setCoords(1, mDragStartUpper + delta);
            break;
        default:
            break;
    }
    emit rangeChanged(range());
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->markGeometryDirty();
    mParentPlot->replot(QCustomPlot::rpQueuedReplot);
}

void QCPItemHSpan::mouseReleaseEvent(QMouseEvent* event, const QPointF& startPos)
{
    Q_UNUSED(event)
    Q_UNUSED(startPos)
    mDragPart = hpNone;
}

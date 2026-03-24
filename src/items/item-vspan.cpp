#include "item-vspan.h"

#include "../core.h"
#include "../painting/painter.h"
#include "../painting/span-rhi-layer.h"

QCPItemVSpan::QCPItemVSpan(QCustomPlot* parentPlot)
    : QCPAbstractItem(parentPlot)
    , lowerEdge(createPosition(QLatin1String("lowerEdge")))
    , upperEdge(createPosition(QLatin1String("upperEdge")))
    , center(createAnchor(QLatin1String("center"), aiCenter))
{
    lowerEdge->setTypeX(QCPItemPosition::ptPlotCoords);
    lowerEdge->setTypeY(QCPItemPosition::ptAxisRectRatio);
    lowerEdge->setCoords(0, 0);

    upperEdge->setTypeX(QCPItemPosition::ptPlotCoords);
    upperEdge->setTypeY(QCPItemPosition::ptAxisRectRatio);
    upperEdge->setCoords(1, 1);

    setPen(QPen(Qt::black));
    setSelectedPen(QPen(Qt::blue, 2));
    setBrush(QBrush(QColor(0, 0, 255, 50)));
    setSelectedBrush(QBrush(QColor(0, 0, 255, 80)));
    setBorderPen(QPen(Qt::black, 2));
    setSelectedBorderPen(QPen(Qt::blue, 2));

    connect(this, &QCPAbstractItem::selectionChanged, this, [this](bool) {
        if (mParentPlot && mParentPlot->spanRhiLayer())
            mParentPlot->spanRhiLayer()->markGeometryDirty();
    });
}

QCPItemVSpan::~QCPItemVSpan()
{
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->unregisterSpan(this);
}

QCPRange QCPItemVSpan::range() const
{
    return QCPRange(lowerEdge->coords().x(), upperEdge->coords().x());
}

void QCPItemVSpan::setRange(const QCPRange& range)
{
    lowerEdge->setCoords(range.lower, 0);
    upperEdge->setCoords(range.upper, 1);
    emit rangeChanged(range);
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->markGeometryDirty();
}

void QCPItemVSpan::setPen(const QPen& pen)
{
    mPen = pen;
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->markGeometryDirty();
}

void QCPItemVSpan::setSelectedPen(const QPen& pen)
{
    mSelectedPen = pen;
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->markGeometryDirty();
}

void QCPItemVSpan::setBrush(const QBrush& brush)
{
    mBrush = brush;
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->markGeometryDirty();
}

void QCPItemVSpan::setSelectedBrush(const QBrush& brush)
{
    mSelectedBrush = brush;
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->markGeometryDirty();
}

void QCPItemVSpan::setBorderPen(const QPen& pen)
{
    mBorderPen = pen;
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->markGeometryDirty();
}

void QCPItemVSpan::setSelectedBorderPen(const QPen& pen)
{
    mSelectedBorderPen = pen;
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->markGeometryDirty();
}

void QCPItemVSpan::setMovable(bool movable) { mMovable = movable; }

QPen QCPItemVSpan::mainPen() const { return mSelected ? mSelectedPen : mPen; }
QBrush QCPItemVSpan::mainBrush() const { return mSelected ? mSelectedBrush : mBrush; }
QPen QCPItemVSpan::mainBorderPen() const { return mSelected ? mSelectedBorderPen : mBorderPen; }

double QCPItemVSpan::selectTest(const QPointF& pos, bool onlySelectable, QVariant* details) const
{
    if (onlySelectable && !mSelectable)
        return -1;

    auto* keyAxis = lowerEdge->keyAxis();
    if (!keyAxis)
        return -1;

    const double lowerPx = keyAxis->coordToPixel(lowerEdge->coords().x());
    const double upperPx = keyAxis->coordToPixel(upperEdge->coords().x());
    const QRectF axisRect = clipRect();
    const double left = qMin(lowerPx, upperPx);
    const double right = qMax(lowerPx, upperPx);

    const double tolerance = mParentPlot->selectionTolerance();

    // check edges first (priority)
    const double distLower = qAbs(pos.x() - lowerPx);
    const double distUpper = qAbs(pos.x() - upperPx);

    if (distLower <= tolerance && axisRect.top() <= pos.y() && pos.y() <= axisRect.bottom())
    {
        if (details)
            details->setValue(static_cast<int>(hpLowerEdge));
        return distLower;
    }
    if (distUpper <= tolerance && axisRect.top() <= pos.y() && pos.y() <= axisRect.bottom())
    {
        if (details)
            details->setValue(static_cast<int>(hpUpperEdge));
        return distUpper;
    }

    // check fill
    bool filledRect = mBrush.style() != Qt::NoBrush && mBrush.color().alpha() != 0;
    if (filledRect && pos.x() >= left && pos.x() <= right
        && pos.y() >= axisRect.top() && pos.y() <= axisRect.bottom())
    {
        if (details)
            details->setValue(static_cast<int>(hpFill));
        return 0;
    }

    return -1;
}

void QCPItemVSpan::draw(QCPPainter* painter)
{
    if (auto* layer = mParentPlot ? mParentPlot->spanRhiLayer() : nullptr)
    {
        layer->registerSpan(this); // idempotent — handles late RHI init
        if (!painter->modes().testFlag(QCPPainter::pmVectorized)
            && !painter->modes().testFlag(QCPPainter::pmNoCaching))
            return;
    }

    auto* keyAxis = lowerEdge->keyAxis();
    if (!keyAxis)
        return;

    const double lowerPx = keyAxis->coordToPixel(lowerEdge->coords().x());
    const double upperPx = keyAxis->coordToPixel(upperEdge->coords().x());
    const QRectF axRect = clipRect();

    const double left = qMin(lowerPx, upperPx);
    const double right = qMax(lowerPx, upperPx);
    QRectF spanRect(left, axRect.top(), right - left, axRect.height());

    if (!spanRect.intersects(axRect))
        return;

    // fill
    painter->setPen(mainPen());
    painter->setBrush(mainBrush());
    painter->drawRect(spanRect);

    // border lines
    painter->setPen(mainBorderPen());
    painter->drawLine(QPointF(lowerPx, axRect.top()), QPointF(lowerPx, axRect.bottom()));
    painter->drawLine(QPointF(upperPx, axRect.top()), QPointF(upperPx, axRect.bottom()));
}

QPointF QCPItemVSpan::anchorPixelPosition(int anchorId) const
{
    if (anchorId == aiCenter)
    {
        auto* keyAxis = lowerEdge->keyAxis();
        if (!keyAxis)
            return {};
        const double midX = (keyAxis->coordToPixel(lowerEdge->coords().x())
                             + keyAxis->coordToPixel(upperEdge->coords().x())) * 0.5;
        const QRectF axRect = clipRect();
        return QPointF(midX, (axRect.top() + axRect.bottom()) * 0.5);
    }
    qDebug() << Q_FUNC_INFO << "invalid anchorId" << anchorId;
    return {};
}

void QCPItemVSpan::mousePressEvent(QMouseEvent* event, const QVariant& details)
{
    if (!mMovable)
    {
        event->ignore();
        return;
    }
    mDragPart = static_cast<HitPart>(details.toInt());
    mDragStartLower = lowerEdge->coords().x();
    mDragStartUpper = upperEdge->coords().x();
    event->accept();
}

void QCPItemVSpan::mouseMoveEvent(QMouseEvent* event, const QPointF& startPos)
{
    auto* keyAxis = lowerEdge->keyAxis();
    if (!keyAxis || mDragPart == hpNone)
        return;

    const double startCoord = keyAxis->pixelToCoord(startPos.x());
    const double currentCoord = keyAxis->pixelToCoord(event->pos().x());
    const double delta = currentCoord - startCoord;

    switch (mDragPart)
    {
        case hpLowerEdge:
            lowerEdge->setCoords(mDragStartLower + delta, 0);
            break;
        case hpUpperEdge:
            upperEdge->setCoords(mDragStartUpper + delta, 1);
            break;
        case hpFill:
            lowerEdge->setCoords(mDragStartLower + delta, 0);
            upperEdge->setCoords(mDragStartUpper + delta, 1);
            break;
        default:
            break;
    }
    emit rangeChanged(range());
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->markGeometryDirty();
    mParentPlot->replot(QCustomPlot::rpQueuedReplot);
}

void QCPItemVSpan::mouseReleaseEvent(QMouseEvent* event, const QPointF& startPos)
{
    Q_UNUSED(event)
    Q_UNUSED(startPos)
    mDragPart = hpNone;
}

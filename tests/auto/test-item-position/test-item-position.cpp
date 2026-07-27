#include "test-item-position.h"
#include "qcustomplot.h"

namespace
{
// The axis rect only has a meaningful geometry once the layout has run.
QCPItemText* laidOutTextItem(QCustomPlot* plot)
{
    plot->replot();
    return new QCPItemText(plot);
}
}

void TestItemPosition::init()
{
    mPlot = new QCustomPlot(nullptr);
    mPlot->resize(640, 480);
    mPlot->show();
}

void TestItemPosition::cleanup()
{
    delete mPlot;
    mPlot = nullptr;
}

void TestItemPosition::axisRectAbsoluteIsOffsetByAxisRectOrigin()
{
    QCPItemText* item = laidOutTextItem(mPlot);
    const QPoint origin = mPlot->axisRect()->topLeft();
    // A non-trivial left/top margin is what makes this distinct from ptAbsolute.
    QVERIFY(origin.x() > 0);
    QVERIFY(origin.y() > 0);

    item->position->setType(QCPItemPosition::ptAxisRectAbsolute);
    item->position->setCoords(10, 20);

    QCOMPARE(item->position->pixelPosition(), QPointF(origin.x() + 10, origin.y() + 20));
}

void TestItemPosition::axisRectAbsoluteFollowsAxisRectOnResize()
{
    QCPItemText* item = laidOutTextItem(mPlot);
    item->position->setType(QCPItemPosition::ptAxisRectAbsolute);
    item->position->setCoords(10, 20);

    mPlot->resize(900, 700);
    mPlot->replot();
    const QPoint origin = mPlot->axisRect()->topLeft();

    QCOMPARE(item->position->pixelPosition(), QPointF(origin.x() + 10, origin.y() + 20));
    // The stored coords are the offset; only the resolved pixel position moves.
    QCOMPARE(item->position->coords(), QPointF(10, 20));
}

void TestItemPosition::axisRectAbsoluteSetPixelPositionRoundTrips()
{
    QCPItemText* item = laidOutTextItem(mPlot);
    item->position->setType(QCPItemPosition::ptAxisRectAbsolute);
    const QPoint origin = mPlot->axisRect()->topLeft();

    item->position->setPixelPosition(QPointF(origin.x() + 33, origin.y() + 44));

    QCOMPARE(item->position->coords(), QPointF(33, 44));
    QCOMPARE(item->position->pixelPosition(), QPointF(origin.x() + 33, origin.y() + 44));
}

void TestItemPosition::axisRectAbsoluteRespectsParentAnchor()
{
    QCPItemText* anchorItem = laidOutTextItem(mPlot);
    anchorItem->position->setType(QCPItemPosition::ptAbsolute);
    anchorItem->position->setCoords(100, 50);

    QCPItemText* item = new QCPItemText(mPlot);
    item->position->setType(QCPItemPosition::ptAxisRectAbsolute);
    QVERIFY(item->position->setParentAnchor(anchorItem->position));
    item->position->setCoords(10, 20);

    // A parent anchor takes precedence over the axis rect origin, mirroring
    // ptAxisRectRatio's behaviour.
    QCOMPARE(item->position->pixelPosition(), QPointF(110, 70));
}

void TestItemPosition::absoluteRemainsWidgetRelative()
{
    QCPItemText* item = laidOutTextItem(mPlot);
    item->position->setType(QCPItemPosition::ptAbsolute);
    item->position->setCoords(10, 20);

    // Guards against a fix that makes every static type axis-rect-relative.
    QCOMPARE(item->position->pixelPosition(), QPointF(10, 20));
}

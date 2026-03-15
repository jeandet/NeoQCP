#include "test-hspan.h"
#include "../../../src/qcp.h"

void TestHSpan::init()
{
    mPlot = new QCustomPlot();
    mPlot->resize(400, 300);
    mPlot->xAxis->setRange(0, 10);
    mPlot->yAxis->setRange(0, 10);
    mPlot->replot();
}

void TestHSpan::cleanup()
{
    delete mPlot;
    mPlot = nullptr;
}

void TestHSpan::createAndRange()
{
    auto* span = new QCPItemHSpan(mPlot);
    span->setRange(QCPRange(2, 5));
    QCOMPARE(span->range().lower, 2.0);
    QCOMPARE(span->range().upper, 5.0);
}

void TestHSpan::setRange()
{
    auto* span = new QCPItemHSpan(mPlot);

    QSignalSpy spy(span, &QCPItemHSpan::rangeChanged);
    span->setRange(QCPRange(3, 7));

    QCOMPARE(spy.count(), 1);
    auto emittedRange = spy.at(0).at(0).value<QCPRange>();
    QCOMPARE(emittedRange.lower, 3.0);
    QCOMPARE(emittedRange.upper, 7.0);
}

void TestHSpan::invertedRange()
{
    auto* span = new QCPItemHSpan(mPlot);
    span->setRange(QCPRange(8, 2));
    // setRange normalizes: lower <= upper
    QCOMPARE(span->range().lower, 2.0);
    QCOMPARE(span->range().upper, 8.0);
    // draw should not crash
    mPlot->replot();
    QVERIFY(true);
}

void TestHSpan::selectTestEdges()
{
    auto* span = new QCPItemHSpan(mPlot);
    span->setRange(QCPRange(2, 8));
    mPlot->replot();

    double lowerPx = mPlot->yAxis->coordToPixel(2);
    double midX = mPlot->xAxis->axisRect()->center().x();

    QVariant details;
    double dist = span->selectTest(QPointF(midX, lowerPx), false, &details);
    QVERIFY(dist >= 0);
    QCOMPARE(details.toInt(), static_cast<int>(QCPItemHSpan::hpLowerEdge));
}

void TestHSpan::selectTestFill()
{
    auto* span = new QCPItemHSpan(mPlot);
    span->setRange(QCPRange(2, 8));
    mPlot->replot();

    double midPx = mPlot->yAxis->coordToPixel(5);
    double midX = mPlot->xAxis->axisRect()->center().x();

    QVariant details;
    double dist = span->selectTest(QPointF(midX, midPx), false, &details);
    QVERIFY(dist >= 0);
    QCOMPARE(details.toInt(), static_cast<int>(QCPItemHSpan::hpFill));
}

void TestHSpan::selectTestMiss()
{
    auto* span = new QCPItemHSpan(mPlot);
    span->setRange(QCPRange(2, 4));
    mPlot->replot();

    double farPx = mPlot->yAxis->coordToPixel(9);
    double midX = mPlot->xAxis->axisRect()->center().x();

    double dist = span->selectTest(QPointF(midX, farPx), false);
    QCOMPARE(dist, -1.0);
}

void TestHSpan::movableFlag()
{
    auto* span = new QCPItemHSpan(mPlot);
    QVERIFY(span->movable());
    span->setMovable(false);
    QVERIFY(!span->movable());
}

void TestHSpan::drawDoesNotCrash()
{
    auto* span = new QCPItemHSpan(mPlot);
    span->setRange(QCPRange(1, 9));
    mPlot->replot();
    QVERIFY(true);
}

void TestHSpan::drawOnLogAxis()
{
    mPlot->yAxis->setScaleType(QCPAxis::stLogarithmic);
    mPlot->yAxis->setRange(1, 1000);
    auto* span = new QCPItemHSpan(mPlot);
    span->setRange(QCPRange(10, 100));
    mPlot->replot();
    QVERIFY(true);
}

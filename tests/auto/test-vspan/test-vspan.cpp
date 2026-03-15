#include "test-vspan.h"
#include "../../../src/qcp.h"

void TestVSpan::init()
{
    mPlot = new QCustomPlot();
    mPlot->resize(400, 300);
    mPlot->xAxis->setRange(0, 10);
    mPlot->yAxis->setRange(0, 10);
    mPlot->replot();
}

void TestVSpan::cleanup()
{
    delete mPlot;
    mPlot = nullptr;
}

void TestVSpan::createAndRange()
{
    auto* span = new QCPItemVSpan(mPlot);
    span->setRange(QCPRange(2, 5));
    QCOMPARE(span->range().lower, 2.0);
    QCOMPARE(span->range().upper, 5.0);
}

void TestVSpan::setRange()
{
    auto* span = new QCPItemVSpan(mPlot);

    QSignalSpy spy(span, &QCPItemVSpan::rangeChanged);
    span->setRange(QCPRange(3, 7));

    QCOMPARE(spy.count(), 1);
    auto emittedRange = spy.at(0).at(0).value<QCPRange>();
    QCOMPARE(emittedRange.lower, 3.0);
    QCOMPARE(emittedRange.upper, 7.0);
}

void TestVSpan::invertedRange()
{
    auto* span = new QCPItemVSpan(mPlot);
    span->setRange(QCPRange(8, 2));
    // setRange normalizes: lower <= upper
    QCOMPARE(span->range().lower, 2.0);
    QCOMPARE(span->range().upper, 8.0);
    // draw should not crash
    mPlot->replot();
    QVERIFY(true);
}

void TestVSpan::selectTestEdges()
{
    auto* span = new QCPItemVSpan(mPlot);
    span->setRange(QCPRange(2, 8));
    mPlot->replot();

    double lowerPx = mPlot->xAxis->coordToPixel(2);
    double midY = mPlot->yAxis->axisRect()->center().y();

    QVariant details;
    double dist = span->selectTest(QPointF(lowerPx, midY), false, &details);
    QVERIFY(dist >= 0);
    QCOMPARE(details.toInt(), static_cast<int>(QCPItemVSpan::hpLowerEdge));
}

void TestVSpan::selectTestFill()
{
    auto* span = new QCPItemVSpan(mPlot);
    span->setRange(QCPRange(2, 8));
    mPlot->replot();

    double midPx = mPlot->xAxis->coordToPixel(5);
    double midY = mPlot->yAxis->axisRect()->center().y();

    QVariant details;
    double dist = span->selectTest(QPointF(midPx, midY), false, &details);
    QVERIFY(dist >= 0);
    QCOMPARE(details.toInt(), static_cast<int>(QCPItemVSpan::hpFill));
}

void TestVSpan::selectTestMiss()
{
    auto* span = new QCPItemVSpan(mPlot);
    span->setRange(QCPRange(2, 4));
    mPlot->replot();

    double farPx = mPlot->xAxis->coordToPixel(9);
    double midY = mPlot->yAxis->axisRect()->center().y();

    double dist = span->selectTest(QPointF(farPx, midY), false);
    QCOMPARE(dist, -1.0);
}

void TestVSpan::movableFlag()
{
    auto* span = new QCPItemVSpan(mPlot);
    QVERIFY(span->movable());
    span->setMovable(false);
    QVERIFY(!span->movable());
}

void TestVSpan::drawDoesNotCrash()
{
    auto* span = new QCPItemVSpan(mPlot);
    span->setRange(QCPRange(1, 9));
    mPlot->replot();
    QVERIFY(true);
}

void TestVSpan::drawOnLogAxis()
{
    mPlot->xAxis->setScaleType(QCPAxis::stLogarithmic);
    mPlot->xAxis->setRange(1, 1000);
    auto* span = new QCPItemVSpan(mPlot);
    span->setRange(QCPRange(10, 100));
    mPlot->replot();
    QVERIFY(true);
}

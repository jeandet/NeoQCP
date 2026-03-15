#include "test-rspan.h"
#include "../../../src/qcp.h"

void TestRSpan::init()
{
    mPlot = new QCustomPlot();
    mPlot->resize(400, 300);
    mPlot->xAxis->setRange(0, 10);
    mPlot->yAxis->setRange(0, 10);
    mPlot->replot();
}

void TestRSpan::cleanup()
{
    delete mPlot;
    mPlot = nullptr;
}

void TestRSpan::createAndRanges()
{
    auto* span = new QCPItemRSpan(mPlot);
    span->setKeyRange(QCPRange(2, 8));
    span->setValueRange(QCPRange(3, 7));

    QCOMPARE(span->keyRange().lower, 2.0);
    QCOMPARE(span->keyRange().upper, 8.0);
    QCOMPARE(span->valueRange().lower, 3.0);
    QCOMPARE(span->valueRange().upper, 7.0);
}

void TestRSpan::selectTestEdges()
{
    auto* span = new QCPItemRSpan(mPlot);
    span->setKeyRange(QCPRange(2, 8));
    span->setValueRange(QCPRange(2, 8));
    mPlot->replot();

    const double midY = (mPlot->yAxis->coordToPixel(2) + mPlot->yAxis->coordToPixel(8)) * 0.5;
    const double midX = (mPlot->xAxis->coordToPixel(2) + mPlot->xAxis->coordToPixel(8)) * 0.5;

    // left edge
    {
        QVariant details;
        double dist = span->selectTest(
            QPointF(mPlot->xAxis->coordToPixel(2), midY), false, &details);
        QVERIFY(dist >= 0);
        QCOMPARE(details.toInt(), static_cast<int>(QCPItemRSpan::hpLeft));
    }

    // right edge
    {
        QVariant details;
        double dist = span->selectTest(
            QPointF(mPlot->xAxis->coordToPixel(8), midY), false, &details);
        QVERIFY(dist >= 0);
        QCOMPARE(details.toInt(), static_cast<int>(QCPItemRSpan::hpRight));
    }

    // top edge
    {
        QVariant details;
        double dist = span->selectTest(
            QPointF(midX, mPlot->yAxis->coordToPixel(8)), false, &details);
        QVERIFY(dist >= 0);
        QCOMPARE(details.toInt(), static_cast<int>(QCPItemRSpan::hpTop));
    }

    // bottom edge
    {
        QVariant details;
        double dist = span->selectTest(
            QPointF(midX, mPlot->yAxis->coordToPixel(2)), false, &details);
        QVERIFY(dist >= 0);
        QCOMPARE(details.toInt(), static_cast<int>(QCPItemRSpan::hpBottom));
    }
}

void TestRSpan::selectTestFill()
{
    auto* span = new QCPItemRSpan(mPlot);
    span->setKeyRange(QCPRange(2, 8));
    span->setValueRange(QCPRange(2, 8));
    mPlot->replot();

    const double midX = mPlot->xAxis->coordToPixel(5);
    const double midY = mPlot->yAxis->coordToPixel(5);

    QVariant details;
    double dist = span->selectTest(QPointF(midX, midY), false, &details);
    QVERIFY(dist >= 0);
    QCOMPARE(details.toInt(), static_cast<int>(QCPItemRSpan::hpFill));
}

void TestRSpan::drawDoesNotCrash()
{
    auto* span = new QCPItemRSpan(mPlot);
    span->setKeyRange(QCPRange(1, 9));
    span->setValueRange(QCPRange(1, 9));
    mPlot->replot();
    QVERIFY(true);
}

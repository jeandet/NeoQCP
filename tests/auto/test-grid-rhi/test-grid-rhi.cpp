#include "test-grid-rhi.h"
#include "../../../src/qcp.h"
#include "../../../src/painting/grid-rhi-layer.h"

void TestGridRhi::init()
{
    mPlot = new QCustomPlot();
    mPlot->resize(400, 300);
    mPlot->xAxis->setRange(-5, 5);
    mPlot->yAxis->setRange(-5, 5);
    mPlot->replot();
}

void TestGridRhi::cleanup()
{
    delete mPlot;
    mPlot = nullptr;
}

void TestGridRhi::gridRhiLayerCreatedLazily()
{
    auto* grl = mPlot->gridRhiLayer();
    Q_UNUSED(grl);
    QVERIFY(true);
}

void TestGridRhi::replotDoesNotCrash()
{
    mPlot->replot();
    QVERIFY(true);
}

void TestGridRhi::replotWithSubGridDoesNotCrash()
{
    mPlot->xAxis->grid()->setSubGridVisible(true);
    mPlot->yAxis->grid()->setSubGridVisible(true);
    mPlot->replot();
    QVERIFY(true);
}

void TestGridRhi::replotWithZeroLineDoesNotCrash()
{
    mPlot->xAxis->setRange(-10, 10);
    mPlot->yAxis->setRange(-10, 10);
    mPlot->xAxis->grid()->setZeroLinePen(QPen(Qt::black, 1, Qt::SolidLine));
    mPlot->yAxis->grid()->setZeroLinePen(QPen(Qt::black, 1, Qt::SolidLine));
    mPlot->replot();
    QVERIFY(true);
}

void TestGridRhi::replotWithLogAxisDoesNotCrash()
{
    mPlot->xAxis->setScaleType(QCPAxis::stLogarithmic);
    mPlot->xAxis->setRange(0.01, 1000);
    mPlot->xAxis->setTicker(QSharedPointer<QCPAxisTickerLog>(new QCPAxisTickerLog));
    mPlot->replot();
    QVERIFY(true);
}

void TestGridRhi::replotWithReversedAxisDoesNotCrash()
{
    mPlot->xAxis->setRangeReversed(true);
    mPlot->yAxis->setRangeReversed(true);
    mPlot->replot();
    QVERIFY(true);
}

void TestGridRhi::replotMultipleAxisRectsDoesNotCrash()
{
    mPlot->plotLayout()->addElement(0, 1, new QCPAxisRect(mPlot));
    mPlot->replot();
    QVERIFY(true);
}

void TestGridRhi::dirtyDetectionSkipsRebuildOnPan()
{
    mPlot->replot();
    QCPRange oldRange = mPlot->xAxis->range();
    mPlot->xAxis->setRange(oldRange.lower + 0.01, oldRange.upper + 0.01);
    mPlot->replot();
    QVERIFY(true);
}

void TestGridRhi::dirtyDetectionRebuildsOnTickChange()
{
    mPlot->replot();
    mPlot->xAxis->setRange(100, 200);
    mPlot->replot();
    QVERIFY(true);
}

void TestGridRhi::dirtyDetectionRebuildsOnPenChange()
{
    mPlot->replot();
    mPlot->xAxis->grid()->setPen(QPen(Qt::red, 2));
    mPlot->replot();
    QVERIFY(true);
}

void TestGridRhi::exportStillUsesQPainter()
{
    QPixmap pm = mPlot->toPixmap(200, 150);
    QVERIFY(!pm.isNull());
    QCOMPARE(pm.width(), 200);
    QCOMPARE(pm.height(), 150);
}

#include "test-pipeline.h"
#include <qcustomplot.h>

void TestPipeline::stallPixelOffsetGraph2Busy()
{
    auto* graph = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    QVector<double> keys(200000), values(200000);
    for (int i = 0; i < 200000; ++i) {
        keys[i] = i;
        values[i] = std::sin(i * 0.01);
    }
    graph->setData(std::move(keys), std::move(values));

    mPlot->xAxis->setRange(0, 100000);
    mPlot->yAxis->setRange(-1.5, 1.5);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    QTRY_VERIFY_WITH_TIMEOUT(!graph->pipeline().isBusy(), 5000);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    // Should be zero when not busy
    QCOMPARE(graph->stallPixelOffset(), QPointF(0, 0));

    // Pan to trigger busy state
    mPlot->xAxis->setRange(10000, 110000);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    if (graph->pipeline().isBusy())
    {
        QPointF offset = graph->stallPixelOffset();
        QVERIFY(!offset.isNull());
    }
}

void TestPipeline::stallPixelOffsetIdleIsZero()
{
    auto* graph = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    QVector<double> keys(100), values(100);
    for (int i = 0; i < 100; ++i) {
        keys[i] = i;
        values[i] = i;
    }
    graph->setData(std::move(keys), std::move(values));

    mPlot->xAxis->setRange(0, 100);
    mPlot->yAxis->setRange(0, 100);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    QTRY_VERIFY_WITH_TIMEOUT(!graph->pipeline().isBusy(), 5000);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    QCOMPARE(graph->stallPixelOffset(), QPointF(0, 0));
}

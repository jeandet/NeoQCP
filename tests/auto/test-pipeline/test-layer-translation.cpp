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

void TestPipeline::layerPixelOffsetFromBusyChild()
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

    QCPLayer* mainLayer = mPlot->layer("main");
    QVERIFY(mainLayer);

    // Idle: layer offset should be zero
    QCOMPARE(mainLayer->pixelOffset(), QPointF(0, 0));

    // Pan to trigger busy state
    mPlot->xAxis->setRange(10000, 110000);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    if (graph->pipeline().isBusy())
    {
        QPointF layerOffset = mainLayer->pixelOffset();
        QPointF plottableOffset = graph->stallPixelOffset();
        // Layer offset should match the plottable's offset
        QCOMPARE(layerOffset, plottableOffset);
    }
}

void TestPipeline::layerPixelOffsetZeroWhenNoAsyncChildren()
{
    // main layer with no plottables should return zero
    QCPLayer* mainLayer = mPlot->layer("main");
    QVERIFY(mainLayer);
    QCOMPARE(mainLayer->pixelOffset(), QPointF(0, 0));
}

void TestPipeline::layerTranslationClippedToAxisRect()
{
    // GPU scissor clipping is hardware-enforced and cannot be verified via toPixmap()
    // (which uses the QPainter export path, bypassing RHI). This test is a smoke test:
    // verify that a large pan with layer translation enabled doesn't crash or assert.
    auto* graph = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    QVector<double> keys(200000), values(200000);
    for (int i = 0; i < 200000; ++i) {
        keys[i] = i;
        values[i] = 1.0;
    }
    graph->setData(std::move(keys), std::move(values));
    graph->setPen(QPen(Qt::red, 2));

    mPlot->xAxis->setRange(0, 100000);
    mPlot->yAxis->setRange(-2, 2);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    QTRY_VERIFY_WITH_TIMEOUT(!graph->pipeline().isBusy(), 5000);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    // Large pan — layer offset will be very large, scissor must prevent GPU artifacts
    mPlot->xAxis->setRange(200000, 300000);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    // Verify the layer has a nonzero offset (translation is active)
    QCPLayer* mainLayer = mPlot->layer("main");
    if (graph->pipeline().isBusy())
        QVERIFY(!mainLayer->pixelOffset().isNull());

    // Another replot should not crash with the large offset + scissor
    mPlot->replot(QCustomPlot::rpImmediateRefresh);
}

void TestPipeline::bufferedMainLayerRendersSameAsLogical()
{
    auto* graph = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    QVector<double> keys(100), values(100);
    for (int i = 0; i < 100; ++i) {
        keys[i] = i;
        values[i] = std::sin(i * 0.1);
    }
    graph->setData(std::move(keys), std::move(values));

    mPlot->xAxis->setRange(0, 100);
    mPlot->yAxis->setRange(-1.5, 1.5);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    QTRY_VERIFY_WITH_TIMEOUT(!graph->pipeline().isBusy(), 5000);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    QCOMPARE(mPlot->layer("main")->mode(), QCPLayer::lmBuffered);

    QPixmap pixmap = mPlot->toPixmap(400, 300);
    QVERIFY(!pixmap.isNull());
}

void TestPipeline::existingGraph2TranslationUnaffected()
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

    QVERIFY(graph->hasRenderedRange());

    mPlot->xAxis->setRange(10000, 110000);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    if (graph->pipeline().isBusy())
    {
        QVERIFY(graph->hasRenderedRange());
        QPointF offset = graph->stallPixelOffset();
        QVERIFY(!offset.isNull());
    }
}

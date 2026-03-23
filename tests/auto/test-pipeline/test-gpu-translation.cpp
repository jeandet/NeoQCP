#include "test-pipeline.h"
#include <qcustomplot.h>

void TestPipeline::graph2TranslationOffsetWhenBusy()
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

    mPlot->xAxis->setRange(10000, 110000);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    if (graph->pipeline().isBusy())
    {
        QVERIFY(graph->hasRenderedRange());
    }
}

void TestPipeline::graph2TranslationResetsOnFreshData()
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

    mPlot->xAxis->setRange(10000, 110000);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    QTRY_VERIFY_WITH_TIMEOUT(!graph->pipeline().isBusy(), 5000);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    QVERIFY(!graph->pipeline().isBusy());
}

void TestPipeline::multiGraphTranslationOffsetWhenBusy()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    constexpr int N = 200000;
    QVector<double> keys(N);
    std::vector<QVector<double>> columns(3, QVector<double>(N));
    for (int i = 0; i < N; ++i) {
        keys[i] = i;
        for (int c = 0; c < 3; ++c)
            columns[c][i] = std::sin(i * 0.01 + c);
    }
    mg->setData(std::move(keys), std::move(columns));

    mPlot->xAxis->setRange(0, 100000);
    mPlot->yAxis->setRange(-1.5, 1.5);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    QTRY_VERIFY_WITH_TIMEOUT(!mg->pipeline().isBusy(), 5000);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    mPlot->xAxis->setRange(10000, 110000);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    if (mg->pipeline().isBusy())
        QVERIFY(mg->hasRenderedRange());
}

void TestPipeline::colormap2TranslationOffsetWhenBusy()
{
    auto* cm = new QCPColorMap2(mPlot->xAxis, mPlot->yAxis);

    constexpr int NX = 500, NY = 500;
    std::vector<double> x(NX), y(NY), z(NX * NY);
    for (int i = 0; i < NX; ++i) x[i] = i;
    for (int i = 0; i < NY; ++i) y[i] = i;
    for (int i = 0; i < NX * NY; ++i) z[i] = std::sin(i * 0.001);
    cm->setData(std::move(x), std::move(y), std::move(z));

    mPlot->xAxis->setRange(0, 500);
    mPlot->yAxis->setRange(0, 500);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    QTRY_VERIFY_WITH_TIMEOUT(!cm->pipeline().isBusy(), 5000);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    mPlot->xAxis->setRange(50, 550);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    if (cm->pipeline().isBusy())
        QVERIFY(cm->hasRenderedRange());
}

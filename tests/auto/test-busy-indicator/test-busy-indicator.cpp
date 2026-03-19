#include "test-busy-indicator.h"
#include <qcustomplot.h>

void TestBusyIndicator::init()
{
    mPlot = new QCustomPlot(nullptr);
    mPlot->show();
}

void TestBusyIndicator::cleanup()
{
    delete mPlot;
}

void TestBusyIndicator::externalBusyDefault()
{
    auto* g = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    QCOMPARE(g->busy(), false);
    QCOMPARE(g->visuallyBusy(), false);
}

void TestBusyIndicator::setBusyEmitsBusyChanged()
{
    auto* g = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    QSignalSpy spy(g, &QCPAbstractPlottable::busyChanged);
    g->setBusy(true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);
    g->setBusy(false);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toBool(), false);
}

void TestBusyIndicator::debounceShowDelay()
{
    auto* g = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    g->setBusyShowDelayMs(100);
    QSignalSpy spy(g, &QCPAbstractPlottable::visuallyBusyChanged);

    g->setBusy(true);
    QCOMPARE(g->visuallyBusy(), false);

    QTest::qWait(150);
    QCOMPARE(g->visuallyBusy(), true);
    QCOMPARE(spy.count(), 1);
}

void TestBusyIndicator::debounceHideDelay()
{
    auto* g = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    g->setBusyShowDelayMs(10);
    g->setBusyHideDelayMs(100);

    g->setBusy(true);
    QTest::qWait(50);
    QCOMPARE(g->visuallyBusy(), true);

    QSignalSpy spy(g, &QCPAbstractPlottable::visuallyBusyChanged);
    g->setBusy(false);
    QCOMPARE(g->visuallyBusy(), true); // still shown (hide delay)

    QTest::qWait(150);
    QCOMPARE(g->visuallyBusy(), false);
    QCOMPARE(spy.count(), 1);
}

void TestBusyIndicator::fastToggleNoVisualChange()
{
    auto* g = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    g->setBusyShowDelayMs(100);
    QSignalSpy spy(g, &QCPAbstractPlottable::visuallyBusyChanged);

    g->setBusy(true);
    QTest::qWait(30);
    g->setBusy(false);

    QTest::qWait(150);
    QCOMPARE(g->visuallyBusy(), false);
    QCOMPARE(spy.count(), 0);
}

void TestBusyIndicator::perPlottableOverridesTheme()
{
    auto* g = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    QCOMPARE(g->effectiveBusyFadeAlpha(), 0.3);
    g->setBusyFadeAlpha(0.5);
    QCOMPARE(g->effectiveBusyFadeAlpha(), 0.5);
}

void TestBusyIndicator::resetFallsBackToTheme()
{
    auto* g = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    g->setBusyFadeAlpha(0.5);
    QCOMPARE(g->effectiveBusyFadeAlpha(), 0.5);
    g->resetBusyFadeAlpha();
    QCOMPARE(g->effectiveBusyFadeAlpha(), 0.3);
}

void TestBusyIndicator::pipelineBusyContributesToEffective()
{
    auto* g = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    g->setBusyShowDelayMs(10);

    const int N = 10'000'000;
    std::vector<double> keys(N), values(N);
    for (int i = 0; i < N; ++i) {
        keys[i] = i;
        values[i] = i * 0.01;
    }
    g->setData(std::move(keys), std::move(values));

    QCOMPARE(g->busy(), true);
    QTRY_COMPARE_WITH_TIMEOUT(g->busy(), false, 10000);
}

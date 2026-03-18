#include "test-overlay.h"

void TestOverlay::init()
{
    mPlot = new QCustomPlot();
    mPlot->resize(400, 300);
    mPlot->show();
    QTest::qWaitForWindowExposed(mPlot);
}

void TestOverlay::cleanup()
{
    delete mPlot;
}

void TestOverlay::showMessageStoresText()
{
    auto* ov = mPlot->overlay();
    QVERIFY(ov != nullptr);
    ov->showMessage("hello", QCPOverlay::Info);
    QCOMPARE(ov->text(), QString("hello"));
    QCOMPARE(ov->level(), QCPOverlay::Info);
    QVERIFY(ov->visible());
}

void TestOverlay::clearMessageHidesOverlay()
{
    auto* ov = mPlot->overlay();
    ov->showMessage("hello", QCPOverlay::Error);
    ov->clearMessage();
    QCOMPARE(ov->text(), QString());
    QVERIFY(!ov->visible());
}

void TestOverlay::showMessageEmitsSignal()
{
    auto* ov = mPlot->overlay();
    QSignalSpy spy(ov, &QCPOverlay::messageChanged);
    ov->showMessage("test", QCPOverlay::Warning);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QString("test"));
}

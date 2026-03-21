#include "test-creation-mode.h"
#include "../../../src/qcp.h"

void TestCreationMode::init()
{
    mPlot = new QCustomPlot();
    mPlot->resize(400, 300);
    mPlot->xAxis->setRange(0, 10);
    mPlot->yAxis->setRange(0, 10);
    mPlot->replot();
}

void TestCreationMode::cleanup()
{
    delete mPlot;
    mPlot = nullptr;
}

void TestCreationMode::initialStateIsIdle()
{
    QVERIFY(!mPlot->creationModeEnabled());
    QVERIFY(mPlot->itemCreator() == nullptr);
}

void TestCreationMode::noCreatorSetClickDoesNothing()
{
    mPlot->setCreationModeEnabled(true);
    QPoint center = mPlot->axisRect()->rect().center();
    QTest::mouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, center);
    QCOMPARE(mPlot->itemCount(), 0);
}

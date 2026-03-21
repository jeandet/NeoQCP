#include "test-creation-mode.h"
#include "../../../src/qcp.h"

void TestCreationMode::init()
{
    qRegisterMetaType<QCPAbstractItem*>();
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

void TestCreationMode::vspanCreationClickMoveClick()
{
    mPlot->setItemCreator([](QCustomPlot* p, QCPAxis*, QCPAxis*) -> QCPAbstractItem* {
        return new QCPItemVSpan(p);
    });
    mPlot->setCreationModeEnabled(true);

    QSignalSpy created(mPlot, &QCustomPlot::itemCreated);

    QPoint p1 = QPoint(mPlot->xAxis->coordToPixel(2), mPlot->axisRect()->center().y());
    QPoint p2 = QPoint(mPlot->xAxis->coordToPixel(6), mPlot->axisRect()->center().y());

    QTest::mouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p1);
    QCOMPARE(mPlot->itemCount(), 1);

    QTest::mouseMove(mPlot, p2);
    QApplication::processEvents();

    QTest::mouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p2);

    QCOMPARE(created.count(), 1);
    auto* span = qobject_cast<QCPItemVSpan*>(created.at(0).at(0).value<QCPAbstractItem*>());
    QVERIFY(span);
    QVERIFY(qAbs(span->range().lower - 2.0) < 0.5);
    QVERIFY(qAbs(span->range().upper - 6.0) < 0.5);
}

void TestCreationMode::hspanCreationClickMoveClick()
{
    mPlot->setItemCreator([](QCustomPlot* p, QCPAxis*, QCPAxis*) -> QCPAbstractItem* {
        return new QCPItemHSpan(p);
    });
    mPlot->setCreationModeEnabled(true);

    QSignalSpy created(mPlot, &QCustomPlot::itemCreated);

    QPoint p1 = QPoint(mPlot->axisRect()->center().x(), mPlot->yAxis->coordToPixel(3));
    QPoint p2 = QPoint(mPlot->axisRect()->center().x(), mPlot->yAxis->coordToPixel(7));

    QTest::mouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p1);
    QTest::mouseMove(mPlot, p2);
    QApplication::processEvents();
    QTest::mouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p2);

    QCOMPARE(created.count(), 1);
    auto* span = qobject_cast<QCPItemHSpan*>(created.at(0).at(0).value<QCPAbstractItem*>());
    QVERIFY(span);
    QVERIFY(qAbs(span->range().lower - 3.0) < 0.5);
    QVERIFY(qAbs(span->range().upper - 7.0) < 0.5);
}

void TestCreationMode::rspanCreationClickMoveClick()
{
    mPlot->setItemCreator([](QCustomPlot* p, QCPAxis*, QCPAxis*) -> QCPAbstractItem* {
        return new QCPItemRSpan(p);
    });
    mPlot->setCreationModeEnabled(true);

    QSignalSpy created(mPlot, &QCustomPlot::itemCreated);

    QPoint p1 = QPoint(mPlot->xAxis->coordToPixel(2), mPlot->yAxis->coordToPixel(3));
    QPoint p2 = QPoint(mPlot->xAxis->coordToPixel(8), mPlot->yAxis->coordToPixel(7));

    QTest::mouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p1);
    QTest::mouseMove(mPlot, p2);
    QApplication::processEvents();
    QTest::mouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p2);

    QCOMPARE(created.count(), 1);
    auto* rspan = qobject_cast<QCPItemRSpan*>(created.at(0).at(0).value<QCPAbstractItem*>());
    QVERIFY(rspan);
    QVERIFY(qAbs(rspan->keyRange().lower - 2.0) < 0.5);
    QVERIFY(qAbs(rspan->keyRange().upper - 8.0) < 0.5);
}

void TestCreationMode::cancelWithEscape()
{
    mPlot->setItemCreator([](QCustomPlot* p, QCPAxis*, QCPAxis*) -> QCPAbstractItem* {
        return new QCPItemVSpan(p);
    });
    mPlot->setCreationModeEnabled(true);

    QSignalSpy canceled(mPlot, &QCustomPlot::itemCanceled);

    QPoint center = mPlot->axisRect()->rect().center();
    QTest::mouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, center);
    QCOMPARE(mPlot->itemCount(), 1);

    QTest::keyClick(mPlot, Qt::Key_Escape);
    QCOMPARE(canceled.count(), 1);
    QCOMPARE(mPlot->itemCount(), 0);
}

void TestCreationMode::cancelWithRightClick()
{
    mPlot->setItemCreator([](QCustomPlot* p, QCPAxis*, QCPAxis*) -> QCPAbstractItem* {
        return new QCPItemVSpan(p);
    });
    mPlot->setCreationModeEnabled(true);

    QSignalSpy canceled(mPlot, &QCustomPlot::itemCanceled);

    QPoint center = mPlot->axisRect()->rect().center();
    QTest::mouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, center);
    QCOMPARE(mPlot->itemCount(), 1);

    QTest::mouseClick(mPlot, Qt::RightButton, Qt::NoModifier, center);
    QCOMPARE(canceled.count(), 1);
    QCOMPARE(mPlot->itemCount(), 0);
}

void TestCreationMode::cancelCleansUpItem()
{
    mPlot->setItemCreator([](QCustomPlot* p, QCPAxis*, QCPAxis*) -> QCPAbstractItem* {
        return new QCPItemVSpan(p);
    });
    mPlot->setCreationModeEnabled(true);

    QSignalSpy canceled(mPlot, &QCustomPlot::itemCanceled);

    QPoint center = mPlot->axisRect()->rect().center();
    QTest::mouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, center);
    QCOMPARE(mPlot->itemCount(), 1);

    // Programmatic cancel via disabling creation mode
    mPlot->setCreationModeEnabled(false);
    QCOMPARE(canceled.count(), 1);
    QCOMPARE(mPlot->itemCount(), 0);
}

void TestCreationMode::modifierTriggerCreatesItem()
{
    mPlot->setItemCreator([](QCustomPlot* p, QCPAxis*, QCPAxis*) -> QCPAbstractItem* {
        return new QCPItemVSpan(p);
    });
    // NOT in creation mode — using modifier instead
    QVERIFY(!mPlot->creationModeEnabled());

    QSignalSpy created(mPlot, &QCustomPlot::itemCreated);

    QPoint p1 = QPoint(mPlot->xAxis->coordToPixel(3), mPlot->axisRect()->center().y());
    QPoint p2 = QPoint(mPlot->xAxis->coordToPixel(7), mPlot->axisRect()->center().y());

    // Shift+click starts creation
    QTest::mouseClick(mPlot, Qt::LeftButton, Qt::ShiftModifier, p1);
    QCOMPARE(mPlot->itemCount(), 1);

    // Second click commits (no modifier needed for commit)
    QTest::mouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p2);
    QCOMPARE(created.count(), 1);
}

void TestCreationMode::modifierNotHeldDoesNothing()
{
    mPlot->setItemCreator([](QCustomPlot* p, QCPAxis*, QCPAxis*) -> QCPAbstractItem* {
        return new QCPItemVSpan(p);
    });
    QVERIFY(!mPlot->creationModeEnabled());

    QPoint center = mPlot->axisRect()->rect().center();
    // Click without modifier — should not create
    QTest::mouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, center);
    QCOMPARE(mPlot->itemCount(), 0);
}

void TestCreationMode::batchModeStaysActiveAfterCommit()
{
    mPlot->setItemCreator([](QCustomPlot* p, QCPAxis*, QCPAxis*) -> QCPAbstractItem* {
        return new QCPItemVSpan(p);
    });
    mPlot->setCreationModeEnabled(true);

    QSignalSpy created(mPlot, &QCustomPlot::itemCreated);

    QPoint p1 = QPoint(mPlot->xAxis->coordToPixel(2), mPlot->axisRect()->center().y());
    QPoint p2 = QPoint(mPlot->xAxis->coordToPixel(4), mPlot->axisRect()->center().y());
    QPoint p3 = QPoint(mPlot->xAxis->coordToPixel(6), mPlot->axisRect()->center().y());
    QPoint p4 = QPoint(mPlot->xAxis->coordToPixel(8), mPlot->axisRect()->center().y());

    // Create first span
    QTest::mouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p1);
    QTest::mouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p2);
    QCOMPARE(created.count(), 1);
    QCOMPARE(mPlot->itemCount(), 1);

    // Should still be in creation mode — create second span
    QTest::mouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p3);
    QTest::mouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p4);
    QCOMPARE(created.count(), 2);
    QCOMPARE(mPlot->itemCount(), 2);
}

void TestCreationMode::creationTakesPriorityOverSelectionRect()
{
    mPlot->setItemCreator([](QCustomPlot* p, QCPAxis*, QCPAxis*) -> QCPAbstractItem* {
        return new QCPItemVSpan(p);
    });
    mPlot->setSelectionRectMode(QCP::srmZoom);
    mPlot->setCreationModeEnabled(true);

    QSignalSpy created(mPlot, &QCustomPlot::itemCreated);

    QPoint p1 = QPoint(mPlot->xAxis->coordToPixel(3), mPlot->axisRect()->center().y());
    QPoint p2 = QPoint(mPlot->xAxis->coordToPixel(7), mPlot->axisRect()->center().y());

    QTest::mouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p1);
    QTest::mouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p2);

    // Creation should have taken priority — item created, not a zoom rect
    QCOMPARE(created.count(), 1);
    QCOMPARE(mPlot->itemCount(), 1);
}

void TestCreationMode::fallbackTwoPositionItem()
{
    // Use QCPItemLine (has 2 positions: start, end)
    mPlot->setItemCreator([](QCustomPlot* p, QCPAxis*, QCPAxis*) -> QCPAbstractItem* {
        return new QCPItemLine(p);
    });
    mPlot->setCreationModeEnabled(true);

    QSignalSpy created(mPlot, &QCustomPlot::itemCreated);

    QPoint p1 = QPoint(mPlot->xAxis->coordToPixel(2), mPlot->yAxis->coordToPixel(3));
    QPoint p2 = QPoint(mPlot->xAxis->coordToPixel(8), mPlot->yAxis->coordToPixel(7));

    QTest::mouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p1);
    QTest::mouseMove(mPlot, p2);
    QApplication::processEvents();
    QTest::mouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p2);

    QCOMPARE(created.count(), 1);
    auto* line = qobject_cast<QCPItemLine*>(created.at(0).at(0).value<QCPAbstractItem*>());
    QVERIFY(line);
}

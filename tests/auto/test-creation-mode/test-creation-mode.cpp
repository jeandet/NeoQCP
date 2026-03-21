#include "test-creation-mode.h"
#include "../../../src/qcp.h"
#include "../../../src/item-creation-state.h"
#include <QMouseEvent>
#include <QKeyEvent>

namespace {
void sendMouseClick(QWidget* w, Qt::MouseButton button, Qt::KeyboardModifiers mods, const QPoint& pos)
{
    QMouseEvent press(QEvent::MouseButtonPress, pos, w->mapToGlobal(pos), button, button, mods);
    QMouseEvent release(QEvent::MouseButtonRelease, pos, w->mapToGlobal(pos), button, Qt::NoButton, mods);
    QApplication::sendEvent(w, &press);
    QApplication::sendEvent(w, &release);
}

void sendMouseMove(QWidget* w, const QPoint& pos)
{
    QMouseEvent move(QEvent::MouseMove, pos, w->mapToGlobal(pos), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(w, &move);
}

void sendKeyClick(QWidget* w, Qt::Key key)
{
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QApplication::sendEvent(w, &press);
    QApplication::sendEvent(w, &release);
}
} // namespace

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
    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, center);
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

    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p1);
    QCOMPARE(mPlot->itemCount(), 1);

    sendMouseMove(mPlot, p2);
    QApplication::processEvents();

    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p2);

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

    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p1);
    sendMouseMove(mPlot, p2);
    QApplication::processEvents();
    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p2);

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

    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p1);
    sendMouseMove(mPlot, p2);
    QApplication::processEvents();
    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p2);

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
    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, center);
    QCOMPARE(mPlot->itemCount(), 1);

    sendKeyClick(mPlot, Qt::Key_Escape);
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
    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, center);
    QCOMPARE(mPlot->itemCount(), 1);

    sendMouseClick(mPlot, Qt::RightButton, Qt::NoModifier, center);
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
    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, center);
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
    sendMouseClick(mPlot, Qt::LeftButton, Qt::ShiftModifier, p1);
    QCOMPARE(mPlot->itemCount(), 1);

    // Second click commits (no modifier needed for commit)
    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p2);
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
    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, center);
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
    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p1);
    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p2);
    QCOMPARE(created.count(), 1);
    QCOMPARE(mPlot->itemCount(), 1);

    // Should still be in creation mode — create second span
    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p3);
    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p4);
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

    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p1);
    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p2);

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

    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p1);
    sendMouseMove(mPlot, p2);
    QApplication::processEvents();
    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p2);

    QCOMPARE(created.count(), 1);
    auto* line = qobject_cast<QCPItemLine*>(created.at(0).at(0).value<QCPAbstractItem*>());
    QVERIFY(line);
}

void TestCreationMode::cursorChangesInCreationMode()
{
    mPlot->setItemCreator([](QCustomPlot* p, QCPAxis*, QCPAxis*) -> QCPAbstractItem* {
        return new QCPItemVSpan(p);
    });

    // Entering creation mode sets crosshair
    mPlot->setCreationModeEnabled(true);
    QCOMPARE(mPlot->cursor().shape(), Qt::CrossCursor);

    // During drawing, cursor should be crosshair
    QPoint center = mPlot->axisRect()->rect().center();
    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, center);
    QCOMPARE(mPlot->cursor().shape(), Qt::CrossCursor);

    // After commit, cursor stays crosshair (still in batch mode)
    QPoint p2 = QPoint(center.x() + 50, center.y());
    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p2);
    QCOMPARE(mPlot->cursor().shape(), Qt::CrossCursor);

    // Exiting creation mode restores arrow
    mPlot->setCreationModeEnabled(false);
    QCOMPARE(mPlot->cursor().shape(), Qt::ArrowCursor);
}

void TestCreationMode::clickOutsideAxisRectDoesNothing()
{
    mPlot->setItemCreator([](QCustomPlot* p, QCPAxis*, QCPAxis*) -> QCPAbstractItem* {
        return new QCPItemVSpan(p);
    });
    mPlot->setCreationModeEnabled(true);

    // Click outside the axis rect (top-left corner of widget)
    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
    QCOMPARE(mPlot->itemCount(), 0);
}

void TestCreationMode::creatorReturnsNullDoesNotCrash()
{
    mPlot->setItemCreator([](QCustomPlot*, QCPAxis*, QCPAxis*) -> QCPAbstractItem* {
        return nullptr;
    });
    mPlot->setCreationModeEnabled(true);

    QPoint center = mPlot->axisRect()->rect().center();
    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, center);
    QCOMPARE(mPlot->itemCount(), 0);
    // Should not crash or enter Drawing state
}

void TestCreationMode::disableCreationModeDuringDrawingCancels()
{
    mPlot->setItemCreator([](QCustomPlot* p, QCPAxis*, QCPAxis*) -> QCPAbstractItem* {
        return new QCPItemVSpan(p);
    });
    mPlot->setCreationModeEnabled(true);

    QSignalSpy canceled(mPlot, &QCustomPlot::itemCanceled);

    QPoint center = mPlot->axisRect()->rect().center();
    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, center);
    QCOMPARE(mPlot->itemCount(), 1);

    // Disabling creation mode while drawing should cancel
    mPlot->setCreationModeEnabled(false);
    QCOMPARE(canceled.count(), 1);
    QCOMPARE(mPlot->itemCount(), 0);
}

void TestCreationMode::batchModeIgnoresExistingItems()
{
    // Pre-create an existing VSpan
    auto* existing = new QCPItemVSpan(mPlot);
    existing->setRange(QCPRange(4, 6));
    existing->setSelectable(true);
    mPlot->replot();
    QCOMPARE(mPlot->itemCount(), 1);

    mPlot->setItemCreator([](QCustomPlot* p, QCPAxis*, QCPAxis*) -> QCPAbstractItem* {
        return new QCPItemVSpan(p);
    });
    mPlot->setCreationModeEnabled(true);

    QSignalSpy created(mPlot, &QCustomPlot::itemCreated);

    // Click right on the existing span — should start a NEW creation, not select/drag it
    double midPx = mPlot->xAxis->coordToPixel(5);
    QPoint onExisting = QPoint(midPx, mPlot->axisRect()->center().y());
    QPoint p2 = QPoint(mPlot->xAxis->coordToPixel(8), mPlot->axisRect()->center().y());

    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, onExisting);
    QCOMPARE(mPlot->itemCount(), 2); // existing + new in-progress

    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, p2);
    QCOMPARE(created.count(), 1);
    QCOMPARE(mPlot->itemCount(), 2);
}

void TestCreationMode::multiAxisRectCreation()
{
    // Add a second axis rect (subplot)
    auto* layout = mPlot->plotLayout();
    auto* axisRect2 = new QCPAxisRect(mPlot);
    layout->addElement(1, 0, axisRect2);
    axisRect2->axis(QCPAxis::atBottom)->setRange(0, 100);
    axisRect2->axis(QCPAxis::atLeft)->setRange(0, 100);
    mPlot->replot();

    mPlot->setItemCreator([](QCustomPlot* p, QCPAxis* keyAxis, QCPAxis* /*valueAxis*/) -> QCPAbstractItem* {
        auto* span = new QCPItemVSpan(p);
        span->setClipAxisRect(keyAxis->axisRect());
        span->setClipToAxisRect(true);
        return span;
    });
    mPlot->setCreationModeEnabled(true);

    QSignalSpy created(mPlot, &QCustomPlot::itemCreated);

    // Click inside the second axis rect
    QPoint center2 = axisRect2->rect().center();
    QPoint right2 = QPoint(center2.x() + 50, center2.y());

    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, center2);
    sendMouseClick(mPlot, Qt::LeftButton, Qt::NoModifier, right2);

    QCOMPARE(created.count(), 1);
    auto* span = qobject_cast<QCPItemVSpan*>(created.at(0).at(0).value<QCPAbstractItem*>());
    QVERIFY(span);
    // The span's range should be in the second axis rect's coordinate system (0-100)
    QVERIFY(span->range().lower > 10); // rough sanity check — not in 0-10 range of first rect
}

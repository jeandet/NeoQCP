#pragma once
#include <QtTest/QtTest>

class QCustomPlot;

class TestCreationMode : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    // State machine basics
    void initialStateIsIdle();
    void noCreatorSetClickDoesNothing();

    // Click-move-click creation
    void vspanCreationClickMoveClick();
    void hspanCreationClickMoveClick();
    void rspanCreationClickMoveClick();

    // Cancellation
    void cancelWithEscape();
    void cancelWithRightClick();
    void cancelCleansUpItem();

    // Modifier trigger and batch mode
    void modifierTriggerCreatesItem();
    void modifierNotHeldDoesNothing();
    void batchModeStaysActiveAfterCommit();

private:
    QCustomPlot* mPlot = nullptr;
};

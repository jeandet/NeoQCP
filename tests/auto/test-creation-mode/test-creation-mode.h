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

private:
    QCustomPlot* mPlot = nullptr;
};

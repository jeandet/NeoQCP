#pragma once
#include <QtTest/QtTest>

class QCustomPlot;

class TestRSpan : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void createAndRanges();
    void selectTestEdges();
    void selectTestFill();
    void drawDoesNotCrash();
    void exportFallbackRendersSpan();
    void dirtyTrackingReplots();
    void drawOnLogAxis();

private:
    QCustomPlot* mPlot = nullptr;
};

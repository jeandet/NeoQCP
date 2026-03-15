#pragma once
#include <QtTest/QtTest>

class QCustomPlot;

class TestWaterfall : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void uniformOffset();
    void customOffset();
    void customOffsetFallback();

    void normalizationFactors();
    void normalizationDisabled();
    void invalidateNormalization();

    void adapterValueAt();
    void singleComponentValueRange();

    void getValueRangeNormalized();
    void getValueRangeUnnormalized();
    void getValueRangeSignDomain();

    void drawDoesNotCrash();

private:
    QCustomPlot* mPlot = nullptr;
};

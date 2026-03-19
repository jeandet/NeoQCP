#pragma once
#include <QtTest/QtTest>

class QCustomPlot;

class TestBusyIndicator : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void externalBusyDefault();
    void setBusyEmitsBusyChanged();
    void debounceShowDelay();
    void debounceHideDelay();
    void fastToggleNoVisualChange();
    void perPlottableOverridesTheme();
    void resetFallsBackToTheme();

private:
    QCustomPlot* mPlot = nullptr;
};

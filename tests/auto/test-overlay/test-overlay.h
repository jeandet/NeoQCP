#include <QtTest/QtTest>
#include "qcustomplot.h"

class TestOverlay : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void showMessageStoresText();
    void clearMessageHidesOverlay();
    void showMessageEmitsSignal();

private:
    QCustomPlot* mPlot;
};

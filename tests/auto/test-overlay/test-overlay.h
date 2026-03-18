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
    void compactRectIsSingleLine();
    void fitContentRectFitsText();
    void fullWidgetRectCoversWidget();
    void positionTop();
    void positionBottom();
    void positionLeft();
    void positionRight();
    void opacityRoundtrip();
    void showMessageTriggersReplot();

private:
    QCustomPlot* mPlot;
};

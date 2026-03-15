#pragma once
#include <QtTest/QtTest>

class QCustomPlot;

class TestRichText : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void createAndSetHtml();
    void clearHtmlSwitchesToPlainText();
    void drawDoesNotCrash();
    void drawPlainFallback();

private:
    QCustomPlot* mPlot = nullptr;
};

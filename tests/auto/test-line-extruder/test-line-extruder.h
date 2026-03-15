#pragma once
#include <QObject>
#include <QTest>

class TestLineExtruder : public QObject
{
    Q_OBJECT
private slots:
    void horizontalSegment();
    void verticalSegment();
    void miterJoin();
    void bevelFallback();
    void nanGap();
    void singlePoint();
    void emptyInput();
    void twoPoints();
    void consecutiveNaNs();
    void nanAtStart();
    void nanAtEnd();
    void duplicatePoints();
    void zeroWidthPen();
    void fillHorizontalBaseline();
    void fillVerticalBaseline();
    void fillTooFewPoints();
    void fillMinimalTrapezoid();
};

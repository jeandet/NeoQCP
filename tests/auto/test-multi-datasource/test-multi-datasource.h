#pragma once
#include <QtTest/QtTest>

class QCustomPlot;

class TestMultiDataSource : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void soaOwningVector();
    void soaViewSpan();
    void soaColumnCount();
    void soaKeyAccess();
    void soaValueAccess();
    void soaFindBeginEnd();
    void soaKeyRange();
    void soaValueRangePerColumn();
    void soaValueRangeWithKeyRestriction();
    void soaGetLines();
    void soaGetOptimizedLineData();
    void soaEmpty();
    void soaMixedTypes();

    // Row-major data source
    void rowMajorValueAt();
    void rowMajorWithPadding();
    void rowMajorGetLines();

private:
    QCustomPlot* mPlot = nullptr;
};

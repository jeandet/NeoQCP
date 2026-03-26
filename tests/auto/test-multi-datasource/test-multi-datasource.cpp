#include "test-multi-datasource.h"
#include "qcustomplot.h"
#include "datasource/soa-multi-datasource.h"
#include "datasource/row-major-multi-datasource.h"
#include <vector>
#include <span>

void TestMultiDataSource::init()
{
    mPlot = new QCustomPlot();
    mPlot->resize(400, 300);
}

void TestMultiDataSource::cleanup()
{
    delete mPlot;
    mPlot = nullptr;
}

void TestMultiDataSource::soaOwningVector()
{
    std::vector<double> keys = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<std::vector<double>> cols = {
        {10.0, 20.0, 30.0, 40.0, 50.0},
        {-1.0, -2.0, -3.0, -4.0, -5.0},
        {0.5, 1.5, 2.5, 3.5, 4.5}
    };
    QCPSoAMultiDataSource src(std::move(keys), std::move(cols));
    QCOMPARE(src.columnCount(), 3);
    QCOMPARE(src.size(), 5);
    QVERIFY(!src.empty());
}

void TestMultiDataSource::soaViewSpan()
{
    std::vector<double> keys = {1.0, 2.0, 3.0};
    std::vector<double> v0 = {10.0, 20.0, 30.0};
    std::vector<double> v1 = {-1.0, -2.0, -3.0};

    QCPSoAMultiDataSource<std::span<const double>, std::span<const double>> src(
        std::span<const double>(keys),
        {std::span<const double>(v0), std::span<const double>(v1)});
    QCOMPARE(src.columnCount(), 2);
    QCOMPARE(src.size(), 3);
}

void TestMultiDataSource::soaColumnCount()
{
    std::vector<double> keys = {1.0};
    std::vector<std::vector<double>> cols;
    QCPSoAMultiDataSource src(std::move(keys), std::move(cols));
    QCOMPARE(src.columnCount(), 0);
}

void TestMultiDataSource::soaKeyAccess()
{
    std::vector<double> keys = {1.0, 2.0, 3.0};
    std::vector<std::vector<double>> cols = {{10.0, 20.0, 30.0}};
    QCPSoAMultiDataSource src(std::move(keys), std::move(cols));
    QCOMPARE(src.keyAt(0), 1.0);
    QCOMPARE(src.keyAt(1), 2.0);
    QCOMPARE(src.keyAt(2), 3.0);
}

void TestMultiDataSource::soaValueAccess()
{
    std::vector<double> keys = {1.0, 2.0, 3.0};
    std::vector<std::vector<double>> cols = {
        {10.0, 20.0, 30.0},
        {-1.0, -2.0, -3.0}
    };
    QCPSoAMultiDataSource src(std::move(keys), std::move(cols));
    QCOMPARE(src.valueAt(0, 0), 10.0);
    QCOMPARE(src.valueAt(0, 2), 30.0);
    QCOMPARE(src.valueAt(1, 0), -1.0);
    QCOMPARE(src.valueAt(1, 2), -3.0);
}

void TestMultiDataSource::soaFindBeginEnd()
{
    std::vector<double> keys = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<std::vector<double>> cols = {{0.0, 0.0, 0.0, 0.0, 0.0}};
    QCPSoAMultiDataSource src(std::move(keys), std::move(cols));
    QCOMPARE(src.findBegin(3.0, false), 2);
    QCOMPARE(src.findBegin(3.0, true), 1);
    QCOMPARE(src.findEnd(3.0, false), 3);
    QCOMPARE(src.findEnd(3.0, true), 4);
}

void TestMultiDataSource::soaKeyRange()
{
    std::vector<double> keys = {-2.0, 1.0, 3.0, 5.0};
    std::vector<std::vector<double>> cols = {{0.0, 0.0, 0.0, 0.0}};
    QCPSoAMultiDataSource src(std::move(keys), std::move(cols));
    bool found = false;
    auto range = src.keyRange(found);
    QVERIFY(found);
    QCOMPARE(range.lower, -2.0);
    QCOMPARE(range.upper, 5.0);
}

void TestMultiDataSource::soaValueRangePerColumn()
{
    std::vector<double> keys = {1.0, 2.0, 3.0};
    std::vector<std::vector<double>> cols = {
        {10.0, 20.0, 30.0},
        {-5.0, -15.0, -25.0}
    };
    QCPSoAMultiDataSource src(std::move(keys), std::move(cols));
    bool found = false;
    auto r0 = src.valueRange(0, found);
    QVERIFY(found);
    QCOMPARE(r0.lower, 10.0);
    QCOMPARE(r0.upper, 30.0);
    auto r1 = src.valueRange(1, found);
    QVERIFY(found);
    QCOMPARE(r1.lower, -25.0);
    QCOMPARE(r1.upper, -5.0);
}

void TestMultiDataSource::soaValueRangeWithKeyRestriction()
{
    std::vector<double> keys = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<std::vector<double>> cols = {{10.0, 50.0, 30.0, 20.0, 40.0}};
    QCPSoAMultiDataSource src(std::move(keys), std::move(cols));
    bool found = false;
    auto range = src.valueRange(0, found, QCP::sdBoth, QCPRange(2.0, 4.0));
    QVERIFY(found);
    QCOMPARE(range.lower, 20.0);
    QCOMPARE(range.upper, 50.0);
}

void TestMultiDataSource::soaGetLines()
{
    std::vector<double> keys = {1.0, 2.0, 3.0};
    std::vector<std::vector<double>> cols = {
        {10.0, 20.0, 30.0},
        {-1.0, -2.0, -3.0}
    };
    QCPSoAMultiDataSource src(std::move(keys), std::move(cols));
    auto* keyAxis = mPlot->xAxis;
    auto* valueAxis = mPlot->yAxis;
    keyAxis->setRange(0, 4);
    valueAxis->setRange(-5, 35);
    mPlot->replot();
    auto lines0 = src.getLines(0, 0, 3, keyAxis, valueAxis);
    auto lines1 = src.getLines(1, 0, 3, keyAxis, valueAxis);
    QCOMPARE(lines0.size(), 3);
    QCOMPARE(lines1.size(), 3);
    QVERIFY(lines0[0].y() != lines1[0].y());
}

void TestMultiDataSource::soaGetOptimizedLineData()
{
    std::vector<double> keys = {1.0, 2.0, 3.0};
    std::vector<std::vector<double>> cols = {{10.0, 20.0, 30.0}};
    QCPSoAMultiDataSource src(std::move(keys), std::move(cols));
    auto* keyAxis = mPlot->xAxis;
    auto* valueAxis = mPlot->yAxis;
    keyAxis->setRange(0, 4);
    valueAxis->setRange(0, 35);
    mPlot->replot();
    auto lines = src.getLines(0, 0, 3, keyAxis, valueAxis);
    auto optimized = src.getOptimizedLineData(0, 0, 3, 400, keyAxis, valueAxis);
    QCOMPARE(optimized.size(), lines.size());
}

void TestMultiDataSource::soaEmpty()
{
    std::vector<double> keys;
    std::vector<std::vector<double>> cols;
    QCPSoAMultiDataSource src(std::move(keys), std::move(cols));
    QCOMPARE(src.size(), 0);
    QVERIFY(src.empty());
    QCOMPARE(src.columnCount(), 0);
    bool found = false;
    src.keyRange(found);
    QVERIFY(!found);
}

void TestMultiDataSource::soaMixedTypes()
{
    std::vector<float> keys = {1.0f, 2.0f, 3.0f};
    std::vector<std::vector<int>> cols = {{10, 20, 30}, {-1, -2, -3}};
    QCPSoAMultiDataSource src(std::move(keys), std::move(cols));
    QCOMPARE(src.size(), 3);
    QCOMPARE(src.columnCount(), 2);
    QCOMPARE(src.keyAt(0), 1.0);
    QCOMPARE(src.valueAt(1, 2), -3.0);
}

void TestMultiDataSource::rowMajorValueAt()
{
    // 3 rows, 2 columns, packed (stride == columns)
    //   row0: [10, 20]
    //   row1: [30, 40]
    //   row2: [50, 60]
    std::vector<double> keys = {1.0, 2.0, 3.0};
    std::vector<double> values = {10, 20, 30, 40, 50, 60};
    QCPRowMajorMultiDataSource<double, double> src(
        std::span<const double>(keys), values.data(), 3, 2, 2);

    QCOMPARE(src.size(), 3);
    QCOMPARE(src.columnCount(), 2);
    QCOMPARE(src.keyAt(0), 1.0);
    QCOMPARE(src.keyAt(2), 3.0);
    QCOMPARE(src.valueAt(0, 0), 10.0); // col 0, row 0
    QCOMPARE(src.valueAt(1, 0), 20.0); // col 1, row 0
    QCOMPARE(src.valueAt(0, 1), 30.0); // col 0, row 1
    QCOMPARE(src.valueAt(1, 1), 40.0); // col 1, row 1
    QCOMPARE(src.valueAt(0, 2), 50.0); // col 0, row 2
    QCOMPARE(src.valueAt(1, 2), 60.0); // col 1, row 2
}

void TestMultiDataSource::rowMajorWithPadding()
{
    // 3 rows, 2 columns, stride=4 (2 padding elements per row)
    //   row0: [10, 20, __, __]
    //   row1: [30, 40, __, __]
    //   row2: [50, 60, __, __]
    std::vector<double> keys = {1.0, 2.0, 3.0};
    std::vector<double> values = {10, 20, 99, 99,
                                   30, 40, 99, 99,
                                   50, 60, 99, 99};
    QCPRowMajorMultiDataSource<double, double> src(
        std::span<const double>(keys), values.data(), 3, 2, 4);

    QCOMPARE(src.size(), 3);
    QCOMPARE(src.columnCount(), 2);
    QCOMPARE(src.valueAt(0, 0), 10.0);
    QCOMPARE(src.valueAt(1, 0), 20.0);
    QCOMPARE(src.valueAt(0, 1), 30.0);
    QCOMPARE(src.valueAt(1, 1), 40.0);
    QCOMPARE(src.valueAt(0, 2), 50.0);
    QCOMPARE(src.valueAt(1, 2), 60.0);

    // Verify StridedColumnView sees the right values
    qcp::detail::StridedColumnView<double> col0(values.data() + 0, 3, 4);
    QCOMPARE(col0[0], 10.0);
    QCOMPARE(col0[1], 30.0);
    QCOMPARE(col0[2], 50.0);
    QCOMPARE(col0.size(), 3);
}

void TestMultiDataSource::rowMajorGetLines()
{
    mPlot->xAxis->setRange(0, 6);
    mPlot->yAxis->setRange(0, 110);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    std::vector<double> keys = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> values = {10, 20,
                                   30, 40,
                                   50, 60,
                                   70, 80,
                                   90, 100};
    QCPRowMajorMultiDataSource<double, double> src(
        std::span<const double>(keys), values.data(), 5, 2, 2);

    auto lines0 = src.getLines(0, 0, 5, mPlot->xAxis, mPlot->yAxis);
    auto lines1 = src.getLines(1, 0, 5, mPlot->xAxis, mPlot->yAxis);
    QCOMPARE(lines0.size(), 5);
    QCOMPARE(lines1.size(), 5);

    auto optLines = src.getOptimizedLineData(0, 0, 5, 400,
                                              mPlot->xAxis, mPlot->yAxis);
    QVERIFY(optLines.size() > 0);
    QVERIFY(optLines.size() <= 5);
}

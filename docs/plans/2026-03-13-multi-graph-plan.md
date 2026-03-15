# QCPMultiGraph Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a multi-component graph plottable (`QCPMultiGraph`) that shares a key axis across N value columns, with per-component style/selection and a grouped legend item.

**Architecture:** Three layers — abstract multi data source interface, template SoA implementation reusing `qcp::algo::*`, and QCPMultiGraph plottable with per-component descriptors. A `QCPGroupLegendItem` provides collapsible legend grouping.

**Tech Stack:** C++20, Qt6, Meson, Qt Test

**Spec:** `docs/specs/2026-03-13-multi-graph-design.md`

---

## Chunk 1: Data Source Layer

### Task 1: Abstract Multi Data Source Interface

**Files:**
- Create: `src/datasource/abstract-multi-datasource.h`

- [ ] **Step 1: Create the abstract interface header**

```cpp
// src/datasource/abstract-multi-datasource.h
#pragma once
#include "abstract-datasource.h"

class QCPAbstractMultiDataSource {
public:
    virtual ~QCPAbstractMultiDataSource() = default;

    virtual int columnCount() const = 0;
    virtual int size() const = 0;
    virtual bool empty() const { return size() == 0; }

    virtual double keyAt(int i) const = 0;
    virtual QCPRange keyRange(bool& found, QCP::SignDomain sd = QCP::sdBoth) const = 0;
    virtual int findBegin(double sortKey, bool expandedRange = true) const = 0;
    virtual int findEnd(double sortKey, bool expandedRange = true) const = 0;

    virtual double valueAt(int column, int i) const = 0;
    virtual QCPRange valueRange(int column, bool& found,
                                QCP::SignDomain sd = QCP::sdBoth,
                                const QCPRange& inKeyRange = QCPRange()) const = 0;

    virtual QVector<QPointF> getOptimizedLineData(
        int column, int begin, int end, int pixelWidth,
        QCPAxis* keyAxis, QCPAxis* valueAxis) const = 0;

    virtual QVector<QPointF> getLines(
        int column, int begin, int end,
        QCPAxis* keyAxis, QCPAxis* valueAxis) const = 0;
};
```

- [ ] **Step 2: Add to umbrella header `src/qcp.h`**

Add after the existing `datasource/soa-datasource-2d.h` include:
```cpp
#include "datasource/abstract-multi-datasource.h"
```

- [ ] **Step 3: Build to verify compilation**

Run: `meson compile -C build`
Expected: Clean build, no errors.

- [ ] **Step 4: Commit**

```bash
git add src/datasource/abstract-multi-datasource.h src/qcp.h
git commit -m "feat: add QCPAbstractMultiDataSource interface"
```

### Task 2: Template Multi Data Source + Tests

**Files:**
- Create: `src/datasource/soa-multi-datasource.h`
- Create: `tests/auto/test-multi-datasource/test-multi-datasource.h`
- Create: `tests/auto/test-multi-datasource/test-multi-datasource.cpp`
- Modify: `tests/auto/autotest.cpp`
- Modify: `tests/auto/meson.build`
- Modify: `src/qcp.h`

- [ ] **Step 1: Write test header**

```cpp
// tests/auto/test-multi-datasource/test-multi-datasource.h
#pragma once
#include <QtTest/QtTest>

class QCustomPlot;

class TestMultiDataSource : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    // SoA multi data source tests
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

private:
    QCustomPlot* mPlot = nullptr;
};
```

- [ ] **Step 2: Write test implementation**

```cpp
// tests/auto/test-multi-datasource/test-multi-datasource.cpp
#include "test-multi-datasource.h"
#include "qcustomplot.h"
#include "datasource/soa-multi-datasource.h"
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
    // Zero columns is valid (no data to draw)
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
    // Different values should give different y pixel positions
    QVERIFY(lines0[0].y() != lines1[0].y());
}

void TestMultiDataSource::soaGetOptimizedLineData()
{
    // With few points, optimized should return same count as getLines
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
    // Float keys, int values
    std::vector<float> keys = {1.0f, 2.0f, 3.0f};
    std::vector<std::vector<int>> cols = {{10, 20, 30}, {-1, -2, -3}};
    QCPSoAMultiDataSource src(std::move(keys), std::move(cols));
    QCOMPARE(src.size(), 3);
    QCOMPARE(src.columnCount(), 2);
    QCOMPARE(src.keyAt(0), 1.0);
    QCOMPARE(src.valueAt(1, 2), -3.0);
}
```

- [ ] **Step 3: Write the template data source**

```cpp
// src/datasource/soa-multi-datasource.h
#pragma once
#include "abstract-multi-datasource.h"
#include "algorithms.h"
#include <QtGlobal>
#include <vector>

template <IndexableNumericRange KeyContainer, IndexableNumericRange ValueContainer>
class QCPSoAMultiDataSource final : public QCPAbstractMultiDataSource {
public:
    using K = std::ranges::range_value_t<KeyContainer>;
    using V = std::ranges::range_value_t<ValueContainer>;

    QCPSoAMultiDataSource(KeyContainer keys, std::vector<ValueContainer> valueColumns)
        : mKeys(std::move(keys)), mValues(std::move(valueColumns))
    {
        for (const auto& col : mValues)
            Q_ASSERT(std::ranges::size(col) == std::ranges::size(mKeys));
    }

    int columnCount() const override { return static_cast<int>(mValues.size()); }
    int size() const override { return static_cast<int>(std::ranges::size(mKeys)); }

    double keyAt(int i) const override
    {
        Q_ASSERT(i >= 0 && i < size());
        return static_cast<double>(mKeys[i]);
    }

    double valueAt(int column, int i) const override
    {
        Q_ASSERT(column >= 0 && column < columnCount());
        Q_ASSERT(i >= 0 && i < size());
        return static_cast<double>(mValues[column][i]);
    }

    QCPRange keyRange(bool& found, QCP::SignDomain sd = QCP::sdBoth) const override
    {
        return qcp::algo::keyRange(mKeys, found, sd);
    }

    QCPRange valueRange(int column, bool& found, QCP::SignDomain sd = QCP::sdBoth,
                        const QCPRange& inKeyRange = QCPRange()) const override
    {
        Q_ASSERT(column >= 0 && column < columnCount());
        return qcp::algo::valueRange(mKeys, mValues[column], found, sd, inKeyRange);
    }

    int findBegin(double sortKey, bool expandedRange = true) const override
    {
        return qcp::algo::findBegin(mKeys, sortKey, expandedRange);
    }

    int findEnd(double sortKey, bool expandedRange = true) const override
    {
        return qcp::algo::findEnd(mKeys, sortKey, expandedRange);
    }

    QVector<QPointF> getOptimizedLineData(int column, int begin, int end, int pixelWidth,
                                           QCPAxis* keyAxis, QCPAxis* valueAxis) const override
    {
        Q_ASSERT(column >= 0 && column < columnCount());
        return qcp::algo::optimizedLineData(mKeys, mValues[column], begin, end, pixelWidth,
                                             keyAxis, valueAxis);
    }

    QVector<QPointF> getLines(int column, int begin, int end,
                               QCPAxis* keyAxis, QCPAxis* valueAxis) const override
    {
        Q_ASSERT(column >= 0 && column < columnCount());
        return qcp::algo::linesToPixels(mKeys, mValues[column], begin, end, keyAxis, valueAxis);
    }

private:
    KeyContainer mKeys;
    std::vector<ValueContainer> mValues;
};
```

- [ ] **Step 4: Add to umbrella header `src/qcp.h`**

Add after `abstract-multi-datasource.h`:
```cpp
#include "datasource/soa-multi-datasource.h"
```

- [ ] **Step 5: Register test in build system**

Add to `tests/auto/meson.build` — in `test_srcs`:
```
'test-multi-datasource/test-multi-datasource.cpp',
```
In `test_headers`:
```
'test-multi-datasource/test-multi-datasource.h',
```

Add to `tests/auto/autotest.cpp`:
```cpp
#include "test-multi-datasource/test-multi-datasource.h"
// ... in main():
QCPTEST(TestMultiDataSource);
```

- [ ] **Step 6: Build and run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All tests pass including the new `TestMultiDataSource`.

- [ ] **Step 7: Commit**

```bash
git add src/datasource/soa-multi-datasource.h src/qcp.h \
        tests/auto/test-multi-datasource/ tests/auto/autotest.cpp tests/auto/meson.build
git commit -m "feat: add QCPSoAMultiDataSource with tests"
```

---

## Chunk 2: Base Class Change + QCPMultiGraph Core

### Task 3: Make addToLegend/removeFromLegend Virtual

**Files:**
- Modify: `src/plottables/plottable.h:176-179`

- [ ] **Step 1: Make the two methods virtual**

In `src/plottables/plottable.h`, change lines 176-179 from:
```cpp
    bool addToLegend(QCPLegend* legend);
    bool addToLegend();
    bool removeFromLegend(QCPLegend* legend) const;
    bool removeFromLegend() const;
```
to:
```cpp
    virtual bool addToLegend(QCPLegend* legend);
    bool addToLegend();
    virtual bool removeFromLegend(QCPLegend* legend) const;
    bool removeFromLegend() const;
```

Only the single-argument overloads need `virtual`. The no-argument overloads delegate to them.

- [ ] **Step 2: Build and run existing tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All existing tests pass. This is a source-compatible change.

- [ ] **Step 3: Commit**

```bash
git add src/plottables/plottable.h
git commit -m "refactor: make addToLegend/removeFromLegend virtual"
```

### Task 4: QCPMultiGraph — Skeleton + Data Source Wiring

**Files:**
- Create: `src/plottables/plottable-multigraph.h`
- Create: `src/plottables/plottable-multigraph.cpp`
- Modify: `meson.build` (add .cpp to static_library sources)
- Modify: `meson.build` (add .h to neoqcp_moc_headers)
- Modify: `src/qcp.h`

- [ ] **Step 1: Write test for QCPMultiGraph creation and data source wiring**

Create `tests/auto/test-multigraph/test-multigraph.h`:
```cpp
#pragma once
#include <QtTest/QtTest>

class QCustomPlot;

class TestMultiGraph : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    // Data source wiring
    void creation();
    void setDataSourceShared();
    void setDataSourceUnique();
    void setDataConvenience();
    void viewDataConvenience();
    void componentCountMatchesDataSource();
    void componentValueAt();
    void dataCountDelegate();
    void dataMainKeyDelegate();
    void dataMainValueColumn0();
    void findBeginEndDelegate();
    void keyRangeDelegate();
    void valueRangeUnion();

private:
    QCustomPlot* mPlot = nullptr;
};
```

Create `tests/auto/test-multigraph/test-multigraph.cpp`:
```cpp
#include "test-multigraph.h"
#include "qcustomplot.h"
#include "datasource/soa-multi-datasource.h"
#include <vector>
#include <span>

void TestMultiGraph::init()
{
    mPlot = new QCustomPlot();
    mPlot->resize(400, 300);
}

void TestMultiGraph::cleanup()
{
    delete mPlot;
    mPlot = nullptr;
}

void TestMultiGraph::creation()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    QVERIFY(mg);
    QCOMPARE(mg->componentCount(), 0);
    QCOMPARE(mg->dataCount(), 0);
    QVERIFY(mg->dataSource() == nullptr);
}

void TestMultiGraph::setDataSourceShared()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    auto src = std::make_shared<QCPSoAMultiDataSource<std::vector<double>, std::vector<double>>>(
        std::vector<double>{1.0, 2.0, 3.0},
        std::vector<std::vector<double>>{{10.0, 20.0, 30.0}, {-1.0, -2.0, -3.0}});
    mg->setDataSource(src);
    QCOMPARE(mg->componentCount(), 2);
    QCOMPARE(mg->dataCount(), 3);
}

void TestMultiGraph::setDataSourceUnique()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    auto src = std::make_unique<QCPSoAMultiDataSource<std::vector<double>, std::vector<double>>>(
        std::vector<double>{1.0, 2.0},
        std::vector<std::vector<double>>{{10.0, 20.0}});
    mg->setDataSource(std::move(src));
    QCOMPARE(mg->componentCount(), 1);
    QCOMPARE(mg->dataCount(), 2);
}

void TestMultiGraph::setDataConvenience()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    mg->setData(std::vector<double>{1.0, 2.0, 3.0},
                std::vector<std::vector<double>>{{10.0, 20.0, 30.0}, {-1.0, -2.0, -3.0}});
    QCOMPARE(mg->componentCount(), 2);
    QCOMPARE(mg->dataCount(), 3);
}

void TestMultiGraph::viewDataConvenience()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    std::vector<double> keys = {1.0, 2.0, 3.0};
    std::vector<double> v0 = {10.0, 20.0, 30.0};
    std::vector<double> v1 = {-1.0, -2.0, -3.0};
    mg->viewData(std::span<const double>(keys),
                 std::vector<std::span<const double>>{
                     std::span<const double>(v0),
                     std::span<const double>(v1)});
    QCOMPARE(mg->componentCount(), 2);
    QCOMPARE(mg->dataCount(), 3);
}

void TestMultiGraph::componentCountMatchesDataSource()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    // Set 3-column source
    mg->setData(std::vector<double>{1.0, 2.0},
                std::vector<std::vector<double>>{{1.0, 2.0}, {3.0, 4.0}, {5.0, 6.0}});
    QCOMPARE(mg->componentCount(), 3);

    // Replace with 1-column source — components shrink
    mg->setData(std::vector<double>{1.0, 2.0},
                std::vector<std::vector<double>>{{1.0, 2.0}});
    QCOMPARE(mg->componentCount(), 1);
}

void TestMultiGraph::componentValueAt()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    mg->setData(std::vector<double>{1.0, 2.0, 3.0},
                std::vector<std::vector<double>>{{10.0, 20.0, 30.0}, {-1.0, -2.0, -3.0}});
    QCOMPARE(mg->componentValueAt(0, 1), 20.0);
    QCOMPARE(mg->componentValueAt(1, 2), -3.0);
}

void TestMultiGraph::dataCountDelegate()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    QCOMPARE(mg->dataCount(), 0);
    mg->setData(std::vector<double>{1.0, 2.0, 3.0},
                std::vector<std::vector<double>>{{0.0, 0.0, 0.0}});
    QCOMPARE(mg->dataCount(), 3);
}

void TestMultiGraph::dataMainKeyDelegate()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    mg->setData(std::vector<double>{1.0, 2.0, 3.0},
                std::vector<std::vector<double>>{{0.0, 0.0, 0.0}});
    QCOMPARE(mg->dataMainKey(0), 1.0);
    QCOMPARE(mg->dataMainKey(2), 3.0);
}

void TestMultiGraph::dataMainValueColumn0()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    mg->setData(std::vector<double>{1.0, 2.0},
                std::vector<std::vector<double>>{{10.0, 20.0}, {-1.0, -2.0}});
    // dataMainValue always returns column 0
    QCOMPARE(mg->dataMainValue(0), 10.0);
    QCOMPARE(mg->dataMainValue(1), 20.0);
}

void TestMultiGraph::findBeginEndDelegate()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    mg->setData(std::vector<double>{1.0, 2.0, 3.0, 4.0, 5.0},
                std::vector<std::vector<double>>{{0.0, 0.0, 0.0, 0.0, 0.0}});
    QCOMPARE(mg->findBegin(3.0, false), 2);
    QCOMPARE(mg->findEnd(3.0, true), 4);
}

void TestMultiGraph::keyRangeDelegate()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    mg->setData(std::vector<double>{-2.0, 1.0, 5.0},
                std::vector<std::vector<double>>{{0.0, 0.0, 0.0}});
    bool found = false;
    auto range = mg->getKeyRange(found);
    QVERIFY(found);
    QCOMPARE(range.lower, -2.0);
    QCOMPARE(range.upper, 5.0);
}

void TestMultiGraph::valueRangeUnion()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    mg->setData(std::vector<double>{1.0, 2.0, 3.0},
                std::vector<std::vector<double>>{{10.0, 20.0, 30.0}, {-25.0, -15.0, -5.0}});
    bool found = false;
    auto range = mg->getValueRange(found);
    QVERIFY(found);
    QCOMPARE(range.lower, -25.0);
    QCOMPARE(range.upper, 30.0);
}
```

- [ ] **Step 2: Create QCPMultiGraph header**

```cpp
// src/plottables/plottable-multigraph.h
#pragma once
#include "plottable.h"
#include "plottable1d.h"
#include "datasource/abstract-multi-datasource.h"
#include "datasource/soa-multi-datasource.h"
#include <memory>
#include <span>

struct QCP_LIB_DECL QCPGraphComponent {
    QString name;
    QPen pen;
    QPen selectedPen;
    QCPScatterStyle scatterStyle;
    QCPDataSelection selection;
    bool visible = true;
};

class QCP_LIB_DECL QCPMultiGraph : public QCPAbstractPlottable, public QCPPlottableInterface1D {
    Q_OBJECT
public:
    enum LineStyle { lsNone, lsLine, lsStepLeft, lsStepRight, lsStepCenter, lsImpulse };
    Q_ENUM(LineStyle)

    explicit QCPMultiGraph(QCPAxis* keyAxis, QCPAxis* valueAxis);
    ~QCPMultiGraph() override;

    // Data source
    void setDataSource(std::unique_ptr<QCPAbstractMultiDataSource> source);
    void setDataSource(std::shared_ptr<QCPAbstractMultiDataSource> source);
    QCPAbstractMultiDataSource* dataSource() const { return mDataSource.get(); }
    void dataChanged();

    // Convenience: owning
    template <IndexableNumericRange KC, IndexableNumericRange VC>
    void setData(KC&& keys, std::vector<VC>&& valueColumns)
    {
        using KD = std::decay_t<KC>;
        using VD = std::decay_t<VC>;
        setDataSource(std::make_shared<QCPSoAMultiDataSource<KD, VD>>(
            std::forward<KC>(keys), std::forward<std::vector<VC>>(valueColumns)));
    }

    // Convenience: non-owning spans
    template <typename K, typename V>
    void viewData(std::span<const K> keys, std::vector<std::span<const V>> valueColumns)
    {
        setDataSource(std::make_shared<
            QCPSoAMultiDataSource<std::span<const K>, std::span<const V>>>(
            keys, std::move(valueColumns)));
    }

    // Components
    int componentCount() const { return mComponents.size(); }
    QCPGraphComponent& component(int index) { return mComponents[index]; }
    const QCPGraphComponent& component(int index) const { return mComponents[index]; }
    void setComponentNames(const QStringList& names);
    void setComponentColors(const QList<QColor>& colors);
    void setComponentPens(const QList<QPen>& pens);

    // Per-component value access (for tracers/tooltips)
    double componentValueAt(int column, int index) const;

    // Shared style
    LineStyle lineStyle() const { return mLineStyle; }
    void setLineStyle(LineStyle style) { mLineStyle = style; }
    bool adaptiveSampling() const { return mAdaptiveSampling; }
    void setAdaptiveSampling(bool enabled) { mAdaptiveSampling = enabled; }
    int scatterSkip() const { return mScatterSkip; }
    void setScatterSkip(int skip) { mScatterSkip = qMax(0, skip); }

    // Per-component selection
    QCPDataSelection componentSelection(int index) const;
    void setComponentSelection(int index, const QCPDataSelection& sel);

    // QCPPlottableInterface1D
    int dataCount() const override;
    double dataMainKey(int index) const override;
    double dataSortKey(int index) const override;
    double dataMainValue(int index) const override;
    QCPRange dataValueRange(int index) const override;
    QPointF dataPixelPosition(int index) const override;
    bool sortKeyIsMainKey() const override { return true; }
    QCPDataSelection selectTestRect(const QRectF& rect, bool onlySelectable) const override;
    int findBegin(double sortKey, bool expandedRange) const override;
    int findEnd(double sortKey, bool expandedRange) const override;

    // QCPAbstractPlottable
    QCPPlottableInterface1D* interface1D() override { return this; }
    double selectTest(const QPointF& pos, bool onlySelectable,
                      QVariant* details = nullptr) const override;
    QCPRange getKeyRange(bool& foundRange,
                         QCP::SignDomain inSignDomain = QCP::sdBoth) const override;
    QCPRange getValueRange(bool& foundRange,
                           QCP::SignDomain inSignDomain = QCP::sdBoth,
                           const QCPRange& inKeyRange = QCPRange()) const override;

    // Legend
    bool addToLegend(QCPLegend* legend) override;
    bool removeFromLegend(QCPLegend* legend) const override;

protected:
    void draw(QCPPainter* painter) override;
    void drawLegendIcon(QCPPainter* painter, const QRectF& rect) const override;

    void selectEvent(QMouseEvent* event, bool additive, const QVariant& details,
                     bool* selectionStateChanged) override;
    void deselectEvent(bool* selectionStateChanged) override;

private:
    std::shared_ptr<QCPAbstractMultiDataSource> mDataSource;
    QVector<QCPGraphComponent> mComponents;
    LineStyle mLineStyle = lsLine;
    bool mAdaptiveSampling = true;
    int mScatterSkip = 0;

    void syncComponentCount();
    void updateBaseSelection();

    // Line style transforms (pixel-space in, pixel-space out)
    static QVector<QPointF> toStepLeftLines(const QVector<QPointF>& lines, bool keyIsVertical);
    static QVector<QPointF> toStepRightLines(const QVector<QPointF>& lines, bool keyIsVertical);
    static QVector<QPointF> toStepCenterLines(const QVector<QPointF>& lines, bool keyIsVertical);
    QVector<QPointF> toImpulseLines(const QVector<QPointF>& lines, bool keyIsVertical) const;
};
```

Note: the `soa-multi-datasource.h` include is already in the header code above — required for the `setData`/`viewData` templates.

- [ ] **Step 3: Create QCPMultiGraph .cpp — skeleton with data source wiring, 1D interface, range queries**

```cpp
// src/plottables/plottable-multigraph.cpp
#include "plottable-multigraph.h"
#include "Profiling.hpp"
#include "../axis/axis.h"
#include "../core.h"
#include "../layoutelements/layoutelement-axisrect.h"
#include "../painting/painter.h"
#include "../vector2d.h"

static QPen defaultSelectedPen(const QPen& pen)
{
    QPen sel = pen;
    sel.setWidthF(pen.widthF() + 1.5);
    QColor c = pen.color();
    c.setAlphaF(qMin(1.0, c.alphaF() * 1.3));
    sel.setColor(c);
    return sel;
}

static const QList<QColor> sDefaultColors = {
    QColor(31, 119, 180),  QColor(255, 127, 14), QColor(44, 160, 44),
    QColor(214, 39, 40),   QColor(148, 103, 189), QColor(140, 86, 75),
    QColor(227, 119, 194), QColor(127, 127, 127), QColor(188, 189, 34),
    QColor(23, 190, 207)
};

QCPMultiGraph::QCPMultiGraph(QCPAxis* keyAxis, QCPAxis* valueAxis)
    : QCPAbstractPlottable(keyAxis, valueAxis)
{
}

QCPMultiGraph::~QCPMultiGraph() = default;

void QCPMultiGraph::setDataSource(std::unique_ptr<QCPAbstractMultiDataSource> source)
{
    mDataSource = std::move(source);
    syncComponentCount();
}

void QCPMultiGraph::setDataSource(std::shared_ptr<QCPAbstractMultiDataSource> source)
{
    mDataSource = std::move(source);
    syncComponentCount();
}

void QCPMultiGraph::dataChanged()
{
    if (mParentPlot)
        mParentPlot->replot();
}

void QCPMultiGraph::syncComponentCount()
{
    int newCount = mDataSource ? mDataSource->columnCount() : 0;
    int oldCount = mComponents.size();
    if (newCount > oldCount) {
        mComponents.resize(newCount);
        for (int i = oldCount; i < newCount; ++i) {
            auto& c = mComponents[i];
            QColor color = sDefaultColors[i % sDefaultColors.size()];
            c.pen = QPen(color, 1.0);
            c.selectedPen = defaultSelectedPen(c.pen);
            c.name = QString("Component %1").arg(i);
        }
    } else if (newCount < oldCount) {
        mComponents.resize(newCount);
    }
}

void QCPMultiGraph::updateBaseSelection()
{
    QCPDataSelection combined;
    for (const auto& c : mComponents) {
        for (int i = 0; i < c.selection.dataRangeCount(); ++i)
            combined.addDataRange(c.selection.dataRange(i), false);
    }
    combined.simplify();
    mSelection = combined;
}

// --- Component API ---

void QCPMultiGraph::setComponentNames(const QStringList& names)
{
    int n = qMin(names.size(), mComponents.size());
    for (int i = 0; i < n; ++i)
        mComponents[i].name = names[i];
}

void QCPMultiGraph::setComponentColors(const QList<QColor>& colors)
{
    int n = qMin(colors.size(), mComponents.size());
    for (int i = 0; i < n; ++i) {
        mComponents[i].pen.setColor(colors[i]);
        mComponents[i].selectedPen = defaultSelectedPen(mComponents[i].pen);
    }
}

void QCPMultiGraph::setComponentPens(const QList<QPen>& pens)
{
    int n = qMin(pens.size(), mComponents.size());
    for (int i = 0; i < n; ++i) {
        mComponents[i].pen = pens[i];
        mComponents[i].selectedPen = defaultSelectedPen(pens[i]);
    }
}

double QCPMultiGraph::componentValueAt(int column, int index) const
{
    return mDataSource ? mDataSource->valueAt(column, index) : 0.0;
}

QCPDataSelection QCPMultiGraph::componentSelection(int index) const
{
    return (index >= 0 && index < mComponents.size()) ? mComponents[index].selection : QCPDataSelection();
}

void QCPMultiGraph::setComponentSelection(int index, const QCPDataSelection& sel)
{
    if (index >= 0 && index < mComponents.size()) {
        mComponents[index].selection = sel;
        updateBaseSelection();
    }
}

// --- QCPPlottableInterface1D ---

int QCPMultiGraph::dataCount() const
{
    return mDataSource ? mDataSource->size() : 0;
}

double QCPMultiGraph::dataMainKey(int index) const
{
    return mDataSource ? mDataSource->keyAt(index) : 0.0;
}

double QCPMultiGraph::dataSortKey(int index) const
{
    return mDataSource ? mDataSource->keyAt(index) : 0.0;
}

double QCPMultiGraph::dataMainValue(int index) const
{
    // Always column 0
    return (mDataSource && mDataSource->columnCount() > 0)
        ? mDataSource->valueAt(0, index) : 0.0;
}

QCPRange QCPMultiGraph::dataValueRange(int index) const
{
    if (!mDataSource || mDataSource->columnCount() == 0)
        return QCPRange(0, 0);
    double vmin = std::numeric_limits<double>::max();
    double vmax = std::numeric_limits<double>::lowest();
    for (int c = 0; c < mDataSource->columnCount(); ++c) {
        if (c < mComponents.size() && !mComponents[c].visible) continue;
        double v = mDataSource->valueAt(c, index);
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
    }
    return QCPRange(vmin, vmax);
}

QPointF QCPMultiGraph::dataPixelPosition(int index) const
{
    if (!mDataSource || !mKeyAxis || !mValueAxis || mDataSource->columnCount() == 0)
        return {};
    return coordsToPixels(mDataSource->keyAt(index), mDataSource->valueAt(0, index));
}

int QCPMultiGraph::findBegin(double sortKey, bool expandedRange) const
{
    return mDataSource ? mDataSource->findBegin(sortKey, expandedRange) : 0;
}

int QCPMultiGraph::findEnd(double sortKey, bool expandedRange) const
{
    return mDataSource ? mDataSource->findEnd(sortKey, expandedRange) : 0;
}

// --- Range queries ---

QCPRange QCPMultiGraph::getKeyRange(bool& foundRange, QCP::SignDomain inSignDomain) const
{
    if (!mDataSource || mDataSource->empty()) {
        foundRange = false;
        return {};
    }
    return mDataSource->keyRange(foundRange, inSignDomain);
}

QCPRange QCPMultiGraph::getValueRange(bool& foundRange, QCP::SignDomain inSignDomain,
                                       const QCPRange& inKeyRange) const
{
    foundRange = false;
    if (!mDataSource || mDataSource->empty())
        return {};

    double lower = std::numeric_limits<double>::max();
    double upper = std::numeric_limits<double>::lowest();
    for (int c = 0; c < mComponents.size(); ++c) {
        if (!mComponents[c].visible) continue;
        bool colFound = false;
        auto colRange = mDataSource->valueRange(c, colFound, inSignDomain, inKeyRange);
        if (colFound) {
            if (colRange.lower < lower) lower = colRange.lower;
            if (colRange.upper > upper) upper = colRange.upper;
            foundRange = true;
        }
    }
    return foundRange ? QCPRange(lower, upper) : QCPRange();
}

// --- Selection (stubs for now, full implementation in Task 5) ---

QCPDataSelection QCPMultiGraph::selectTestRect(const QRectF& /*rect*/, bool /*onlySelectable*/) const
{
    return QCPDataSelection();
}

double QCPMultiGraph::selectTest(const QPointF& /*pos*/, bool /*onlySelectable*/, QVariant* /*details*/) const
{
    return -1;
}

void QCPMultiGraph::selectEvent(QMouseEvent* event, bool additive, const QVariant& details,
                                 bool* selectionStateChanged)
{
    Q_UNUSED(event); Q_UNUSED(additive); Q_UNUSED(details); Q_UNUSED(selectionStateChanged);
}

void QCPMultiGraph::deselectEvent(bool* selectionStateChanged)
{
    bool changed = false;
    for (auto& c : mComponents) {
        if (!c.selection.isEmpty()) {
            c.selection = QCPDataSelection();
            changed = true;
        }
    }
    if (changed) {
        updateBaseSelection();
        if (selectionStateChanged)
            *selectionStateChanged = true;
    }
}

// --- Drawing (stub for now, full implementation in Task 6) ---

void QCPMultiGraph::draw(QCPPainter* /*painter*/)
{
}

void QCPMultiGraph::drawLegendIcon(QCPPainter* /*painter*/, const QRectF& /*rect*/) const
{
}

// --- Legend (stubs for now, full implementation in Task 7) ---

bool QCPMultiGraph::addToLegend(QCPLegend* legend)
{
    // For now, use default plottable legend item. Task 7 replaces with QCPGroupLegendItem.
    return QCPAbstractPlottable::addToLegend(legend);
}

bool QCPMultiGraph::removeFromLegend(QCPLegend* legend) const
{
    return QCPAbstractPlottable::removeFromLegend(legend);
}

// --- Line style transforms (identical to QCPGraph2) ---

QVector<QPointF> QCPMultiGraph::toStepLeftLines(const QVector<QPointF>& lines, bool keyIsVertical)
{
    if (lines.size() < 2) return lines;
    QVector<QPointF> result;
    result.resize(lines.size() * 2);
    if (keyIsVertical) {
        double lastValue = lines.first().x();
        for (int i = 0; i < lines.size(); ++i) {
            const double key = lines[i].y();
            result[i * 2 + 0] = QPointF(lastValue, key);
            lastValue = lines[i].x();
            result[i * 2 + 1] = QPointF(lastValue, key);
        }
    } else {
        double lastValue = lines.first().y();
        for (int i = 0; i < lines.size(); ++i) {
            const double key = lines[i].x();
            result[i * 2 + 0] = QPointF(key, lastValue);
            lastValue = lines[i].y();
            result[i * 2 + 1] = QPointF(key, lastValue);
        }
    }
    return result;
}

QVector<QPointF> QCPMultiGraph::toStepRightLines(const QVector<QPointF>& lines, bool keyIsVertical)
{
    if (lines.size() < 2) return lines;
    QVector<QPointF> result;
    result.resize(lines.size() * 2);
    if (keyIsVertical) {
        double lastKey = lines.first().y();
        for (int i = 0; i < lines.size(); ++i) {
            const double value = lines[i].x();
            result[i * 2 + 0] = QPointF(value, lastKey);
            lastKey = lines[i].y();
            result[i * 2 + 1] = QPointF(value, lastKey);
        }
    } else {
        double lastKey = lines.first().x();
        for (int i = 0; i < lines.size(); ++i) {
            const double value = lines[i].y();
            result[i * 2 + 0] = QPointF(lastKey, value);
            lastKey = lines[i].x();
            result[i * 2 + 1] = QPointF(lastKey, value);
        }
    }
    return result;
}

QVector<QPointF> QCPMultiGraph::toStepCenterLines(const QVector<QPointF>& lines, bool keyIsVertical)
{
    if (lines.size() < 2) return lines;
    QVector<QPointF> result;
    result.resize(lines.size() * 2);
    if (keyIsVertical) {
        double lastKey = lines.first().y();
        double lastValue = lines.first().x();
        result[0] = QPointF(lastValue, lastKey);
        for (int i = 1; i < lines.size(); ++i) {
            const double midKey = (lines[i].y() + lastKey) * 0.5;
            result[i * 2 - 1] = QPointF(lastValue, midKey);
            lastValue = lines[i].x();
            lastKey = lines[i].y();
            result[i * 2 + 0] = QPointF(lastValue, midKey);
        }
        result[lines.size() * 2 - 1] = QPointF(lastValue, lastKey);
    } else {
        double lastKey = lines.first().x();
        double lastValue = lines.first().y();
        result[0] = QPointF(lastKey, lastValue);
        for (int i = 1; i < lines.size(); ++i) {
            const double midKey = (lines[i].x() + lastKey) * 0.5;
            result[i * 2 - 1] = QPointF(midKey, lastValue);
            lastValue = lines[i].y();
            lastKey = lines[i].x();
            result[i * 2 + 0] = QPointF(midKey, lastValue);
        }
        result[lines.size() * 2 - 1] = QPointF(lastKey, lastValue);
    }
    return result;
}

QVector<QPointF> QCPMultiGraph::toImpulseLines(const QVector<QPointF>& lines, bool keyIsVertical) const
{
    QVector<QPointF> result;
    result.resize(lines.size() * 2);
    const double zeroPixel = mValueAxis->coordToPixel(0);
    if (keyIsVertical) {
        for (int i = 0; i < lines.size(); ++i) {
            const double key = lines[i].y();
            result[i * 2 + 0] = QPointF(zeroPixel, key);
            result[i * 2 + 1] = QPointF(lines[i].x(), key);
        }
    } else {
        for (int i = 0; i < lines.size(); ++i) {
            const double key = lines[i].x();
            result[i * 2 + 0] = QPointF(key, zeroPixel);
            result[i * 2 + 1] = QPointF(key, lines[i].y());
        }
    }
    return result;
}
```

- [ ] **Step 4: Register in build system**

In `meson.build`, add to `NeoQCP` static_library sources:
```
'src/plottables/plottable-multigraph.cpp',
```

Add to `neoqcp_moc_headers`:
```
'src/plottables/plottable-multigraph.h',
```

Add to `src/qcp.h`:
```cpp
#include "plottables/plottable-multigraph.h"
```

Register test in `tests/auto/meson.build` and `tests/auto/autotest.cpp` (same pattern as Task 2 Step 5).

- [ ] **Step 5: Build and run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/plottables/plottable-multigraph.h src/plottables/plottable-multigraph.cpp \
        src/qcp.h meson.build tests/auto/test-multigraph/ tests/auto/autotest.cpp tests/auto/meson.build
git commit -m "feat: add QCPMultiGraph skeleton with data source wiring"
```

---

## Chunk 3: Selection + Drawing

### Task 5: Per-Component Selection

**Files:**
- Modify: `src/plottables/plottable-multigraph.cpp` (replace stubs)
- Modify: `tests/auto/test-multigraph/test-multigraph.h` (add selection tests)
- Modify: `tests/auto/test-multigraph/test-multigraph.cpp`

- [ ] **Step 1: Add selection test slots to header**

Add to `test-multigraph.h` private slots:
```cpp
    // Selection
    void selectTestFindsClosestComponent();
    void selectTestReturnsComponentInDetails();
    void selectTestRectPerComponent();
    void selectEventSingleComponent();
    void selectEventAdditive();
    void deselectEventClearsAll();
    void componentSelectionUpdatesBase();
```

- [ ] **Step 2: Write selection tests**

Append to `test-multigraph.cpp`:
```cpp
void TestMultiGraph::selectTestFindsClosestComponent()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    mPlot->xAxis->setRange(0, 4);
    mPlot->yAxis->setRange(-10, 50);
    mg->setData(std::vector<double>{1.0, 2.0, 3.0},
                std::vector<std::vector<double>>{{10.0, 20.0, 30.0}, {40.0, 40.0, 40.0}});
    mPlot->replot();

    // Click near component 1 (value=40) at key=2
    QPointF pixPos = mg->coordsToPixels(2.0, 40.0);
    QVariant details;
    double dist = mg->selectTest(pixPos, false, &details);
    QVERIFY(dist >= 0);
    auto map = details.toMap();
    QCOMPARE(map["componentIndex"].toInt(), 1);
}

void TestMultiGraph::selectTestReturnsComponentInDetails()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    mPlot->xAxis->setRange(0, 4);
    mPlot->yAxis->setRange(0, 35);
    mg->setData(std::vector<double>{1.0, 2.0, 3.0},
                std::vector<std::vector<double>>{{10.0, 20.0, 30.0}});
    mPlot->replot();

    QPointF pixPos = mg->coordsToPixels(2.0, 20.0);
    QVariant details;
    mg->selectTest(pixPos, false, &details);
    auto map = details.toMap();
    QVERIFY(map.contains("componentIndex"));
    QVERIFY(map.contains("dataIndex"));
    QCOMPARE(map["componentIndex"].toInt(), 0);
    QCOMPARE(map["dataIndex"].toInt(), 1);
}

void TestMultiGraph::selectTestRectPerComponent()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    mg->setSelectable(QCP::stDataRange);
    mPlot->xAxis->setRange(0, 4);
    mPlot->yAxis->setRange(0, 35);
    mg->setData(std::vector<double>{1.0, 2.0, 3.0},
                std::vector<std::vector<double>>{{10.0, 20.0, 30.0}, {5.0, 5.0, 5.0}});
    mPlot->replot();

    // Select rect that contains component 0 points at key 1-3, value 8-32
    QPointF tl = mg->coordsToPixels(1.0, 32.0);
    QPointF br = mg->coordsToPixels(3.0, 8.0);
    QRectF rect(tl, br);
    auto sel = mg->selectTestRect(rect.normalized(), false);

    // Should find 3 points in component 0, 0 in component 1 (value=5 is outside 8-32)
    QVERIFY(!sel.isEmpty());
    QCOMPARE(mg->componentSelection(0).dataRangeCount(), 1);
    QVERIFY(mg->componentSelection(1).isEmpty());
}

void TestMultiGraph::selectEventSingleComponent()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    mg->setSelectable(QCP::stDataRange);
    mg->setData(std::vector<double>{1.0, 2.0, 3.0},
                std::vector<std::vector<double>>{{10.0, 20.0, 30.0}, {-1.0, -2.0, -3.0}});

    QVariantMap details;
    details["componentIndex"] = 1;
    details["dataIndex"] = 1;
    QVariant detailsVar = details;

    bool changed = false;
    mg->selectEvent(nullptr, false, detailsVar, &changed);
    QVERIFY(changed);
    QVERIFY(!mg->componentSelection(1).isEmpty());
    QVERIFY(mg->componentSelection(0).isEmpty());
}

void TestMultiGraph::selectEventAdditive()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    mg->setSelectable(QCP::stDataRange);
    mg->setData(std::vector<double>{1.0, 2.0, 3.0},
                std::vector<std::vector<double>>{{10.0, 20.0, 30.0}, {-1.0, -2.0, -3.0}});

    // First selection on component 0
    QVariantMap d0;
    d0["componentIndex"] = 0;
    d0["dataIndex"] = 0;
    bool changed = false;
    mg->selectEvent(nullptr, false, QVariant(d0), &changed);

    // Additive selection on component 1
    QVariantMap d1;
    d1["componentIndex"] = 1;
    d1["dataIndex"] = 1;
    mg->selectEvent(nullptr, true, QVariant(d1), &changed);

    QVERIFY(!mg->componentSelection(0).isEmpty());
    QVERIFY(!mg->componentSelection(1).isEmpty());
}

void TestMultiGraph::deselectEventClearsAll()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    mg->setSelectable(QCP::stDataRange);
    mg->setData(std::vector<double>{1.0, 2.0, 3.0},
                std::vector<std::vector<double>>{{10.0, 20.0, 30.0}, {-1.0, -2.0, -3.0}});

    mg->setComponentSelection(0, QCPDataSelection(QCPDataRange(0, 2)));
    mg->setComponentSelection(1, QCPDataSelection(QCPDataRange(1, 3)));
    QVERIFY(mg->selected());

    bool changed = false;
    mg->deselectEvent(&changed);
    QVERIFY(changed);
    QVERIFY(!mg->selected());
    QVERIFY(mg->componentSelection(0).isEmpty());
    QVERIFY(mg->componentSelection(1).isEmpty());
}

void TestMultiGraph::componentSelectionUpdatesBase()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    mg->setData(std::vector<double>{1.0, 2.0, 3.0},
                std::vector<std::vector<double>>{{10.0, 20.0, 30.0}});
    QVERIFY(!mg->selected());
    mg->setComponentSelection(0, QCPDataSelection(QCPDataRange(0, 2)));
    QVERIFY(mg->selected());
}
```

- [ ] **Step 3: Implement selectTest, selectTestRect, selectEvent**

Replace the stubs in `plottable-multigraph.cpp`:

```cpp
double QCPMultiGraph::selectTest(const QPointF& pos, bool onlySelectable, QVariant* details) const
{
    if ((onlySelectable && mSelectable == QCP::stNone) || !mDataSource || mDataSource->empty())
        return -1;
    if (!mKeyAxis || !mValueAxis)
        return -1;

    double posKeyMin, posKeyMax, dummy;
    pixelsToCoords(
        pos - QPointF(mParentPlot->selectionTolerance(), mParentPlot->selectionTolerance()),
        posKeyMin, dummy);
    pixelsToCoords(
        pos + QPointF(mParentPlot->selectionTolerance(), mParentPlot->selectionTolerance()),
        posKeyMax, dummy);
    if (posKeyMin > posKeyMax) qSwap(posKeyMin, posKeyMax);

    int begin = mDataSource->findBegin(posKeyMin, true);
    int end = mDataSource->findEnd(posKeyMax, true);
    if (begin == end) return -1;

    double minDistSqr = (std::numeric_limits<double>::max)();
    int minDistIndex = -1;
    int minDistComponent = -1;
    QCPRange keyRange(mKeyAxis->range());
    QCPRange valRange(mValueAxis->range());

    for (int c = 0; c < mComponents.size(); ++c) {
        if (!mComponents[c].visible) continue;
        for (int i = begin; i < end; ++i) {
            double k = mDataSource->keyAt(i);
            double v = mDataSource->valueAt(c, i);
            if (keyRange.contains(k) && valRange.contains(v)) {
                double distSqr = QCPVector2D(coordsToPixels(k, v) - pos).lengthSquared();
                if (distSqr < minDistSqr) {
                    minDistSqr = distSqr;
                    minDistIndex = i;
                    minDistComponent = c;
                }
            }
        }
    }

    if (details && minDistIndex >= 0) {
        QVariantMap map;
        map["componentIndex"] = minDistComponent;
        map["dataIndex"] = minDistIndex;
        details->setValue(map);
    }
    return minDistIndex >= 0 ? qSqrt(minDistSqr) : -1;
}

QCPDataSelection QCPMultiGraph::selectTestRect(const QRectF& rect, bool onlySelectable) const
{
    QCPDataSelection unionResult;
    if ((onlySelectable && mSelectable == QCP::stNone) || !mDataSource || mDataSource->empty())
        return unionResult;
    if (!mKeyAxis || !mValueAxis)
        return unionResult;

    double key1, value1, key2, value2;
    pixelsToCoords(rect.topLeft(), key1, value1);
    pixelsToCoords(rect.bottomRight(), key2, value2);
    if (key1 > key2) qSwap(key1, key2);
    if (value1 > value2) qSwap(value1, value2);
    QCPRange keyRange(key1, key2);
    QCPRange valueRange(value1, value2);

    int begin = mDataSource->findBegin(keyRange.lower, false);
    int end = mDataSource->findEnd(keyRange.upper, false);

    for (int c = 0; c < mComponents.size(); ++c) {
        if (!mComponents[c].visible) continue;
        QCPDataSelection colSel;
        int segBegin = -1;
        for (int i = begin; i < end; ++i) {
            double k = mDataSource->keyAt(i);
            double v = mDataSource->valueAt(c, i);
            if (segBegin == -1) {
                if (keyRange.contains(k) && valueRange.contains(v))
                    segBegin = i;
            } else if (!keyRange.contains(k) || !valueRange.contains(v)) {
                colSel.addDataRange(QCPDataRange(segBegin, i), false);
                segBegin = -1;
            }
        }
        if (segBegin != -1)
            colSel.addDataRange(QCPDataRange(segBegin, end), false);
        colSel.simplify();
        // Store on component (const_cast because selectTestRect is const but updates component state)
        const_cast<QCPGraphComponent&>(mComponents[c]).selection = colSel;
        for (int r = 0; r < colSel.dataRangeCount(); ++r)
            unionResult.addDataRange(colSel.dataRange(r), false);
    }
    unionResult.simplify();
    const_cast<QCPMultiGraph*>(this)->updateBaseSelection();
    return unionResult;
}

void QCPMultiGraph::selectEvent(QMouseEvent* event, bool additive, const QVariant& details,
                                 bool* selectionStateChanged)
{
    Q_UNUSED(event);
    auto map = details.toMap();
    int compIdx = map.value("componentIndex", -1).toInt();
    int dataIdx = map.value("dataIndex", -1).toInt();
    if (compIdx < 0 || compIdx >= mComponents.size() || dataIdx < 0)
        return;

    if (!additive) {
        for (auto& c : mComponents)
            c.selection = QCPDataSelection();
    }
    mComponents[compIdx].selection.addDataRange(QCPDataRange(dataIdx, dataIdx + 1), false);
    mComponents[compIdx].selection.simplify();
    updateBaseSelection();
    if (selectionStateChanged)
        *selectionStateChanged = true;
}
```

- [ ] **Step 4: Build and run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/plottables/plottable-multigraph.cpp tests/auto/test-multigraph/
git commit -m "feat: implement per-component selection for QCPMultiGraph"
```

### Task 6: Drawing

**Files:**
- Modify: `src/plottables/plottable-multigraph.cpp` (replace draw stub)
- Modify: `tests/auto/test-multigraph/test-multigraph.h`
- Modify: `tests/auto/test-multigraph/test-multigraph.cpp`

- [ ] **Step 1: Add render test slots**

Add to `test-multigraph.h` private slots:
```cpp
    // Rendering
    void renderBasicDoesNotCrash();
    void renderWithSelection();
    void renderHiddenComponent();
    void renderEmptySource();
    void renderAllLineStyles();
```

- [ ] **Step 2: Write render tests**

```cpp
void TestMultiGraph::renderBasicDoesNotCrash()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    mg->setData(std::vector<double>{1.0, 2.0, 3.0},
                std::vector<std::vector<double>>{{10.0, 20.0, 30.0}, {-1.0, -2.0, -3.0}});
    mPlot->xAxis->setRange(0, 4);
    mPlot->yAxis->setRange(-5, 35);
    mPlot->replot(); // Should not crash
}

void TestMultiGraph::renderWithSelection()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    mg->setData(std::vector<double>{1.0, 2.0, 3.0},
                std::vector<std::vector<double>>{{10.0, 20.0, 30.0}});
    mg->setComponentSelection(0, QCPDataSelection(QCPDataRange(0, 2)));
    mPlot->xAxis->setRange(0, 4);
    mPlot->yAxis->setRange(0, 35);
    mPlot->replot(); // Should not crash
}

void TestMultiGraph::renderHiddenComponent()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    mg->setData(std::vector<double>{1.0, 2.0, 3.0},
                std::vector<std::vector<double>>{{10.0, 20.0, 30.0}, {-1.0, -2.0, -3.0}});
    mg->component(1).visible = false;
    mPlot->xAxis->setRange(0, 4);
    mPlot->yAxis->setRange(-5, 35);
    mPlot->replot(); // Should not crash, should only draw component 0
}

void TestMultiGraph::renderEmptySource()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    mPlot->replot(); // No data source — should not crash
}

void TestMultiGraph::renderAllLineStyles()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    mg->setData(std::vector<double>{1.0, 2.0, 3.0, 4.0, 5.0},
                std::vector<std::vector<double>>{{10.0, 20.0, 15.0, 25.0, 30.0}});
    mPlot->xAxis->setRange(0, 6);
    mPlot->yAxis->setRange(0, 35);

    for (auto style : {QCPMultiGraph::lsNone, QCPMultiGraph::lsLine,
                       QCPMultiGraph::lsStepLeft, QCPMultiGraph::lsStepRight,
                       QCPMultiGraph::lsStepCenter, QCPMultiGraph::lsImpulse}) {
        mg->setLineStyle(style);
        mPlot->replot(); // Should not crash
    }
}
```

- [ ] **Step 3: Implement draw()**

Replace the draw stub in `plottable-multigraph.cpp`:

```cpp
void QCPMultiGraph::draw(QCPPainter* painter)
{
    PROFILE_HERE_N("QCPMultiGraph::draw");
    if (!mKeyAxis || !mValueAxis || !mDataSource || mDataSource->empty())
        return;
    if (mKeyAxis->range().size() <= 0)
        return;

    int begin = mDataSource->findBegin(mKeyAxis->range().lower);
    int end = mDataSource->findEnd(mKeyAxis->range().upper);
    if (begin >= end)
        return;

    const bool keyIsVertical = mKeyAxis->orientation() == Qt::Vertical;
    const int pixelWidth = static_cast<int>(mKeyAxis->axisRect()->width());

    for (int c = 0; c < mComponents.size(); ++c) {
        const auto& comp = mComponents[c];
        if (!comp.visible) continue;
        if (mLineStyle == lsNone && comp.scatterStyle.isNone()) continue;

        QVector<QPointF> lines;
        if (mAdaptiveSampling)
            lines = mDataSource->getOptimizedLineData(c, begin, end, pixelWidth,
                                                       mKeyAxis.data(), mValueAxis.data());
        else
            lines = mDataSource->getLines(c, begin, end, mKeyAxis.data(), mValueAxis.data());

        if (lines.isEmpty()) continue;

        // Apply line style transform
        if (mLineStyle != lsNone && mLineStyle != lsLine) {
            switch (mLineStyle) {
                case lsStepLeft:   lines = toStepLeftLines(lines, keyIsVertical); break;
                case lsStepRight:  lines = toStepRightLines(lines, keyIsVertical); break;
                case lsStepCenter: lines = toStepCenterLines(lines, keyIsVertical); break;
                case lsImpulse:    lines = toImpulseLines(lines, keyIsVertical); break;
                default: break;
            }
        }

        // Draw lines
        if (mLineStyle != lsNone) {
            applyDefaultAntialiasingHint(painter);
            if (mLineStyle == lsImpulse) {
                QPen impulsePen = comp.pen;
                impulsePen.setCapStyle(Qt::FlatCap);
                // TODO: handle per-component selection for impulse lines
                painter->setPen(comp.selection.isEmpty() ? impulsePen : comp.selectedPen);
                painter->drawLines(lines);
            } else {
                // Simple path: draw all as normal or selected
                // TODO: split into selected/unselected segments for partial selection
                painter->setPen(comp.selection.isEmpty() ? comp.pen : comp.selectedPen);
                painter->setBrush(Qt::NoBrush);
                painter->drawPolyline(lines.constData(), lines.size());
            }
        }

        // Draw scatters
        if (!comp.scatterStyle.isNone()) {
            applyScattersAntialiasingHint(painter);
            comp.scatterStyle.applyTo(painter, comp.pen);
            const int skip = mScatterSkip + 1;
            for (int i = 0; i < lines.size(); i += skip)
                comp.scatterStyle.drawShape(painter, lines[i].x(), lines[i].y());
        }
    }
}

void QCPMultiGraph::drawLegendIcon(QCPPainter* painter, const QRectF& rect) const
{
    applyDefaultAntialiasingHint(painter);
    if (mComponents.isEmpty()) return;

    // Draw stacked horizontal line segments, one per component
    int n = mComponents.size();
    double segWidth = rect.width() / n;
    double y = rect.center().y();
    for (int i = 0; i < n; ++i) {
        if (!mComponents[i].visible) continue;
        painter->setPen(mComponents[i].pen);
        double x0 = rect.left() + i * segWidth;
        double x1 = x0 + segWidth;
        painter->drawLine(QLineF(x0, y, x1, y));
    }
}
```

- [ ] **Step 4: Build and run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/plottables/plottable-multigraph.cpp tests/auto/test-multigraph/
git commit -m "feat: implement QCPMultiGraph rendering"
```

---

## Chunk 4: Grouped Legend + Manual Test

### Task 7: QCPGroupLegendItem

**Files:**
- Create: `src/layoutelements/layoutelement-legend-group.h`
- Create: `src/layoutelements/layoutelement-legend-group.cpp`
- Modify: `src/plottables/plottable-multigraph.cpp` (replace legend stubs)
- Modify: `meson.build`
- Modify: `src/qcp.h`
- Modify: `tests/auto/test-multigraph/test-multigraph.h`
- Modify: `tests/auto/test-multigraph/test-multigraph.cpp`

- [ ] **Step 1: Add legend test slots**

Add to `test-multigraph.h`:
```cpp
    // Legend
    void addToLegendCreatesGroupItem();
    void removeFromLegendWorks();
    void legendExpandCollapse();
    void legendGroupSelectsAll();
```

- [ ] **Step 2: Write legend tests**

```cpp
void TestMultiGraph::addToLegendCreatesGroupItem()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    mg->setData(std::vector<double>{1.0, 2.0},
                std::vector<std::vector<double>>{{10.0, 20.0}, {-1.0, -2.0}});
    mg->setName("B field");
    mg->addToLegend();
    QCOMPARE(mPlot->legend->itemCount(), 1);
    auto* item = qobject_cast<QCPGroupLegendItem*>(mPlot->legend->item(0));
    QVERIFY(item != nullptr);
    QCOMPARE(item->multiGraph(), mg);
}

void TestMultiGraph::removeFromLegendWorks()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    mg->setData(std::vector<double>{1.0, 2.0},
                std::vector<std::vector<double>>{{10.0, 20.0}});
    mg->addToLegend();
    QCOMPARE(mPlot->legend->itemCount(), 1);
    mg->removeFromLegend();
    QCOMPARE(mPlot->legend->itemCount(), 0);
}

void TestMultiGraph::legendExpandCollapse()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    mg->setData(std::vector<double>{1.0, 2.0},
                std::vector<std::vector<double>>{{10.0, 20.0}, {-1.0, -2.0}});
    mg->addToLegend();
    auto* item = qobject_cast<QCPGroupLegendItem*>(mPlot->legend->item(0));
    QVERIFY(item);
    QVERIFY(!item->expanded());
    item->setExpanded(true);
    QVERIFY(item->expanded());
    // Expanded size should be larger
    QSize collapsed = item->minimumOuterSizeHint();
    item->setExpanded(false);
    QSize expandedCheck = item->minimumOuterSizeHint();
    item->setExpanded(true);
    QSize expanded = item->minimumOuterSizeHint();
    QVERIFY(expanded.height() > expandedCheck.height());
}

void TestMultiGraph::legendGroupSelectsAll()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    mg->setSelectable(QCP::stDataRange);
    mg->setData(std::vector<double>{1.0, 2.0, 3.0},
                std::vector<std::vector<double>>{{10.0, 20.0, 30.0}, {-1.0, -2.0, -3.0}});
    mPlot->replot();
    // Verify the group legend item was created via addToLegend
    mg->addToLegend();
    auto* item = qobject_cast<QCPGroupLegendItem*>(mPlot->legend->item(0));
    QVERIFY(item);
}
```

- [ ] **Step 3: Create QCPGroupLegendItem header**

```cpp
// src/layoutelements/layoutelement-legend-group.h
#pragma once
#include "layoutelement-legend.h"

class QCPMultiGraph;

class QCP_LIB_DECL QCPGroupLegendItem : public QCPAbstractLegendItem
{
    Q_OBJECT
public:
    QCPGroupLegendItem(QCPLegend* parent, QCPMultiGraph* multiGraph);

    QCPMultiGraph* multiGraph() const { return mMultiGraph; }
    bool expanded() const { return mExpanded; }
    void setExpanded(bool expanded);

protected:
    void draw(QCPPainter* painter) override;
    QSize minimumOuterSizeHint() const override;
    void selectEvent(QMouseEvent* event, bool additive, const QVariant& details,
                     bool* selectionStateChanged) override;
    void deselectEvent(bool* selectionStateChanged) override;

private:
    QCPMultiGraph* mMultiGraph;
    bool mExpanded = false;

    int hitComponent(const QPointF& pos) const;
    QRectF groupRowRect() const;
    QRectF componentRowRect(int index) const;
};
```

- [ ] **Step 4: Create QCPGroupLegendItem implementation**

Create `src/layoutelements/layoutelement-legend-group.cpp` with:
- Constructor setting parent/multiGraph
- `draw()` that renders collapsed (stacked color segments + name) or expanded (header + indented component rows)
- `minimumOuterSizeHint()` returning appropriate size based on expanded state
- `selectEvent()` using `hitComponent()` to determine group vs component click
- `hitComponent()` mapping click position to -1 (group) or component index

This follows the same QPainter drawing patterns as `QCPPlottableLegendItem::draw()` in `layoutelement-legend.cpp`.

**Important:** `layoutelement-legend-group.cpp` must include `../plottables/plottable-multigraph.h` to access `QCPMultiGraph` member functions (`componentCount()`, `component()`, `name()`).

- [ ] **Step 5: Update QCPMultiGraph legend methods**

Replace stubs in `plottable-multigraph.cpp`:

```cpp
bool QCPMultiGraph::addToLegend(QCPLegend* legend)
{
    if (!legend) {
        if (mParentPlot)
            legend = mParentPlot->legend;
        else
            return false;
    }
    if (legend->parentPlot() != mParentPlot)
        return false;
    auto* item = new QCPGroupLegendItem(legend, this);
    return legend->addItem(item);
}

bool QCPMultiGraph::removeFromLegend(QCPLegend* legend) const
{
    if (!legend) {
        if (mParentPlot)
            legend = mParentPlot->legend;
        else
            return false;
    }
    for (int i = 0; i < legend->itemCount(); ++i) {
        if (auto* groupItem = qobject_cast<QCPGroupLegendItem*>(legend->item(i))) {
            if (groupItem->multiGraph() == this)
                return legend->removeItem(i);
        }
    }
    return false;
}
```

- [ ] **Step 6: Register in build system**

Add to `meson.build`:
- `'src/layoutelements/layoutelement-legend-group.cpp'` in static_library sources
- `'src/layoutelements/layoutelement-legend-group.h'` in `neoqcp_moc_headers`

Add to `src/qcp.h`:
```cpp
#include "layoutelements/layoutelement-legend-group.h"
```

Add `#include "layoutelements/layoutelement-legend-group.h"` to `plottable-multigraph.cpp`.

- [ ] **Step 7: Build and run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/layoutelements/layoutelement-legend-group.h \
        src/layoutelements/layoutelement-legend-group.cpp \
        src/plottables/plottable-multigraph.cpp \
        src/qcp.h meson.build \
        tests/auto/test-multigraph/
git commit -m "feat: add QCPGroupLegendItem for multi-graph legend grouping"
```

### Task 8: Manual Test — Side-by-Side Comparison

**Files:**
- Modify: `tests/manual/mainwindow.h`
- Modify: `tests/manual/mainwindow.cpp`

- [ ] **Step 1: Add test method declaration**

Add to `mainwindow.h` in the test methods section:
```cpp
void setupMultiGraphComparisonTest(QCustomPlot *customPlot);
```

- [ ] **Step 2: Implement the comparison test**

Add to `mainwindow.cpp`:
```cpp
void MainWindow::setupMultiGraphComparisonTest(QCustomPlot *customPlot)
{
    const int numComponents = 10;
    const int numPoints = 100000;

    // Generate shared test data
    std::vector<double> keys(numPoints);
    std::vector<std::vector<double>> allValues(numComponents, std::vector<double>(numPoints));
    for (int i = 0; i < numPoints; ++i) {
        keys[i] = i * 0.001; // 0 to 100
        for (int c = 0; c < numComponents; ++c) {
            double phase = c * 0.7;
            double amplitude = 1.0 + c * 0.3;
            allValues[c][i] = amplitude * std::sin(keys[i] * (1.0 + c * 0.2) + phase);
        }
    }

    QList<QColor> colors = {
        QColor(31, 119, 180),  QColor(255, 127, 14), QColor(44, 160, 44),
        QColor(214, 39, 40),   QColor(148, 103, 189), QColor(140, 86, 75),
        QColor(227, 119, 194), QColor(127, 127, 127), QColor(188, 189, 34),
        QColor(23, 190, 207)
    };

    // --- Left plot: 10 separate QCPGraph2 ---
    customPlot->plotLayout()->clear();
    auto* leftRect = new QCPAxisRect(customPlot);
    auto* rightRect = new QCPAxisRect(customPlot);
    customPlot->plotLayout()->addElement(0, 0, leftRect);
    customPlot->plotLayout()->addElement(0, 1, rightRect);

    for (int c = 0; c < numComponents; ++c) {
        auto* graph = new QCPGraph2(leftRect->axis(QCPAxis::atBottom),
                                     leftRect->axis(QCPAxis::atLeft));
        graph->setData(std::vector<double>(keys), std::vector<double>(allValues[c]));
        graph->setPen(QPen(colors[c], 1.0));
        graph->setName(QString("G2 comp %1").arg(c));
    }
    leftRect->axis(QCPAxis::atBottom)->setRange(0, 100);
    leftRect->axis(QCPAxis::atLeft)->setRange(-5, 5);

    // --- Right plot: 1 QCPMultiGraph ---
    auto* mg = new QCPMultiGraph(rightRect->axis(QCPAxis::atBottom),
                                  rightRect->axis(QCPAxis::atLeft));
    mg->setData(std::vector<double>(keys), std::vector<std::vector<double>>(allValues));
    mg->setComponentColors(colors);
    QStringList names;
    for (int c = 0; c < numComponents; ++c)
        names << QString("MG comp %1").arg(c);
    mg->setComponentNames(names);
    mg->setName("Multi-component");
    mg->addToLegend();

    rightRect->axis(QCPAxis::atBottom)->setRange(0, 100);
    rightRect->axis(QCPAxis::atLeft)->setRange(-5, 5);

    customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);
    customPlot->replot();
}
```

- [ ] **Step 3: Wire up test in MainWindow constructor**

In the constructor of `MainWindow` (in `mainwindow.cpp`), add a call:
```cpp
setupMultiGraphComparisonTest(mCustomPlot);
```

(Comment out the existing `setup*` call so this one runs instead.)

- [ ] **Step 4: Build and run manually**

Run: `meson compile -C build && ./build/tests/manual/manual`
Expected: Window shows two plots side by side. Left has 10 separate graphs, right has 1 multi-graph with 10 components. Both should look identical. Pan/zoom should work.

- [ ] **Step 5: Commit**

```bash
git add tests/manual/mainwindow.h tests/manual/mainwindow.cpp
git commit -m "test: add QCPMultiGraph vs QCPGraph2 side-by-side manual test"
```

---

## Summary

| Task | Description | Dependencies |
|------|------------|-------------|
| 1 | Abstract multi data source interface | None |
| 2 | Template SoA multi data source + tests | Task 1 |
| 3 | Make addToLegend/removeFromLegend virtual | None |
| 4 | QCPMultiGraph skeleton + data wiring + tests | Tasks 1, 2, 3 |
| 5 | Per-component selection | Task 4 |
| 6 | Drawing | Task 4 |
| 7 | QCPGroupLegendItem | Task 4 |
| 8 | Manual side-by-side test | Tasks 5, 6, 7 |

Tasks 1+3 are independent and can be done in parallel. Tasks 5, 6, 7 are independent of each other (all depend on Task 4) and can be done in parallel.

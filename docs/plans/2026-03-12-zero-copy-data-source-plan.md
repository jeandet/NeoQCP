# Zero-Copy Data Source Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a zero-copy, type-generic data source layer and a new `QCPGraph2` plottable that can plot directly from user-owned containers without data conversion.

**Architecture:** Three layers — `QCPAbstractDataSource` (virtual interface), `QCPSoADataSource<KC,VC>` (template holding/viewing typed containers), and `qcp::algo` free function templates (native-type algorithms). `QCPGraph2` is a QObject plottable that delegates to the data source. See `docs/specs/2026-03-12-zero-copy-data-source-design.md` for full spec.

**Tech Stack:** C++20 (concepts, `std::span`, `if constexpr`), Qt6, Meson, Qt Test

---

## Chunk 1: Concepts, Abstract Data Source, and Algorithm Core

### Task 1: Concepts and Abstract Data Source

**Files:**
- Create: `src/datasource/abstract-datasource.h`

- [ ] **Step 1: Create `src/datasource/abstract-datasource.h`**

```cpp
#pragma once
#include "global.h"
#include "axis/range.h"
#include <QPointF>
#include <QVector>
#include <concepts>
#include <ranges>
#include <span>

class QCPAxis;

// Concepts for data source container requirements
template <typename C>
concept IndexableNumericRange = std::ranges::random_access_range<C>
    && std::is_arithmetic_v<std::ranges::range_value_t<C>>;

template <typename C>
concept ContiguousNumericRange = IndexableNumericRange<C>
    && std::ranges::contiguous_range<C>;

// Non-templated abstract base class for all data sources.
// QCPGraph2 holds a pointer to this; virtual dispatch happens once per render.
class QCPAbstractDataSource {
public:
    virtual ~QCPAbstractDataSource() = default;

    virtual int size() const = 0;
    virtual bool empty() const { return size() == 0; }

    // Range queries (for axis auto-scaling)
    virtual QCPRange keyRange(bool& foundRange,
                              QCP::SignDomain sd = QCP::sdBoth) const = 0;
    virtual QCPRange valueRange(bool& foundRange,
                                QCP::SignDomain sd = QCP::sdBoth,
                                const QCPRange& inKeyRange = QCPRange()) const = 0;

    // Binary search on sorted keys.
    // expandedRange=true includes one extra point beyond the boundary
    // (needed for correct line rendering at viewport edges).
    virtual int findBegin(double sortKey, bool expandedRange = true) const = 0;
    virtual int findEnd(double sortKey, bool expandedRange = true) const = 0;

    // Per-element access (slow path: selection, tooltips)
    virtual double keyAt(int i) const = 0;
    virtual double valueAt(int i) const = 0;

    // Processed outputs — implementations run native-type algorithms internally,
    // cast to double/QPointF only at the pixel-coordinate output step.
    virtual QVector<QPointF> getOptimizedLineData(
        int begin, int end, int pixelWidth,
        QCPAxis* keyAxis, QCPAxis* valueAxis) const = 0;

    virtual QVector<QPointF> getLines(
        int begin, int end,
        QCPAxis* keyAxis, QCPAxis* valueAxis) const = 0;
};
```

- [ ] **Step 2: Verify it compiles**

Run: `meson compile -C build`
Expected: PASS (header-only, no new sources yet — will be included by later files)

- [ ] **Step 3: Commit**

```bash
git add src/datasource/abstract-datasource.h
git commit -m "feat: add QCPAbstractDataSource interface and container concepts"
```

### Task 2: Algorithm Free Functions

**Files:**
- Create: `src/datasource/algorithms.h`

The algorithms are extracted from `QCPGraph::getOptimizedLineData` (plottable-graph.cpp:1122-1240), `QCPGraph::dataToLines` (plottable-graph.cpp:707-738), and `QCPDataContainer::findBegin/findEnd` (datacontainer.h). They are templatized to work on any `IndexableNumericRange` container.

- [ ] **Step 1: Write failing test for `qcp::algo::findBegin` / `qcp::algo::findEnd`**

Create: `tests/auto/test-datasource/test-datasource.h`

```cpp
#pragma once
#include <QtTest/QtTest>

class QCustomPlot;

class TestDataSource : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    // Algorithm tests
    void algoFindBegin();
    void algoFindEnd();
    void algoKeyRange();
    void algoValueRange();
    void algoLinesToPixels();
    void algoOptimizedLineData();

    // SoA data source tests
    void soaOwningVector();
    void soaViewSpan();
    void soaFindBeginEnd();
    void soaRangeQueries();
    void soaIntValues();

    // QCPGraph2 integration tests
    void graph2Creation();
    void graph2SetDataOwning();
    void graph2ViewData();
    void graph2SharedSource();
    void graph2AxisRanges();
    void graph2Render();

private:
    QCustomPlot* mPlot = nullptr;
};
```

This is the **complete** test header — all test slots are declared upfront. Subsequent tasks add implementations to the .cpp file only.

Create: `tests/auto/test-datasource/test-datasource.cpp`

```cpp
#include "test-datasource.h"
#include "datasource/algorithms.h"
#include <vector>

void TestDataSource::init() {}

void TestDataSource::cleanup()
{
    delete mPlot;
    mPlot = nullptr;
}

void TestDataSource::algoFindBegin()
{
    std::vector<double> keys = {1.0, 2.0, 3.0, 4.0, 5.0};

    // Exact match, no expansion
    QCOMPARE(qcp::algo::findBegin(keys, 3.0, false), 2);
    // Before all data
    QCOMPARE(qcp::algo::findBegin(keys, 0.0, false), 0);
    // After all data
    QCOMPARE(qcp::algo::findBegin(keys, 6.0, false), 5);
    // With expansion: one point before
    QCOMPARE(qcp::algo::findBegin(keys, 3.0, true), 1);
    // Expansion at boundary clamps to 0
    QCOMPARE(qcp::algo::findBegin(keys, 1.0, true), 0);

    // Float keys
    std::vector<float> fkeys = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    QCOMPARE(qcp::algo::findBegin(fkeys, 3.0, false), 2);

    // Int keys
    std::vector<int> ikeys = {10, 20, 30, 40, 50};
    QCOMPARE(qcp::algo::findBegin(ikeys, 25.0, false), 2);
}

void TestDataSource::algoFindEnd()
{
    std::vector<double> keys = {1.0, 2.0, 3.0, 4.0, 5.0};

    // Exact match, no expansion: past-the-end of elements <= sortKey
    QCOMPARE(qcp::algo::findEnd(keys, 3.0, false), 3);
    // Before all data
    QCOMPARE(qcp::algo::findEnd(keys, 0.0, false), 0);
    // After all data
    QCOMPARE(qcp::algo::findEnd(keys, 6.0, false), 5);
    // With expansion: one point after
    QCOMPARE(qcp::algo::findEnd(keys, 3.0, true), 4);
    // Expansion at boundary clamps to size
    QCOMPARE(qcp::algo::findEnd(keys, 5.0, true), 5);
}

void TestDataSource::algoKeyRange()
{
    std::vector<double> keys = {-2.0, 1.0, 3.0, 5.0};
    bool found = false;

    auto range = qcp::algo::keyRange(keys, found);
    QVERIFY(found);
    QCOMPARE(range.lower, -2.0);
    QCOMPARE(range.upper, 5.0);

    // Positive only
    range = qcp::algo::keyRange(keys, found, QCP::sdPositive);
    QVERIFY(found);
    QCOMPARE(range.lower, 1.0);
    QCOMPARE(range.upper, 5.0);

    // Negative only
    range = qcp::algo::keyRange(keys, found, QCP::sdNegative);
    QVERIFY(found);
    QCOMPARE(range.lower, -2.0);
    QCOMPARE(range.upper, -2.0);

    // Empty
    std::vector<double> empty;
    range = qcp::algo::keyRange(empty, found);
    QVERIFY(!found);

    // Float
    std::vector<float> fkeys = {1.0f, 5.0f};
    range = qcp::algo::keyRange(fkeys, found);
    QVERIFY(found);
    QCOMPARE(range.lower, 1.0);
    QCOMPARE(range.upper, 5.0);
}

void TestDataSource::algoValueRange()
{
    std::vector<double> keys = {1.0, 2.0, 3.0, 4.0};
    std::vector<double> values = {-1.0, 5.0, 2.0, 8.0};
    bool found = false;

    auto range = qcp::algo::valueRange(keys, values, found);
    QVERIFY(found);
    QCOMPARE(range.lower, -1.0);
    QCOMPARE(range.upper, 8.0);

    // With key range restriction
    range = qcp::algo::valueRange(keys, values, found, QCP::sdBoth, QCPRange(2.0, 3.0));
    QVERIFY(found);
    QCOMPARE(range.lower, 2.0);
    QCOMPARE(range.upper, 5.0);

    // Positive only
    range = qcp::algo::valueRange(keys, values, found, QCP::sdPositive);
    QVERIFY(found);
    QCOMPARE(range.lower, 2.0);
    QCOMPARE(range.upper, 8.0);
}

void TestDataSource::algoLinesToPixels()
{
    // Tested in integration with QCPGraph2 (needs axis objects)
}

void TestDataSource::algoOptimizedLineData()
{
    // Tested in integration with QCPGraph2 (needs axis objects)
}
```

- [ ] **Step 2: Register test in build system**

Modify: `tests/auto/meson.build` — add test-datasource to sources and headers lists.

Modify: `tests/auto/autotest.cpp` — add `#include "test-datasource/test-datasource.h"` and `QCPTEST(TestDataSource)`.

- [ ] **Step 3: Run test to verify it fails**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: FAIL — `algorithms.h` does not exist yet

- [ ] **Step 4: Implement `src/datasource/algorithms.h`**

Header-only. Key implementation notes:
- `findBegin`: use `std::lower_bound` on the container via `std::ranges::begin/end`, comparing element values to `sortKey`. If `expandedRange`, decrement result by 1 (clamped to 0).
- `findEnd`: use `std::upper_bound`. If `expandedRange`, increment result by 1 (clamped to size).
- `keyRange`: iterate container, track min/max respecting `SignDomain`.
- `valueRange`: iterate both containers in parallel, filter by `inKeyRange` if non-empty, track min/max of values respecting `SignDomain`.
- `linesToPixels`: iterate [begin,end), call `keyAxis->coordToPixel(static_cast<double>(keys[i]))` and same for values, handle vertical vs horizontal key axis orientation.
- `optimizedLineData`: extract the adaptive sampling algorithm from `QCPGraph::getOptimizedLineData` (plottable-graph.cpp:1122-1240), replacing `it->key`/`it->value` with `keys[i]`/`values[i]` and `QCPGraphData` outputs with direct `QPointF` via `linesToPixels` logic. Use `if constexpr (std::is_integral_v<K>)` to skip NaN checks for integer types.

```cpp
#pragma once
#include "abstract-datasource.h"
#include "axis/axis.h"
#include <algorithm>
#include <cmath>

namespace qcp::algo {

template <IndexableNumericRange KC>
int findBegin(const KC& keys, double sortKey, bool expandedRange = true)
{
    const int sz = static_cast<int>(std::ranges::size(keys));
    if (sz == 0) return 0;

    auto it = std::lower_bound(std::ranges::begin(keys), std::ranges::end(keys),
                                sortKey, [](const auto& elem, double sk) {
                                    return static_cast<double>(elem) < sk;
                                });
    int idx = static_cast<int>(it - std::ranges::begin(keys));
    if (expandedRange && idx > 0)
        --idx;
    return idx;
}

template <IndexableNumericRange KC>
int findEnd(const KC& keys, double sortKey, bool expandedRange = true)
{
    const int sz = static_cast<int>(std::ranges::size(keys));
    if (sz == 0) return 0;

    auto it = std::upper_bound(std::ranges::begin(keys), std::ranges::end(keys),
                                sortKey, [](double sk, const auto& elem) {
                                    return sk < static_cast<double>(elem);
                                });
    int idx = static_cast<int>(it - std::ranges::begin(keys));
    if (expandedRange && idx < sz)
        ++idx;
    return idx;
}

template <IndexableNumericRange KC>
QCPRange keyRange(const KC& keys, bool& foundRange, QCP::SignDomain sd = QCP::sdBoth)
{
    foundRange = false;
    const int sz = static_cast<int>(std::ranges::size(keys));
    if (sz == 0) return {};

    double lower = std::numeric_limits<double>::max();
    double upper = std::numeric_limits<double>::lowest();
    for (int i = 0; i < sz; ++i)
    {
        double k = static_cast<double>(keys[i]);
        if (sd == QCP::sdPositive && k <= 0) continue;
        if (sd == QCP::sdNegative && k >= 0) continue;
        if (k < lower) lower = k;
        if (k > upper) upper = k;
        foundRange = true;
    }
    return foundRange ? QCPRange(lower, upper) : QCPRange();
}

template <IndexableNumericRange KC, IndexableNumericRange VC>
QCPRange valueRange(const KC& keys, const VC& values, bool& foundRange,
                    QCP::SignDomain sd = QCP::sdBoth,
                    const QCPRange& inKeyRange = QCPRange())
{
    foundRange = false;
    const int sz = static_cast<int>(std::ranges::size(values));
    if (sz == 0) return {};

    const bool hasKeyRestriction = inKeyRange.lower != inKeyRange.upper
                                    || inKeyRange.lower != 0.0;

    double lower = std::numeric_limits<double>::max();
    double upper = std::numeric_limits<double>::lowest();
    for (int i = 0; i < sz; ++i)
    {
        if (hasKeyRestriction)
        {
            double k = static_cast<double>(keys[i]);
            if (k < inKeyRange.lower || k > inKeyRange.upper)
                continue;
        }
        double v = static_cast<double>(values[i]);
        if (sd == QCP::sdPositive && v <= 0) continue;
        if (sd == QCP::sdNegative && v >= 0) continue;
        if (v < lower) lower = v;
        if (v > upper) upper = v;
        foundRange = true;
    }
    return foundRange ? QCPRange(lower, upper) : QCPRange();
}

template <IndexableNumericRange KC, IndexableNumericRange VC>
QVector<QPointF> linesToPixels(const KC& keys, const VC& values,
                                int begin, int end,
                                QCPAxis* keyAxis, QCPAxis* valueAxis)
{
    using V = std::ranges::range_value_t<VC>;
    QVector<QPointF> result;
    result.resize(end - begin);

    if (keyAxis->orientation() == Qt::Vertical)
    {
        for (int i = begin; i < end; ++i)
        {
            auto& pt = result[i - begin];
            double v = static_cast<double>(values[i]);
            if constexpr (!std::is_integral_v<V>)
            {
                if (std::isnan(v)) { pt = QPointF(0, 0); continue; }
            }
            pt.setX(valueAxis->coordToPixel(v));
            pt.setY(keyAxis->coordToPixel(static_cast<double>(keys[i])));
        }
    }
    else
    {
        for (int i = begin; i < end; ++i)
        {
            auto& pt = result[i - begin];
            double v = static_cast<double>(values[i]);
            if constexpr (!std::is_integral_v<V>)
            {
                if (std::isnan(v)) { pt = QPointF(0, 0); continue; }
            }
            pt.setX(keyAxis->coordToPixel(static_cast<double>(keys[i])));
            pt.setY(valueAxis->coordToPixel(v));
        }
    }
    return result;
}

template <IndexableNumericRange KC, IndexableNumericRange VC>
QVector<QPointF> optimizedLineData(const KC& keys, const VC& values,
                                    int begin, int end,
                                    int pixelWidth,
                                    QCPAxis* keyAxis, QCPAxis* valueAxis)
{
    using K = std::ranges::range_value_t<KC>;
    using V = std::ranges::range_value_t<VC>;

    const int dataCount = end - begin;
    if (dataCount <= 0) return {};

    // Determine if adaptive sampling should kick in
    double keyPixelSpan = qAbs(keyAxis->coordToPixel(static_cast<double>(keys[begin]))
                                - keyAxis->coordToPixel(static_cast<double>(keys[end - 1])));
    int maxCount = (std::numeric_limits<int>::max)();
    if (2 * keyPixelSpan + 2 < static_cast<double>((std::numeric_limits<int>::max)()))
        maxCount = int(2 * keyPixelSpan + 2);

    if (dataCount < maxCount)
        return linesToPixels(keys, values, begin, end, keyAxis, valueAxis);

    // Adaptive sampling: consolidate multiple data points per pixel into min/max clusters.
    // Extracted from QCPGraph::getOptimizedLineData (plottable-graph.cpp:1148-1240).
    QVector<QPointF> result;
    result.reserve(maxCount);

    const bool isVertical = keyAxis->orientation() == Qt::Vertical;
    auto toPixel = [&](double k, double v) -> QPointF {
        return isVertical ? QPointF(valueAxis->coordToPixel(v), keyAxis->coordToPixel(k))
                          : QPointF(keyAxis->coordToPixel(k), valueAxis->coordToPixel(v));
    };

    int i = begin;
    double minValue = static_cast<double>(values[i]);
    double maxValue = minValue;
    int currentIntervalFirst = i;
    int reversedFactor = keyAxis->pixelOrientation();
    int reversedRound = reversedFactor == -1 ? 1 : 0;
    double currentIntervalStartKey = keyAxis->pixelToCoord(
        int(keyAxis->coordToPixel(static_cast<double>(keys[begin])) + reversedRound));
    double lastIntervalEndKey = currentIntervalStartKey;
    double keyEpsilon = qAbs(currentIntervalStartKey
        - keyAxis->pixelToCoord(keyAxis->coordToPixel(currentIntervalStartKey)
                                + 1.0 * reversedFactor));
    bool keyEpsilonVariable = keyAxis->scaleType() == QCPAxis::stLogarithmic;
    int intervalDataCount = 1;
    ++i;

    while (i < end)
    {
        double k = static_cast<double>(keys[i]);
        double v = static_cast<double>(values[i]);
        if (k < currentIntervalStartKey + keyEpsilon)
        {
            if (v < minValue) minValue = v;
            else if (v > maxValue) maxValue = v;
            ++intervalDataCount;
        }
        else
        {
            if (intervalDataCount >= 2)
            {
                double firstVal = static_cast<double>(values[currentIntervalFirst]);
                if (lastIntervalEndKey < currentIntervalStartKey - keyEpsilon)
                    result.append(toPixel(currentIntervalStartKey + keyEpsilon * 0.2, firstVal));
                result.append(toPixel(currentIntervalStartKey + keyEpsilon * 0.25, minValue));
                result.append(toPixel(currentIntervalStartKey + keyEpsilon * 0.75, maxValue));
                if (k > currentIntervalStartKey + keyEpsilon * 2)
                {
                    double prevVal = static_cast<double>(values[i - 1]);
                    result.append(toPixel(currentIntervalStartKey + keyEpsilon * 0.8, prevVal));
                }
            }
            else
            {
                double fk = static_cast<double>(keys[currentIntervalFirst]);
                double fv = static_cast<double>(values[currentIntervalFirst]);
                result.append(toPixel(fk, fv));
            }
            lastIntervalEndKey = static_cast<double>(keys[i - 1]);
            minValue = v;
            maxValue = v;
            currentIntervalFirst = i;
            currentIntervalStartKey = keyAxis->pixelToCoord(
                int(keyAxis->coordToPixel(k) + reversedRound));
            if (keyEpsilonVariable)
                keyEpsilon = qAbs(currentIntervalStartKey
                    - keyAxis->pixelToCoord(keyAxis->coordToPixel(currentIntervalStartKey)
                                            + 1.0 * reversedFactor));
            intervalDataCount = 1;
        }
        ++i;
    }
    // Handle last interval
    if (intervalDataCount >= 2)
    {
        double firstVal = static_cast<double>(values[currentIntervalFirst]);
        if (lastIntervalEndKey < currentIntervalStartKey - keyEpsilon)
            result.append(toPixel(currentIntervalStartKey + keyEpsilon * 0.2, firstVal));
        result.append(toPixel(currentIntervalStartKey + keyEpsilon * 0.25, minValue));
        result.append(toPixel(currentIntervalStartKey + keyEpsilon * 0.75, maxValue));
    }
    else
    {
        double fk = static_cast<double>(keys[currentIntervalFirst]);
        double fv = static_cast<double>(values[currentIntervalFirst]);
        result.append(toPixel(fk, fv));
    }

    return result;
}

} // namespace qcp::algo
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All `algoFindBegin`, `algoFindEnd`, `algoKeyRange`, `algoValueRange` tests PASS

- [ ] **Step 6: Commit**

```bash
git add src/datasource/algorithms.h tests/auto/test-datasource/
git commit -m "feat: add qcp::algo free function templates for data source algorithms"
```

### Task 3: SoA Data Source Template

**Files:**
- Create: `src/datasource/soa-datasource.h`

- [ ] **Step 1: Write failing tests for QCPSoADataSource**

All test slots are already declared in the header (Task 2 Step 1). Add implementations to `tests/auto/test-datasource/test-datasource.cpp`:

```cpp
#include "datasource/soa-datasource.h"

void TestDataSource::soaOwningVector()
{
    std::vector<double> keys = {1.0, 2.0, 3.0};
    std::vector<double> values = {10.0, 20.0, 30.0};
    QCPSoADataSource<std::vector<double>, std::vector<double>> src(
        std::move(keys), std::move(values));

    QCOMPARE(src.size(), 3);
    QVERIFY(!src.empty());
    QCOMPARE(src.keyAt(0), 1.0);
    QCOMPARE(src.keyAt(2), 3.0);
    QCOMPARE(src.valueAt(1), 20.0);
}

void TestDataSource::soaViewSpan()
{
    double keys[] = {1.0, 2.0, 3.0};
    float values[] = {10.0f, 20.0f, 30.0f};
    QCPSoADataSource<std::span<const double>, std::span<const float>> src(
        std::span{keys}, std::span{values});

    QCOMPARE(src.size(), 3);
    QCOMPARE(src.keyAt(1), 2.0);
    QCOMPARE(src.valueAt(2), 30.0);
}

void TestDataSource::soaFindBeginEnd()
{
    std::vector<double> keys = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> values = {10.0, 20.0, 30.0, 40.0, 50.0};
    QCPSoADataSource<std::vector<double>, std::vector<double>> src(
        std::move(keys), std::move(values));

    QCOMPARE(src.findBegin(3.0, false), 2);
    QCOMPARE(src.findEnd(3.0, false), 3);
    QCOMPARE(src.findBegin(3.0, true), 1);
    QCOMPARE(src.findEnd(3.0, true), 4);
}

void TestDataSource::soaRangeQueries()
{
    std::vector<double> keys = {1.0, 2.0, 3.0};
    std::vector<double> values = {-5.0, 10.0, 3.0};
    QCPSoADataSource<std::vector<double>, std::vector<double>> src(
        std::move(keys), std::move(values));

    bool found = false;
    auto kr = src.keyRange(found);
    QVERIFY(found);
    QCOMPARE(kr.lower, 1.0);
    QCOMPARE(kr.upper, 3.0);

    auto vr = src.valueRange(found);
    QVERIFY(found);
    QCOMPARE(vr.lower, -5.0);
    QCOMPARE(vr.upper, 10.0);
}

void TestDataSource::soaIntValues()
{
    std::vector<double> keys = {1.0, 2.0, 3.0};
    std::vector<int> values = {100, 200, 300};
    QCPSoADataSource<std::vector<double>, std::vector<int>> src(
        std::move(keys), std::move(values));

    QCOMPARE(src.size(), 3);
    QCOMPARE(src.valueAt(0), 100.0);
    QCOMPARE(src.valueAt(2), 300.0);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: FAIL — `soa-datasource.h` does not exist

- [ ] **Step 3: Implement `src/datasource/soa-datasource.h`**

```cpp
#pragma once
#include "abstract-datasource.h"
#include "algorithms.h"

template <IndexableNumericRange KeyContainer, IndexableNumericRange ValueContainer>
class QCPSoADataSource final : public QCPAbstractDataSource {
public:
    using K = std::ranges::range_value_t<KeyContainer>;
    using V = std::ranges::range_value_t<ValueContainer>;

    QCPSoADataSource(KeyContainer keys, ValueContainer values)
        : mKeys(std::move(keys)), mValues(std::move(values)) {}

    const KeyContainer& keys() const { return mKeys; }
    const ValueContainer& values() const { return mValues; }

    int size() const override
    {
        return static_cast<int>(std::ranges::size(mKeys));
    }

    double keyAt(int i) const override
    {
        return static_cast<double>(mKeys[i]);
    }

    double valueAt(int i) const override
    {
        return static_cast<double>(mValues[i]);
    }

    QCPRange keyRange(bool& foundRange, QCP::SignDomain sd = QCP::sdBoth) const override
    {
        return qcp::algo::keyRange(mKeys, foundRange, sd);
    }

    QCPRange valueRange(bool& foundRange, QCP::SignDomain sd = QCP::sdBoth,
                        const QCPRange& inKeyRange = QCPRange()) const override
    {
        return qcp::algo::valueRange(mKeys, mValues, foundRange, sd, inKeyRange);
    }

    int findBegin(double sortKey, bool expandedRange = true) const override
    {
        return qcp::algo::findBegin(mKeys, sortKey, expandedRange);
    }

    int findEnd(double sortKey, bool expandedRange = true) const override
    {
        return qcp::algo::findEnd(mKeys, sortKey, expandedRange);
    }

    QVector<QPointF> getOptimizedLineData(int begin, int end, int pixelWidth,
                                           QCPAxis* keyAxis, QCPAxis* valueAxis) const override
    {
        return qcp::algo::optimizedLineData(mKeys, mValues, begin, end, pixelWidth,
                                             keyAxis, valueAxis);
    }

    QVector<QPointF> getLines(int begin, int end,
                               QCPAxis* keyAxis, QCPAxis* valueAxis) const override
    {
        return qcp::algo::linesToPixels(mKeys, mValues, begin, end, keyAxis, valueAxis);
    }

private:
    KeyContainer mKeys;
    ValueContainer mValues;
};
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All soa tests PASS

- [ ] **Step 5: Commit**

```bash
git add src/datasource/soa-datasource.h
git commit -m "feat: add QCPSoADataSource template with concept-constrained containers"
```

---

## Chunk 2: QCPGraph2 Plottable

### Task 4: QCPGraph2 Skeleton

**Files:**
- Create: `src/plottables/plottable-graph2.h`
- Create: `src/plottables/plottable-graph2.cpp`
- Modify: `src/qcp.h` — add include for `plottables/plottable-graph2.h`
- Modify: `meson.build` — add `plottable-graph2.cpp` to sources, `plottable-graph2.h` to moc headers

- [ ] **Step 1: Write failing test for QCPGraph2 creation**

All test slots are already declared in the header (Task 2 Step 1). Add implementations to `tests/auto/test-datasource/test-datasource.cpp`:

```cpp
#include "qcustomplot.h"

void TestDataSource::graph2Creation()
{
    mPlot = new QCustomPlot();
    auto* graph = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    Q_UNUSED(graph);
    QVERIFY(graph->dataSource() == nullptr);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson compile -C build`
Expected: FAIL — `QCPGraph2` class not found

- [ ] **Step 3: Create `src/plottables/plottable-graph2.h`**

```cpp
#pragma once
#include "plottable.h"
#include "plottable1d.h"
#include "datasource/abstract-datasource.h"
#include "datasource/soa-datasource.h"
#include <memory>
#include <span>

class QCP_LIB_DECL QCPGraph2 : public QCPAbstractPlottable, public QCPPlottableInterface1D {
    Q_OBJECT
public:
    explicit QCPGraph2(QCPAxis* keyAxis, QCPAxis* valueAxis);
    virtual ~QCPGraph2() override;

    // Data source
    void setDataSource(std::unique_ptr<QCPAbstractDataSource> source);
    void setDataSource(std::shared_ptr<QCPAbstractDataSource> source);
    QCPAbstractDataSource* dataSource() const { return mDataSource.get(); }

    // Convenience: owning
    template <IndexableNumericRange KC, IndexableNumericRange VC>
    void setData(KC&& keys, VC&& values)
    {
        using KD = std::decay_t<KC>;
        using VD = std::decay_t<VC>;
        setDataSource(std::make_shared<QCPSoADataSource<KD, VD>>(
            std::forward<KC>(keys), std::forward<VC>(values)));
    }

    // Convenience: non-owning view from raw pointers
    template <typename K, typename V>
    void viewData(const K* keys, const V* values, int count)
    {
        setDataSource(std::make_shared<QCPSoADataSource<std::span<const K>, std::span<const V>>>(
            std::span<const K>(keys, count), std::span<const V>(values, count)));
    }

    // Convenience: non-owning view from spans
    template <typename K, typename V>
    void viewData(std::span<const K> keys, std::span<const V> values)
    {
        setDataSource(std::make_shared<QCPSoADataSource<std::span<const K>, std::span<const V>>>(
            keys, values));
    }

    void dataChanged();

    // Adaptive sampling control
    bool adaptiveSampling() const { return mAdaptiveSampling; }
    void setAdaptiveSampling(bool enabled) { mAdaptiveSampling = enabled; }

    // QCPPlottableInterface1D
    int dataCount() const override;
    double dataMainKey(int index) const override;
    double dataSortKey(int index) const override;
    double dataMainValue(int index) const override;
    QCPRange dataValueRange(int index) const override;
    QPointF dataPixelPosition(int index) const override;
    bool sortKeyIsMainKey() const override { return true; }
    QCPDataSelection selectTestRect(const QRectF& rect, bool onlySelectable) const override;
    int findBegin(double sortKey, bool expandedRange = true) const override;
    int findEnd(double sortKey, bool expandedRange = true) const override;

    // QCPAbstractPlottable
    QCPPlottableInterface1D* interface1D() override { return this; }
    double selectTest(const QPointF& pos, bool onlySelectable,
                      QVariant* details = nullptr) const override;
    QCPRange getKeyRange(bool& foundRange,
                         QCP::SignDomain inSignDomain = QCP::sdBoth) const override;
    QCPRange getValueRange(bool& foundRange,
                           QCP::SignDomain inSignDomain = QCP::sdBoth,
                           const QCPRange& inKeyRange = QCPRange()) const override;

protected:
    void draw(QCPPainter* painter) override;
    void drawLegendIcon(QCPPainter* painter, const QRectF& rect) const override;

private:
    std::shared_ptr<QCPAbstractDataSource> mDataSource;
    bool mAdaptiveSampling = true;
};
```

- [ ] **Step 4: Create `src/plottables/plottable-graph2.cpp`**

Implement all non-template methods. Key implementation notes:
- Constructor: call `QCPAbstractPlottable(keyAxis, valueAxis)`, set default pen/brush.
- `draw()`: if no data source or empty, return. Get visible range via `findBegin`/`findEnd` on axis ranges. Call `mDataSource->getOptimizedLineData(...)` or `mDataSource->getLines(...)` depending on `mAdaptiveSampling`. Draw the resulting `QVector<QPointF>` with `drawPolyline`-style code (pen + antialiasing).
- `drawLegendIcon()`: draw a horizontal line in the legend rect using current pen. Same as `QCPGraph::drawLegendIcon` simplified (no scatter/fill for now).
- `getKeyRange`/`getValueRange`: delegate to `mDataSource->keyRange()`/`mDataSource->valueRange()`.
- `selectTest`: iterate data points near `pos`, return distance to nearest. Use `QCPPlottableInterface1D` methods.
- `selectTestRect`: iterate visible points, return `QCPDataSelection` of points inside rect.
- `interface1D` methods: delegate to `mDataSource->keyAt()`/`valueAt()`/`findBegin()`/`findEnd()`/`size()`.

- [ ] **Step 5: Add to build system**

Modify: `meson.build` — add `'src/plottables/plottable-graph2.cpp'` to the sources list (around line 180) and `'src/plottables/plottable-graph2.h'` to the moc_headers list (around line 85).

Modify: `src/qcp.h` — add `#include "plottables/plottable-graph2.h"` after the graph include (around line 68).

- [ ] **Step 6: Run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: `graph2Creation` PASS

- [ ] **Step 7: Commit**

```bash
git add src/plottables/plottable-graph2.h src/plottables/plottable-graph2.cpp src/qcp.h meson.build
git commit -m "feat: add QCPGraph2 plottable with data source integration"
```

### Task 5: QCPGraph2 Data Binding Tests

**Files:**
- Modify: `tests/auto/test-datasource/test-datasource.cpp`

- [ ] **Step 1: Implement remaining integration tests**

```cpp
void TestDataSource::graph2SetDataOwning()
{
    mPlot = new QCustomPlot();
    auto* graph = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);

    std::vector<double> keys = {1.0, 2.0, 3.0};
    std::vector<float> values = {10.0f, 20.0f, 30.0f};
    graph->setData(std::move(keys), std::move(values));

    QVERIFY(graph->dataSource() != nullptr);
    QCOMPARE(graph->dataSource()->size(), 3);
    QCOMPARE(graph->dataSource()->keyAt(0), 1.0);
    QCOMPARE(graph->dataSource()->valueAt(2), 30.0);
}

void TestDataSource::graph2ViewData()
{
    mPlot = new QCustomPlot();
    auto* graph = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);

    double keys[] = {1.0, 2.0, 3.0};
    int values[] = {100, 200, 300};
    graph->viewData(keys, values, 3);

    QVERIFY(graph->dataSource() != nullptr);
    QCOMPARE(graph->dataSource()->size(), 3);
    QCOMPARE(graph->dataSource()->valueAt(1), 200.0);
}

void TestDataSource::graph2SharedSource()
{
    mPlot = new QCustomPlot();
    auto* g1 = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    auto* g2 = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);

    auto source = std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
        std::vector<double>{1.0, 2.0}, std::vector<double>{10.0, 20.0});
    g1->setDataSource(source);
    g2->setDataSource(source);

    QCOMPARE(g1->dataSource(), g2->dataSource());
    QCOMPARE(g1->dataSource()->size(), 2);
}

void TestDataSource::graph2AxisRanges()
{
    mPlot = new QCustomPlot();
    auto* graph = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);

    std::vector<double> keys = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> values = {-1.0, 5.0, 2.0, 8.0, 3.0};
    graph->setData(std::move(keys), std::move(values));

    bool found = false;
    auto kr = graph->getKeyRange(found);
    QVERIFY(found);
    QCOMPARE(kr.lower, 1.0);
    QCOMPARE(kr.upper, 5.0);

    auto vr = graph->getValueRange(found);
    QVERIFY(found);
    QCOMPARE(vr.lower, -1.0);
    QCOMPARE(vr.upper, 8.0);
}
```

- [ ] **Step 2: Run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All PASS

- [ ] **Step 3: Commit**

```bash
git add tests/auto/test-datasource/
git commit -m "test: add QCPGraph2 integration tests for data binding and axis ranges"
```

### Task 6: QCPGraph2 Rendering Smoke Test

**Files:**
- Modify: `tests/auto/test-datasource/test-datasource.cpp`

- [ ] **Step 1: Add rendering smoke test implementation**

Already declared in header. Add to `tests/auto/test-datasource/test-datasource.cpp`:

```cpp
void TestDataSource::graph2Render()
{
    mPlot = new QCustomPlot();
    mPlot->resize(400, 300);
    auto* graph = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);

    std::vector<double> keys = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> values = {1.0, 4.0, 2.0, 5.0, 3.0};
    graph->setData(std::move(keys), std::move(values));

    graph->rescaleAxes();
    // Should not crash
    mPlot->replot(QCustomPlot::rpImmediateRefresh);
    QVERIFY(true);

    // Also test with float values
    std::vector<double> keys2 = {0.0, 1.0, 2.0};
    std::vector<float> vals2 = {0.0f, 1.0f, 0.5f};
    graph->setData(std::move(keys2), std::move(vals2));
    graph->rescaleAxes();
    mPlot->replot(QCustomPlot::rpImmediateRefresh);
    QVERIFY(true);

    // Test with int values
    double keys3[] = {0.0, 1.0, 2.0};
    int vals3[] = {0, 100, 50};
    graph->viewData(keys3, vals3, 3);
    graph->rescaleAxes();
    mPlot->replot(QCustomPlot::rpImmediateRefresh);
    QVERIFY(true);
}
```

- [ ] **Step 2: Run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: PASS — replot completes without crash for all type combinations

- [ ] **Step 3: Commit**

```bash
git add tests/auto/test-datasource/
git commit -m "test: add QCPGraph2 rendering smoke tests with multiple numeric types"
```

---

## Chunk 3: Polish and Verify

### Task 7: Verify All Existing Tests Still Pass

- [ ] **Step 1: Run full test suite**

Run: `meson test -C build --print-errorlogs`
Expected: All existing tests PASS (no regressions)

- [ ] **Step 2: Verify build on clean configure**

Run: `rm -rf build && meson setup build && meson compile -C build && meson test -C build --print-errorlogs`
Expected: Clean build + all tests PASS

- [ ] **Step 3: Commit any remaining fixes**

### Task 8: Benchmark QCPGraph vs QCPGraph2

**Files:**
- Create: `tests/benchmark/bench-graph2.cpp` (or add to existing benchmark)

- [ ] **Step 1: Write benchmark**

Create a benchmark that:
1. Creates 1M `double` data points, measures `QCPGraph::setData` time vs `QCPGraph2::setData` (move) vs `QCPGraph2::viewData` (zero-copy)
2. Creates 1M `float` data points, measures `QCPGraph::setData` (requires conversion) vs `QCPGraph2::setData` (native float)
3. Measures replot time for both with 1M points

Use Qt Test benchmarking: `QBENCHMARK { ... }`

- [ ] **Step 2: Run benchmark**

Run: `meson test --benchmark -C build --print-errorlogs`
Expected: `QCPGraph2::viewData` should show near-zero setData time. Replot times should be comparable or faster.

- [ ] **Step 3: Commit**

```bash
git add tests/benchmark/
git commit -m "bench: add QCPGraph vs QCPGraph2 performance comparison"
```

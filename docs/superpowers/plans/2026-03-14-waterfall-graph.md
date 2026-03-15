# QCPWaterfallGraph Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add waterfall/record-section plot support to NeoQCP — traces offset vertically by distance or uniform spacing, with optional per-trace normalization.

**Architecture:** `QCPWaterfallGraph` inherits `QCPMultiGraph`. A `QCPWaterfallDataAdapter` wraps the user's data source and transforms values (offset + normalization + gain) in coordinate space before the existing pixel pipeline. This makes adaptive sampling, selection, and hit testing work automatically.

**Tech Stack:** C++20, Qt 6.7+, Meson build system, Qt Test framework

**Spec:** `docs/superpowers/specs/2026-03-14-waterfall-graph-design.md`

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `src/plottables/plottable-multigraph.h` | Modify | `private:` → `protected:`, make `setDataSource(shared_ptr)` virtual, route `unique_ptr` overload through it |
| `src/plottables/plottable-multigraph.cpp` | Modify | `setDataSource(unique_ptr)` calls `setDataSource(shared_ptr)` |
| `src/plottables/plottable-waterfall.h` | Create | `QCPWaterfallDataAdapter` + `QCPWaterfallGraph` declarations |
| `src/plottables/plottable-waterfall.cpp` | Create | Implementation of adapter and waterfall graph |
| `src/qcp.h` | Modify | Add `#include "plottables/plottable-waterfall.h"` |
| `meson.build` | Modify | Add waterfall source + moc header |
| `tests/auto/test-waterfall/test-waterfall.h` | Create | Unit test declarations |
| `tests/auto/test-waterfall/test-waterfall.cpp` | Create | Unit test implementations |
| `tests/auto/meson.build` | Modify | Add waterfall test files |
| `tests/manual/mainwindow.h` | Modify | Add `setupWaterfallTest` declaration |
| `tests/manual/mainwindow.cpp` | Modify | Add `setupWaterfallTest` implementation |

---

## Chunk 1: QCPMultiGraph Protected + Build Scaffolding

### Task 1: Make QCPMultiGraph internals protected + setDataSource virtual

**Files:**
- Modify: `src/plottables/plottable-multigraph.h:28-29,113`
- Modify: `src/plottables/plottable-multigraph.cpp:34-38`

**Why setDataSource must be virtual:** `setData()` and `viewData()` are non-virtual template methods that call `setDataSource(shared_ptr)`. Without virtual dispatch, a subclass's `setDataSource` override would be bypassed when called through inherited templates. Making the `shared_ptr` overload virtual fixes this.

- [ ] **Step 1: Make setDataSource(shared_ptr) virtual and route unique_ptr through it**

In `src/plottables/plottable-multigraph.h`, change lines 28-29:

```cpp
    // Data source
    void setDataSource(std::unique_ptr<QCPAbstractMultiDataSource> source);
    virtual void setDataSource(std::shared_ptr<QCPAbstractMultiDataSource> source);
```

In `src/plottables/plottable-multigraph.cpp`, change `setDataSource(unique_ptr)` (lines 34-38) to route through the shared_ptr overload:

```cpp
void QCPMultiGraph::setDataSource(std::unique_ptr<QCPAbstractMultiDataSource> source)
{
    setDataSource(std::shared_ptr<QCPAbstractMultiDataSource>(std::move(source)));
}
```

- [ ] **Step 2: Change private to protected**

In `src/plottables/plottable-multigraph.h`, change line 113 from `private:` to `protected:`. This exposes `mDataSource`, `mComponents`, `mLineStyle`, `mAdaptiveSampling`, `mScatterSkip`, `syncComponentCount()`, `updateBaseSelection()`, and all line-style transform methods to subclasses.

- [ ] **Step 3: Verify existing tests still pass**

Run: `meson test -C build --print-errorlogs`
Expected: All existing tests pass (no behavioral change).

- [ ] **Step 4: Commit**

```bash
git add src/plottables/plottable-multigraph.h src/plottables/plottable-multigraph.cpp
git commit -m "refactor: make QCPMultiGraph data members protected, setDataSource virtual"
```

### Task 2: Create empty waterfall files + build integration

**Files:**
- Create: `src/plottables/plottable-waterfall.h`
- Create: `src/plottables/plottable-waterfall.cpp`
- Modify: `src/qcp.h:72` (after multigraph include)
- Modify: `meson.build:85` (moc headers) and `meson.build:193` (sources)

- [ ] **Step 1: Create header with minimal class stubs**

Create `src/plottables/plottable-waterfall.h`:

```cpp
#pragma once
#include "plottable-multigraph.h"
#include <memory>

class QCPWaterfallDataAdapter : public QCPAbstractMultiDataSource {
public:
    explicit QCPWaterfallDataAdapter(std::shared_ptr<QCPAbstractMultiDataSource> source);

    void setSource(std::shared_ptr<QCPAbstractMultiDataSource> source);
    QCPAbstractMultiDataSource* source() const { return mSource.get(); }

    void setOffsets(const QVector<double>& offsets) { mOffsets = offsets; }
    void setNormFactors(const QVector<double>& factors) { mNormFactors = factors; }
    void setGain(double gain) { mGain = gain; }

    // Pure delegation
    int columnCount() const override;
    int size() const override;
    double keyAt(int i) const override;
    QCPRange keyRange(bool& found, QCP::SignDomain sd = QCP::sdBoth) const override;
    int findBegin(double sortKey, bool expandedRange = true) const override;
    int findEnd(double sortKey, bool expandedRange = true) const override;

    // Transformed
    double valueAt(int column, int i) const override;
    QCPRange valueRange(int column, bool& found, QCP::SignDomain sd = QCP::sdBoth,
                        const QCPRange& inKeyRange = QCPRange()) const override;
    QVector<QPointF> getLines(int column, int begin, int end,
                               QCPAxis* keyAxis, QCPAxis* valueAxis) const override;
    QVector<QPointF> getOptimizedLineData(int column, int begin, int end, int pixelWidth,
                                           QCPAxis* keyAxis, QCPAxis* valueAxis) const override;

private:
    std::shared_ptr<QCPAbstractMultiDataSource> mSource;
    QVector<double> mOffsets;
    QVector<double> mNormFactors;
    double mGain = 1.0;

    double transform(int column, double rawValue) const;
};

class QCP_LIB_DECL QCPWaterfallGraph : public QCPMultiGraph {
    Q_OBJECT
public:
    enum OffsetMode { omUniform, omCustom };
    Q_ENUM(OffsetMode)

    explicit QCPWaterfallGraph(QCPAxis* keyAxis, QCPAxis* valueAxis);

    void setDataSource(std::shared_ptr<QCPAbstractMultiDataSource> source) override;

    OffsetMode offsetMode() const { return mOffsetMode; }
    void setOffsetMode(OffsetMode mode);
    double uniformSpacing() const { return mUniformSpacing; }
    void setUniformSpacing(double spacing);
    QVector<double> offsets() const { return mUserOffsets; }
    void setOffsets(const QVector<double>& offsets);

    bool normalize() const { return mNormalize; }
    void setNormalize(bool enabled);
    double gain() const { return mGain; }
    void setGain(double gain);

    void invalidateNormalization();

protected:
    void draw(QCPPainter* painter) override;
    QCPRange getValueRange(bool& foundRange,
                           QCP::SignDomain inSignDomain = QCP::sdBoth,
                           const QCPRange& inKeyRange = QCPRange()) const override;

private:
    OffsetMode mOffsetMode = omUniform;
    double mUniformSpacing = 1.0;
    QVector<double> mUserOffsets;
    bool mNormalize = true;
    double mGain = 1.0;
    mutable bool mNormDirty = true;
    mutable QVector<double> mCachedNormFactors;

    std::shared_ptr<QCPAbstractMultiDataSource> mOriginalSource;
    std::shared_ptr<QCPWaterfallDataAdapter> mAdapter;

    double effectiveOffset(int component) const;
    void updateAdapter() const;
    void recomputeNormFactors() const;
};
```

- [ ] **Step 2: Create minimal .cpp with constructor stubs**

Create `src/plottables/plottable-waterfall.cpp`:

```cpp
#include "plottable-waterfall.h"
#include "datasource/algorithms.h"

// --- QCPWaterfallDataAdapter ---

QCPWaterfallDataAdapter::QCPWaterfallDataAdapter(
    std::shared_ptr<QCPAbstractMultiDataSource> source)
    : mSource(std::move(source))
{
}

void QCPWaterfallDataAdapter::setSource(std::shared_ptr<QCPAbstractMultiDataSource> source)
{
    mSource = std::move(source);
}

// Pure delegation — all guard against null mSource
int QCPWaterfallDataAdapter::columnCount() const { return mSource ? mSource->columnCount() : 0; }
int QCPWaterfallDataAdapter::size() const { return mSource ? mSource->size() : 0; }
double QCPWaterfallDataAdapter::keyAt(int i) const { return mSource ? mSource->keyAt(i) : 0.0; }
QCPRange QCPWaterfallDataAdapter::keyRange(bool& found, QCP::SignDomain sd) const
{
    if (!mSource) { found = false; return QCPRange(); }
    return mSource->keyRange(found, sd);
}
int QCPWaterfallDataAdapter::findBegin(double sortKey, bool expandedRange) const { return mSource ? mSource->findBegin(sortKey, expandedRange) : 0; }
int QCPWaterfallDataAdapter::findEnd(double sortKey, bool expandedRange) const { return mSource ? mSource->findEnd(sortKey, expandedRange) : 0; }

double QCPWaterfallDataAdapter::transform(int column, double rawValue) const
{
    double offset = (column < mOffsets.size()) ? mOffsets[column] : 0.0;
    double norm = (column < mNormFactors.size()) ? mNormFactors[column] : 1.0;
    return offset + rawValue * norm * mGain;
}

double QCPWaterfallDataAdapter::valueAt(int column, int i) const
{
    if (!mSource) return 0.0;
    return transform(column, mSource->valueAt(column, i));
}

QCPRange QCPWaterfallDataAdapter::valueRange(int column, bool& found, QCP::SignDomain sd,
                                              const QCPRange& inKeyRange) const
{
    if (!mSource) { found = false; return QCPRange(); }
    // Get raw range, then transform the bounds
    QCPRange raw = mSource->valueRange(column, found, QCP::sdBoth, inKeyRange);
    if (!found) return QCPRange();
    double a = transform(column, raw.lower);
    double b = transform(column, raw.upper);
    QCPRange result(qMin(a, b), qMax(a, b));
    // Apply sign domain filter
    if (sd == QCP::sdPositive) result.lower = qMax(result.lower, 0.0);
    if (sd == QCP::sdNegative) result.upper = qMin(result.upper, 0.0);
    found = (result.lower < result.upper);
    return result;
}

QVector<QPointF> QCPWaterfallDataAdapter::getLines(int column, int begin, int end,
                                                     QCPAxis* keyAxis, QCPAxis* valueAxis) const
{
    if (!mSource || begin >= end) return {};
    // Build transformed value buffer, then use algo
    std::vector<double> keys(end - begin);
    std::vector<double> vals(end - begin);
    for (int i = begin; i < end; ++i) {
        keys[i - begin] = mSource->keyAt(i);
        vals[i - begin] = transform(column, mSource->valueAt(column, i));
    }
    return qcp::algo::linesToPixels(keys, vals, 0, end - begin, keyAxis, valueAxis);
}

QVector<QPointF> QCPWaterfallDataAdapter::getOptimizedLineData(int column, int begin, int end,
                                                                 int pixelWidth,
                                                                 QCPAxis* keyAxis,
                                                                 QCPAxis* valueAxis) const
{
    if (!mSource || begin >= end) return {};
    // Build transformed value buffer, then use algo
    std::vector<double> keys(end - begin);
    std::vector<double> vals(end - begin);
    for (int i = begin; i < end; ++i) {
        keys[i - begin] = mSource->keyAt(i);
        vals[i - begin] = transform(column, mSource->valueAt(column, i));
    }
    return qcp::algo::optimizedLineData(keys, vals, 0, end - begin, pixelWidth,
                                         keyAxis, valueAxis);
}

// --- QCPWaterfallGraph ---

QCPWaterfallGraph::QCPWaterfallGraph(QCPAxis* keyAxis, QCPAxis* valueAxis)
    : QCPMultiGraph(keyAxis, valueAxis)
    , mAdapter(std::make_shared<QCPWaterfallDataAdapter>(nullptr))
{
}

void QCPWaterfallGraph::setDataSource(std::shared_ptr<QCPAbstractMultiDataSource> source)
{
    mOriginalSource = std::move(source);
    mAdapter->setSource(mOriginalSource);
    mNormDirty = true;
    QCPMultiGraph::setDataSource(mAdapter);
}

void QCPWaterfallGraph::setOffsetMode(OffsetMode mode) { mOffsetMode = mode; }
void QCPWaterfallGraph::setUniformSpacing(double spacing) { mUniformSpacing = spacing; }
void QCPWaterfallGraph::setOffsets(const QVector<double>& offsets) { mUserOffsets = offsets; }
void QCPWaterfallGraph::setNormalize(bool enabled) { mNormalize = enabled; mNormDirty = true; }
void QCPWaterfallGraph::setGain(double gain) { mGain = gain; }
void QCPWaterfallGraph::invalidateNormalization() { mNormDirty = true; }

double QCPWaterfallGraph::effectiveOffset(int component) const
{
    if (mOffsetMode == omCustom && component < mUserOffsets.size())
        return mUserOffsets[component];
    return component * mUniformSpacing;
}

void QCPWaterfallGraph::recomputeNormFactors() const
{
    if (!mOriginalSource) {
        mCachedNormFactors.clear();
        return;
    }
    int cols = mOriginalSource->columnCount();
    int n = mOriginalSource->size();
    mCachedNormFactors.resize(cols);
    for (int c = 0; c < cols; ++c) {
        if (!mNormalize) {
            mCachedNormFactors[c] = 1.0;
            continue;
        }
        double maxAbs = 0.0;
        for (int i = 0; i < n; ++i)
            maxAbs = qMax(maxAbs, qAbs(mOriginalSource->valueAt(c, i)));
        mCachedNormFactors[c] = (maxAbs > 0.0) ? (1.0 / maxAbs) : 1.0;
    }
    mNormDirty = false;
}

void QCPWaterfallGraph::updateAdapter() const
{
    if (!mOriginalSource) return;
    if (mNormDirty)
        recomputeNormFactors();

    int cols = mOriginalSource->columnCount();
    QVector<double> offsets(cols);
    for (int c = 0; c < cols; ++c)
        offsets[c] = effectiveOffset(c);

    mAdapter->setOffsets(offsets);
    mAdapter->setNormFactors(mCachedNormFactors);
    mAdapter->setGain(mGain);
}

void QCPWaterfallGraph::draw(QCPPainter* painter)
{
    updateAdapter();
    QCPMultiGraph::draw(painter);
}

QCPRange QCPWaterfallGraph::getValueRange(bool& foundRange,
                                           QCP::SignDomain inSignDomain,
                                           const QCPRange& inKeyRange) const
{
    foundRange = false;
    if (!mOriginalSource || mOriginalSource->columnCount() == 0)
        return QCPRange();

    // Use adapter's valueRange if inKeyRange is specified
    if (inKeyRange != QCPRange()) {
        updateAdapter();
        QCPRange merged;
        bool first = true;
        for (int c = 0; c < mOriginalSource->columnCount(); ++c) {
            bool f = false;
            QCPRange r = mAdapter->valueRange(c, f, inSignDomain, inKeyRange);
            if (f) {
                if (first) { merged = r; first = false; }
                else merged.expand(r);
            }
        }
        foundRange = !first;
        return merged;
    }

    // Simple offset-based range with margin
    int cols = mOriginalSource->columnCount();
    double lo = effectiveOffset(0), hi = lo;
    for (int c = 1; c < cols; ++c) {
        double o = effectiveOffset(c);
        lo = qMin(lo, o);
        hi = qMax(hi, o);
    }

    double margin;
    if (mNormalize) {
        margin = mGain;
    } else {
        double maxAmp = 0.0;
        for (int c = 0; c < cols; ++c) {
            bool f = false;
            QCPRange r = mOriginalSource->valueRange(c, f, QCP::sdBoth);
            if (f) maxAmp = qMax(maxAmp, qMax(qAbs(r.lower), qAbs(r.upper)));
        }
        margin = maxAmp * mGain;
    }

    lo -= margin;
    hi += margin;

    if (inSignDomain == QCP::sdPositive) lo = qMax(lo, 0.0);
    if (inSignDomain == QCP::sdNegative) hi = qMin(hi, 0.0);
    foundRange = (lo < hi);
    return QCPRange(lo, hi);
}
```

- [ ] **Step 3: Add include to qcp.h**

In `src/qcp.h`, after line 72 (`#include "plottables/plottable-multigraph.h"`), add:

```cpp
#include "plottables/plottable-waterfall.h"
```

- [ ] **Step 4: Add to meson.build**

In `meson.build`, add to the moc headers section (after line 85 `plottable-multigraph.h`):

```
'src/plottables/plottable-waterfall.h',
```

Add to the sources section (after line 193 `plottable-multigraph.cpp`):

```
'src/plottables/plottable-waterfall.cpp',
```

- [ ] **Step 5: Build to verify compilation**

Run: `meson compile -C build`
Expected: Compiles successfully with no errors.

- [ ] **Step 6: Commit**

```bash
git add src/plottables/plottable-waterfall.h src/plottables/plottable-waterfall.cpp src/qcp.h meson.build
git commit -m "feat: add QCPWaterfallGraph and QCPWaterfallDataAdapter"
```

---

## Chunk 2: Unit Tests

### Task 3: Create unit tests for QCPWaterfallGraph

**Files:**
- Create: `tests/auto/test-waterfall/test-waterfall.h`
- Create: `tests/auto/test-waterfall/test-waterfall.cpp`
- Modify: `tests/auto/meson.build`

- [ ] **Step 1: Create test header**

Create `tests/auto/test-waterfall/test-waterfall.h`:

```cpp
#pragma once
#include <QtTest/QtTest>

class QCustomPlot;

class TestWaterfall : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    // Offset model
    void uniformOffset();
    void customOffset();
    void customOffsetFallback();

    // Normalization
    void normalizationFactors();
    void normalizationDisabled();
    void invalidateNormalization();

    // Adapter transform
    void adapterValueAt();
    void singleComponentValueRange();

    // Value range
    void getValueRangeNormalized();
    void getValueRangeUnnormalized();
    void getValueRangeSignDomain();

    // Integration
    void drawDoesNotCrash();

private:
    QCustomPlot* mPlot = nullptr;
};
```

- [ ] **Step 2: Create test implementation**

Create `tests/auto/test-waterfall/test-waterfall.cpp`:

```cpp
#include "test-waterfall.h"
#include "../../../src/qcp.h"

void TestWaterfall::init()
{
    mPlot = new QCustomPlot();
    mPlot->resize(400, 300);
}

void TestWaterfall::cleanup()
{
    delete mPlot;
    mPlot = nullptr;
}

void TestWaterfall::uniformOffset()
{
    auto* wf = new QCPWaterfallGraph(mPlot->xAxis, mPlot->yAxis);
    wf->setOffsetMode(QCPWaterfallGraph::omUniform);
    wf->setUniformSpacing(10.0);

    // Set data with 3 columns so component count syncs
    std::vector<double> keys = {0.0, 1.0, 2.0};
    std::vector<std::vector<double>> vals = {{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}, {7.0, 8.0, 9.0}};
    wf->setData(std::move(keys), std::move(vals));

    // effectiveOffset is private, but we can verify via getValueRange
    // Component 0: offset=0, Component 1: offset=10, Component 2: offset=20
    bool found = false;
    QCPRange range = wf->getValueRange(found);
    QVERIFY(found);
    // With normalization on (default), margin = gain (1.0)
    // offsets: 0, 10, 20 → range should be [-1, 21]
    QCOMPARE(range.lower, -1.0);
    QCOMPARE(range.upper, 21.0);
}

void TestWaterfall::customOffset()
{
    auto* wf = new QCPWaterfallGraph(mPlot->xAxis, mPlot->yAxis);
    wf->setOffsetMode(QCPWaterfallGraph::omCustom);
    wf->setOffsets({50.0, 100.0, 150.0});

    std::vector<double> keys = {0.0, 1.0};
    std::vector<std::vector<double>> vals = {{1.0, -1.0}, {2.0, -2.0}, {0.5, -0.5}};
    wf->setData(std::move(keys), std::move(vals));

    bool found = false;
    QCPRange range = wf->getValueRange(found);
    QVERIFY(found);
    // offsets: 50, 100, 150 → margin = 1.0 (normalized + gain=1)
    QCOMPARE(range.lower, 49.0);
    QCOMPARE(range.upper, 151.0);
}

void TestWaterfall::customOffsetFallback()
{
    auto* wf = new QCPWaterfallGraph(mPlot->xAxis, mPlot->yAxis);
    wf->setOffsetMode(QCPWaterfallGraph::omCustom);
    wf->setOffsets({50.0});  // Only 1 offset for 3 components
    wf->setUniformSpacing(10.0);

    std::vector<double> keys = {0.0, 1.0};
    std::vector<std::vector<double>> vals = {{1.0, -1.0}, {2.0, -2.0}, {0.5, -0.5}};
    wf->setData(std::move(keys), std::move(vals));

    bool found = false;
    QCPRange range = wf->getValueRange(found);
    QVERIFY(found);
    // Component 0: offset=50 (custom), Component 1: offset=10 (fallback 1*10),
    // Component 2: offset=20 (fallback 2*10)
    // min offset=10, max offset=50, margin=1.0
    QCOMPARE(range.lower, 9.0);
    QCOMPARE(range.upper, 51.0);
}

void TestWaterfall::normalizationFactors()
{
    auto* wf = new QCPWaterfallGraph(mPlot->xAxis, mPlot->yAxis);
    wf->setOffsetMode(QCPWaterfallGraph::omUniform);
    wf->setUniformSpacing(100.0);
    wf->setNormalize(true);
    wf->setGain(10.0);

    // Component 0 has max|value|=5, Component 1 has max|value|=2
    std::vector<double> keys = {0.0, 1.0, 2.0};
    std::vector<std::vector<double>> vals = {{5.0, -3.0, 1.0}, {1.0, 2.0, -1.0}};
    wf->setData(std::move(keys), std::move(vals));

    // Force adapter update via draw
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    // Verify via adapter's valueAt: offset + raw * (1/maxAbs) * gain
    // Component 0, index 0: 0 + 5.0 * (1/5) * 10 = 10.0
    // Component 1, index 1: 100 + 2.0 * (1/2) * 10 = 110.0
    // We can't access the adapter directly, but we can verify the
    // displayed range: offsets 0, 100; margin = gain = 10
    bool found = false;
    QCPRange range = wf->getValueRange(found);
    QVERIFY(found);
    QCOMPARE(range.lower, -10.0);
    QCOMPARE(range.upper, 110.0);
}

void TestWaterfall::normalizationDisabled()
{
    auto* wf = new QCPWaterfallGraph(mPlot->xAxis, mPlot->yAxis);
    wf->setOffsetMode(QCPWaterfallGraph::omUniform);
    wf->setUniformSpacing(100.0);
    wf->setNormalize(false);
    wf->setGain(1.0);

    std::vector<double> keys = {0.0, 1.0};
    std::vector<std::vector<double>> vals = {{5.0, -3.0}, {1.0, -1.0}};
    wf->setData(std::move(keys), std::move(vals));

    bool found = false;
    QCPRange range = wf->getValueRange(found);
    QVERIFY(found);
    // offsets: 0, 100; maxRawAmplitude=5; margin=5*1=5
    QCOMPARE(range.lower, -5.0);
    QCOMPARE(range.upper, 105.0);
}

void TestWaterfall::invalidateNormalization()
{
    auto* wf = new QCPWaterfallGraph(mPlot->xAxis, mPlot->yAxis);
    wf->setOffsetMode(QCPWaterfallGraph::omCustom);
    wf->setOffsets({0.0});
    wf->setNormalize(true);
    wf->setGain(1.0);

    // Use span-based data that we can mutate
    auto keys = std::make_shared<std::vector<double>>(std::vector<double>{0.0, 1.0});
    auto vals0 = std::make_shared<std::vector<double>>(std::vector<double>{2.0, -2.0});

    wf->viewData(std::span<const double>(*keys),
                 {std::span<const double>(*vals0)});

    // Force adapter update: normFactor = 1/2 = 0.5
    // dataMainValue(0) = offset(0) + 2.0 * 0.5 * 1.0 = 1.0
    mPlot->replot(QCustomPlot::rpImmediateRefresh);
    QCOMPARE(wf->dataMainValue(0), 1.0);

    // Mutate data in place — without invalidation, old normFactor (0.5) is used
    (*vals0)[0] = 10.0;
    (*vals0)[1] = -10.0;

    // Invalidate and replot to recompute normFactor = 1/10 = 0.1
    wf->invalidateNormalization();
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    // dataMainValue(0) = 0 + 10.0 * 0.1 * 1.0 = 1.0 (still 1.0 but via new factor)
    QCOMPARE(wf->dataMainValue(0), 1.0);

    // Verify via dataMainValue(1): 0 + (-10.0) * 0.1 * 1.0 = -1.0
    // (index 1 should also be normalized correctly)
    // We can verify the value range margin is still gain=1
    bool found = false;
    QCPRange range = wf->getValueRange(found);
    QVERIFY(found);
    QCOMPARE(range.lower, -1.0);
    QCOMPARE(range.upper, 1.0);
}

void TestWaterfall::adapterValueAt()
{
    auto* wf = new QCPWaterfallGraph(mPlot->xAxis, mPlot->yAxis);
    wf->setOffsetMode(QCPWaterfallGraph::omCustom);
    wf->setOffsets({50.0, 100.0});
    wf->setNormalize(false);
    wf->setGain(2.0);

    std::vector<double> keys = {0.0, 1.0};
    std::vector<std::vector<double>> vals = {{3.0, -1.0}, {5.0, 2.0}};
    wf->setData(std::move(keys), std::move(vals));

    // Force adapter update
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    // Use interface1D to check transformed values (dataMainValue uses component 0)
    // Component 0, index 0: 50 + 3.0 * 1.0 * 2.0 = 56.0
    double v = wf->dataMainValue(0);
    QCOMPARE(v, 56.0);
}

void TestWaterfall::singleComponentValueRange()
{
    auto* wf = new QCPWaterfallGraph(mPlot->xAxis, mPlot->yAxis);
    wf->setOffsetMode(QCPWaterfallGraph::omCustom);
    wf->setOffsets({0.0});
    wf->setNormalize(false);
    wf->setGain(1.0);

    std::vector<double> keys = {0.0, 1.0, 2.0};
    std::vector<std::vector<double>> vals = {{-3.0, 0.0, 5.0}};
    wf->setData(std::move(keys), std::move(vals));

    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    // With offset=0, gain=1, no normalization, transformed = raw
    // Range should be [-3, 5]
    bool found = false;
    QCPRange range = wf->getValueRange(found);
    QVERIFY(found);
    // margin = maxAmp * gain = 5 * 1 = 5
    // lo = 0 - 5 = -5, hi = 0 + 5 = 5
    QCOMPARE(range.lower, -5.0);
    QCOMPARE(range.upper, 5.0);
}

void TestWaterfall::getValueRangeNormalized()
{
    auto* wf = new QCPWaterfallGraph(mPlot->xAxis, mPlot->yAxis);
    wf->setOffsetMode(QCPWaterfallGraph::omCustom);
    wf->setOffsets({10.0, 20.0});
    wf->setNormalize(true);
    wf->setGain(3.0);

    std::vector<double> keys = {0.0, 1.0};
    std::vector<std::vector<double>> vals = {{1.0, -1.0}, {2.0, -2.0}};
    wf->setData(std::move(keys), std::move(vals));

    bool found = false;
    QCPRange range = wf->getValueRange(found);
    QVERIFY(found);
    // offsets: 10, 20; margin = gain = 3
    QCOMPARE(range.lower, 7.0);
    QCOMPARE(range.upper, 23.0);
}

void TestWaterfall::getValueRangeUnnormalized()
{
    auto* wf = new QCPWaterfallGraph(mPlot->xAxis, mPlot->yAxis);
    wf->setOffsetMode(QCPWaterfallGraph::omCustom);
    wf->setOffsets({10.0, 20.0});
    wf->setNormalize(false);
    wf->setGain(2.0);

    std::vector<double> keys = {0.0, 1.0};
    std::vector<std::vector<double>> vals = {{3.0, -1.0}, {5.0, -5.0}};
    wf->setData(std::move(keys), std::move(vals));

    bool found = false;
    QCPRange range = wf->getValueRange(found);
    QVERIFY(found);
    // offsets: 10, 20; maxAmp=5; margin=5*2=10
    QCOMPARE(range.lower, 0.0);   // 10 - 10
    QCOMPARE(range.upper, 30.0);  // 20 + 10
}

void TestWaterfall::getValueRangeSignDomain()
{
    auto* wf = new QCPWaterfallGraph(mPlot->xAxis, mPlot->yAxis);
    wf->setOffsetMode(QCPWaterfallGraph::omCustom);
    wf->setOffsets({10.0, 20.0});
    wf->setNormalize(true);
    wf->setGain(3.0);

    std::vector<double> keys = {0.0, 1.0};
    std::vector<std::vector<double>> vals = {{1.0, -1.0}, {2.0, -2.0}};
    wf->setData(std::move(keys), std::move(vals));

    // Full range: [7, 23]
    bool found = false;

    // Positive only
    QCPRange posRange = wf->getValueRange(found, QCP::sdPositive);
    QVERIFY(found);
    QCOMPARE(posRange.lower, 7.0);  // already positive
    QCOMPARE(posRange.upper, 23.0);

    // Negative only — entire range is positive, so should not be found
    QCPRange negRange = wf->getValueRange(found, QCP::sdNegative);
    QVERIFY(!found);
}

void TestWaterfall::drawDoesNotCrash()
{
    auto* wf = new QCPWaterfallGraph(mPlot->xAxis, mPlot->yAxis);
    wf->setOffsetMode(QCPWaterfallGraph::omCustom);
    wf->setOffsets({50.0, 100.0, 150.0});
    wf->setNormalize(true);
    wf->setGain(15.0);

    std::vector<double> keys = {0.0, 1.0, 2.0, 3.0, 4.0};
    std::vector<std::vector<double>> vals = {
        {1.0, 0.5, -0.3, 0.8, -0.2},
        {0.3, -0.7, 1.0, -0.5, 0.1},
        {-0.5, 0.9, 0.2, -1.0, 0.4}
    };
    wf->setData(std::move(keys), std::move(vals));

    // Should not crash
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    // Also test with empty data
    auto* wf2 = new QCPWaterfallGraph(mPlot->xAxis, mPlot->yAxis);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);
}
```

- [ ] **Step 3: Register tests in autotest.cpp and meson.build**

In `tests/auto/meson.build`, add to `test_srcs` (after multigraph entry):

```
'test-waterfall/test-waterfall.cpp',
```

Add to `test_headers`:

```
'test-waterfall/test-waterfall.h',
```

Then register in `tests/auto/autotest.cpp`. Check how other tests are registered:

```cpp
#include "test-waterfall/test-waterfall.h"
// and at the end of main(), add:
QCPTEST(TestWaterfall);
```

(Follow the exact same `QCPTEST(ClassName);` pattern as the other tests in `autotest.cpp`.)

- [ ] **Step 4: Create test directory**

```bash
mkdir -p tests/auto/test-waterfall
```

- [ ] **Step 5: Build and run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All tests pass, including the new waterfall tests.

- [ ] **Step 6: Commit**

```bash
git add tests/auto/test-waterfall/ tests/auto/meson.build tests/auto/autotest.cpp
git commit -m "test: add unit tests for QCPWaterfallGraph"
```

---

## Chunk 3: Manual Test

### Task 4: Add realistic seismograph manual test

**Files:**
- Modify: `tests/manual/mainwindow.h:59` (add declaration)
- Modify: `tests/manual/mainwindow.cpp` (add implementation + activate)

- [ ] **Step 1: Add declaration to mainwindow.h**

After line 59 (`setupMultiGraphComparisonTest`), add:

```cpp
void setupWaterfallTest(QCustomPlot *customPlot);
```

- [ ] **Step 2: Add implementation to mainwindow.cpp**

Add at the end of the file (before the closing brace of the last method, or simply at the end):

```cpp
void MainWindow::setupWaterfallTest(QCustomPlot *customPlot)
{
    // Seismograph record section: 8 stations at varying distances
    const QVector<double> stationDistances = {20, 45, 70, 95, 120, 150, 185, 220};
    const QStringList stationNames = {
        "Station A", "Station B", "Station C", "Station D",
        "Station E", "Station F", "Station G", "Station H"
    };
    const double sampleRate = 100.0;  // Hz
    const double duration = 40.0;     // seconds
    const double pWaveVelocity = 6.0; // km/s
    const int nSamples = static_cast<int>(duration * sampleRate);

    // Generate time axis
    std::vector<double> keys(nSamples);
    for (int i = 0; i < nSamples; ++i)
        keys[i] = i / sampleRate;

    // Generate synthetic seismograms
    std::vector<std::vector<double>> vals(stationDistances.size());
    for (int s = 0; s < stationDistances.size(); ++s) {
        vals[s].resize(nSamples);
        double arrivalTime = stationDistances[s] / pWaveVelocity;
        double freq = 2.0 + 0.5 * s;  // slightly different freq per station
        double decay = 1.5;

        for (int i = 0; i < nSamples; ++i) {
            double t = keys[i];
            double dt = t - arrivalTime;
            if (dt < 0) {
                // Pre-arrival noise
                vals[s][i] = 0.02 * sin(13.7 * t + s) * cos(7.3 * t);
            } else {
                // Damped sinusoid + noise
                double signal = exp(-decay * dt) * sin(2.0 * std::numbers::pi * freq * dt);
                double noise = 0.02 * sin(13.7 * t + s) * cos(7.3 * t);
                vals[s][i] = signal + noise;
            }
        }
    }

    auto* wf = new QCPWaterfallGraph(customPlot->xAxis, customPlot->yAxis);
    wf->setData(std::move(keys), std::move(vals));
    wf->setOffsetMode(QCPWaterfallGraph::omCustom);
    wf->setOffsets(stationDistances);
    wf->setNormalize(true);
    wf->setGain(15.0);
    wf->setComponentNames(stationNames);

    // Configure Y-axis with station distance ticks
    auto ticker = QSharedPointer<QCPAxisTickerText>(new QCPAxisTickerText);
    for (int i = 0; i < stationDistances.size(); ++i)
        ticker->addTick(stationDistances[i], QString("%1 km").arg(stationDistances[i]));
    customPlot->yAxis->setTicker(ticker);
    customPlot->yAxis->setLabel("Epicentral Distance (km)");
    customPlot->xAxis->setLabel("Time (s)");
    customPlot->xAxis->setRange(0, duration);

    // Let y-axis auto-range from the waterfall
    customPlot->yAxis->rescale();

    customPlot->legend->setVisible(true);
    customPlot->replot();
}
```

- [ ] **Step 3: Activate the test in the constructor**

In `mainwindow.cpp`, comment out line 44 (`setupMultiGraphComparisonTest`) and add:

```cpp
setupWaterfallTest(mCustomPlot);
```

- [ ] **Step 4: Build and run manually**

Run: `meson compile -C build && ./build/tests/manual/manual-test`
Expected: Window shows 8 seismograph traces offset by station distance, with P-wave arrivals visible as damped sinusoids at progressively later times for more distant stations.

- [ ] **Step 5: Commit**

```bash
git add tests/manual/mainwindow.h tests/manual/mainwindow.cpp
git commit -m "test: add seismograph waterfall manual test"
```

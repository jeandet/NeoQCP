# QCPColorMap2 + Async Resampler Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add QCPColorMap2 — a colormap plottable that accepts raw (x, y, z) arrays via the QCPGraph2 data source pattern, auto-detects Y dimensionality, and resamples to screen resolution in a background thread.

**Architecture:** Three new layers (2D data source, shared resampler scheduler, colormap plottable) built on top of the existing QCPGraph2 data source infrastructure. The resampler scheduler is designed for future reuse by QCPGraph2. The resampling algorithm is ported from SciQLopPlots' ColormapResampler.

**Tech Stack:** C++20, Qt6 (QThread, QMutex, signals/slots), Meson build system

**Spec:** `docs/specs/2026-03-13-colormap2-async-resampler-design.md`

---

## File Map

| File | Action | Responsibility |
|------|--------|---------------|
| `src/datasource/abstract-datasource-2d.h` | Create | `QCPAbstractDataSource2D` virtual interface |
| `src/datasource/soa-datasource-2d.h` | Create | `QCPSoADataSource2D<XC,YC,ZC>` template (header-only) |
| `src/datasource/algorithms-2d.h` | Create | `qcp::algo2d::` range/search templates (header-only) |
| `src/datasource/resample.h` | Create | `qcp::algo2d::resample()` declaration |
| `src/datasource/resample.cpp` | Create | `qcp::algo2d::resample()` implementation |
| `src/datasource/resampler-scheduler.h` | Create | `QCPResamplerScheduler` declaration |
| `src/datasource/resampler-scheduler.cpp` | Create | `QCPResamplerScheduler` implementation |
| `src/plottables/colormap-resampler.h` | Create | `QCPColormapResampler` declaration |
| `src/plottables/colormap-resampler.cpp` | Create | `QCPColormapResampler` implementation |
| `src/plottables/plottable-colormap2.h` | Create | `QCPColorMap2` declaration + template methods |
| `src/plottables/plottable-colormap2.cpp` | Create | `QCPColorMap2` non-template implementation |
| `src/qcp.h` | Modify | Add includes for new headers |
| `meson.build` | Modify | Add new `.cpp` files to lib sources, new headers to MOC list |
| `tests/auto/test-datasource2d/test-datasource2d.h` | Create | Test class declaration |
| `tests/auto/test-datasource2d/test-datasource2d.cpp` | Create | Data source 2D + algorithm + resampler + colormap2 tests |
| `tests/auto/autotest.cpp` | Modify | Add `#include` and `QCPTEST` for new test class |
| `tests/auto/meson.build` | Modify | Add new test files |
| `tests/manual/mainwindow.h` | Modify | Add `setupColorMap2Test` declaration |
| `tests/manual/mainwindow.cpp` | Modify | Add `setupColorMap2Test` implementation |

---

## Chunk 1: Data Source Infrastructure

### Task 1: QCPAbstractDataSource2D

**Files:**
- Create: `src/datasource/abstract-datasource-2d.h`

- [ ] **Step 1: Create the abstract interface**

```cpp
// src/datasource/abstract-datasource-2d.h
#pragma once
#include "abstract-datasource.h" // for IndexableNumericRange concept, QCPRange
#include "global.h"              // for QCP::SignDomain

class QCPAbstractDataSource2D
{
public:
    virtual ~QCPAbstractDataSource2D() = default;

    virtual int xSize() const = 0;
    virtual int ySize() const = 0;
    virtual bool yIs2D() const = 0;

    virtual double xAt(int i) const = 0;
    virtual double yAt(int i, int j) const = 0;
    virtual double zAt(int i, int j) const = 0;

    virtual QCPRange xRange(bool& found, QCP::SignDomain sd = QCP::sdBoth) const = 0;
    virtual QCPRange yRange(bool& found, QCP::SignDomain sd = QCP::sdBoth) const = 0;
    virtual QCPRange zRange(bool& found, int xBegin = 0, int xEnd = -1) const = 0;

    virtual int findXBegin(double sortKey) const = 0;
    virtual int findXEnd(double sortKey) const = 0;
};
```

- [ ] **Step 2: Verify it compiles**

Run: `meson compile -C build 2>&1 | tail -20`
Expected: builds successfully (header-only, not yet included anywhere)

- [ ] **Step 3: Commit**

```bash
git add src/datasource/abstract-datasource-2d.h
git commit -m "feat: add QCPAbstractDataSource2D interface"
```

### Task 2: 2D Algorithms

**Files:**
- Create: `src/datasource/algorithms-2d.h`
- Create: `tests/auto/test-datasource2d/test-datasource2d.h`
- Create: `tests/auto/test-datasource2d/test-datasource2d.cpp`
- Modify: `tests/auto/autotest.cpp`
- Modify: `tests/auto/meson.build`

- [ ] **Step 1: Write failing tests for findXBegin/findXEnd and range queries**

Create test header `tests/auto/test-datasource2d/test-datasource2d.h`:

```cpp
#pragma once
#include <QObject>

class QCustomPlot;

class TestDataSource2D : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    // Algorithm tests
    void algo2dFindXBegin();
    void algo2dFindXEnd();
    void algo2dXRange();
    void algo2dYRange();
    void algo2dZRange();

    // SoA 2D data source tests
    void soa2dUniformY();
    void soa2dVariableY();
    void soa2dSpanView();
    void soa2dMixedTypes();
    void soa2dRangeQueries();

    // Resampler scheduler tests
    void schedulerSubmitIdle();
    void schedulerCoalescing();

    // Resample algorithm tests
    void resampleUniformGrid();
    void resampleVariableY();
    void resampleGapDetection();
    void resampleLogY();
    void resampleEmptyBins();

    // QCPColormapResampler tests
    void resamplerAsyncRoundTrip();

    // QCPColorMap2 integration tests
    void colormap2Creation();
    void colormap2SetDataOwning();
    void colormap2ViewData();
    void colormap2Render();
    void colormap2AxisRescale();

private:
    QCustomPlot* mPlot = nullptr;
};
```

Create test cpp `tests/auto/test-datasource2d/test-datasource2d.cpp` (start with algorithm tests only):

```cpp
#include "test-datasource2d.h"
#include <qcustomplot.h>
#include <datasource/algorithms-2d.h>
#include <QtTest/QtTest>

void TestDataSource2D::init()
{
    mPlot = new QCustomPlot();
    mPlot->resize(400, 300);
}

void TestDataSource2D::cleanup()
{
    delete mPlot;
    mPlot = nullptr;
}

void TestDataSource2D::algo2dFindXBegin()
{
    std::vector<double> x = {1.0, 2.0, 3.0, 4.0, 5.0};
    QCOMPARE(qcp::algo2d::findXBegin(x, 2.5), 1); // element before 2.5
    QCOMPARE(qcp::algo2d::findXBegin(x, 1.0), 0);
    QCOMPARE(qcp::algo2d::findXBegin(x, 0.5), 0);
    QCOMPARE(qcp::algo2d::findXBegin(x, 5.5), 4);
}

void TestDataSource2D::algo2dFindXEnd()
{
    std::vector<double> x = {1.0, 2.0, 3.0, 4.0, 5.0};
    QCOMPARE(qcp::algo2d::findXEnd(x, 3.5), 4); // element after 3.5
    QCOMPARE(qcp::algo2d::findXEnd(x, 5.0), 5);
    QCOMPARE(qcp::algo2d::findXEnd(x, 0.5), 1);
}

void TestDataSource2D::algo2dXRange()
{
    std::vector<double> x = {1.0, 3.0, 5.0};
    bool found = false;
    auto range = qcp::algo2d::xRange(x, found);
    QVERIFY(found);
    QCOMPARE(range.lower, 1.0);
    QCOMPARE(range.upper, 5.0);
}

void TestDataSource2D::algo2dYRange()
{
    std::vector<double> y = {10.0, 20.0, 30.0};
    bool found = false;
    auto range = qcp::algo2d::yRange(y, found);
    QVERIFY(found);
    QCOMPARE(range.lower, 10.0);
    QCOMPARE(range.upper, 30.0);
}

void TestDataSource2D::algo2dZRange()
{
    // 3x2 grid: x=[0,1,2], ySize=2, z=[1,2, 3,4, 5,6]
    std::vector<double> z = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    bool found = false;

    // Full range
    auto range = qcp::algo2d::zRange(z, 2, found);
    QVERIFY(found);
    QCOMPARE(range.lower, 1.0);
    QCOMPARE(range.upper, 6.0);

    // Visible x window [1, 3) → rows 1 and 2 → z values {3,4,5,6}
    found = false;
    range = qcp::algo2d::zRange(z, 2, found, 1, 3);
    QVERIFY(found);
    QCOMPARE(range.lower, 3.0);
    QCOMPARE(range.upper, 6.0);
}

// Stubs for tests not yet implemented — will be filled in later tasks
void TestDataSource2D::soa2dUniformY() { }
void TestDataSource2D::soa2dVariableY() { }
void TestDataSource2D::soa2dSpanView() { }
void TestDataSource2D::soa2dMixedTypes() { }
void TestDataSource2D::soa2dRangeQueries() { }
void TestDataSource2D::schedulerSubmitIdle() { }
void TestDataSource2D::schedulerCoalescing() { }
void TestDataSource2D::resampleUniformGrid() { }
void TestDataSource2D::resampleVariableY() { }
void TestDataSource2D::resampleGapDetection() { }
void TestDataSource2D::resampleLogY() { }
void TestDataSource2D::resampleEmptyBins() { }
void TestDataSource2D::resamplerAsyncRoundTrip() { }
void TestDataSource2D::colormap2Creation() { }
void TestDataSource2D::colormap2SetDataOwning() { }
void TestDataSource2D::colormap2ViewData() { }
void TestDataSource2D::colormap2Render() { }
void TestDataSource2D::colormap2AxisRescale() { }
```

Add to `tests/auto/autotest.cpp`:
```cpp
#include "test-datasource2d/test-datasource2d.h"
// ... and in main():
QCPTEST(TestDataSource2D);
```

Add to `tests/auto/meson.build` — append to `test_srcs` and `test_headers`:
```
'test-datasource2d/test-datasource2d.cpp'
'test-datasource2d/test-datasource2d.h'
```

- [ ] **Step 2: Run tests to verify algorithm tests fail (functions not defined)**

Run: `meson compile -C build 2>&1 | tail -20`
Expected: FAIL — `qcp::algo2d` not found

- [ ] **Step 3: Implement algorithms-2d.h**

```cpp
// src/datasource/algorithms-2d.h
#pragma once
#include "abstract-datasource.h" // for IndexableNumericRange concept
#include "algorithms.h"          // for qcp::algo::findBegin/findEnd
#include <cmath>
#include <vector>

class QCPRange;

namespace qcp::algo2d {

template <IndexableNumericRange XC>
int findXBegin(const XC& x, double sortKey)
{
    return qcp::algo::findBegin(x, sortKey, true);
}

template <IndexableNumericRange XC>
int findXEnd(const XC& x, double sortKey)
{
    return qcp::algo::findEnd(x, sortKey, true);
}

template <IndexableNumericRange XC>
QCPRange xRange(const XC& x, bool& found, QCP::SignDomain sd = QCP::sdBoth)
{
    return qcp::algo::keyRange(x, found, sd);
}

template <IndexableNumericRange YC>
QCPRange yRange(const YC& y, bool& found, QCP::SignDomain sd = QCP::sdBoth)
{
    return qcp::algo::keyRange(y, found, sd);
}

template <IndexableNumericRange ZC>
QCPRange zRange(const ZC& z, int ySize, bool& found, int xBegin = 0, int xEnd = -1)
{
    found = false;
    if (std::ranges::empty(z) || ySize <= 0)
        return {};

    int totalRows = static_cast<int>(std::ranges::size(z)) / ySize;
    if (xEnd < 0)
        xEnd = totalRows;
    xBegin = std::max(0, xBegin);
    xEnd = std::min(xEnd, totalRows);

    double minVal = std::numeric_limits<double>::max();
    double maxVal = std::numeric_limits<double>::lowest();

    for (int i = xBegin; i < xEnd; ++i)
    {
        for (int j = 0; j < ySize; ++j)
        {
            double v = static_cast<double>(z[i * ySize + j]);
            if (std::isnan(v))
                continue;
            if (v < minVal) minVal = v;
            if (v > maxVal) maxVal = v;
            found = true;
        }
    }

    return found ? QCPRange(minVal, maxVal) : QCPRange();
}

} // namespace qcp::algo2d
```

- [ ] **Step 4: Run tests to verify algorithm tests pass**

Run: `meson test -C build --print-errorlogs 2>&1 | tail -30`
Expected: algo2d* tests PASS, stub tests PASS (empty)

- [ ] **Step 5: Commit**

```bash
git add src/datasource/algorithms-2d.h \
        tests/auto/test-datasource2d/test-datasource2d.h \
        tests/auto/test-datasource2d/test-datasource2d.cpp \
        tests/auto/autotest.cpp \
        tests/auto/meson.build
git commit -m "feat: add 2D algorithms (findX, ranges) with tests"
```

### Task 3: QCPSoADataSource2D

**Files:**
- Create: `src/datasource/soa-datasource-2d.h`
- Modify: `tests/auto/test-datasource2d/test-datasource2d.cpp`

- [ ] **Step 1: Write failing tests for SoA 2D data source**

Replace the stub test methods in `test-datasource2d.cpp` with real tests:

```cpp
// Add includes:
#include <datasource/soa-datasource-2d.h>
#include <span>

void TestDataSource2D::soa2dUniformY()
{
    // 3x2 grid: x=[1,2,3], y=[10,20] (uniform), z=[1,2,3,4,5,6]
    std::vector<double> x = {1.0, 2.0, 3.0};
    std::vector<double> y = {10.0, 20.0};
    std::vector<double> z = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};

    QCPSoADataSource2D src(std::move(x), std::move(y), std::move(z));

    QCOMPARE(src.xSize(), 3);
    QCOMPARE(src.ySize(), 2);
    QVERIFY(!src.yIs2D());
    QCOMPARE(src.xAt(0), 1.0);
    QCOMPARE(src.xAt(2), 3.0);
    QCOMPARE(src.yAt(0, 0), 10.0);  // y is 1D, i ignored
    QCOMPARE(src.yAt(2, 1), 20.0);  // same y regardless of i
    QCOMPARE(src.zAt(0, 0), 1.0);
    QCOMPARE(src.zAt(2, 1), 6.0);
}

void TestDataSource2D::soa2dVariableY()
{
    // 2x3 grid: x=[1,2], y=[10,20,30, 11,21,31] (2D), z=[1,2,3,4,5,6]
    std::vector<double> x = {1.0, 2.0};
    std::vector<double> y = {10.0, 20.0, 30.0, 11.0, 21.0, 31.0};
    std::vector<double> z = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};

    QCPSoADataSource2D src(std::move(x), std::move(y), std::move(z));

    QCOMPARE(src.xSize(), 2);
    QCOMPARE(src.ySize(), 3);
    QVERIFY(src.yIs2D());
    QCOMPARE(src.yAt(0, 0), 10.0);
    QCOMPARE(src.yAt(0, 2), 30.0);
    QCOMPARE(src.yAt(1, 0), 11.0);  // different y for different x row
    QCOMPARE(src.yAt(1, 2), 31.0);
}

void TestDataSource2D::soa2dSpanView()
{
    double x[] = {1.0, 2.0, 3.0};
    float y[] = {10.0f, 20.0f};
    double z[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};

    QCPSoADataSource2D<std::span<const double>, std::span<const float>, std::span<const double>>
        src(std::span{x}, std::span{y}, std::span{z});

    QCOMPARE(src.xSize(), 3);
    QCOMPARE(src.ySize(), 2);
    QVERIFY(!src.yIs2D());
    QCOMPARE(src.yAt(0, 0), 10.0);
    QCOMPARE(src.zAt(1, 0), 3.0);
}

void TestDataSource2D::soa2dMixedTypes()
{
    std::vector<double> x = {1.0, 2.0};
    std::vector<float> y = {10.0f, 20.0f, 30.0f};
    std::vector<int> z = {1, 2, 3, 4, 5, 6};

    QCPSoADataSource2D src(std::move(x), std::move(y), std::move(z));

    QCOMPARE(src.xSize(), 2);
    QCOMPARE(src.ySize(), 3);
    QVERIFY(!src.yIs2D());
    QCOMPARE(src.zAt(0, 0), 1.0);
    QCOMPARE(src.zAt(1, 2), 6.0);
}

void TestDataSource2D::soa2dRangeQueries()
{
    std::vector<double> x = {1.0, 3.0, 5.0};
    std::vector<double> y = {10.0, 20.0};
    std::vector<double> z = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};

    QCPSoADataSource2D src(std::move(x), std::move(y), std::move(z));

    bool found = false;
    auto xr = src.xRange(found);
    QVERIFY(found);
    QCOMPARE(xr.lower, 1.0);
    QCOMPARE(xr.upper, 5.0);

    found = false;
    auto yr = src.yRange(found);
    QVERIFY(found);
    QCOMPARE(yr.lower, 10.0);
    QCOMPARE(yr.upper, 20.0);

    found = false;
    auto zr = src.zRange(found);
    QVERIFY(found);
    QCOMPARE(zr.lower, 1.0);
    QCOMPARE(zr.upper, 6.0);

    QCOMPARE(src.findXBegin(2.0), 0); // with expansion
    QCOMPARE(src.findXEnd(4.0), 3);   // with expansion
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `meson compile -C build 2>&1 | tail -20`
Expected: FAIL — `QCPSoADataSource2D` not found

- [ ] **Step 3: Implement soa-datasource-2d.h**

```cpp
// src/datasource/soa-datasource-2d.h
#pragma once
#include "abstract-datasource-2d.h"
#include "algorithms-2d.h"
#include <ranges>
#include <span>

template <IndexableNumericRange XC, IndexableNumericRange YC, IndexableNumericRange ZC>
class QCPSoADataSource2D final : public QCPAbstractDataSource2D
{
public:
    using X = std::ranges::range_value_t<XC>;
    using Y = std::ranges::range_value_t<YC>;
    using Z = std::ranges::range_value_t<ZC>;

    QCPSoADataSource2D(XC x, YC y, ZC z)
        : mX(std::move(x)), mY(std::move(y)), mZ(std::move(z))
    {
        auto nx = std::ranges::size(mX);
        auto ny = std::ranges::size(mY);
        auto nz = std::ranges::size(mZ);
        Q_ASSERT(nx > 0 && nz > 0 && nz % nx == 0);
        mYSize = static_cast<int>(nz / nx);
        mYIs2D = (ny == nz);
        Q_ASSERT(ny == nz || ny == static_cast<decltype(ny)>(mYSize));
    }

    const XC& x() const { return mX; }
    const YC& y() const { return mY; }
    const ZC& z() const { return mZ; }

    int xSize() const override { return static_cast<int>(std::ranges::size(mX)); }
    int ySize() const override { return mYSize; }
    bool yIs2D() const override { return mYIs2D; }

    double xAt(int i) const override { return static_cast<double>(mX[i]); }

    double yAt(int i, int j) const override
    {
        return mYIs2D ? static_cast<double>(mY[i * mYSize + j])
                      : static_cast<double>(mY[j]);
    }

    double zAt(int i, int j) const override
    {
        return static_cast<double>(mZ[i * mYSize + j]);
    }

    QCPRange xRange(bool& found, QCP::SignDomain sd = QCP::sdBoth) const override
    {
        return qcp::algo2d::xRange(mX, found, sd);
    }

    QCPRange yRange(bool& found, QCP::SignDomain sd = QCP::sdBoth) const override
    {
        return qcp::algo2d::yRange(mY, found, sd);
    }

    QCPRange zRange(bool& found, int xBegin = 0, int xEnd = -1) const override
    {
        return qcp::algo2d::zRange(mZ, mYSize, found, xBegin, xEnd);
    }

    int findXBegin(double sortKey) const override
    {
        return qcp::algo2d::findXBegin(mX, sortKey);
    }

    int findXEnd(double sortKey) const override
    {
        return qcp::algo2d::findXEnd(mX, sortKey);
    }

private:
    XC mX;
    YC mY;
    ZC mZ;
    int mYSize;
    bool mYIs2D;
};
```

- [ ] **Step 4: Run tests to verify SoA 2D tests pass**

Run: `meson test -C build --print-errorlogs 2>&1 | tail -30`
Expected: soa2d* tests PASS

- [ ] **Step 5: Commit**

```bash
git add src/datasource/soa-datasource-2d.h \
        tests/auto/test-datasource2d/test-datasource2d.cpp
git commit -m "feat: add QCPSoADataSource2D with auto Y-dimensionality detection"
```

---

## Chunk 2: Resampler Scheduler + Resample Algorithm

### Task 4: QCPResamplerScheduler

**Files:**
- Create: `src/datasource/resampler-scheduler.h`
- Create: `src/datasource/resampler-scheduler.cpp`
- Modify: `meson.build` — add `src/datasource/resampler-scheduler.cpp` to library sources
- Modify: `tests/auto/test-datasource2d/test-datasource2d.cpp`

- [ ] **Step 1: Write failing tests for scheduler**

Replace scheduler stubs in `test-datasource2d.cpp`:

```cpp
// Add includes:
#include <datasource/resampler-scheduler.h>
#include <QSignalSpy>
#include <QElapsedTimer>

void TestDataSource2D::schedulerSubmitIdle()
{
    QCPResamplerScheduler scheduler;
    scheduler.start();

    bool ran = false;
    QEventLoop loop;
    scheduler.submit([&ran, &loop] {
        ran = true;
        QMetaObject::invokeMethod(&loop, "quit", Qt::QueuedConnection);
    });
    loop.exec();

    QVERIFY(ran);
    scheduler.stop();
}

void TestDataSource2D::schedulerCoalescing()
{
    QCPResamplerScheduler scheduler;
    scheduler.start();

    // Submit a slow job, then two fast ones while it's busy.
    // Only the last fast one should run.
    std::atomic<int> lastValue{0};
    int completionCount = 0;
    QEventLoop loop;

    scheduler.submit([&] {
        QThread::msleep(50); // slow job
        lastValue.store(1);
    });

    // Give the slow job time to start
    QThread::msleep(10);

    scheduler.submit([&] {
        lastValue.store(2); // should be replaced
    });

    scheduler.submit([&lastValue, &loop] {
        lastValue.store(3); // should be the one that runs
        QMetaObject::invokeMethod(&loop, "quit", Qt::QueuedConnection);
    });

    loop.exec();
    QCOMPARE(lastValue.load(), 3); // coalesced: only last ran
    scheduler.stop();
}
```

- [ ] **Step 2: Run to verify tests fail (class not defined)**

Run: `meson compile -C build 2>&1 | tail -20`
Expected: FAIL — `QCPResamplerScheduler` not found

- [ ] **Step 3: Implement resampler-scheduler.h**

```cpp
// src/datasource/resampler-scheduler.h
#pragma once
#include <QObject>
#include <QThread>
#include <QMutex>
#include <functional>

class QCPResamplerScheduler : public QObject
{
    Q_OBJECT

public:
    explicit QCPResamplerScheduler(QObject* parent = nullptr);
    ~QCPResamplerScheduler() override;

    void start();
    void stop();
    void submit(std::function<void()> work);

Q_SIGNALS:
    void workReady();

private Q_SLOTS:
    void runNext();

private:
    QThread mThread;
    QMutex mMutex;
    std::function<void()> mPending;
    bool mBusy = false;
};
```

- [ ] **Step 4: Implement resampler-scheduler.cpp**

```cpp
// src/datasource/resampler-scheduler.cpp
#include "resampler-scheduler.h"

QCPResamplerScheduler::QCPResamplerScheduler(QObject* parent)
    : QObject(parent)
{
    this->moveToThread(&mThread);
    connect(this, &QCPResamplerScheduler::workReady,
            this, &QCPResamplerScheduler::runNext,
            Qt::QueuedConnection);
}

QCPResamplerScheduler::~QCPResamplerScheduler()
{
    stop();
}

void QCPResamplerScheduler::start()
{
    mThread.start(QThread::LowPriority);
}

void QCPResamplerScheduler::stop()
{
    if (mThread.isRunning())
    {
        mThread.quit();
        mThread.wait();
    }
}

void QCPResamplerScheduler::submit(std::function<void()> work)
{
    QMutexLocker lock(&mMutex);
    mPending = std::move(work);
    if (!mBusy)
    {
        mBusy = true;
        Q_EMIT workReady();
    }
}

void QCPResamplerScheduler::runNext()
{
    std::function<void()> work;
    {
        QMutexLocker lock(&mMutex);
        work = std::move(mPending);
        mPending = nullptr;
    }

    while (work)
    {
        try { work(); } catch (...) { }

        QMutexLocker lock(&mMutex);
        work = std::move(mPending);
        mPending = nullptr;
        if (!work)
            mBusy = false;
    }
}
```

- [ ] **Step 5: Add to meson.build**

In `meson.build`, add `'src/datasource/resampler-scheduler.cpp'` to the `NeoQCP` static_library sources list and `'src/datasource/resampler-scheduler.h'` to `neoqcp_moc_headers`.

- [ ] **Step 6: Run tests to verify scheduler tests pass**

Run: `meson test -C build --print-errorlogs 2>&1 | tail -30`
Expected: scheduler* tests PASS

- [ ] **Step 7: Commit**

```bash
git add src/datasource/resampler-scheduler.h \
        src/datasource/resampler-scheduler.cpp \
        meson.build \
        tests/auto/test-datasource2d/test-datasource2d.cpp
git commit -m "feat: add QCPResamplerScheduler with request coalescing"
```

### Task 5: Resample Algorithm

**Files:**
- Modify: `src/datasource/algorithms-2d.h`
- Modify: `tests/auto/test-datasource2d/test-datasource2d.cpp`

- [ ] **Step 1: Write failing tests for resample()**

Replace resample stubs in `test-datasource2d.cpp`:

```cpp
void TestDataSource2D::resampleUniformGrid()
{
    // 5x3 uniform grid, linear ramp z = i*3 + j
    std::vector<double> x = {0.0, 1.0, 2.0, 3.0, 4.0};
    std::vector<double> y = {0.0, 1.0, 2.0};
    std::vector<double> z(15);
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 3; ++j)
            z[i * 3 + j] = i * 3.0 + j;

    QCPSoADataSource2D src(std::move(x), std::move(y), std::move(z));

    // Resample to 5x3 (same size) — should be near-identity
    auto* result = qcp::algo2d::resample(src, 0, 5, 5, 3, false, 1.5);
    QVERIFY(result != nullptr);
    QCOMPARE(result->keySize(), 5);
    QCOMPARE(result->valueSize(), 3);

    // Check corners are approximately correct
    QVERIFY(std::abs(result->cell(0, 0) - 0.0) < 1.0);
    QVERIFY(std::abs(result->cell(4, 2) - 14.0) < 2.0);

    delete result;
}

void TestDataSource2D::resampleVariableY()
{
    // 2x2 variable-y grid
    std::vector<double> x = {0.0, 1.0};
    std::vector<double> y = {10.0, 20.0, 15.0, 25.0}; // 2D y
    std::vector<double> z = {1.0, 2.0, 3.0, 4.0};

    QCPSoADataSource2D src(std::move(x), std::move(y), std::move(z));
    QVERIFY(src.yIs2D());

    auto* result = qcp::algo2d::resample(src, 0, 2, 2, 2, false, 1.5);
    QVERIFY(result != nullptr);
    QVERIFY(result->keySize() > 0);
    QVERIFY(result->valueSize() > 0);
    delete result;
}

void TestDataSource2D::resampleGapDetection()
{
    // x with a gap: [0, 1, 2, 10, 11, 12]
    std::vector<double> x = {0.0, 1.0, 2.0, 10.0, 11.0, 12.0};
    std::vector<double> y = {0.0, 1.0};
    std::vector<double> z = {1.0, 1.0, 2.0, 2.0, 3.0, 3.0, 4.0, 4.0, 5.0, 5.0, 6.0, 6.0};

    QCPSoADataSource2D src(std::move(x), std::move(y), std::move(z));

    auto* result = qcp::algo2d::resample(src, 0, 6, 12, 2, false, 1.5);
    QVERIFY(result != nullptr);

    // Bins in the gap region should be NaN
    // The gap is between x=2 and x=10, so middle bins should be NaN
    int midKey = result->keySize() / 2;
    QVERIFY(std::isnan(result->cell(midKey, 0)));

    delete result;
}

void TestDataSource2D::resampleLogY()
{
    std::vector<double> x = {0.0, 1.0};
    std::vector<double> y = {1.0, 10.0, 100.0};
    std::vector<double> z = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};

    QCPSoADataSource2D src(std::move(x), std::move(y), std::move(z));

    auto* result = qcp::algo2d::resample(src, 0, 2, 2, 3, true, 1.5);
    QVERIFY(result != nullptr);
    QVERIFY(result->keySize() > 0);
    QVERIFY(result->valueSize() > 0);
    delete result;
}

void TestDataSource2D::resampleEmptyBins()
{
    // Sparse data: 2 x-points mapped to 10-wide target → most bins empty
    std::vector<double> x = {0.0, 9.0};
    std::vector<double> y = {0.0, 1.0};
    std::vector<double> z = {1.0, 2.0, 3.0, 4.0};

    QCPSoADataSource2D src(std::move(x), std::move(y), std::move(z));

    auto* result = qcp::algo2d::resample(src, 0, 2, 10, 2, false, 1.5);
    QVERIFY(result != nullptr);

    // Most middle bins should be NaN (empty)
    int nanCount = 0;
    for (int i = 0; i < result->keySize(); ++i)
        for (int j = 0; j < result->valueSize(); ++j)
            if (std::isnan(result->cell(i, j)))
                ++nanCount;
    QVERIFY(nanCount > 0);

    delete result;
}
```

- [ ] **Step 2: Run to verify tests fail**

Add `#include <datasource/resample.h>` to the test file.

Run: `meson compile -C build 2>&1 | tail -20`
Expected: FAIL — `qcp::algo2d::resample` undefined (declared but not compiled)

- [ ] **Step 3: Create resample.h (declaration only) and resample.cpp (implementation)**

Create `src/datasource/resample.h` — declaration with forward-declared `QCPColorMapData`:

```cpp
// src/datasource/resample.h
#pragma once

class QCPAbstractDataSource2D;
class QCPColorMapData;

namespace qcp::algo2d {

// Core resampling algorithm (ported from SciQLopPlots).
// Returns a new QCPColorMapData* at screen resolution. Caller owns it.
// Returns nullptr if input is insufficient (srcCount < 2, zero target size, etc.).
QCPColorMapData* resample(
    const QCPAbstractDataSource2D& src,
    int xBegin, int xEnd,
    int targetWidth, int targetHeight,
    bool yLogScale,
    double gapThreshold);

} // namespace qcp::algo2d
```

Create `src/datasource/resample.cpp` — full implementation. This file depends on `QCPColorMapData` (from `plottable-colormap.h`), keeping that dependency out of the header-only datasource layer:

```cpp
// src/datasource/resample.cpp
#include "resample.h"
#include "abstract-datasource-2d.h"
#include <plottables/plottable-colormap.h> // for QCPColorMapData
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace qcp::algo2d {

namespace {

std::vector<double> generateRange(double start, double end, int n, bool log)
{
    std::vector<double> range(n);
    if (n <= 1)
    {
        if (n == 1)
            range[0] = start;
        return range;
    }
    if (log && start > 0 && end > 0)
    {
        double logStart = std::log10(start);
        double step = (std::log10(end) - logStart) / (n - 1);
        for (int i = 0; i < n; ++i)
            range[i] = std::pow(10.0, logStart + i * step);
    }
    else
    {
        double step = (end - start) / (n - 1);
        for (int i = 0; i < n; ++i)
            range[i] = start + i * step;
    }
    return range;
}

int findBin(double value, const std::vector<double>& axis)
{
    auto it = std::lower_bound(axis.begin(), axis.end(), value);
    if (it == axis.end())
        return static_cast<int>(axis.size()) - 1;
    int idx = static_cast<int>(std::distance(axis.begin(), it));
    if (idx > 0 && (value - axis[idx - 1]) < (axis[idx] - value))
        --idx;
    return std::clamp(idx, 0, static_cast<int>(axis.size()) - 1);
}

} // anonymous namespace

QCPColorMapData* resample(
    const QCPAbstractDataSource2D& src,
    int xBegin, int xEnd,
    int targetWidth, int targetHeight,
    bool yLogScale,
    double gapThreshold)
{
    int srcCount = xEnd - xBegin;
    if (srcCount < 2 || targetWidth < 1 || targetHeight < 1)
        return nullptr;

    double xMin = src.xAt(xBegin);
    double xMax = src.xAt(xEnd - 1);
    int ys = src.ySize();

    double yMin = std::numeric_limits<double>::max();
    double yMax = std::numeric_limits<double>::lowest();
    for (int i = xBegin; i < xEnd; ++i)
    {
        for (int j = 0; j < ys; ++j)
        {
            double yv = src.yAt(i, j);
            if (!std::isnan(yv))
            {
                yMin = std::min(yMin, yv);
                yMax = std::max(yMax, yv);
            }
        }
    }
    if (yMin >= yMax)
        return nullptr;

    int nx = std::min(targetWidth, srcCount);
    int ny = std::min(targetHeight, ys);

    auto xAxis = generateRange(xMin, xMax, nx, false);
    auto yAxis = generateRange(yMin, yMax, ny, yLogScale);

    auto* data = new QCPColorMapData(nx, ny, {xAxis.front(), xAxis.back()},
                                              {yAxis.front(), yAxis.back()});
    for (int i = 0; i < nx; ++i)
        for (int j = 0; j < ny; ++j)
            data->setCell(i, j, 0.0);

    std::vector<uint32_t> counts(nx * ny, 0);
    int xDestIdx = 0;
    double prevDx = 0;

    for (int xi = xBegin; xi < xEnd; ++xi)
    {
        double xVal = src.xAt(xi);

        while (xDestIdx < nx - 1 && xVal > xAxis[xDestIdx])
            ++xDestIdx;

        // Gap detection (bidirectional)
        if (xi > xBegin && xi < xEnd - 1 && gapThreshold > 0)
        {
            double dx = src.xAt(xi) - src.xAt(xi - 1);
            double nextDx = src.xAt(xi + 1) - src.xAt(xi);
            if (prevDx > 0 && dx > gapThreshold * prevDx && nextDx > gapThreshold * dx)
            {
                prevDx = dx;
                continue;
            }
            prevDx = dx;
        }

        for (int yj = 0; yj < ys; ++yj)
        {
            double yVal = src.yAt(xi, yj);
            if (std::isnan(yVal))
                continue;
            int yDestIdx = findBin(yVal, yAxis);
            double zVal = src.zAt(xi, yj);
            if (std::isnan(zVal))
                continue;
            data->setCell(xDestIdx, yDestIdx, data->cell(xDestIdx, yDestIdx) + zVal);
            counts[xDestIdx * ny + yDestIdx] += 1;
        }
    }

    for (int i = 0; i < nx; ++i)
    {
        for (int j = 0; j < ny; ++j)
        {
            uint32_t c = counts[i * ny + j];
            if (c > 0)
                data->setCell(i, j, data->cell(i, j) / c);
            else
                data->setCell(i, j, std::nan(""));
        }
    }

    data->recalculateDataBounds();
    return data;
}

} // namespace qcp::algo2d
```

Add `'src/datasource/resample.cpp'` to `meson.build` library sources.

- [ ] **Step 4: Run tests to verify resample tests pass**

Run: `meson test -C build --print-errorlogs 2>&1 | tail -30`
Expected: resample* tests PASS

- [ ] **Step 5: Commit**

```bash
git add src/datasource/resample.h \
        src/datasource/resample.cpp \
        meson.build \
        tests/auto/test-datasource2d/test-datasource2d.cpp
git commit -m "feat: add resample algorithm with gap detection and log-y support"
```

---

## Chunk 3: QCPColormapResampler + QCPColorMap2

### Task 6: QCPColormapResampler

**Files:**
- Create: `src/plottables/colormap-resampler.h`
- Create: `src/plottables/colormap-resampler.cpp`
- Modify: `meson.build`
- Modify: `tests/auto/test-datasource2d/test-datasource2d.cpp`

- [ ] **Step 1: Write failing test for async resampler**

Replace resamplerAsyncRoundTrip stub:

```cpp
#include <plottables/colormap-resampler.h>

void TestDataSource2D::resamplerAsyncRoundTrip()
{
    std::vector<double> x = {0.0, 1.0, 2.0, 3.0, 4.0};
    std::vector<double> y = {0.0, 1.0, 2.0};
    std::vector<double> z(15);
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 3; ++j)
            z[i * 3 + j] = i * 3.0 + j;

    auto src = std::make_shared<QCPSoADataSource2D<
        std::vector<double>, std::vector<double>, std::vector<double>>>(
        std::move(x), std::move(y), std::move(z));

    QCPColormapResampler resampler;

    QCPColorMapData* received = nullptr;
    uint64_t receivedGen = 0;
    QEventLoop loop;
    connect(&resampler, &QCPColormapResampler::finished, [&](uint64_t gen, QCPColorMapData* data) {
        received = data;
        receivedGen = gen;
        loop.quit();
    });

    resampler.request(src, QCPRange(0, 4), QSize(5, 3), false, 1.5);
    loop.exec();

    QVERIFY(received != nullptr);
    QVERIFY(receivedGen > 0);
    QCOMPARE(received->keySize(), 5);
    QCOMPARE(received->valueSize(), 3);

    delete received;
}
```

- [ ] **Step 2: Run to verify test fails**

Run: `meson compile -C build 2>&1 | tail -20`
Expected: FAIL — `QCPColormapResampler` not found

- [ ] **Step 3: Implement colormap-resampler.h**

```cpp
// src/plottables/colormap-resampler.h
#pragma once
#include <QObject>
#include <QSize>
#include <datasource/resampler-scheduler.h>
#include <datasource/abstract-datasource-2d.h>
#include <axis/range.h>
#include <memory>
#include <atomic>

class QCPColorMapData;

class QCPColormapResampler : public QObject
{
    Q_OBJECT

public:
    explicit QCPColormapResampler(QObject* parent = nullptr);
    ~QCPColormapResampler() override;

    void request(
        std::shared_ptr<QCPAbstractDataSource2D> source,
        QCPRange xRange,
        QSize plotSize,
        bool yLogScale,
        double gapThreshold);

    void stop();

Q_SIGNALS:
    void finished(uint64_t generation, QCPColorMapData* result);

private:
    QCPResamplerScheduler mScheduler;
    std::atomic<uint64_t> mGeneration{0};
};
```

- [ ] **Step 4: Implement colormap-resampler.cpp**

```cpp
// src/plottables/colormap-resampler.cpp
#include "colormap-resampler.h"
#include <datasource/resample.h>
#include <plottables/plottable-colormap.h> // for QCPColorMapData

QCPColormapResampler::QCPColormapResampler(QObject* parent)
    : QObject(parent)
{
    // Register metatypes for queued signal connections
    static bool registered = [] {
        qRegisterMetaType<QCPColorMapData*>("QCPColorMapData*");
        qRegisterMetaType<uint64_t>("uint64_t");
        return true;
    }();
    Q_UNUSED(registered);
    mScheduler.start();
}

QCPColormapResampler::~QCPColormapResampler()
{
    stop();
}

void QCPColormapResampler::stop()
{
    mScheduler.stop();
}

void QCPColormapResampler::request(
    std::shared_ptr<QCPAbstractDataSource2D> source,
    QCPRange xRange,
    QSize plotSize,
    bool yLogScale,
    double gapThreshold)
{
    if (!source || source->xSize() == 0)
        return;

    uint64_t gen = ++mGeneration;

    mScheduler.submit([this, source, xRange, plotSize, yLogScale, gapThreshold, gen] {
        int xBegin = source->findXBegin(xRange.lower);
        int xEnd = source->findXEnd(xRange.upper);

        auto* result = qcp::algo2d::resample(
            *source, xBegin, xEnd,
            plotSize.width(), plotSize.height(),
            yLogScale, gapThreshold);

        Q_EMIT finished(gen, result);
    });
}
```

- [ ] **Step 5: Add to meson.build**

Add `'src/plottables/colormap-resampler.cpp'` to library sources and `'src/plottables/colormap-resampler.h'` to `neoqcp_moc_headers`.

- [ ] **Step 6: Run tests to verify async test passes**

Run: `meson test -C build --print-errorlogs 2>&1 | tail -30`
Expected: resamplerAsyncRoundTrip PASS

- [ ] **Step 7: Commit**

```bash
git add src/plottables/colormap-resampler.h \
        src/plottables/colormap-resampler.cpp \
        meson.build \
        tests/auto/test-datasource2d/test-datasource2d.cpp
git commit -m "feat: add QCPColormapResampler with async resample + generation counter"
```

### Task 7: QCPColorMap2

**Files:**
- Create: `src/plottables/plottable-colormap2.h`
- Create: `src/plottables/plottable-colormap2.cpp`
- Modify: `src/qcp.h`
- Modify: `meson.build`
- Modify: `tests/auto/test-datasource2d/test-datasource2d.cpp`

- [ ] **Step 1: Write failing tests for QCPColorMap2**

Replace colormap2 stubs:

```cpp
#include <plottables/plottable-colormap2.h>

void TestDataSource2D::colormap2Creation()
{
    auto* cm = new QCPColorMap2(mPlot->xAxis, mPlot->yAxis);
    QVERIFY(cm->dataSource() == nullptr);
    QCOMPARE(cm->gapThreshold(), 1.5);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);
    // Should not crash with null data source
}

void TestDataSource2D::colormap2SetDataOwning()
{
    auto* cm = new QCPColorMap2(mPlot->xAxis, mPlot->yAxis);

    std::vector<double> x = {0.0, 1.0, 2.0};
    std::vector<double> y = {0.0, 1.0};
    std::vector<double> z = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    cm->setData(std::move(x), std::move(y), std::move(z));

    QVERIFY(cm->dataSource() != nullptr);
    QCOMPARE(cm->dataSource()->xSize(), 3);
    QCOMPARE(cm->dataSource()->ySize(), 2);
    QVERIFY(!cm->dataSource()->yIs2D());
}

void TestDataSource2D::colormap2ViewData()
{
    auto* cm = new QCPColorMap2(mPlot->xAxis, mPlot->yAxis);

    double x[] = {0.0, 1.0};
    float y[] = {0.0f, 1.0f, 2.0f};
    double z[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    cm->viewData(x, 2, y, 3, z, 6);

    QVERIFY(cm->dataSource() != nullptr);
    QCOMPARE(cm->dataSource()->xSize(), 2);
    QCOMPARE(cm->dataSource()->ySize(), 3);
}

void TestDataSource2D::colormap2Render()
{
    mPlot->resize(400, 300);
    auto* cm = new QCPColorMap2(mPlot->xAxis, mPlot->yAxis);

    std::vector<double> x = {0.0, 1.0, 2.0, 3.0, 4.0};
    std::vector<double> y = {0.0, 1.0, 2.0};
    std::vector<double> z(15);
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 3; ++j)
            z[i * 3 + j] = i * 3.0 + j;
    cm->setData(std::move(x), std::move(y), std::move(z));
    cm->setGradient(QCPColorGradient(QCPColorGradient::gpJet));
    cm->setDataRange(QCPRange(0, 14));
    cm->rescaleAxes();

    // Render without waiting for async resample — draw() handles null mResampledData
    mPlot->replot(QCustomPlot::rpImmediateRefresh);
    // Should not crash even with no resampled data yet

    // Now wait for async resample and render again
    QEventLoop loop;
    // Use a timeout as safety net; the resample should complete much faster
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.start(2000);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    // Process events until replot is triggered by onResampleFinished
    connect(mPlot, &QCustomPlot::afterReplot, &loop, &QEventLoop::quit);
    loop.exec();

    // Render with resampled data
    mPlot->replot(QCustomPlot::rpImmediateRefresh);
    // Should not crash
}

void TestDataSource2D::colormap2AxisRescale()
{
    auto* cm = new QCPColorMap2(mPlot->xAxis, mPlot->yAxis);

    std::vector<double> x = {1.0, 2.0, 3.0};
    std::vector<double> y = {10.0, 20.0};
    std::vector<double> z = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    cm->setData(std::move(x), std::move(y), std::move(z));

    bool found = false;
    auto kr = cm->getKeyRange(found, QCP::sdBoth);
    QVERIFY(found);
    QCOMPARE(kr.lower, 1.0);
    QCOMPARE(kr.upper, 3.0);

    found = false;
    auto vr = cm->getValueRange(found, QCP::sdBoth, QCPRange());
    QVERIFY(found);
    QCOMPARE(vr.lower, 10.0);
    QCOMPARE(vr.upper, 20.0);
}
```

- [ ] **Step 2: Run to verify tests fail**

Run: `meson compile -C build 2>&1 | tail -20`
Expected: FAIL — `QCPColorMap2` not found

- [ ] **Step 3: Implement plottable-colormap2.h**

```cpp
// src/plottables/plottable-colormap2.h
#pragma once
#include "plottable.h"
#include "colormap-resampler.h"
#include <datasource/soa-datasource-2d.h>
#include <colorgradient.h>
#include <memory>
#include <span>

class QCPColorScale;
class QCPColorMapData;

class QCP_LIB_DECL QCPColorMap2 : public QCPAbstractPlottable
{
    Q_OBJECT

public:
    explicit QCPColorMap2(QCPAxis* keyAxis, QCPAxis* valueAxis);
    ~QCPColorMap2() override;

    // Data source management
    void setDataSource(std::unique_ptr<QCPAbstractDataSource2D> source);
    void setDataSource(std::shared_ptr<QCPAbstractDataSource2D> source);
    QCPAbstractDataSource2D* dataSource() const { return mDataSource.get(); }
    void dataChanged();

    // Owning setData
    template <IndexableNumericRange XC, IndexableNumericRange YC, IndexableNumericRange ZC>
    void setData(XC&& x, YC&& y, ZC&& z)
    {
        setDataSource(std::make_shared<QCPSoADataSource2D<
            std::decay_t<XC>, std::decay_t<YC>, std::decay_t<ZC>>>(
            std::forward<XC>(x), std::forward<YC>(y), std::forward<ZC>(z)));
    }

    // Non-owning views
    template <typename X, typename Y, typename Z>
    void viewData(const X* x, int nx, const Y* y, int ny, const Z* z, int nz)
    {
        setDataSource(std::make_shared<QCPSoADataSource2D<
            std::span<const X>, std::span<const Y>, std::span<const Z>>>(
            std::span<const X>{x, static_cast<size_t>(nx)},
            std::span<const Y>{y, static_cast<size_t>(ny)},
            std::span<const Z>{z, static_cast<size_t>(nz)}));
    }

    template <typename X, typename Y, typename Z>
    void viewData(std::span<const X> x, std::span<const Y> y, std::span<const Z> z)
    {
        setDataSource(std::make_shared<QCPSoADataSource2D<
            std::span<const X>, std::span<const Y>, std::span<const Z>>>(x, y, z));
    }

    // Properties
    void setGapThreshold(double threshold) { mGapThreshold = threshold; }
    double gapThreshold() const { return mGapThreshold; }

    QCPColorGradient gradient() const { return mGradient; }
    void setGradient(const QCPColorGradient& gradient);

    QCPColorScale* colorScale() const { return mColorScale; }
    void setColorScale(QCPColorScale* colorScale);

    QCPRange dataRange() const { return mDataRange; }
    void setDataRange(const QCPRange& range);
    void rescaleDataRange(bool recalc = false);

    double selectTest(const QPointF& pos, bool onlySelectable, QVariant* details = nullptr) const override;

    QCPRange getKeyRange(bool& foundRange, QCP::SignDomain inSignDomain = QCP::sdBoth) const override;
    QCPRange getValueRange(bool& foundRange, QCP::SignDomain inSignDomain = QCP::sdBoth,
                           const QCPRange& inKeyRange = QCPRange()) const override;

protected:
    void draw(QCPPainter* painter) override;
    void drawLegendIcon(QCPPainter* painter, const QRectF& rect) const override;

private:
    std::shared_ptr<QCPAbstractDataSource2D> mDataSource;
    QCPColormapResampler* mResampler;
    QCPColorGradient mGradient;
    QCPColorScale* mColorScale = nullptr;
    QCPRange mDataRange;
    double mGapThreshold = 1.5;
    uint64_t mCurrentGeneration = 0;

    QCPColorMapData* mResampledData = nullptr;
    QImage mMapImage;
    bool mMapImageInvalidated = true;

    void requestResample();
    void onResampleFinished(uint64_t generation, QCPColorMapData* data);
    void updateMapImage();
};
```

- [ ] **Step 4: Implement plottable-colormap2.cpp**

```cpp
// src/plottables/plottable-colormap2.cpp
#include "plottable-colormap2.h"
#include "plottable-colormap.h" // for QCPColorMapData
#include <painting/painter.h>
#include <layoutelements/layoutelement-colorscale.h>
#include <layoutelements/layoutelement-axisrect.h>
#include <axis/axis.h>
#include <Profiling.hpp>

QCPColorMap2::QCPColorMap2(QCPAxis* keyAxis, QCPAxis* valueAxis)
    : QCPAbstractPlottable(keyAxis, valueAxis)
    , mResampler(new QCPColormapResampler(this))
    , mGradient(QCPColorGradient::gpCold)
{
    connect(mResampler, &QCPColormapResampler::finished,
            this, &QCPColorMap2::onResampleFinished,
            Qt::QueuedConnection);

    if (mKeyAxis)
    {
        connect(mKeyAxis.data(), qOverload<const QCPRange&>(&QCPAxis::rangeChanged),
                this, [this](const QCPRange&) { requestResample(); });
    }
    if (mValueAxis)
    {
        connect(mValueAxis.data(), qOverload<const QCPRange&>(&QCPAxis::rangeChanged),
                this, [this](const QCPRange&) { requestResample(); });
        connect(mValueAxis.data(), &QCPAxis::scaleTypeChanged,
                this, [this](QCPAxis::ScaleType) { requestResample(); });
    }
    // Resize trigger: resampled grid dimensions depend on axis rect pixel size
    if (mKeyAxis && mKeyAxis->axisRect())
    {
        connect(mKeyAxis->axisRect(), &QCPAxisRect::sizeChanged,
                this, [this]() { requestResample(); });
    }
}

QCPColorMap2::~QCPColorMap2()
{
    mResampler->stop();
    delete mResampledData;
}

void QCPColorMap2::setDataSource(std::unique_ptr<QCPAbstractDataSource2D> source)
{
    setDataSource(std::shared_ptr<QCPAbstractDataSource2D>(std::move(source)));
}

void QCPColorMap2::setDataSource(std::shared_ptr<QCPAbstractDataSource2D> source)
{
    mDataSource = std::move(source);
    requestResample();
}

void QCPColorMap2::dataChanged()
{
    requestResample();
}

void QCPColorMap2::setGradient(const QCPColorGradient& gradient)
{
    if (mGradient != gradient)
    {
        mGradient = gradient;
        mMapImageInvalidated = true;
        if (mParentPlot)
            mParentPlot->replot();
    }
}

void QCPColorMap2::setColorScale(QCPColorScale* colorScale)
{
    mColorScale = colorScale;
}

void QCPColorMap2::setDataRange(const QCPRange& range)
{
    if (mDataRange.lower != range.lower || mDataRange.upper != range.upper)
    {
        mDataRange = range;
        mMapImageInvalidated = true;
        if (mParentPlot)
            mParentPlot->replot();
    }
}

void QCPColorMap2::rescaleDataRange(bool recalc)
{
    if (!mDataSource)
        return;
    bool found = false;
    QCPRange range;
    if (mResampledData && !recalc)
        range = mResampledData->dataBounds();
    else
        range = mDataSource->zRange(found);
    if (range.lower < range.upper)
        setDataRange(range);
}

void QCPColorMap2::requestResample()
{
    if (!mDataSource || !mKeyAxis || !mValueAxis)
        return;

    auto axisRect = mKeyAxis->axisRect();
    if (!axisRect)
        return;

    QSize plotSize(static_cast<int>(axisRect->width()),
                   static_cast<int>(axisRect->height()));
    if (plotSize.width() <= 0 || plotSize.height() <= 0)
        return;

    bool yLog = mValueAxis->scaleType() == QCPAxis::stLogarithmic;
    mResampler->request(mDataSource, mKeyAxis->range(), plotSize, yLog, mGapThreshold);
}

void QCPColorMap2::onResampleFinished(uint64_t generation, QCPColorMapData* data)
{
    if (generation < mCurrentGeneration)
    {
        delete data;
        return;
    }
    mCurrentGeneration = generation;
    delete mResampledData;
    mResampledData = data;
    mMapImageInvalidated = true;
    if (mParentPlot)
        mParentPlot->replot();
}

void QCPColorMap2::updateMapImage()
{
    if (!mResampledData)
        return;

    int keySize = mResampledData->keySize();
    int valueSize = mResampledData->valueSize();
    if (keySize == 0 || valueSize == 0)
        return;

    mMapImage = QImage(keySize, valueSize, QImage::Format_ARGB32_Premultiplied);

    const double* rawData = mResampledData->mData;
    for (int y = 0; y < valueSize; ++y)
    {
        QRgb* pixels = reinterpret_cast<QRgb*>(mMapImage.scanLine(valueSize - 1 - y));
        mGradient.colorize(rawData + y * keySize, mDataRange, pixels, keySize, 1, false);
    }

    mMapImageInvalidated = false;
}

void QCPColorMap2::draw(QCPPainter* painter)
{
    PROFILE_HERE_N("QCPColorMap2::draw");
    if (!mKeyAxis || !mValueAxis || !mResampledData)
        return;

    if (mMapImageInvalidated)
        updateMapImage();

    if (mMapImage.isNull())
        return;

    QCPRange keyRange = mResampledData->keyRange();
    QCPRange valueRange = mResampledData->valueRange();

    QPointF topLeft = QPointF(mKeyAxis->coordToPixel(keyRange.lower),
                              mValueAxis->coordToPixel(valueRange.upper));
    QPointF bottomRight = QPointF(mKeyAxis->coordToPixel(keyRange.upper),
                                  mValueAxis->coordToPixel(valueRange.lower));
    QRectF imageRect(topLeft, bottomRight);

    bool mirrorX = mKeyAxis->rangeReversed();
    bool mirrorY = !mValueAxis->rangeReversed(); // default: mathematical coords = not mirrored

    applyDefaultAntialiasingHint(painter);
    painter->drawImage(imageRect, mMapImage.mirrored(mirrorX, mirrorY));
}

void QCPColorMap2::drawLegendIcon(QCPPainter* painter, const QRectF& rect) const
{
    painter->setBrush(QBrush(mGradient.color(0.5, QCPRange(0, 1))));
    painter->setPen(Qt::NoPen);
    painter->drawRect(rect);
}

QCPRange QCPColorMap2::getKeyRange(bool& foundRange, QCP::SignDomain inSignDomain) const
{
    if (!mDataSource)
    {
        foundRange = false;
        return {};
    }
    return mDataSource->xRange(foundRange, inSignDomain);
}

QCPRange QCPColorMap2::getValueRange(bool& foundRange, QCP::SignDomain inSignDomain,
                                     const QCPRange&) const
{
    if (!mDataSource)
    {
        foundRange = false;
        return {};
    }
    return mDataSource->yRange(foundRange, inSignDomain);
}

double QCPColorMap2::selectTest(const QPointF& pos, bool onlySelectable, QVariant*) const
{
    if (onlySelectable && !mSelectable)
        return -1;
    if (!mKeyAxis || !mValueAxis || !mDataSource)
        return -1;

    double key = mKeyAxis->pixelToCoord(pos.x());
    double value = mValueAxis->pixelToCoord(pos.y());

    bool foundKey = false, foundValue = false;
    auto kr = mDataSource->xRange(foundKey);
    auto vr = mDataSource->yRange(foundValue);

    if (foundKey && foundValue && kr.contains(key) && vr.contains(value))
        return 0; // inside colormap area
    return -1;
}
```

Note: The `updateMapImage()` accesses `mResampledData->mData` directly. `QCPColorMapData::mData` is a protected member — QCPColorMap accesses it internally (same file). Since QCPColorMap2 is a different class, we need to either:
- Use the public `cell(keyIndex, valueIndex)` method (slower, per-cell virtual-ish call), or
- Access data row-by-row via `colorize()` using the public `cell()` accessor.

The practical approach: use `cell()` for correctness, optimize later if needed.

Update `updateMapImage()` to use public API:

```cpp
void QCPColorMap2::updateMapImage()
{
    if (!mResampledData)
        return;

    int keySize = mResampledData->keySize();
    int valueSize = mResampledData->valueSize();
    if (keySize == 0 || valueSize == 0)
        return;

    mMapImage = QImage(keySize, valueSize, QImage::Format_ARGB32_Premultiplied);

    // Build a row buffer for colorize()
    std::vector<double> rowData(keySize);
    for (int y = 0; y < valueSize; ++y)
    {
        for (int x = 0; x < keySize; ++x)
            rowData[x] = mResampledData->cell(x, y);

        QRgb* pixels = reinterpret_cast<QRgb*>(mMapImage.scanLine(valueSize - 1 - y));
        mGradient.colorize(rowData.data(), mDataRange, pixels, keySize);
    }

    mMapImageInvalidated = false;
}
```

- [ ] **Step 5: Add to meson.build and qcp.h**

In `meson.build`:
- Add `'src/plottables/plottable-colormap2.cpp'` to library sources
- Add `'src/plottables/plottable-colormap2.h'` to `neoqcp_moc_headers`

In `src/qcp.h`, add after the existing plottable/datasource includes:
```cpp
#include "datasource/abstract-datasource-2d.h"
#include "datasource/soa-datasource-2d.h"
#include "datasource/algorithms-2d.h"
#include "datasource/resample.h"
#include "plottables/plottable-colormap2.h"
```

- [ ] **Step 6: Run tests to verify all tests pass**

Run: `meson test -C build --print-errorlogs 2>&1 | tail -40`
Expected: all colormap2* tests PASS

- [ ] **Step 7: Run the full test suite to verify no regressions**

Run: `meson test -C build --print-errorlogs`
Expected: all tests PASS

- [ ] **Step 8: Commit**

```bash
git add src/plottables/plottable-colormap2.h \
        src/plottables/plottable-colormap2.cpp \
        src/qcp.h \
        meson.build \
        tests/auto/test-datasource2d/test-datasource2d.cpp
git commit -m "feat: add QCPColorMap2 with async resampling and zero-copy data sources"
```

---

## Chunk 4: Build Integration + Final Verification

### Task 8: Verify Full Build and Test Suite

- [ ] **Step 1: Clean build from scratch**

Run: `rm -rf build && meson setup build && meson compile -C build`
Expected: clean build, no warnings from new code

- [ ] **Step 2: Run all tests**

Run: `meson test -C build --print-errorlogs`
Expected: all tests PASS

- [ ] **Step 3: Run benchmarks**

Run: `meson test --benchmark -C build --print-errorlogs`
Expected: benchmarks run (no new benchmarks yet, just verify no regressions)

- [ ] **Step 4: Verify with different build type**

Run: `rm -rf build-debug && meson setup --buildtype=debug build-debug && meson compile -C build-debug && meson test -C build-debug --print-errorlogs`
Expected: debug build passes all tests (assertions enabled)

### Task 9: Manual Test Example

**Files:**
- Modify: `tests/manual/mainwindow.h`
- Modify: `tests/manual/mainwindow.cpp`

- [ ] **Step 1: Add declaration to mainwindow.h**

Add after `setupGraph2Test`:

```cpp
void setupColorMap2Test(QCustomPlot *customPlot);
```

- [ ] **Step 2: Add setupColorMap2Test implementation to mainwindow.cpp**

```cpp
void MainWindow::setupColorMap2Test(QCustomPlot *customPlot)
{
  // --- ColorMap2 with uniform Y (1D) ---
  // Generates a 200x50 spectrogram with z = sin(x) * cos(y)
  const int nx = 200;
  const int ny1d = 50;
  {
    std::vector<double> x(nx), y(ny1d), z(nx * ny1d);
    for (int i = 0; i < nx; ++i)
      x[i] = i * 0.1;
    for (int j = 0; j < ny1d; ++j)
      y[j] = j * 0.5;
    for (int i = 0; i < nx; ++i)
      for (int j = 0; j < ny1d; ++j)
        z[i * ny1d + j] = qSin(x[i] * 0.5) * qCos(y[j] * 0.3);

    auto *cm1 = new QCPColorMap2(customPlot->xAxis, customPlot->yAxis);
    cm1->setData(std::move(x), std::move(y), std::move(z));
    cm1->setGradient(QCPColorGradient(QCPColorGradient::gpJet));
    cm1->setDataRange(QCPRange(-1, 1));
  }

  // Add a color scale
  auto *colorScale = new QCPColorScale(customPlot);
  customPlot->plotLayout()->addElement(0, 1, colorScale);
  colorScale->setGradient(QCPColorGradient(QCPColorGradient::gpJet));
  colorScale->setDataRange(QCPRange(-1, 1));

  customPlot->rescaleAxes();
  customPlot->xAxis->scaleRange(1.05, customPlot->xAxis->range().center());
  customPlot->yAxis->scaleRange(1.05, customPlot->yAxis->range().center());
}
```

- [ ] **Step 3: Wire up in constructor**

In the `MainWindow` constructor, add after `//setupGraph2Test(mCustomPlot);`:

```cpp
  //setupColorMap2Test(mCustomPlot);
```

- [ ] **Step 4: Verify it compiles**

Run: `meson compile -C build 2>&1 | tail -10`
Expected: compiles without errors

- [ ] **Step 5: Commit**

```bash
git add tests/manual/mainwindow.h tests/manual/mainwindow.cpp
git commit -m "feat: add QCPColorMap2 manual test with uniform-Y spectrogram"
```

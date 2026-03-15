# Interactive Items & Utilities Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add interactive span items (VSpan, HSpan, RSpan), rich text item, data locator utility, and expose `calculateAutoMargin()` publicly.

**Architecture:** New item classes inherit `QCPAbstractItem` directly, following the existing item pattern (positions + anchors + draw/selectTest). Drag interaction uses `QCPLayerable`'s existing `mousePressEvent`/`mouseMoveEvent`/`mouseReleaseEvent` virtual overrides. Data locator is a plain utility class.

**Tech Stack:** C++20, Qt6, Meson, Qt Test

**Spec:** `docs/superpowers/specs/2026-03-14-interactive-items-design.md`

---

## Chunk 1: QCPItemVSpan

### Task 1: QCPItemVSpan header and stub

**Files:**
- Create: `src/items/item-vspan.h`
- Create: `src/items/item-vspan.cpp`

- [ ] **Step 1: Create the header**

```cpp
// src/items/item-vspan.h
#ifndef QCP_ITEM_VSPAN_H
#define QCP_ITEM_VSPAN_H

#include "../global.h"
#include "item.h"

class QCPPainter;
class QCustomPlot;

class QCP_LIB_DECL QCPItemVSpan : public QCPAbstractItem
{
    Q_OBJECT
    Q_PROPERTY(QPen pen READ pen WRITE setPen)
    Q_PROPERTY(QPen selectedPen READ selectedPen WRITE setSelectedPen)
    Q_PROPERTY(QBrush brush READ brush WRITE setBrush)
    Q_PROPERTY(QBrush selectedBrush READ selectedBrush WRITE setSelectedBrush)
    Q_PROPERTY(QPen borderPen READ borderPen WRITE setBorderPen)
    Q_PROPERTY(QPen selectedBorderPen READ selectedBorderPen WRITE setSelectedBorderPen)
    Q_PROPERTY(bool movable READ movable WRITE setMovable)

public:
    explicit QCPItemVSpan(QCustomPlot* parentPlot);
    virtual ~QCPItemVSpan() override;

    // getters:
    QPen pen() const { return mPen; }
    QPen selectedPen() const { return mSelectedPen; }
    QBrush brush() const { return mBrush; }
    QBrush selectedBrush() const { return mSelectedBrush; }
    QPen borderPen() const { return mBorderPen; }
    QPen selectedBorderPen() const { return mSelectedBorderPen; }
    bool movable() const { return mMovable; }
    QCPRange range() const;

    // setters:
    void setPen(const QPen& pen);
    void setSelectedPen(const QPen& pen);
    void setBrush(const QBrush& brush);
    void setSelectedBrush(const QBrush& brush);
    void setBorderPen(const QPen& pen);
    void setSelectedBorderPen(const QPen& pen);
    void setMovable(bool movable);
    void setRange(const QCPRange& range);

    // reimplemented virtual methods:
    virtual double selectTest(const QPointF& pos, bool onlySelectable,
                              QVariant* details = nullptr) const override;

    QCPItemPosition* const lowerEdge;
    QCPItemPosition* const upperEdge;
    QCPItemAnchor* const center;

signals:
    void rangeChanged(const QCPRange& newRange);
    void deleteRequested();

    // hit detail: which part was grabbed (public for test and external use)
    enum HitPart { hpNone = -2, hpFill = -1, hpLowerEdge = 0, hpUpperEdge = 1 };

protected:
    enum AnchorIndex { aiCenter };

    // property members:
    QPen mPen, mSelectedPen;
    QBrush mBrush, mSelectedBrush;
    QPen mBorderPen, mSelectedBorderPen;
    bool mMovable = true;

    // drag state:
    HitPart mDragPart = hpNone;
    double mDragStartLower = 0.0;
    double mDragStartUpper = 0.0;

    // reimplemented virtual methods:
    virtual void draw(QCPPainter* painter) override;
    virtual QPointF anchorPixelPosition(int anchorId) const override;
    virtual void mousePressEvent(QMouseEvent* event, const QVariant& details) override;
    virtual void mouseMoveEvent(QMouseEvent* event, const QPointF& startPos) override;
    virtual void mouseReleaseEvent(QMouseEvent* event, const QPointF& startPos) override;

    // non-virtual methods:
    QPen mainPen() const;
    QBrush mainBrush() const;
    QPen mainBorderPen() const;
};

#endif // QCP_ITEM_VSPAN_H
```

- [ ] **Step 2: Create the implementation stub**

```cpp
// src/items/item-vspan.cpp
#include "item-vspan.h"

#include "../core.h"
#include "../painting/painter.h"

QCPItemVSpan::QCPItemVSpan(QCustomPlot* parentPlot)
    : QCPAbstractItem(parentPlot)
    , lowerEdge(createPosition(QLatin1String("lowerEdge")))
    , upperEdge(createPosition(QLatin1String("upperEdge")))
    , center(createAnchor(QLatin1String("center"), aiCenter))
{
    lowerEdge->setTypeX(QCPItemPosition::ptPlotCoords);
    lowerEdge->setTypeY(QCPItemPosition::ptAxisRectRatio);
    lowerEdge->setCoords(0, 0);

    upperEdge->setTypeX(QCPItemPosition::ptPlotCoords);
    upperEdge->setTypeY(QCPItemPosition::ptAxisRectRatio);
    upperEdge->setCoords(1, 1);

    setPen(QPen(Qt::black));
    setSelectedPen(QPen(Qt::blue, 2));
    setBrush(QBrush(QColor(0, 0, 255, 50)));
    setSelectedBrush(QBrush(QColor(0, 0, 255, 80)));
    setBorderPen(QPen(Qt::black, 2));
    setSelectedBorderPen(QPen(Qt::blue, 2));
}

QCPItemVSpan::~QCPItemVSpan() { }

QCPRange QCPItemVSpan::range() const
{
    return QCPRange(lowerEdge->coords().x(), upperEdge->coords().x());
}

void QCPItemVSpan::setRange(const QCPRange& range)
{
    lowerEdge->setCoords(range.lower, 0);
    upperEdge->setCoords(range.upper, 1);
    emit rangeChanged(range);
}

void QCPItemVSpan::setPen(const QPen& pen) { mPen = pen; }
void QCPItemVSpan::setSelectedPen(const QPen& pen) { mSelectedPen = pen; }
void QCPItemVSpan::setBrush(const QBrush& brush) { mBrush = brush; }
void QCPItemVSpan::setSelectedBrush(const QBrush& brush) { mSelectedBrush = brush; }
void QCPItemVSpan::setBorderPen(const QPen& pen) { mBorderPen = pen; }
void QCPItemVSpan::setSelectedBorderPen(const QPen& pen) { mSelectedBorderPen = pen; }
void QCPItemVSpan::setMovable(bool movable) { mMovable = movable; }

QPen QCPItemVSpan::mainPen() const { return mSelected ? mSelectedPen : mPen; }
QBrush QCPItemVSpan::mainBrush() const { return mSelected ? mSelectedBrush : mBrush; }
QPen QCPItemVSpan::mainBorderPen() const { return mSelected ? mSelectedBorderPen : mBorderPen; }

double QCPItemVSpan::selectTest(const QPointF& pos, bool onlySelectable, QVariant* details) const
{
    if (onlySelectable && !mSelectable)
        return -1;

    auto* keyAxis = lowerEdge->keyAxis();
    if (!keyAxis)
        return -1;

    const double lowerPx = keyAxis->coordToPixel(lowerEdge->coords().x());
    const double upperPx = keyAxis->coordToPixel(upperEdge->coords().x());
    const QRectF axisRect = clipRect();
    const double left = qMin(lowerPx, upperPx);
    const double right = qMax(lowerPx, upperPx);

    const double tolerance = mParentPlot->selectionTolerance();

    // check edges first (priority)
    const double distLower = qAbs(pos.x() - lowerPx);
    const double distUpper = qAbs(pos.x() - upperPx);

    if (distLower <= tolerance && axisRect.top() <= pos.y() && pos.y() <= axisRect.bottom())
    {
        if (details)
            details->setValue(static_cast<int>(hpLowerEdge));
        return distLower;
    }
    if (distUpper <= tolerance && axisRect.top() <= pos.y() && pos.y() <= axisRect.bottom())
    {
        if (details)
            details->setValue(static_cast<int>(hpUpperEdge));
        return distUpper;
    }

    // check fill
    bool filledRect = mBrush.style() != Qt::NoBrush && mBrush.color().alpha() != 0;
    if (filledRect && pos.x() >= left && pos.x() <= right
        && pos.y() >= axisRect.top() && pos.y() <= axisRect.bottom())
    {
        if (details)
            details->setValue(static_cast<int>(hpFill));
        return 0;
    }

    return -1;
}

void QCPItemVSpan::draw(QCPPainter* painter)
{
    auto* keyAxis = lowerEdge->keyAxis();
    if (!keyAxis)
        return;

    const double lowerPx = keyAxis->coordToPixel(lowerEdge->coords().x());
    const double upperPx = keyAxis->coordToPixel(upperEdge->coords().x());
    const QRectF axRect = clipRect();

    const double left = qMin(lowerPx, upperPx);
    const double right = qMax(lowerPx, upperPx);
    QRectF spanRect(left, axRect.top(), right - left, axRect.height());

    if (!spanRect.intersects(axRect))
        return;

    // fill
    painter->setPen(mainPen());
    painter->setBrush(mainBrush());
    painter->drawRect(spanRect);

    // border lines
    painter->setPen(mainBorderPen());
    painter->drawLine(QPointF(lowerPx, axRect.top()), QPointF(lowerPx, axRect.bottom()));
    painter->drawLine(QPointF(upperPx, axRect.top()), QPointF(upperPx, axRect.bottom()));
}

QPointF QCPItemVSpan::anchorPixelPosition(int anchorId) const
{
    if (anchorId == aiCenter)
    {
        auto* keyAxis = lowerEdge->keyAxis();
        if (!keyAxis)
            return {};
        const double midX = (keyAxis->coordToPixel(lowerEdge->coords().x())
                             + keyAxis->coordToPixel(upperEdge->coords().x())) * 0.5;
        const QRectF axRect = clipRect();
        return QPointF(midX, (axRect.top() + axRect.bottom()) * 0.5);
    }
    qDebug() << Q_FUNC_INFO << "invalid anchorId" << anchorId;
    return {};
}

void QCPItemVSpan::mousePressEvent(QMouseEvent* event, const QVariant& details)
{
    if (!mMovable)
    {
        event->ignore();
        return;
    }
    mDragPart = static_cast<HitPart>(details.toInt());
    mDragStartLower = lowerEdge->coords().x();
    mDragStartUpper = upperEdge->coords().x();
    event->accept();
}

void QCPItemVSpan::mouseMoveEvent(QMouseEvent* event, const QPointF& startPos)
{
    auto* keyAxis = lowerEdge->keyAxis();
    if (!keyAxis || mDragPart == hpNone)
        return;

    const double startCoord = keyAxis->pixelToCoord(startPos.x());
    const double currentCoord = keyAxis->pixelToCoord(event->pos().x());
    const double delta = currentCoord - startCoord;

    switch (mDragPart)
    {
        case hpLowerEdge:
            lowerEdge->setCoords(mDragStartLower + delta, 0);
            break;
        case hpUpperEdge:
            upperEdge->setCoords(mDragStartUpper + delta, 1);
            break;
        case hpFill:
            lowerEdge->setCoords(mDragStartLower + delta, 0);
            upperEdge->setCoords(mDragStartUpper + delta, 1);
            break;
        default:
            break;
    }
    emit rangeChanged(range());
    mParentPlot->replot(QCustomPlot::rpQueuedReplot);
}

void QCPItemVSpan::mouseReleaseEvent(QMouseEvent* event, const QPointF& startPos)
{
    Q_UNUSED(event)
    Q_UNUSED(startPos)
    mDragPart = hpNone;
}
```

- [ ] **Step 3: Add to build system**

In `meson.build`, add to `neoqcp_moc_headers`:
```
'src/items/item-vspan.h',
```

In `meson.build`, add to `NeoQCP` static_library sources:
```
'src/items/item-vspan.cpp',
```

In `src/qcp.h`, add:
```cpp
#include "items/item-vspan.h"
```

- [ ] **Step 4: Verify build**

Run: `meson compile -C build`
Expected: PASS, no errors

- [ ] **Step 5: Commit**

```bash
git add src/items/item-vspan.h src/items/item-vspan.cpp src/qcp.h meson.build
git commit -m "feat: add QCPItemVSpan interactive vertical span item"
```

### Task 2: QCPItemVSpan unit tests

**Files:**
- Create: `tests/auto/test-vspan/test-vspan.h`
- Create: `tests/auto/test-vspan/test-vspan.cpp`
- Modify: `tests/auto/autotest.cpp`
- Modify: `tests/auto/meson.build`

- [ ] **Step 1: Create test header**

```cpp
// tests/auto/test-vspan/test-vspan.h
#pragma once
#include <QtTest/QtTest>

class QCustomPlot;

class TestVSpan : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void createAndRange();
    void setRange();
    void invertedRange();
    void selectTestEdges();
    void selectTestFill();
    void selectTestMiss();
    void movableFlag();
    void drawDoesNotCrash();
    void drawOnLogAxis();

private:
    QCustomPlot* mPlot = nullptr;
};
```

- [ ] **Step 2: Create test implementation**

```cpp
// tests/auto/test-vspan/test-vspan.cpp
#include "test-vspan.h"
#include "../../../src/qcp.h"

void TestVSpan::init()
{
    mPlot = new QCustomPlot();
    mPlot->resize(400, 300);
    mPlot->xAxis->setRange(0, 10);
    mPlot->yAxis->setRange(0, 10);
    mPlot->replot();
}

void TestVSpan::cleanup()
{
    delete mPlot;
    mPlot = nullptr;
}

void TestVSpan::createAndRange()
{
    auto* span = new QCPItemVSpan(mPlot);
    span->setRange(QCPRange(2, 5));
    QCOMPARE(span->range().lower, 2.0);
    QCOMPARE(span->range().upper, 5.0);
}

void TestVSpan::setRange()
{
    auto* span = new QCPItemVSpan(mPlot);

    QSignalSpy spy(span, &QCPItemVSpan::rangeChanged);
    span->setRange(QCPRange(3, 7));

    QCOMPARE(spy.count(), 1);
    auto emittedRange = spy.at(0).at(0).value<QCPRange>();
    QCOMPARE(emittedRange.lower, 3.0);
    QCOMPARE(emittedRange.upper, 7.0);
}

void TestVSpan::invertedRange()
{
    auto* span = new QCPItemVSpan(mPlot);
    span->setRange(QCPRange(8, 2));
    // range() preserves order as-given
    QCOMPARE(span->range().lower, 8.0);
    QCOMPARE(span->range().upper, 2.0);
    // draw normalizes internally — should not crash
    mPlot->replot();
    QVERIFY(true);
}

void TestVSpan::drawOnLogAxis()
{
    mPlot->xAxis->setScaleType(QCPAxis::stLogarithmic);
    mPlot->xAxis->setRange(1, 1000);
    auto* span = new QCPItemVSpan(mPlot);
    span->setRange(QCPRange(10, 100));
    mPlot->replot();
    QVERIFY(true);
}

void TestVSpan::selectTestEdges()
{
    auto* span = new QCPItemVSpan(mPlot);
    span->setRange(QCPRange(2, 8));
    mPlot->replot();

    double lowerPx = mPlot->xAxis->coordToPixel(2);
    double midY = mPlot->yAxis->axisRect()->center().y();

    QVariant details;
    double dist = span->selectTest(QPointF(lowerPx, midY), false, &details);
    QVERIFY(dist >= 0);
    QCOMPARE(details.toInt(), static_cast<int>(QCPItemVSpan::hpLowerEdge));
}

void TestVSpan::selectTestFill()
{
    auto* span = new QCPItemVSpan(mPlot);
    span->setRange(QCPRange(2, 8));
    mPlot->replot();

    double midPx = mPlot->xAxis->coordToPixel(5);
    double midY = mPlot->yAxis->axisRect()->center().y();

    QVariant details;
    double dist = span->selectTest(QPointF(midPx, midY), false, &details);
    QVERIFY(dist >= 0);
    QCOMPARE(details.toInt(), static_cast<int>(QCPItemVSpan::hpFill));
}

void TestVSpan::selectTestMiss()
{
    auto* span = new QCPItemVSpan(mPlot);
    span->setRange(QCPRange(2, 4));
    mPlot->replot();

    double farPx = mPlot->xAxis->coordToPixel(9);
    double midY = mPlot->yAxis->axisRect()->center().y();

    double dist = span->selectTest(QPointF(farPx, midY), false);
    QCOMPARE(dist, -1.0);
}

void TestVSpan::movableFlag()
{
    auto* span = new QCPItemVSpan(mPlot);
    QVERIFY(span->movable());
    span->setMovable(false);
    QVERIFY(!span->movable());
}

void TestVSpan::drawDoesNotCrash()
{
    auto* span = new QCPItemVSpan(mPlot);
    span->setRange(QCPRange(1, 9));
    mPlot->replot();
    // reaching here without crash is the test
    QVERIFY(true);
}
```

- [ ] **Step 3: Register test in autotest.cpp and meson.build**

In `tests/auto/autotest.cpp`, add:
```cpp
#include "test-vspan/test-vspan.h"
```
and in `main()`:
```cpp
QCPTEST(TestVSpan);
```

In `tests/auto/meson.build`, add to `test_srcs`:
```
'test-vspan/test-vspan.cpp',
```
and to `test_headers`:
```
'test-vspan/test-vspan.h',
```

- [ ] **Step 4: Build and run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All tests PASS including new TestVSpan tests

- [ ] **Step 5: Commit**

```bash
git add tests/auto/test-vspan/ tests/auto/autotest.cpp tests/auto/meson.build
git commit -m "test: add unit tests for QCPItemVSpan"
```

## Chunk 2: QCPItemHSpan and QCPItemRSpan

### Task 3: QCPItemHSpan

**Files:**
- Create: `src/items/item-hspan.h`
- Create: `src/items/item-hspan.cpp`
- Modify: `src/qcp.h`
- Modify: `meson.build`

- [ ] **Step 1: Create item-hspan.h**

Mirror `item-vspan.h` but swap key/value axis semantics:
- Positions `lowerEdge`/`upperEdge` use `setTypeY(ptPlotCoords)` on value axis, `setTypeX(ptAxisRectRatio)` fixed at 0/1
- `range()` returns `QCPRange(lowerEdge->coords().y(), upperEdge->coords().y())`
- `setRange()` sets Y coordinates
- `selectTest()` checks Y proximity instead of X
- `draw()` draws horizontal border lines
- `mouseMoveEvent()` converts Y pixel delta to value-axis coord delta

The class structure is identical to QCPItemVSpan, just with X↔Y swapped throughout.

- [ ] **Step 2: Create item-hspan.cpp**

Same implementation as QCPItemVSpan but with axis swap:
- Constructor: `lowerEdge->setTypeY(ptPlotCoords); lowerEdge->setTypeX(ptAxisRectRatio); lowerEdge->setCoords(0, 0);`
- `upperEdge->setTypeY(ptPlotCoords); upperEdge->setTypeX(ptAxisRectRatio); upperEdge->setCoords(1, 1);`
- `range()`: `return QCPRange(lowerEdge->coords().y(), upperEdge->coords().y());`
- `draw()`: use `valueAxis()->coordToPixel()` for horizontal lines, span full width
- `selectTest()`: check `pos.y()` against edge Y positions
- `mouseMoveEvent()`: use `valueAxis()->pixelToCoord(pos.y())` for delta

- [ ] **Step 3: Add to build system and qcp.h**

Same pattern as Task 1 Step 3, for `item-hspan.h/.cpp`.

- [ ] **Step 4: Verify build**

Run: `meson compile -C build`
Expected: PASS

- [ ] **Step 5: Add HSpan tests**

Create `tests/auto/test-hspan/test-hspan.h` and `tests/auto/test-hspan/test-hspan.cpp` mirroring the VSpan tests but for horizontal spans. Register in `autotest.cpp` and `meson.build`.

- [ ] **Step 6: Run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All tests PASS

- [ ] **Step 7: Commit**

```bash
git add src/items/item-hspan.h src/items/item-hspan.cpp src/qcp.h meson.build \
        tests/auto/test-hspan/ tests/auto/autotest.cpp tests/auto/meson.build
git commit -m "feat: add QCPItemHSpan interactive horizontal span item"
```

### Task 4: QCPItemRSpan

**Files:**
- Create: `src/items/item-rspan.h`
- Create: `src/items/item-rspan.cpp`
- Modify: `src/qcp.h`
- Modify: `meson.build`

- [ ] **Step 1: Create item-rspan.h**

```cpp
#ifndef QCP_ITEM_RSPAN_H
#define QCP_ITEM_RSPAN_H

#include "../global.h"
#include "item.h"

class QCPPainter;
class QCustomPlot;

class QCP_LIB_DECL QCPItemRSpan : public QCPAbstractItem
{
    Q_OBJECT
    Q_PROPERTY(QPen pen READ pen WRITE setPen)
    Q_PROPERTY(QPen selectedPen READ selectedPen WRITE setSelectedPen)
    Q_PROPERTY(QBrush brush READ brush WRITE setBrush)
    Q_PROPERTY(QBrush selectedBrush READ selectedBrush WRITE setSelectedBrush)
    Q_PROPERTY(QPen borderPen READ borderPen WRITE setBorderPen)
    Q_PROPERTY(QPen selectedBorderPen READ selectedBorderPen WRITE setSelectedBorderPen)
    Q_PROPERTY(bool movable READ movable WRITE setMovable)

public:
    explicit QCPItemRSpan(QCustomPlot* parentPlot);
    virtual ~QCPItemRSpan() override;

    QPen pen() const { return mPen; }
    QPen selectedPen() const { return mSelectedPen; }
    QBrush brush() const { return mBrush; }
    QBrush selectedBrush() const { return mSelectedBrush; }
    QPen borderPen() const { return mBorderPen; }
    QPen selectedBorderPen() const { return mSelectedBorderPen; }
    bool movable() const { return mMovable; }
    QCPRange keyRange() const;
    QCPRange valueRange() const;

    void setPen(const QPen& pen);
    void setSelectedPen(const QPen& pen);
    void setBrush(const QBrush& brush);
    void setSelectedBrush(const QBrush& brush);
    void setBorderPen(const QPen& pen);
    void setSelectedBorderPen(const QPen& pen);
    void setMovable(bool movable);
    void setKeyRange(const QCPRange& range);
    void setValueRange(const QCPRange& range);

    virtual double selectTest(const QPointF& pos, bool onlySelectable,
                              QVariant* details = nullptr) const override;

    QCPItemPosition* const leftEdge;
    QCPItemPosition* const rightEdge;
    QCPItemPosition* const topEdge;
    QCPItemPosition* const bottomEdge;

    QCPItemAnchor* const topLeft;
    QCPItemAnchor* const topRight;
    QCPItemAnchor* const bottomRight;
    QCPItemAnchor* const bottomLeft;
    QCPItemAnchor* const center;

signals:
    void keyRangeChanged(const QCPRange& newRange);
    void valueRangeChanged(const QCPRange& newRange);
    void deleteRequested();

    // hit detail (public for test and external use)
    enum HitPart { hpNone = -2, hpFill = -1, hpLeft = 0, hpRight = 1, hpTop = 2, hpBottom = 3 };

protected:
    enum AnchorIndex { aiTopLeft, aiTopRight, aiBottomRight, aiBottomLeft, aiCenter };

    QPen mPen, mSelectedPen;
    QBrush mBrush, mSelectedBrush;
    QPen mBorderPen, mSelectedBorderPen;
    bool mMovable = true;

    HitPart mDragPart = hpNone;
    double mDragStartLeft = 0, mDragStartRight = 0;
    double mDragStartTop = 0, mDragStartBottom = 0;

    virtual void draw(QCPPainter* painter) override;
    virtual QPointF anchorPixelPosition(int anchorId) const override;
    virtual void mousePressEvent(QMouseEvent* event, const QVariant& details) override;
    virtual void mouseMoveEvent(QMouseEvent* event, const QPointF& startPos) override;
    virtual void mouseReleaseEvent(QMouseEvent* event, const QPointF& startPos) override;

    QPen mainPen() const;
    QBrush mainBrush() const;
    QPen mainBorderPen() const;
};

#endif // QCP_ITEM_RSPAN_H
```

- [ ] **Step 2: Create item-rspan.cpp**

Implementation follows the same pattern as VSpan, but with 4 positions:
- All 4 positions use `ptPlotCoords`
- Constructor: `leftEdge->setCoords(0, 0)`, `rightEdge->setCoords(1, 0)`, `topEdge->setCoords(0, 1)`, `bottomEdge->setCoords(0, 0)` — only the X of left/right and Y of top/bottom matter
- `draw()`: compute rect from `keyAxis->coordToPixel(leftEdge X)`, `keyAxis->coordToPixel(rightEdge X)`, `valueAxis->coordToPixel(topEdge Y)`, `valueAxis->coordToPixel(bottomEdge Y)`
- `selectTest()`: check all 4 edges, then fill
- `mouseMoveEvent()`: move the grabbed edge along its axis (left/right → key axis, top/bottom → value axis), or translate all 4 for fill

- [ ] **Step 3: Add to build system and qcp.h**

- [ ] **Step 4: Verify build**

Run: `meson compile -C build`

- [ ] **Step 5: Add RSpan tests**

Create `tests/auto/test-rspan/test-rspan.h` and `.cpp`. Test:
- `createAndRanges`: set key/value ranges, verify
- `selectTestEdges`: verify each of 4 edges can be hit
- `selectTestFill`: verify fill hit
- `drawDoesNotCrash`

Register in `autotest.cpp` and `meson.build`.

- [ ] **Step 6: Run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All tests PASS

- [ ] **Step 7: Commit**

```bash
git add src/items/item-rspan.h src/items/item-rspan.cpp src/qcp.h meson.build \
        tests/auto/test-rspan/ tests/auto/autotest.cpp tests/auto/meson.build
git commit -m "feat: add QCPItemRSpan interactive rectangle span item"
```

## Chunk 3: QCPItemRichText, QCPDataLocator, and calculateAutoMargin

### Task 5: QCPItemRichText

**Files:**
- Create: `src/items/item-richtext.h`
- Create: `src/items/item-richtext.cpp`
- Modify: `src/qcp.h`
- Modify: `meson.build`

- [ ] **Step 1: Create item-richtext.h**

```cpp
#ifndef QCP_ITEM_RICHTEXT_H
#define QCP_ITEM_RICHTEXT_H

#include "item-text.h"
#include <QTextDocument>

class QCPPainter;

class QCP_LIB_DECL QCPItemRichText : public QCPItemText
{
    Q_OBJECT
    Q_PROPERTY(QString html READ html WRITE setHtml)

public:
    explicit QCPItemRichText(QCustomPlot* parentPlot);
    virtual ~QCPItemRichText() override;

    QString html() const { return mHtml; }
    void setHtml(const QString& html);
    void clearHtml();

    virtual double selectTest(const QPointF& pos, bool onlySelectable,
                              QVariant* details = nullptr) const override;

protected:
    virtual void draw(QCPPainter* painter) override;
    virtual QPointF anchorPixelPosition(int anchorId) const override;

private:
    QString mHtml;
    QTextDocument mDoc;
    QRectF mBoundingRect;
    bool mUseHtml = false;

    QRectF computeDrawRect(const QPointF& pos) const;
};

#endif // QCP_ITEM_RICHTEXT_H
```

- [ ] **Step 2: Create item-richtext.cpp**

Key implementation:
- `setHtml()`: sets `mDoc.setHtml(html)`, computes `mBoundingRect` from `mDoc.size()`, sets `mUseHtml = true`
- `clearHtml()`: clears `mHtml`, sets `mUseHtml = false` (use this instead of hiding base `setText()`)
- `draw()`: if `mUseHtml`, render via `mDoc.drawContents(painter)` with padding/rotation from `QCPItemText`. Otherwise delegate to `QCPItemText::draw()`
- `anchorPixelPosition()`: if `mUseHtml`, compute anchors from `mBoundingRect`. Otherwise delegate
- `selectTest()`: if `mUseHtml`, check against `mBoundingRect`. Otherwise delegate

- [ ] **Step 3: Add to build system and qcp.h**

- [ ] **Step 4: Add tests**

Create `tests/auto/test-richtext/test-richtext.h` and `.cpp`. Test:
- `createAndSetHtml`: set HTML, verify `html()` accessor
- `clearHtmlSwitchesToPlainText`: call `setHtml()`, then `clearHtml()`, verify `html()` is empty
- `drawDoesNotCrash`: set HTML, replot
- `drawPlainFallback`: set text (no HTML), replot

Register in `autotest.cpp` and `meson.build`.

- [ ] **Step 5: Run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All tests PASS

- [ ] **Step 6: Commit**

```bash
git add src/items/item-richtext.h src/items/item-richtext.cpp src/qcp.h meson.build \
        tests/auto/test-richtext/ tests/auto/autotest.cpp tests/auto/meson.build
git commit -m "feat: add QCPItemRichText with HTML rendering via QTextDocument"
```

### Task 6: QCPDataLocator

**Files:**
- Create: `src/data-locator.h`
- Create: `src/data-locator.cpp`
- Modify: `src/qcp.h`
- Modify: `meson.build`

- [ ] **Step 1: Create data-locator.h**

```cpp
#ifndef QCP_DATA_LOCATOR_H
#define QCP_DATA_LOCATOR_H

#include "global.h"

class QCPAbstractPlottable;

class QCP_LIB_DECL QCPDataLocator
{
public:
    QCPDataLocator() = default;

    void setPlottable(QCPAbstractPlottable* plottable);
    bool locate(const QPointF& pixelPos);

    bool isValid() const { return mValid; }
    double key() const { return mKey; }
    double value() const { return mValue; }
    double data() const { return mData; }
    int dataIndex() const { return mDataIndex; }
    QCPAbstractPlottable* plottable() const { return mPlottable; }
    QCPAbstractPlottable* hitPlottable() const { return mHitPlottable; }

private:
    QCPAbstractPlottable* mPlottable = nullptr;
    QCPAbstractPlottable* mHitPlottable = nullptr;
    bool mValid = false;
    double mKey = 0;
    double mValue = 0;
    double mData = std::numeric_limits<double>::quiet_NaN();
    int mDataIndex = -1;

    bool locateGraph(const QPointF& pixelPos);
    bool locateGraph2(const QPointF& pixelPos);
    bool locateCurve(const QPointF& pixelPos);
    bool locateColorMap(const QPointF& pixelPos);
    bool locateColorMap2(const QPointF& pixelPos);
    bool locateMultiGraph(const QPointF& pixelPos);
};

#endif // QCP_DATA_LOCATOR_H
```

- [ ] **Step 2: Create data-locator.cpp**

Implementation strategy:
- `setPlottable()`: store pointer, reset valid state
- `locate()`: use `qobject_cast` to detect plottable type, dispatch to type-specific method
- `locateGraph()`: call `selectTest()` with details to get data index, read from `QCPDataContainer<QCPGraphData>`
- `locateGraph2()`: call `selectTest()`, use `QCPAbstractDataSource::keyAt()/valueAt()`
- `locateCurve()`: same as graph but with `QCPCurveData`
- `locateColorMap()`/`locateColorMap2()`: use `pixelsToCoords()` then `data()` at cell
- `locateMultiGraph()`: iterate constituent graphs, find nearest via `selectTest()`, set `mHitPlottable`

- [ ] **Step 3: Add to build system and qcp.h**

Note: `data-locator.h` has no Q_OBJECT so no MOC needed — only add to sources list, not to `neoqcp_moc_headers`.

- [ ] **Step 4: Add tests**

Create `tests/auto/test-data-locator/test-data-locator.h` and `.cpp`. Test:
- `locateOnGraph`: create QCPGraph with known data, locate at known pixel, verify key/value
- `locateOnGraph2`: same with QCPGraph2
- `locateOnEmptyPlottable`: verify `locate()` returns false, `isValid()` returns false
- `locateOnColorMap`: create QCPColorMap with known cells, verify key/value/data

Register in `autotest.cpp` and `meson.build`.

- [ ] **Step 5: Run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All tests PASS

- [ ] **Step 6: Commit**

```bash
git add src/data-locator.h src/data-locator.cpp src/qcp.h meson.build \
        tests/auto/test-data-locator/ tests/auto/autotest.cpp tests/auto/meson.build
git commit -m "feat: add QCPDataLocator utility for nearest-point lookup"
```

### Task 7: Make calculateAutoMargin public

**Files:**
- Modify: `src/layoutelements/layoutelement-axisrect.h`

- [ ] **Step 1: Move calculateAutoMargin from protected to public**

In `src/layoutelements/layoutelement-axisrect.h`, the line:
```cpp
    virtual int calculateAutoMargin(QCP::MarginSide side) override;
```
is currently under `protected:`. Move it to the `public:` section of `QCPAxisRect`.

- [ ] **Step 2: Verify build**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All tests PASS (no behavior change)

- [ ] **Step 3: Commit**

```bash
git add src/layoutelements/layoutelement-axisrect.h
git commit -m "fix: make QCPAxisRect::calculateAutoMargin() public"
```

## Chunk 4: Delete key support and manual test

### Task 8: Delete key forwarding

**Files:**
- Modify: `src/items/item.h` (add `deleteRequested` signal to `QCPAbstractItem`)
- Modify: `src/core.h`
- Modify: `src/core.cpp`

- [ ] **Step 1: Add deleteRequested signal to QCPAbstractItem**

In `src/items/item.h`, add to `QCPAbstractItem`'s `signals:` section:
```cpp
void deleteRequested();
```

This keeps the delete mechanism generic — any item type can emit it, and `QCustomPlot` doesn't need to know about specific span types.

- [ ] **Step 2: Add keyPressEvent override to QCustomPlot**

In `src/core.h`, add under `protected:` (near the other event overrides):
```cpp
virtual void keyPressEvent(QKeyEvent* event) override;
```

- [ ] **Step 3: Implement keyPressEvent**

In `src/core.cpp`:
```cpp
void QCustomPlot::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
    {
        for (auto* item : mItems)
        {
            if (item->selected())
                emit item->deleteRequested();
        }
    }
    QRhiWidget::keyPressEvent(event);
}
```

No span-specific includes needed — the signal lives on `QCPAbstractItem`.

- [ ] **Step 4: Remove per-span deleteRequested signals**

Since `deleteRequested()` is now on `QCPAbstractItem`, remove the duplicate declaration from `QCPItemVSpan`, `QCPItemHSpan`, and `QCPItemRSpan` headers (they inherit it).

- [ ] **Step 5: Verify build**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add src/items/item.h src/core.h src/core.cpp
git commit -m "feat: add deleteRequested signal to QCPAbstractItem and forward Delete key"
```

### Task 9: Manual test for span items

**Files:**
- Create: `tests/manual/spans/main.cpp`
- Create: `tests/manual/spans/meson.build`
- Modify: `tests/manual/meson.build`

- [ ] **Step 1: Create manual test**

```cpp
// tests/manual/spans/main.cpp
#include <QApplication>
#include "../../../src/qcp.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QCustomPlot plot;
    plot.resize(800, 600);
    plot.xAxis->setRange(0, 100);
    plot.yAxis->setRange(0, 100);
    plot.setInteractions(QCP::iSelectItems | QCP::iRangeDrag | QCP::iRangeZoom);

    // vertical span
    auto* vspan = new QCPItemVSpan(&plot);
    vspan->setRange(QCPRange(20, 40));
    vspan->setBrush(QBrush(QColor(0, 100, 255, 60)));
    vspan->setBorderPen(QPen(Qt::blue, 2));

    // horizontal span
    auto* hspan = new QCPItemHSpan(&plot);
    hspan->setRange(QCPRange(30, 60));
    hspan->setBrush(QBrush(QColor(255, 100, 0, 60)));
    hspan->setBorderPen(QPen(Qt::red, 2));

    // rectangle span
    auto* rspan = new QCPItemRSpan(&plot);
    rspan->setKeyRange(QCPRange(60, 80));
    rspan->setValueRange(QCPRange(20, 80));
    rspan->setBrush(QBrush(QColor(0, 200, 0, 60)));
    rspan->setBorderPen(QPen(Qt::green, 2));

    // connect delete
    QObject::connect(vspan, &QCPItemVSpan::deleteRequested, [&]() {
        plot.removeItem(vspan);
        plot.replot();
    });

    plot.setWindowTitle("Span Items Manual Test");
    plot.show();

    return app.exec();
}
```

- [ ] **Step 2: Create meson.build for manual test**

```meson
executable('manual-spans',
    'main.cpp',
    dependencies: [NeoQCP_dep, qtdeps])
```

- [ ] **Step 3: Add subdir to tests/manual/meson.build**

Add `subdir('spans')` to `tests/manual/meson.build`.

- [ ] **Step 4: Build and run manually**

Run: `meson compile -C build && ./build/tests/manual/spans/manual-spans`
Expected: Window with 3 spans. Drag edges, drag fill, select and press Delete.

- [ ] **Step 5: Commit**

```bash
git add tests/manual/spans/
git commit -m "test: add manual test for span items"
```

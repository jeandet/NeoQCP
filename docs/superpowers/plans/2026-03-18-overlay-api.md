# QCPOverlay Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a QCPOverlay widget-level layerable that displays status/error messages on top of a QCustomPlot widget, with configurable size mode, position, opacity, and collapsible toggle.

**Architecture:** QCPOverlay inherits QCPLayerable directly (not an item, not a layout element). Owned by QCustomPlot as a dedicated member, living on a `"notification"` layer (the existing `"overlay"` layer is used by items). Survives `clear()` by design.

**Tech Stack:** C++20, Qt6, QCPLayerable, QPainter, Qt Test

**Spec:** `docs/specs/2026-03-18-overlay-api-design.md`

---

## File Structure

| Action | File | Responsibility |
|--------|------|---------------|
| Create | `src/overlay.h` | QCPOverlay class declaration |
| Create | `src/overlay.cpp` | QCPOverlay implementation |
| Modify | `src/core.h` | Add `mOverlay` member + `overlay()` accessor |
| Modify | `src/core.cpp` | Implement `overlay()` lazy creation + layer reorder in `replot()` |
| Modify | `src/qcp.h` | Add `#include "overlay.h"` |
| Modify | `meson.build` | Add `src/overlay.cpp` to sources, `src/overlay.h` to MOC headers |
| Create | `tests/auto/test-overlay/test-overlay.h` | Test class declaration |
| Create | `tests/auto/test-overlay/test-overlay.cpp` | Test implementations |
| Modify | `tests/auto/autotest.cpp` | Add `#include` and `QCPTEST(TestOverlay)` |
| Modify | `tests/auto/meson.build` | Add test source + header |

---

### Task 1: Scaffold QCPOverlay class with showMessage/clearMessage

**Files:**
- Create: `src/overlay.h`
- Create: `src/overlay.cpp`
- Modify: `src/qcp.h`
- Modify: `meson.build:41-106` (MOC headers list)
- Modify: `meson.build:147-224` (sources list)

- [ ] **Step 1: Write failing test — show and clear message**

Create `tests/auto/test-overlay/test-overlay.h`:
```cpp
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

private:
    QCustomPlot* mPlot;
};
```

Create `tests/auto/test-overlay/test-overlay.cpp`:
```cpp
#include "test-overlay.h"

void TestOverlay::init()
{
    mPlot = new QCustomPlot();
    mPlot->resize(400, 300);
    mPlot->show();
    QTest::qWaitForWindowExposed(mPlot);
}

void TestOverlay::cleanup()
{
    delete mPlot;
}

void TestOverlay::showMessageStoresText()
{
    auto* ov = mPlot->overlay();
    QVERIFY(ov != nullptr);
    ov->showMessage("hello", QCPOverlay::Info);
    QCOMPARE(ov->text(), QString("hello"));
    QCOMPARE(ov->level(), QCPOverlay::Info);
    QVERIFY(ov->visible());
}

void TestOverlay::clearMessageHidesOverlay()
{
    auto* ov = mPlot->overlay();
    ov->showMessage("hello", QCPOverlay::Error);
    ov->clearMessage();
    QCOMPARE(ov->text(), QString());
    QVERIFY(!ov->visible());
}

void TestOverlay::showMessageEmitsSignal()
{
    auto* ov = mPlot->overlay();
    QSignalSpy spy(ov, &QCPOverlay::messageChanged);
    ov->showMessage("test", QCPOverlay::Warning);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QString("test"));
}
```

- [ ] **Step 2: Wire test into build**

Add to `tests/auto/autotest.cpp`:
```cpp
#include "test-overlay/test-overlay.h"
// ... in main():
QCPTEST(TestOverlay);
```

Add to `tests/auto/meson.build` — append to `test_srcs`:
```
'test-overlay/test-overlay.cpp',
```
Append to `test_headers`:
```
'test-overlay/test-overlay.h',
```

- [ ] **Step 3: Run test to verify it fails**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: compilation error — `QCPOverlay` not defined.

- [ ] **Step 4: Create overlay.h**

```cpp
#ifndef QCP_OVERLAY_H
#define QCP_OVERLAY_H

#include "global.h"
#include "layer.h"

class QCPPainter;

class QCP_LIB_DECL QCPOverlay : public QCPLayerable
{
    Q_OBJECT
public:
    enum Level { Info, Warning, Error };
    enum SizeMode { Compact, FitContent, FullWidget };
    enum Position { Top, Bottom, Left, Right };

    explicit QCPOverlay(QCustomPlot* parentPlot);
    ~QCPOverlay() override = default;

    // getters:
    QString text() const { return mText; }
    Level level() const { return mLevel; }
    SizeMode sizeMode() const { return mSizeMode; }
    Position position() const { return mPosition; }
    bool isCollapsible() const { return mCollapsible; }
    bool isCollapsed() const { return mCollapsed; }
    qreal opacity() const { return mOpacity; }
    QFont font() const { return mFont; }

    // actions:
    void showMessage(const QString& text, Level level = Info,
                     SizeMode sizeMode = Compact, Position position = Top);
    void clearMessage();

    // setters:
    void setCollapsible(bool enabled);
    void setCollapsed(bool collapsed);
    void setOpacity(qreal opacity);
    void setFont(const QFont& font);

signals:
    void messageChanged(const QString& text, QCPOverlay::Level level);
    void collapsedChanged(bool collapsed);

protected:
    void applyDefaultAntialiasingHint(QCPPainter* painter) const override;
    void draw(QCPPainter* painter) override;
    double selectTest(const QPointF& pos, bool onlySelectable,
                      QVariant* details = nullptr) const override;
    void mousePressEvent(QMouseEvent* event, const QVariant& details) override;

private:
    QString mText;
    Level mLevel = Info;
    SizeMode mSizeMode = Compact;
    Position mPosition = Top;
    bool mCollapsible = false;
    bool mCollapsed = false;
    qreal mOpacity = 1.0;
    QFont mFont;

    QColor levelColor() const;
    QRect computeRect() const;
    QRect collapseHandleRect() const;

    Q_DISABLE_COPY(QCPOverlay)
};

#endif // QCP_OVERLAY_H
```

- [ ] **Step 5: Create overlay.cpp with minimal implementation**

```cpp
#include "overlay.h"
#include "core.h"
#include "painting/painter.h"

QCPOverlay::QCPOverlay(QCustomPlot* parentPlot)
    : QCPLayerable(parentPlot)
{
    mFont = QApplication::font();
    setVisible(false);
}

void QCPOverlay::showMessage(const QString& text, Level level,
                              SizeMode sizeMode, Position position)
{
    mText = text;
    mLevel = level;
    mSizeMode = sizeMode;
    mPosition = position;
    setVisible(!text.isEmpty());
    emit messageChanged(mText, mLevel);
    if (mParentPlot)
        mParentPlot->replot(QCustomPlot::rpQueuedReplot);
}

void QCPOverlay::clearMessage()
{
    mText.clear();
    setVisible(false);
    emit messageChanged(mText, mLevel);
    if (mParentPlot)
        mParentPlot->replot(QCustomPlot::rpQueuedReplot);
}

void QCPOverlay::setCollapsible(bool enabled)
{
    mCollapsible = enabled;
}

void QCPOverlay::setCollapsed(bool collapsed)
{
    if (mCollapsed == collapsed)
        return;
    mCollapsed = collapsed;
    emit collapsedChanged(mCollapsed);
    if (mParentPlot)
        mParentPlot->replot(QCustomPlot::rpQueuedReplot);
}

void QCPOverlay::setOpacity(qreal opacity)
{
    mOpacity = qBound(0.0, opacity, 1.0);
}

void QCPOverlay::setFont(const QFont& font)
{
    mFont = font;
}

QColor QCPOverlay::levelColor() const
{
    switch (mLevel) {
        case Info:    return QColor(46, 139, 87);    // sea green
        case Warning: return QColor(204, 153, 0);    // amber
        case Error:   return QColor(178, 34, 34);    // firebrick
    }
    return QColor(46, 139, 87);
}

QRect QCPOverlay::computeRect() const
{
    // stub — implemented in Task 3
    return {};
}

QRect QCPOverlay::collapseHandleRect() const
{
    // stub — implemented in Task 4
    return {};
}

void QCPOverlay::applyDefaultAntialiasingHint(QCPPainter* painter) const
{
    painter->setAntialiasing(true);
}

void QCPOverlay::draw(QCPPainter* /*painter*/)
{
    // stub — implemented in Task 3
}

double QCPOverlay::selectTest(const QPointF& /*pos*/, bool /*onlySelectable*/,
                               QVariant* /*details*/) const
{
    return -1; // stub — implemented in Task 4
}

void QCPOverlay::mousePressEvent(QMouseEvent* /*event*/, const QVariant& /*details*/)
{
    // stub — implemented in Task 4
}
```

- [ ] **Step 6: Add to meson.build and qcp.h**

In `meson.build`, add `'src/overlay.h'` to `neoqcp_moc_headers` list (after `'src/global.h'`).
Add `'src/overlay.cpp'` to the `static_library` sources (after `'src/layer.cpp'`).

In `src/qcp.h`, add `#include "overlay.h"` (after `#include "layer.h"`).

- [ ] **Step 7: Run tests to verify they pass**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All 3 new tests pass, existing tests still pass.

- [ ] **Step 8: Commit**

```bash
git add src/overlay.h src/overlay.cpp src/qcp.h meson.build \
    tests/auto/test-overlay/ tests/auto/autotest.cpp tests/auto/meson.build
git commit -m "feat: scaffold QCPOverlay with showMessage/clearMessage"
```

---

### Task 2: Integrate QCPOverlay into QCustomPlot

**Files:**
- Modify: `src/core.h`
- Modify: `src/core.cpp`

- [ ] **Step 1: Write failing test — overlay() accessor and clear() survival**

Add to `test-overlay.h` private slots:
```cpp
void overlayAccessorCreatesLazily();
void overlaySurvivesClear();
```

Add to `test-overlay.cpp`:
```cpp
void TestOverlay::overlayAccessorCreatesLazily()
{
    // First call creates, second returns same instance
    auto* ov1 = mPlot->overlay();
    auto* ov2 = mPlot->overlay();
    QVERIFY(ov1 != nullptr);
    QCOMPARE(ov1, ov2);
    // Verify it's on the topmost layer
    QCOMPARE(ov1->layer()->name(), QString("notification"));
}

void TestOverlay::overlaySurvivesClear()
{
    auto* ov = mPlot->overlay();
    ov->showMessage("persist", QCPOverlay::Info);
    // QCustomPlot has no single clear() — call all three clear methods
    mPlot->clearPlottables();
    mPlot->clearItems();
    mPlot->clearGraphs();
    QCOMPARE(ov->text(), QString("persist"));
    QVERIFY(ov->visible());
    QCOMPARE(mPlot->overlay(), ov);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: compilation error — `overlay()` not a member of `QCustomPlot`.

- [ ] **Step 3: Add overlay() to QCustomPlot**

In `src/core.h`, add in the public section (near other accessor methods):
```cpp
QCPOverlay* overlay();
```

In the protected members section, add:
```cpp
QCPOverlay* mOverlay = nullptr;
```

Add forward declaration before class: `class QCPOverlay;`

In `src/core.cpp`, implement:
```cpp
QCPOverlay* QCustomPlot::overlay()
{
    if (!mOverlay) {
        // Create "notification" layer above all existing layers
        addLayer(QLatin1String("notification"));
        auto* notifLayer = layer(QLatin1String("notification"));
        notifLayer->setMode(QCPLayer::lmBuffered);
        setupPaintBuffers();

        mOverlay = new QCPOverlay(this);
        mOverlay->setLayer(notifLayer);
    }
    return mOverlay;
}
```

- [ ] **Step 4: Ensure notification layer stays topmost in replot()**

In `src/core.cpp`, at the start of `replot()` (after the re-entrancy guard at line ~2057, before `emit beforeReplot`), add:
```cpp
// Ensure notification layer (overlay) is topmost
if (mOverlay) {
    if (auto* notifLayer = layer(QLatin1String("notification"));
        notifLayer && notifLayer != mLayers.last()) {
        mLayers.removeOne(notifLayer);
        mLayers.append(notifLayer);
        updateLayerIndices();
    }
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All 5 overlay tests pass, existing tests still pass.

- [ ] **Step 6: Commit**

```bash
git add src/core.h src/core.cpp tests/auto/test-overlay/
git commit -m "feat: integrate QCPOverlay into QCustomPlot with lazy creation"
```

---

### Task 3: Implement rendering (draw + computeRect)

**Files:**
- Modify: `src/overlay.cpp`

- [ ] **Step 1: Write failing tests — size modes and positions**

Add to `test-overlay.h` private slots:
```cpp
void compactRectIsSingleLine();
void fitContentRectFitsText();
void fullWidgetRectCoversWidget();
void positionTop();
void positionBottom();
void positionLeft();
void positionRight();
void opacityRoundtrip();
void showMessageTriggersReplot();
```

Add to `test-overlay.cpp`:
```cpp
void TestOverlay::compactRectIsSingleLine()
{
    auto* ov = mPlot->overlay();
    ov->showMessage("test", QCPOverlay::Info, QCPOverlay::Compact, QCPOverlay::Top);
    QApplication::processEvents();
    QApplication::processEvents();
    QRect r = ov->overlayRect();
    QFontMetrics fm(ov->font());
    // Compact: height should be single line + padding (2*4px)
    QCOMPARE(r.height(), fm.height() + 8);
    QCOMPARE(r.width(), mPlot->width());
}

void TestOverlay::fitContentRectFitsText()
{
    auto* ov = mPlot->overlay();
    QString longText = "Line one\nLine two\nLine three";
    ov->showMessage(longText, QCPOverlay::Info, QCPOverlay::FitContent, QCPOverlay::Top);
    QApplication::processEvents();
    QApplication::processEvents();
    QRect r = ov->overlayRect();
    QFontMetrics fm(ov->font());
    // FitContent: height accommodates multiple lines
    QVERIFY(r.height() > fm.height() + 8);
    QCOMPARE(r.width(), mPlot->width());
}

void TestOverlay::fullWidgetRectCoversWidget()
{
    auto* ov = mPlot->overlay();
    ov->showMessage("error", QCPOverlay::Error, QCPOverlay::FullWidget);
    QApplication::processEvents();
    QApplication::processEvents();
    QRect r = ov->overlayRect();
    QCOMPARE(r, mPlot->rect());
}

void TestOverlay::positionTop()
{
    auto* ov = mPlot->overlay();
    ov->showMessage("top", QCPOverlay::Info, QCPOverlay::Compact, QCPOverlay::Top);
    QApplication::processEvents();
    QApplication::processEvents();
    QCOMPARE(ov->overlayRect().top(), 0);
}

void TestOverlay::positionBottom()
{
    auto* ov = mPlot->overlay();
    ov->showMessage("bot", QCPOverlay::Info, QCPOverlay::Compact, QCPOverlay::Bottom);
    QApplication::processEvents();
    QApplication::processEvents();
    QCOMPARE(ov->overlayRect().bottom(), mPlot->height() - 1);
}

void TestOverlay::positionLeft()
{
    auto* ov = mPlot->overlay();
    ov->showMessage("left", QCPOverlay::Info, QCPOverlay::Compact, QCPOverlay::Left);
    QApplication::processEvents();
    QApplication::processEvents();
    QRect r = ov->overlayRect();
    QCOMPARE(r.left(), 0);
    QFontMetrics fm(ov->font());
    QCOMPARE(r.width(), fm.height() + 8); // rotated: width = line height
}

void TestOverlay::positionRight()
{
    auto* ov = mPlot->overlay();
    ov->showMessage("right", QCPOverlay::Info, QCPOverlay::Compact, QCPOverlay::Right);
    QApplication::processEvents();
    QApplication::processEvents();
    QCOMPARE(ov->overlayRect().right(), mPlot->width() - 1);
}

void TestOverlay::opacityRoundtrip()
{
    auto* ov = mPlot->overlay();
    ov->setOpacity(0.75);
    QCOMPARE(ov->opacity(), 0.75);
    ov->setOpacity(-0.5);
    QCOMPARE(ov->opacity(), 0.0);
    ov->setOpacity(1.5);
    QCOMPARE(ov->opacity(), 1.0);
}

void TestOverlay::showMessageTriggersReplot()
{
    auto* ov = mPlot->overlay();
    QSignalSpy spy(mPlot, &QCustomPlot::afterReplot);
    ov->showMessage("trigger", QCPOverlay::Info);
    QApplication::processEvents();
    QApplication::processEvents();
    QVERIFY(spy.count() >= 1);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: compilation error — `overlayRect()` not declared.

- [ ] **Step 3: Implement computeRect() and overlayRect() accessor**

Add to `src/overlay.h` public section:
```cpp
QRect overlayRect() const { return computeRect(); }
```

Replace `computeRect()` stub in `src/overlay.cpp`:
```cpp
QRect QCPOverlay::computeRect() const
{
    if (mText.isEmpty() || !mParentPlot)
        return {};

    const QRect viewport = mParentPlot->rect();
    const QFontMetrics fm(mFont);
    constexpr int pad = 4;
    const bool horizontal = (mPosition == Top || mPosition == Bottom);

    if (mSizeMode == FullWidget)
        return viewport;

    int contentSize = 0;
    if (mSizeMode == Compact || mCollapsed) {
        contentSize = fm.height() + 2 * pad;
    } else { // FitContent
        if (horizontal) {
            QRect textBounds = fm.boundingRect(
                QRect(0, 0, viewport.width() - 2 * pad, 0),
                Qt::AlignLeft | Qt::TextWordWrap, mText);
            contentSize = textBounds.height() + 2 * pad;
        } else {
            QRect textBounds = fm.boundingRect(
                QRect(0, 0, viewport.height() - 2 * pad, 0),
                Qt::AlignLeft | Qt::TextWordWrap, mText);
            contentSize = textBounds.height() + 2 * pad;
        }
    }

    switch (mPosition) {
        case Top:
            return QRect(viewport.left(), viewport.top(),
                         viewport.width(), contentSize);
        case Bottom:
            return QRect(viewport.left(), viewport.bottom() - contentSize + 1,
                         viewport.width(), contentSize);
        case Left:
            return QRect(viewport.left(), viewport.top(),
                         contentSize, viewport.height());
        case Right:
            return QRect(viewport.right() - contentSize + 1, viewport.top(),
                         contentSize, viewport.height());
    }
    return {};
}
```

- [ ] **Step 4: Implement draw()**

Replace `draw()` stub in `src/overlay.cpp`:
```cpp
void QCPOverlay::draw(QCPPainter* painter)
{
    if (mText.isEmpty())
        return;
    // Skip during export
    if (painter->modes().testFlag(QCPPainter::pmNoCaching))
        return;

    const QRect rect = computeRect();
    if (rect.isEmpty())
        return;

    painter->save();

    // Background
    QColor bg = levelColor();
    bg.setAlphaF(mOpacity);
    painter->setPen(Qt::NoPen);
    painter->setBrush(bg);
    painter->drawRoundedRect(rect, 4, 4);

    // Text
    constexpr int pad = 4;
    painter->setPen(Qt::white);
    painter->setFont(mFont);

    const bool horizontal = (mPosition == Top || mPosition == Bottom);
    const QString displayText = mCollapsed ? mText.section('\n', 0, 0) : mText;

    if (horizontal) {
        QRect textRect = rect.adjusted(pad, pad, -pad, -pad);
        int flags = Qt::AlignLeft | Qt::AlignVCenter;
        if (mSizeMode != Compact && !mCollapsed)
            flags = Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap;
        painter->drawText(textRect, flags, displayText);
    } else {
        // Rotated text for Left/Right
        painter->translate(rect.center());
        if (mPosition == Left)
            painter->rotate(-90); // bottom-to-top
        else
            painter->rotate(90);  // top-to-bottom
        QRect textRect(-rect.height() / 2, -rect.width() / 2,
                       rect.height(), rect.width());
        textRect.adjust(pad, pad, -pad, -pad);
        int flags = Qt::AlignLeft | Qt::AlignVCenter;
        if (mSizeMode != Compact && !mCollapsed)
            flags = Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap;
        painter->drawText(textRect, flags, displayText);
    }

    // Collapse handle
    if (mCollapsible) {
        QRect handleRect = collapseHandleRect();
        painter->setPen(QPen(Qt::white, 1.5));
        painter->setBrush(Qt::NoBrush);
        QPointF center = handleRect.center();
        int sz = 4;
        if (horizontal) {
            if (mCollapsed) {
                // ▼ pointing down
                painter->drawLine(QPointF(center.x() - sz, center.y() - sz/2),
                                  QPointF(center.x(), center.y() + sz/2));
                painter->drawLine(QPointF(center.x(), center.y() + sz/2),
                                  QPointF(center.x() + sz, center.y() - sz/2));
            } else {
                // ▲ pointing up
                painter->drawLine(QPointF(center.x() - sz, center.y() + sz/2),
                                  QPointF(center.x(), center.y() - sz/2));
                painter->drawLine(QPointF(center.x(), center.y() - sz/2),
                                  QPointF(center.x() + sz, center.y() + sz/2));
            }
        } else {
            if (mCollapsed) {
                // ▶ or ◀ depending on side
                int dir = (mPosition == Left) ? 1 : -1;
                painter->drawLine(QPointF(center.x() - dir*sz/2, center.y() - sz),
                                  QPointF(center.x() + dir*sz/2, center.y()));
                painter->drawLine(QPointF(center.x() + dir*sz/2, center.y()),
                                  QPointF(center.x() - dir*sz/2, center.y() + sz));
            } else {
                int dir = (mPosition == Left) ? -1 : 1;
                painter->drawLine(QPointF(center.x() - dir*sz/2, center.y() - sz),
                                  QPointF(center.x() + dir*sz/2, center.y()));
                painter->drawLine(QPointF(center.x() + dir*sz/2, center.y()),
                                  QPointF(center.x() - dir*sz/2, center.y() + sz));
            }
        }
    }

    painter->restore();
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All overlay tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/overlay.h src/overlay.cpp tests/auto/test-overlay/
git commit -m "feat: implement QCPOverlay rendering with size modes and positions"
```

---

### Task 4: Implement mouse interaction (collapse toggle + click blocking)

**Files:**
- Modify: `src/overlay.cpp`

- [ ] **Step 1: Write failing tests — collapse click and pass-through**

Add to `test-overlay.h` private slots:
```cpp
void collapseToggle();
void clickPassThroughCompact();
void clickBlockedFullWidget();
void overlayStaysTopmost();
```

Add to `test-overlay.cpp`:
```cpp
void TestOverlay::collapseToggle()
{
    auto* ov = mPlot->overlay();
    ov->showMessage("Line one\nLine two\nLine three",
                    QCPOverlay::Info, QCPOverlay::FitContent, QCPOverlay::Top);
    ov->setCollapsible(true);
    QVERIFY(!ov->isCollapsed());

    QSignalSpy spy(ov, &QCPOverlay::collapsedChanged);
    ov->setCollapsed(true);
    QVERIFY(ov->isCollapsed());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toBool(), true);

    // Toggling again
    ov->setCollapsed(false);
    QVERIFY(!ov->isCollapsed());
    QCOMPARE(spy.count(), 2);
}

void TestOverlay::clickPassThroughCompact()
{
    auto* ov = mPlot->overlay();
    ov->showMessage("status", QCPOverlay::Info, QCPOverlay::Compact, QCPOverlay::Top);
    QApplication::processEvents();
    QApplication::processEvents();

    // Click in the middle of the overlay — should pass through (selectTest returns -1)
    QRect r = ov->overlayRect();
    QPointF center = r.center();
    double dist = ov->selectTest(center, false);
    QCOMPARE(dist, -1.0);
}

void TestOverlay::clickBlockedFullWidget()
{
    auto* ov = mPlot->overlay();
    ov->showMessage("error", QCPOverlay::Error, QCPOverlay::FullWidget);
    QApplication::processEvents();
    QApplication::processEvents();

    // Click anywhere in widget — should be blocked (selectTest returns 0)
    QPointF center = mPlot->rect().center();
    double dist = ov->selectTest(center, false);
    QCOMPARE(dist, 0.0);
}

void TestOverlay::overlayStaysTopmost()
{
    mPlot->overlay(); // create overlay
    mPlot->addLayer("userLayer"); // add a new layer
    mPlot->replot();
    QApplication::processEvents();

    auto* notifLayer = mPlot->layer("notification");
    QVERIFY(notifLayer != nullptr);
    // notification layer should be last (topmost)
    QCOMPARE(notifLayer->index(), mPlot->layerCount() - 1);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: `clickBlockedFullWidget` fails (selectTest returns -1 instead of 0).

- [ ] **Step 3: Implement selectTest() and collapseHandleRect()**

Replace stubs in `src/overlay.cpp`:
```cpp
QRect QCPOverlay::collapseHandleRect() const
{
    if (!mCollapsible)
        return {};
    const QRect rect = computeRect();
    if (rect.isEmpty())
        return {};

    constexpr int handleSize = 20;
    const bool horizontal = (mPosition == Top || mPosition == Bottom);
    if (horizontal) {
        // Handle at the right edge, vertically centered
        return QRect(rect.right() - handleSize, rect.top(),
                     handleSize, rect.height());
    } else {
        // Handle at the bottom edge, horizontally centered
        return QRect(rect.left(), rect.bottom() - handleSize,
                     rect.width(), handleSize);
    }
}

double QCPOverlay::selectTest(const QPointF& pos, bool /*onlySelectable*/,
                               QVariant* /*details*/) const
{
    if (mText.isEmpty() || !visible())
        return -1;

    const QRect rect = computeRect();
    if (!rect.contains(pos.toPoint()))
        return -1;

    // FullWidget blocks all clicks
    if (mSizeMode == FullWidget)
        return 0;

    // Other modes: only the collapse handle is interactive
    if (mCollapsible && collapseHandleRect().contains(pos.toPoint()))
        return 0;

    return -1; // pass through
}

void QCPOverlay::mousePressEvent(QMouseEvent* event, const QVariant& /*details*/)
{
    if (mCollapsible && collapseHandleRect().contains(event->pos()))
        setCollapsed(!mCollapsed);
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All overlay tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/overlay.cpp tests/auto/test-overlay/
git commit -m "feat: implement QCPOverlay mouse interaction and click blocking"
```

---

### Task 5: Export exclusion test

**Files:**
- Modify: `tests/auto/test-overlay/test-overlay.cpp`

- [ ] **Step 1: Write test — overlay excluded from export**

Add to `test-overlay.h` private slots:
```cpp
void overlayExcludedFromExport();
```

Add to `test-overlay.cpp`:
```cpp
void TestOverlay::overlayExcludedFromExport()
{
    auto* ov = mPlot->overlay();
    ov->showMessage("should not appear", QCPOverlay::Error, QCPOverlay::FullWidget);
    QApplication::processEvents();
    QApplication::processEvents();

    // Export to pixmap — overlay should be excluded
    // We verify by checking that the center pixel is NOT the error color
    QPixmap px = mPlot->toPixmap(400, 300);
    QImage img = px.toImage();
    QColor centerColor = img.pixelColor(200, 150);
    QColor errorColor = QColor(178, 34, 34); // firebrick
    QVERIFY(centerColor != errorColor);
}
```

- [ ] **Step 2: Run test to verify it passes**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: passes (draw already bails on `pmNoCaching`).

- [ ] **Step 3: Commit**

```bash
git add tests/auto/test-overlay/
git commit -m "test: verify overlay is excluded from exports"
```

---

### Task 6: Update spec with layer name change

The spec says `"overlay"` layer but we use `"notification"` (since `"overlay"` already exists). Update the spec to match.

**Files:**
- Modify: `docs/specs/2026-03-18-overlay-api-design.md`

- [ ] **Step 1: Update spec**

Replace all references to `"overlay"` layer with `"notification"` layer. Add a note explaining why:

> Note: The layer is named `"notification"` rather than `"overlay"` because QCustomPlot already creates a default `"overlay"` layer used by items like tracers. The `"notification"` layer sits above it.

- [ ] **Step 2: Commit**

```bash
git add docs/specs/2026-03-18-overlay-api-design.md
git commit -m "docs: update overlay spec — layer name is 'notification'"
```

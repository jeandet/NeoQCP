# QCPTheme Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `QCPTheme` class with `Q_PROPERTY`s for semantic color roles, exposable via QSS on `QCustomPlot`.

**Architecture:** `QCPTheme` is a `QObject` holding 7 color properties. `QCustomPlot` owns a default theme and exposes QSS proxy properties that delegate to it. Theme changes are coalesced via `QTimer::singleShot(0)` and propagated to all child elements by `applyTheme()`, which modifies only pen colors (preserving style/width/cap).

**Tech Stack:** C++20, Qt6 (QObject, Q_PROPERTY, QTimer, QPointer), Meson, Qt Test

**Spec:** `docs/plans/2026-03-13-theming-support-design.md`

---

## File Structure

| File | Responsibility |
|---|---|
| `src/theme.h` (new) | `QCPTheme` class: 7 color Q_PROPERTYs, `changed()` signal, `light()`/`dark()` factories |
| `src/theme.cpp` (new) | `QCPTheme` implementation |
| `src/core.h` (modify) | Add `mTheme`, `mOwnedTheme`, proxy Q_PROPERTYs, `setTheme()`, `applyTheme()`, `backgroundBrush()` |
| `src/core.cpp` (modify) | Implement theme integration in constructor, `setTheme()`, `applyTheme()` |
| `src/qcp.h` (modify) | Add `#include "theme.h"` |
| `meson.build` (modify) | Add `src/theme.h` to moc headers, `src/theme.cpp` to sources |
| `tests/auto/test-theme/test-theme.h` (new) | Theme test class |
| `tests/auto/test-theme/test-theme.cpp` (new) | Theme test implementation |
| `tests/auto/autotest.cpp` (modify) | Add theme test |
| `tests/auto/meson.build` (modify) | Add theme test files |

---

## Task 1: QCPTheme class — test + implementation

**Files:**
- Create: `src/theme.h`
- Create: `src/theme.cpp`
- Modify: `src/qcp.h` (line 11, add include)
- Modify: `meson.build` (line 51 for moc, line 139 for source)
- Create: `tests/auto/test-theme/test-theme.h`
- Create: `tests/auto/test-theme/test-theme.cpp`
- Modify: `tests/auto/autotest.cpp`
- Modify: `tests/auto/meson.build`

- [ ] **Step 1: Write test-theme.h**

```cpp
// tests/auto/test-theme/test-theme.h
#include <QtTest/QtTest>
#include "qcustomplot.h"

class TestTheme : public QObject
{
    Q_OBJECT
private slots:
    void lightFactoryDefaults();
    void darkFactoryColors();
    void changedSignalEmitted();
};
```

- [ ] **Step 2: Write test-theme.cpp**

```cpp
// tests/auto/test-theme/test-theme.cpp
#include "test-theme.h"

void TestTheme::lightFactoryDefaults()
{
    auto theme = QCPTheme::light();
    QCOMPARE(theme->background(), QColor(Qt::white));
    QCOMPARE(theme->foreground(), QColor(Qt::black));
    QCOMPARE(theme->grid(), QColor(200, 200, 200));
    QCOMPARE(theme->subGrid(), QColor(220, 220, 220));
    QCOMPARE(theme->selection(), QColor(Qt::blue));
    QCOMPARE(theme->legendBackground(), QColor(Qt::white));
    QCOMPARE(theme->legendBorder(), QColor(Qt::black));
    delete theme;
}

void TestTheme::darkFactoryColors()
{
    auto theme = QCPTheme::dark();
    QCOMPARE(theme->background(), QColor("#1e1e1e"));
    QCOMPARE(theme->foreground(), QColor("#e0e0e0"));
    QCOMPARE(theme->grid(), QColor("#3a3a3a"));
    QCOMPARE(theme->subGrid(), QColor("#2a2a2a"));
    QCOMPARE(theme->selection(), QColor("#4a9eff"));
    QCOMPARE(theme->legendBackground(), QColor("#2a2a2a"));
    QCOMPARE(theme->legendBorder(), QColor("#555555"));
    delete theme;
}

void TestTheme::changedSignalEmitted()
{
    QCPTheme theme;
    QSignalSpy spy(&theme, &QCPTheme::changed);
    theme.setBackground(Qt::red);
    QCOMPARE(spy.count(), 1);
    theme.setForeground(Qt::green);
    QCOMPARE(spy.count(), 2);
}
```

- [ ] **Step 3: Register test in autotest.cpp and meson.build**

In `tests/auto/autotest.cpp`, add:
```cpp
#include "test-theme/test-theme.h"
// ... in main():
QCPTEST(TestTheme);
```

In `tests/auto/meson.build`, add to `test_srcs`:
```
'test-theme/test-theme.cpp',
```
Add to `test_headers`:
```
'test-theme/test-theme.h',
```

- [ ] **Step 4: Write src/theme.h**

```cpp
#ifndef QCP_THEME_H
#define QCP_THEME_H

#include "global.h"
#include <QColor>
#include <QObject>

class QCP_LIB_DECL QCPTheme : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QColor background READ background WRITE setBackground NOTIFY changed)
    Q_PROPERTY(QColor foreground READ foreground WRITE setForeground NOTIFY changed)
    Q_PROPERTY(QColor grid READ grid WRITE setGrid NOTIFY changed)
    Q_PROPERTY(QColor subGrid READ subGrid WRITE setSubGrid NOTIFY changed)
    Q_PROPERTY(QColor selection READ selection WRITE setSelection NOTIFY changed)
    Q_PROPERTY(QColor legendBackground READ legendBackground WRITE setLegendBackground NOTIFY changed)
    Q_PROPERTY(QColor legendBorder READ legendBorder WRITE setLegendBorder NOTIFY changed)

public:
    explicit QCPTheme(QObject* parent = nullptr);

    QColor background() const { return mBackground; }
    QColor foreground() const { return mForeground; }
    QColor grid() const { return mGrid; }
    QColor subGrid() const { return mSubGrid; }
    QColor selection() const { return mSelection; }
    QColor legendBackground() const { return mLegendBackground; }
    QColor legendBorder() const { return mLegendBorder; }

    void setBackground(const QColor& color);
    void setForeground(const QColor& color);
    void setGrid(const QColor& color);
    void setSubGrid(const QColor& color);
    void setSelection(const QColor& color);
    void setLegendBackground(const QColor& color);
    void setLegendBorder(const QColor& color);

    static QCPTheme* light(QObject* parent = nullptr);
    static QCPTheme* dark(QObject* parent = nullptr);

signals:
    void changed();

private:
    QColor mBackground;
    QColor mForeground;
    QColor mGrid;
    QColor mSubGrid;
    QColor mSelection;
    QColor mLegendBackground;
    QColor mLegendBorder;
};

#endif // QCP_THEME_H
```

- [ ] **Step 5: Write src/theme.cpp**

```cpp
#include "theme.h"

QCPTheme::QCPTheme(QObject* parent)
    : QObject(parent)
    , mBackground(Qt::white)
    , mForeground(Qt::black)
    , mGrid(200, 200, 200)
    , mSubGrid(220, 220, 220)
    , mSelection(Qt::blue)
    , mLegendBackground(Qt::white)
    , mLegendBorder(Qt::black)
{
}

void QCPTheme::setBackground(const QColor& color)
{
    if (mBackground != color) { mBackground = color; emit changed(); }
}

void QCPTheme::setForeground(const QColor& color)
{
    if (mForeground != color) { mForeground = color; emit changed(); }
}

void QCPTheme::setGrid(const QColor& color)
{
    if (mGrid != color) { mGrid = color; emit changed(); }
}

void QCPTheme::setSubGrid(const QColor& color)
{
    if (mSubGrid != color) { mSubGrid = color; emit changed(); }
}

void QCPTheme::setSelection(const QColor& color)
{
    if (mSelection != color) { mSelection = color; emit changed(); }
}

void QCPTheme::setLegendBackground(const QColor& color)
{
    if (mLegendBackground != color) { mLegendBackground = color; emit changed(); }
}

void QCPTheme::setLegendBorder(const QColor& color)
{
    if (mLegendBorder != color) { mLegendBorder = color; emit changed(); }
}

QCPTheme* QCPTheme::light(QObject* parent)
{
    return new QCPTheme(parent); // defaults are already the light theme
}

QCPTheme* QCPTheme::dark(QObject* parent)
{
    auto* theme = new QCPTheme(parent);
    // Block signals to avoid emitting changed() for each setter during batch init
    theme->blockSignals(true);
    theme->setBackground(QColor("#1e1e1e"));
    theme->setForeground(QColor("#e0e0e0"));
    theme->setGrid(QColor("#3a3a3a"));
    theme->setSubGrid(QColor("#2a2a2a"));
    theme->setSelection(QColor("#4a9eff"));
    theme->setLegendBackground(QColor("#2a2a2a"));
    theme->setLegendBorder(QColor("#555555"));
    theme->blockSignals(false);
    return theme;
}
```

- [ ] **Step 6: Add to meson.build**

In `meson.build`, add `'src/theme.h'` to `neoqcp_moc_headers` (after line 51, `'src/colorgradient.h'`):
```
'src/theme.h',
```

Add `'src/theme.cpp'` to the static_library sources (after line 139, `'src/colorgradient.cpp'`):
```
'src/theme.cpp',
```

- [ ] **Step 7: Add include to src/qcp.h**

After line 11 (`#include "colorgradient.h"`), add:
```cpp
#include "theme.h"
```

- [ ] **Step 8: Build and run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: all tests pass including `TestTheme::lightFactoryDefaults`, `darkFactoryColors`, `changedSignalEmitted`

- [ ] **Step 9: Commit**

```bash
git add src/theme.h src/theme.cpp src/qcp.h meson.build \
       tests/auto/test-theme/ tests/auto/autotest.cpp tests/auto/meson.build
git commit -m "feat: add QCPTheme class with color roles and light/dark factories"
```

---

## Task 2: QCustomPlot integration — applyTheme()

**Files:**
- Modify: `src/core.h` (lines 54-67 for Q_PROPERTYs, ~120 for backgroundBrush accessor, ~293 for members)
- Modify: `src/core.cpp` (constructor ~401, new methods)
- Modify: `tests/auto/test-theme/test-theme.h`
- Modify: `tests/auto/test-theme/test-theme.cpp`

- [ ] **Step 1: Add propagation tests to test-theme.h**

Replace the test header with:
```cpp
// tests/auto/test-theme/test-theme.h
#include <QtTest/QtTest>
#include "qcustomplot.h"

class TestTheme : public QObject
{
    Q_OBJECT
private slots:
    void lightFactoryDefaults();
    void darkFactoryColors();
    void changedSignalEmitted();
    void init();
    void cleanup();
    void applyThemePropagatesBackground();
    void applyThemePropagatesAxisColors();
    void applyThemePropagatesGridColors();
    void applyThemePropagatesLegendColors();
    void applyThemePropagatesTextElementColor();
    void applyThemePreservesPenStyle();
    void perElementOverrideAfterTheme();

private:
    QCustomPlot* mPlot;
};
```

Note: `lightFactoryDefaults`, `darkFactoryColors`, `changedSignalEmitted` don't use `mPlot` so they run fine with `init()`/`cleanup()` creating/destroying it around them.

- [ ] **Step 2: Write propagation tests in test-theme.cpp**

Append to `test-theme.cpp`:

```cpp
void TestTheme::init()
{
    mPlot = new QCustomPlot(nullptr);
    mPlot->show();
}

void TestTheme::cleanup()
{
    delete mPlot;
}

void TestTheme::applyThemePropagatesBackground()
{
    auto* theme = QCPTheme::dark();
    mPlot->setTheme(theme);
    QCOMPARE(mPlot->backgroundBrush().color(), QColor("#1e1e1e"));
    delete theme;
}

void TestTheme::applyThemePropagatesAxisColors()
{
    auto* theme = QCPTheme::dark();
    mPlot->setTheme(theme);
    auto* axis = mPlot->xAxis;
    QCOMPARE(axis->basePen().color(), QColor("#e0e0e0"));
    QCOMPARE(axis->tickPen().color(), QColor("#e0e0e0"));
    QCOMPARE(axis->subTickPen().color(), QColor("#e0e0e0"));
    QCOMPARE(axis->labelColor(), QColor("#e0e0e0"));
    QCOMPARE(axis->tickLabelColor(), QColor("#e0e0e0"));
    delete theme;
}

void TestTheme::applyThemePropagatesGridColors()
{
    auto* theme = QCPTheme::dark();
    mPlot->setTheme(theme);
    auto* grid = mPlot->xAxis->grid();
    QCOMPARE(grid->pen().color(), QColor("#3a3a3a"));
    QCOMPARE(grid->subGridPen().color(), QColor("#2a2a2a"));
    QCOMPARE(grid->zeroLinePen().color(), QColor("#3a3a3a"));
    delete theme;
}

void TestTheme::applyThemePropagatesLegendColors()
{
    auto* theme = QCPTheme::dark();
    mPlot->setTheme(theme);
    QCOMPARE(mPlot->legend->brush().color(), QColor("#2a2a2a"));
    QCOMPARE(mPlot->legend->borderPen().color(), QColor("#555555"));
    QCOMPARE(mPlot->legend->textColor(), QColor("#e0e0e0"));
    delete theme;
}

void TestTheme::applyThemePropagatesTextElementColor()
{
    auto* title = new QCPTextElement(mPlot, "Test Title");
    mPlot->plotLayout()->insertRow(0);
    mPlot->plotLayout()->addElement(0, 0, title);
    auto* theme = QCPTheme::dark();
    mPlot->setTheme(theme);
    QCOMPARE(title->textColor(), QColor("#e0e0e0"));
    QCOMPARE(title->selectedTextColor(), QColor("#4a9eff"));
    delete theme;
}

void TestTheme::applyThemePreservesPenStyle()
{
    auto* theme = QCPTheme::dark();
    mPlot->setTheme(theme);
    // Grid pen should keep DotLine style, only color changes
    QCOMPARE(mPlot->xAxis->grid()->pen().style(), Qt::DotLine);
    // Axis base pen should keep SquareCap
    QCOMPARE(mPlot->xAxis->basePen().capStyle(), Qt::SquareCap);
    delete theme;
}

void TestTheme::perElementOverrideAfterTheme()
{
    auto* theme = QCPTheme::dark();
    mPlot->setTheme(theme);
    // Override one element after theme
    mPlot->xAxis->setLabelColor(Qt::yellow);
    QCOMPARE(mPlot->xAxis->labelColor(), QColor(Qt::yellow));
    // Other themed properties unchanged
    QCOMPARE(mPlot->xAxis->basePen().color(), QColor("#e0e0e0"));
    delete theme;
}
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: FAIL — `setTheme`, `applyTheme`, `backgroundBrush` don't exist yet

- [ ] **Step 4: Add theme members, Q_PROPERTYs, and backgroundBrush() to src/core.h**

After existing Q_PROPERTYs (line 67), add:
```cpp
    Q_PROPERTY(QColor themeBackground READ themeBackground WRITE setThemeBackground)
    Q_PROPERTY(QColor themeForeground READ themeForeground WRITE setThemeForeground)
    Q_PROPERTY(QColor themeGrid READ themeGrid WRITE setThemeGrid)
    Q_PROPERTY(QColor themeSubGrid READ themeSubGrid WRITE setThemeSubGrid)
    Q_PROPERTY(QColor themeSelection READ themeSelection WRITE setThemeSelection)
    Q_PROPERTY(QColor themeLegendBackground READ themeLegendBackground WRITE setThemeLegendBackground)
    Q_PROPERTY(QColor themeLegendBorder READ themeLegendBorder WRITE setThemeLegendBorder)
```

In the getters section (near line 120, after `QPixmap background()`), add:
```cpp
    QBrush backgroundBrush() const { return mBackgroundBrush; }
```

In the public section, add:
```cpp
    // theme
    QCPTheme* theme() const;
    void setTheme(QCPTheme* theme);
    void applyTheme();

    QColor themeBackground() const;
    void setThemeBackground(const QColor& color);
    QColor themeForeground() const;
    void setThemeForeground(const QColor& color);
    QColor themeGrid() const;
    void setThemeGrid(const QColor& color);
    QColor themeSubGrid() const;
    void setThemeSubGrid(const QColor& color);
    QColor themeSelection() const;
    void setThemeSelection(const QColor& color);
    QColor themeLegendBackground() const;
    void setThemeLegendBackground(const QColor& color);
    QColor themeLegendBorder() const;
    void setThemeLegendBorder(const QColor& color);
```

In the protected member section (near line 293), add:
```cpp
    QCPTheme* mOwnedTheme;
    QPointer<QCPTheme> mTheme;
    bool mThemeDirty;
```

Add `#include <QPointer>` to the includes at the top of `core.h`.

- [ ] **Step 5: Implement theme methods in src/core.cpp**

Add `#include <QTimer>` to `core.cpp` includes.

In the constructor initialization list (after `mBackgroundBrush(Qt::white, Qt::SolidPattern)` at line 401), add:
```cpp
    , mOwnedTheme(nullptr)
    , mThemeDirty(false)
```

In the constructor body (after existing initialization, before the end), add:
```cpp
    mOwnedTheme = new QCPTheme(this);
    mTheme = mOwnedTheme;
    connect(mTheme, &QCPTheme::changed, this, [this]() {
        if (!mThemeDirty) {
            mThemeDirty = true;
            QTimer::singleShot(0, this, [this]() {
                mThemeDirty = false;
                applyTheme();
            });
        }
    });
```

Implement the methods:

```cpp
QCPTheme* QCustomPlot::theme() const
{
    return mTheme;
}

void QCustomPlot::setTheme(QCPTheme* theme)
{
    if (mTheme == theme)
        return;
    if (mTheme)
        disconnect(mTheme, &QCPTheme::changed, this, nullptr);
    mTheme = theme ? theme : mOwnedTheme;
    connect(mTheme, &QCPTheme::changed, this, [this]() {
        if (!mThemeDirty) {
            mThemeDirty = true;
            QTimer::singleShot(0, this, [this]() {
                mThemeDirty = false;
                applyTheme();
            });
        }
    });
    applyTheme();
}

void QCustomPlot::applyTheme()
{
    if (!mTheme)
        return;

    // Background
    mBackgroundBrush.setColor(mTheme->background());

    // Axes and grids
    for (auto* rect : axisRects()) {
        for (auto* axis : rect->axes()) {
            // Axis pens — preserve style/width/cap, change only color
            QPen bp = axis->basePen();    bp.setColor(mTheme->foreground()); axis->setBasePen(bp);
            QPen tp = axis->tickPen();    tp.setColor(mTheme->foreground()); axis->setTickPen(tp);
            QPen sp = axis->subTickPen(); sp.setColor(mTheme->foreground()); axis->setSubTickPen(sp);
            axis->setLabelColor(mTheme->foreground());
            axis->setTickLabelColor(mTheme->foreground());

            // Selected axis pens
            QPen sbp = axis->selectedBasePen();    sbp.setColor(mTheme->selection()); axis->setSelectedBasePen(sbp);
            QPen stp = axis->selectedTickPen();    stp.setColor(mTheme->selection()); axis->setSelectedTickPen(stp);
            QPen ssp = axis->selectedSubTickPen(); ssp.setColor(mTheme->selection()); axis->setSelectedSubTickPen(ssp);
            axis->setSelectedLabelColor(mTheme->selection());
            axis->setSelectedTickLabelColor(mTheme->selection());

            // Grid — preserve DotLine/SolidLine style
            auto* grid = axis->grid();
            QPen gp = grid->pen();         gp.setColor(mTheme->grid());    grid->setPen(gp);
            QPen sgp = grid->subGridPen(); sgp.setColor(mTheme->subGrid()); grid->setSubGridPen(sgp);
            QPen zlp = grid->zeroLinePen(); zlp.setColor(mTheme->grid());  grid->setZeroLinePen(zlp);
        }
    }

    // Legend
    if (mLegend) {
        mLegend->setBrush(QBrush(mTheme->legendBackground()));
        QPen lbp = mLegend->borderPen(); lbp.setColor(mTheme->legendBorder()); mLegend->setBorderPen(lbp);
        mLegend->setTextColor(mTheme->foreground());
        mLegend->setSelectedTextColor(mTheme->selection());
        QPen slbp = mLegend->selectedBorderPen(); slbp.setColor(mTheme->selection()); mLegend->setSelectedBorderPen(slbp);
    }

    // Walk the full layout tree for text elements
    std::function<void(QCPLayoutElement*)> walkLayout = [&](QCPLayoutElement* el) {
        if (!el) return;
        if (auto* te = qobject_cast<QCPTextElement*>(el)) {
            te->setTextColor(mTheme->foreground());
            te->setSelectedTextColor(mTheme->selection());
        }
        if (auto* layout = qobject_cast<QCPLayout*>(el)) {
            for (int i = 0; i < layout->elementCount(); ++i)
                walkLayout(layout->elementAt(i));
        }
    };
    walkLayout(plotLayout());

    // Selection rect
    if (mSelectionRect) {
        QPen srp = mSelectionRect->pen(); srp.setColor(mTheme->selection()); mSelectionRect->setPen(srp);
    }

    replot(QCustomPlot::rpQueuedReplot);
}

// QSS proxy property implementations
QColor QCustomPlot::themeBackground() const { return mTheme ? mTheme->background() : QColor(); }
void QCustomPlot::setThemeBackground(const QColor& c) { if (mTheme) mTheme->setBackground(c); }

QColor QCustomPlot::themeForeground() const { return mTheme ? mTheme->foreground() : QColor(); }
void QCustomPlot::setThemeForeground(const QColor& c) { if (mTheme) mTheme->setForeground(c); }

QColor QCustomPlot::themeGrid() const { return mTheme ? mTheme->grid() : QColor(); }
void QCustomPlot::setThemeGrid(const QColor& c) { if (mTheme) mTheme->setGrid(c); }

QColor QCustomPlot::themeSubGrid() const { return mTheme ? mTheme->subGrid() : QColor(); }
void QCustomPlot::setThemeSubGrid(const QColor& c) { if (mTheme) mTheme->setSubGrid(c); }

QColor QCustomPlot::themeSelection() const { return mTheme ? mTheme->selection() : QColor(); }
void QCustomPlot::setThemeSelection(const QColor& c) { if (mTheme) mTheme->setSelection(c); }

QColor QCustomPlot::themeLegendBackground() const { return mTheme ? mTheme->legendBackground() : QColor(); }
void QCustomPlot::setThemeLegendBackground(const QColor& c) { if (mTheme) mTheme->setLegendBackground(c); }

QColor QCustomPlot::themeLegendBorder() const { return mTheme ? mTheme->legendBorder() : QColor(); }
void QCustomPlot::setThemeLegendBorder(const QColor& c) { if (mTheme) mTheme->setLegendBorder(c); }
```

- [ ] **Step 6: Build and run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: all tests pass

- [ ] **Step 7: Commit**

```bash
git add src/core.h src/core.cpp tests/auto/test-theme/
git commit -m "feat: integrate QCPTheme into QCustomPlot with applyTheme() and QSS proxy properties"
```

---

## Task 3: Theme sharing, QSS proxy, and coalescing tests

**Files:**
- Modify: `tests/auto/test-theme/test-theme.h`
- Modify: `tests/auto/test-theme/test-theme.cpp`

- [ ] **Step 1: Add sharing and proxy tests to test-theme.h**

Add to private slots:
```cpp
void themeSharing();
void setThemeNull_revertsToOwned();
void qssProxyProperties();
void coalescing();
```

- [ ] **Step 2: Write the tests**

```cpp
void TestTheme::themeSharing()
{
    auto* plot2 = new QCustomPlot(nullptr);
    plot2->show();
    auto* theme = QCPTheme::dark();
    mPlot->setTheme(theme);
    plot2->setTheme(theme);

    QCOMPARE(mPlot->backgroundBrush().color(), QColor("#1e1e1e"));
    QCOMPARE(plot2->backgroundBrush().color(), QColor("#1e1e1e"));

    // Change theme, both plots update (after event loop processes coalesced timer)
    theme->setBackground(Qt::red);
    QCoreApplication::processEvents();

    QCOMPARE(mPlot->backgroundBrush().color(), QColor(Qt::red));
    QCOMPARE(plot2->backgroundBrush().color(), QColor(Qt::red));

    // Revert before deleting to avoid dangling QPointer access
    mPlot->setTheme(nullptr);
    plot2->setTheme(nullptr);
    delete plot2;
    delete theme;
}

void TestTheme::setThemeNull_revertsToOwned()
{
    auto* theme = QCPTheme::dark();
    mPlot->setTheme(theme);
    QCOMPARE(mPlot->backgroundBrush().color(), QColor("#1e1e1e"));

    mPlot->setTheme(nullptr);
    QCOMPARE(mPlot->theme()->background(), QColor(Qt::white));

    delete theme;
}

void TestTheme::qssProxyProperties()
{
    mPlot->setProperty("themeBackground", QColor("#1e1e1e"));
    mPlot->setProperty("themeForeground", QColor("#e0e0e0"));
    QCoreApplication::processEvents();

    QCOMPARE(mPlot->backgroundBrush().color(), QColor("#1e1e1e"));
    QCOMPARE(mPlot->xAxis->basePen().color(), QColor("#e0e0e0"));
}

void TestTheme::coalescing()
{
    int replotCount = 0;
    connect(mPlot, &QCustomPlot::afterReplot, [&replotCount]() { ++replotCount; });

    auto* theme = mPlot->theme();
    theme->setBackground(Qt::red);
    theme->setForeground(Qt::green);
    theme->setGrid(Qt::blue);
    // Before event loop, no replot yet
    QCOMPARE(replotCount, 0);

    QCoreApplication::processEvents();
    // After event loop, exactly one replot
    QCOMPARE(replotCount, 1);
}
```

- [ ] **Step 3: Build and run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: all tests pass

- [ ] **Step 4: Commit**

```bash
git add tests/auto/test-theme/
git commit -m "test: add theme sharing, QSS proxy, and coalescing tests"
```

---

## Task 4: Color scale axis support

**Files:**
- Modify: `src/core.cpp` (in `applyTheme()`)
- Modify: `tests/auto/test-theme/test-theme.h`
- Modify: `tests/auto/test-theme/test-theme.cpp`

- [ ] **Step 1: Add color scale test**

Add to private slots in `test-theme.h`:
```cpp
void applyThemePropagatesColorScaleAxis();
```

Add to `test-theme.cpp`:
```cpp
void TestTheme::applyThemePropagatesColorScaleAxis()
{
    auto* colorScale = new QCPColorScale(mPlot);
    mPlot->plotLayout()->addElement(0, 1, colorScale);
    auto* theme = QCPTheme::dark();
    mPlot->setTheme(theme);

    QCOMPARE(colorScale->axis()->basePen().color(), QColor("#e0e0e0"));
    QCOMPARE(colorScale->axis()->labelColor(), QColor("#e0e0e0"));
    QCOMPARE(colorScale->axis()->tickLabelColor(), QColor("#e0e0e0"));

    delete theme;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: FAIL — color scale axis not yet themed

- [ ] **Step 3: Extend walkLayout in applyTheme() in core.cpp**

In `applyTheme()`, extend the existing `walkLayout` lambda to also handle `QCPColorScale`:

```cpp
    // Walk the full layout tree for text elements and color scales
    std::function<void(QCPLayoutElement*)> walkLayout = [&](QCPLayoutElement* el) {
        if (!el) return;
        if (auto* te = qobject_cast<QCPTextElement*>(el)) {
            te->setTextColor(mTheme->foreground());
            te->setSelectedTextColor(mTheme->selection());
        }
        if (auto* cs = qobject_cast<QCPColorScale*>(el)) {
            auto* axis = cs->axis();
            QPen bp = axis->basePen();    bp.setColor(mTheme->foreground()); axis->setBasePen(bp);
            QPen tp = axis->tickPen();    tp.setColor(mTheme->foreground()); axis->setTickPen(tp);
            QPen sp = axis->subTickPen(); sp.setColor(mTheme->foreground()); axis->setSubTickPen(sp);
            axis->setLabelColor(mTheme->foreground());
            axis->setTickLabelColor(mTheme->foreground());
        }
        if (auto* layout = qobject_cast<QCPLayout*>(el)) {
            for (int i = 0; i < layout->elementCount(); ++i)
                walkLayout(layout->elementAt(i));
        }
    };
    walkLayout(plotLayout());
```

This replaces the previous `walkLayout` that only handled `QCPTextElement`.

- [ ] **Step 4: Build and run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: all tests pass

- [ ] **Step 5: Commit**

```bash
git add src/core.cpp tests/auto/test-theme/
git commit -m "feat: theme color scale axes and unify layout walk"
```

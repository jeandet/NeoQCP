# GPU-Accelerated Plottable Rendering — Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** GPU-render QCPGraph and QCPCurve line strokes and baseline fills via QRhi, bypassing QPainter for the rasterization-heavy hot path.

**Architecture:** CPU extrudes polylines into triangle strips and tessellates fill polygons into triangles. Geometry is uploaded to a dynamic QRhi vertex buffer and rendered directly into the QRhiWidget render target, interleaved with the existing QPainter-based layer texture compositing. Fallback to QPainter for non-solid pens, channel fills, exports, and non-RHI contexts.

**Tech Stack:** C++20, Qt 6.7+ QRhi API, GLSL 440 shaders compiled via `qsb`, Meson build system.

**Spec:** `docs/plans/2026-03-12-gpu-plottable-rendering-design.md`

---

## Chunk 1: Line Extruder (Pure Geometry, No QRhi)

### Task 1: Line extruder — triangle strip from polyline

A standalone pure function: takes `QVector<QPointF>` (pixel coords) + pen width → returns `QVector<float>` of `(x, y, r, g, b, a)` triangle-list vertices. Handles miter joins with bevel fallback, flat caps, and NaN gaps.

**Files:**
- Create: `src/painting/line-extruder.h`
- Create: `src/painting/line-extruder.cpp`
- Create: `tests/auto/test-line-extruder/test-line-extruder.h`
- Create: `tests/auto/test-line-extruder/test-line-extruder.cpp`
- Modify: `tests/auto/meson.build`
- Modify: `tests/auto/autotest.cpp`
- Modify: `meson.build`

- [ ] **Step 1: Create test header**

Create `tests/auto/test-line-extruder/test-line-extruder.h`:

```cpp
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
};
```

- [ ] **Step 2: Create test implementation with first test**

Create `tests/auto/test-line-extruder/test-line-extruder.cpp`. Start with the simplest case — a horizontal two-point segment should produce 6 vertices (2 triangles = 1 quad):

```cpp
#include "test-line-extruder.h"
#include "painting/line-extruder.h"

void TestLineExtruder::horizontalSegment()
{
    QVector<QPointF> points = {{0.0, 5.0}, {10.0, 5.0}};
    QColor color(255, 0, 0, 255);
    float penWidth = 2.0f;

    auto verts = QCPLineExtruder::extrudePolyline(points, penWidth, color);

    // 2 points → 1 segment → 1 quad → 6 vertices (triangle list)
    // each vertex: x, y, r, g, b, a = 6 floats
    QCOMPARE(verts.size(), 6 * 6);

    // Verify the quad spans y = 4.0 to y = 6.0 (penWidth/2 = 1.0 offset)
    // and x = 0.0 to x = 10.0
    // Triangle 1: top-left, top-right, bottom-right
    // Triangle 2: top-left, bottom-right, bottom-left
    // Check all Y values are either 4.0 or 6.0
    for (int i = 0; i < 6; ++i) {
        float y = verts[i * 6 + 1];
        QVERIFY(qFuzzyCompare(y, 4.0f) || qFuzzyCompare(y, 6.0f));
        float x = verts[i * 6 + 0];
        QVERIFY(x >= -0.01f && x <= 10.01f);
        // Check color
        QCOMPARE(verts[i * 6 + 2], 1.0f); // r
        QCOMPARE(verts[i * 6 + 3], 0.0f); // g
        QCOMPARE(verts[i * 6 + 4], 0.0f); // b
        QCOMPARE(verts[i * 6 + 5], 1.0f); // a
    }
}

void TestLineExtruder::verticalSegment()
{
    QVector<QPointF> points = {{5.0, 0.0}, {5.0, 10.0}};
    QColor color(0, 255, 0, 255);
    float penWidth = 4.0f;

    auto verts = QCPLineExtruder::extrudePolyline(points, penWidth, color);

    QCOMPARE(verts.size(), 6 * 6);

    // Quad should span x = 3.0 to 7.0
    for (int i = 0; i < 6; ++i) {
        float x = verts[i * 6 + 0];
        QVERIFY(qFuzzyCompare(x, 3.0f) || qFuzzyCompare(x, 7.0f));
    }
}

void TestLineExtruder::miterJoin()
{
    // Three points forming a right angle: should produce 2 quads (12 vertices)
    // with a miter join at the middle point
    QVector<QPointF> points = {{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}};
    QColor color(0, 0, 255, 255);
    float penWidth = 2.0f;

    auto verts = QCPLineExtruder::extrudePolyline(points, penWidth, color);

    // 3 points → 2 segments → 2 quads → 12 vertices
    QCOMPARE(verts.size(), 12 * 6);
}

void TestLineExtruder::bevelFallback()
{
    // Very sharp angle — miter would extend too far, should bevel instead
    // producing extra vertices at the join
    QVector<QPointF> points = {{0.0, 0.0}, {10.0, 0.0}, {0.5, 0.5}};
    QColor color(255, 255, 255, 255);
    float penWidth = 4.0f;

    auto verts = QCPLineExtruder::extrudePolyline(points, penWidth, color);

    // Bevel adds 1 extra triangle (3 verts) at the join
    // 2 quads (12) + 1 bevel triangle (3) = 15 vertices
    QCOMPARE(verts.size(), 15 * 6);
}

void TestLineExtruder::nanGap()
{
    QVector<QPointF> points = {
        {0.0, 0.0}, {10.0, 0.0},
        {qQNaN(), qQNaN()},  // gap
        {20.0, 0.0}, {30.0, 0.0}
    };
    QColor color(255, 0, 0, 255);
    float penWidth = 2.0f;

    auto verts = QCPLineExtruder::extrudePolyline(points, penWidth, color);

    // Two separate segments, each 1 quad = 6 verts → 12 total
    QCOMPARE(verts.size(), 12 * 6);
}

void TestLineExtruder::singlePoint()
{
    QVector<QPointF> points = {{5.0, 5.0}};
    auto verts = QCPLineExtruder::extrudePolyline(points, 2.0f, Qt::red);
    QVERIFY(verts.isEmpty());
}

void TestLineExtruder::emptyInput()
{
    QVector<QPointF> points;
    auto verts = QCPLineExtruder::extrudePolyline(points, 2.0f, Qt::red);
    QVERIFY(verts.isEmpty());
}

void TestLineExtruder::twoPoints()
{
    // Minimal valid input
    QVector<QPointF> points = {{0.0, 0.0}, {10.0, 0.0}};
    auto verts = QCPLineExtruder::extrudePolyline(points, 2.0f, Qt::red);
    QCOMPARE(verts.size(), 6 * 6); // 1 quad = 6 vertices × 6 floats
}

void TestLineExtruder::consecutiveNaNs()
{
    QVector<QPointF> points = {
        {0.0, 0.0}, {10.0, 0.0},
        {qQNaN(), qQNaN()}, {qQNaN(), qQNaN()},
        {20.0, 0.0}, {30.0, 0.0}
    };
    auto verts = QCPLineExtruder::extrudePolyline(points, 2.0f, Qt::red);
    QCOMPARE(verts.size(), 12 * 6); // two separate segments
}

void TestLineExtruder::nanAtStart()
{
    QVector<QPointF> points = {{qQNaN(), qQNaN()}, {0.0, 0.0}, {10.0, 0.0}};
    auto verts = QCPLineExtruder::extrudePolyline(points, 2.0f, Qt::red);
    QCOMPARE(verts.size(), 6 * 6); // one segment after the NaN
}

void TestLineExtruder::nanAtEnd()
{
    QVector<QPointF> points = {{0.0, 0.0}, {10.0, 0.0}, {qQNaN(), qQNaN()}};
    auto verts = QCPLineExtruder::extrudePolyline(points, 2.0f, Qt::red);
    QCOMPARE(verts.size(), 6 * 6); // one segment before the NaN
}

void TestLineExtruder::duplicatePoints()
{
    // Two identical points — zero-length segment, should still produce geometry
    QVector<QPointF> points = {{5.0, 5.0}, {5.0, 5.0}, {15.0, 5.0}};
    auto verts = QCPLineExtruder::extrudePolyline(points, 2.0f, Qt::red);
    QVERIFY(!verts.isEmpty());
}

void TestLineExtruder::zeroWidthPen()
{
    QVector<QPointF> points = {{0.0, 0.0}, {10.0, 0.0}};
    auto verts = QCPLineExtruder::extrudePolyline(points, 0.0f, Qt::red);
    QVERIFY(verts.isEmpty()); // zero width → no geometry
}
```

- [ ] **Step 3: Register tests in build**

Add to `tests/auto/meson.build`:
```
'test-line-extruder/test-line-extruder.cpp',
```
to `test_srcs`, and:
```
'test-line-extruder/test-line-extruder.h',
```
to `test_headers`.

Add to `tests/auto/autotest.cpp`:
```cpp
#include "test-line-extruder/test-line-extruder.h"
```
and add `TESTCLASS(TestLineExtruder)` in the main function (follow the pattern of existing test registrations).

Add `'src/painting/line-extruder.cpp'` to the `static_library('NeoQCP', ...)` source list in `meson.build`.

- [ ] **Step 4: Run tests — verify they fail**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: compilation error — `line-extruder.h` does not exist yet.

- [ ] **Step 5: Create line-extruder header**

Create `src/painting/line-extruder.h`:

```cpp
#pragma once

#include <QColor>
#include <QPointF>
#include <QVector>

namespace QCPLineExtruder
{

// Extrude a polyline into a triangle-list vertex buffer.
// Each vertex: (x, y, r, g, b, a) — 6 floats.
// NaN points in the input create gaps.
// Miter joins with bevel fallback at sharp angles.
QVector<float> extrudePolyline(const QVector<QPointF>& points, float penWidth,
                                const QColor& color);

// Tessellate a baseline fill polygon into a triangle-list vertex buffer.
// The polygon structure is: basePoint0, curvePoint0..N, basePoint1.
// Uses trapezoid decomposition between consecutive curve points.
QVector<float> tessellateFillPolygon(const QPolygonF& polygon, const QColor& color);

} // namespace QCPLineExtruder
```

- [ ] **Step 6: Create line-extruder implementation**

Create `src/painting/line-extruder.cpp`:

```cpp
#include "line-extruder.h"
#include <QtMath>
#include <array>

namespace QCPLineExtruder
{

namespace
{

constexpr float MITER_LIMIT = 4.0f; // bevel if miter length > MITER_LIMIT * penWidth/2

struct Vertex
{
    float x, y, r, g, b, a;
};

std::array<float, 4> colorToFloats(const QColor& c)
{
    // Premultiply alpha for correct src=One, dst=OneMinusSrcAlpha blending
    float a = float(c.alphaF());
    return {float(c.redF()) * a, float(c.greenF()) * a, float(c.blueF()) * a, a};
}

void pushVertex(QVector<float>& out, float x, float y, const std::array<float, 4>& rgba)
{
    out.append(x);
    out.append(y);
    out.append(rgba[0]);
    out.append(rgba[1]);
    out.append(rgba[2]);
    out.append(rgba[3]);
}

void pushQuad(QVector<float>& out,
              QPointF tl, QPointF tr, QPointF br, QPointF bl,
              const std::array<float, 4>& rgba)
{
    // Triangle 1: tl, tr, br
    pushVertex(out, tl.x(), tl.y(), rgba);
    pushVertex(out, tr.x(), tr.y(), rgba);
    pushVertex(out, br.x(), br.y(), rgba);
    // Triangle 2: tl, br, bl
    pushVertex(out, tl.x(), tl.y(), rgba);
    pushVertex(out, br.x(), br.y(), rgba);
    pushVertex(out, bl.x(), bl.y(), rgba);
}

bool isNan(const QPointF& p) { return qIsNaN(p.x()) || qIsNaN(p.y()); }

QPointF perp(const QPointF& dir)
{
    return {-dir.y(), dir.x()};
}

QPointF normalized(const QPointF& p)
{
    double len = qSqrt(p.x() * p.x() + p.y() * p.y());
    if (len < 1e-10) return {0, 1}; // fallback to vertical for zero-length
    return p / len;
}

// Given two consecutive segment normals, compute the miter offset.
// Returns the offset vector from the join point to the left edge.
// If the miter is too long (sharp angle), returns a zero vector to signal bevel.
QPointF miterOffset(const QPointF& n0, const QPointF& n1, float halfWidth)
{
    QPointF tangent = normalized(n0 + n1);
    double dot = tangent.x() * n0.x() + tangent.y() * n0.y();
    if (qAbs(dot) < 1e-6)
        return {}; // parallel, use bevel
    double miterLen = halfWidth / dot;
    if (qAbs(miterLen) > MITER_LIMIT * halfWidth)
        return {}; // too sharp, bevel
    return tangent * miterLen;
}

struct SegmentData
{
    int startIdx;
    int endIdx; // exclusive
};

QVector<SegmentData> splitByNaN(const QVector<QPointF>& points)
{
    QVector<SegmentData> segments;
    int start = 0;
    for (int i = 0; i < points.size(); ++i)
    {
        if (isNan(points[i]))
        {
            if (i - start >= 2)
                segments.append({start, i});
            start = i + 1;
        }
    }
    if (points.size() - start >= 2)
        segments.append({start, points.size()});
    return segments;
}

void extrudeSegment(QVector<float>& out, const QVector<QPointF>& points,
                    int start, int end, float halfWidth, const std::array<float, 4>& rgba)
{
    int count = end - start;
    if (count < 2) return;

    // Compute per-segment normals (perpendicular to direction, pointing "left")
    QVector<QPointF> normals(count - 1);
    for (int i = 0; i < count - 1; ++i)
    {
        QPointF dir = normalized(points[start + i + 1] - points[start + i]);
        normals[i] = perp(dir);
    }

    // For each point, compute left/right edge positions
    QVector<QPointF> left(count), right(count);

    // First point: simple offset
    left[0] = points[start] + normals[0] * halfWidth;
    right[0] = points[start] - normals[0] * halfWidth;

    // Interior points: miter or bevel
    for (int i = 1; i < count - 1; ++i)
    {
        QPointF mo = miterOffset(normals[i - 1], normals[i], halfWidth);
        if (mo.isNull())
        {
            // Bevel: use previous segment's normal for this quad, next segment's for next quad
            // This creates a bevel triangle between the two quads
            QPointF p = points[start + i];
            QPointF leftPrev = p + normals[i - 1] * halfWidth;
            QPointF rightPrev = p - normals[i - 1] * halfWidth;
            QPointF leftNext = p + normals[i] * halfWidth;
            QPointF rightNext = p - normals[i] * halfWidth;

            // Emit the quad for segment i-1 using prev normals
            pushQuad(out, left[i - 1], leftPrev, rightPrev, right[i - 1], rgba);

            // Emit bevel triangle
            // Determine which side the bevel is on (cross product sign)
            double cross = normals[i - 1].x() * normals[i].y() - normals[i - 1].y() * normals[i].x();
            if (cross > 0)
            {
                // Left turn — bevel on the right
                pushVertex(out, rightPrev.x(), rightPrev.y(), rgba);
                pushVertex(out, rightNext.x(), rightNext.y(), rgba);
                pushVertex(out, p.x(), p.y(), rgba);
            }
            else
            {
                // Right turn — bevel on the left
                pushVertex(out, leftPrev.x(), leftPrev.y(), rgba);
                pushVertex(out, leftNext.x(), leftNext.y(), rgba);
                pushVertex(out, p.x(), p.y(), rgba);
            }

            // Set up for next segment
            left[i] = leftNext;
            right[i] = rightNext;
            continue;
        }

        left[i] = points[start + i] + mo;
        right[i] = points[start + i] - mo;

        // Emit the quad for segment i-1
        pushQuad(out, left[i - 1], left[i], right[i], right[i - 1], rgba);
    }

    // Last point: simple offset
    left[count - 1] = points[start + count - 1] + normals[count - 2] * halfWidth;
    right[count - 1] = points[start + count - 1] - normals[count - 2] * halfWidth;

    // Emit the final segment's quad
    // Check: if count >= 2 and the last interior point used miter (not bevel),
    // the last quad hasn't been emitted yet
    if (count == 2)
    {
        // Only one segment, one quad
        pushQuad(out, left[0], left[1], right[1], right[0], rgba);
    }
    else
    {
        // Final segment quad — always needed regardless of whether the
        // previous join was miter or bevel
        pushQuad(out, left[count - 2], left[count - 1], right[count - 1], right[count - 2], rgba);
    }
}

} // anonymous namespace

QVector<float> extrudePolyline(const QVector<QPointF>& points, float penWidth,
                                const QColor& color)
{
    if (points.size() < 2 || penWidth <= 0.0f)
        return {};

    auto rgba = colorToFloats(color);
    float halfWidth = penWidth / 2.0f;

    auto segments = splitByNaN(points);
    QVector<float> out;
    // Reserve approximate size: 2 segments avg, 6 verts per segment point
    out.reserve(points.size() * 6 * 6);

    for (const auto& seg : segments)
        extrudeSegment(out, points, seg.startIdx, seg.endIdx, halfWidth, rgba);

    return out;
}

QVector<float> tessellateFillPolygon(const QPolygonF& polygon, const QColor& color)
{
    // Polygon structure from getFillPolygon():
    //   [0] = basePoint at start
    //   [1..N-2] = curve data points
    //   [N-1] = basePoint at end
    // Tessellate as trapezoids between consecutive curve points projected to baseline.

    if (polygon.size() < 4)
        return {}; // need at least: base0, pt0, pt1, base1

    auto rgba = colorToFloats(color);
    QVector<float> out;

    const QPointF base0 = polygon.first();
    const QPointF base1 = polygon.last();
    int curveCount = polygon.size() - 2; // number of curve points

    if (curveCount < 2)
    {
        // Degenerate: just one curve point — emit a single triangle
        pushVertex(out, base0.x(), base0.y(), rgba);
        pushVertex(out, polygon[1].x(), polygon[1].y(), rgba);
        pushVertex(out, base1.x(), base1.y(), rgba);
        return out;
    }

    // Reserve: (curveCount - 1) quads × 6 verts × 6 floats
    //        + 2 triangles for the end caps
    out.reserve((curveCount - 1) * 36 + 2 * 18);

    // Determine baseline direction for projection.
    // For horizontal key axis: baseline is horizontal, project vertically (same X).
    // For vertical key axis: baseline is vertical, project horizontally (same Y).
    // We detect this from the base points: if they share the same Y, baseline is horizontal.
    bool horizontalBaseline = qFuzzyCompare(base0.y(), base1.y());

    auto projectToBaseline = [&](const QPointF& pt) -> QPointF {
        if (horizontalBaseline)
            return {pt.x(), base0.y()};
        else
            return {base0.x(), pt.y()};
    };

    // First cap triangle: base0 → curve[0] → projected(curve[0])
    QPointF proj0 = projectToBaseline(polygon[1]);
    pushVertex(out, base0.x(), base0.y(), rgba);
    pushVertex(out, polygon[1].x(), polygon[1].y(), rgba);
    pushVertex(out, proj0.x(), proj0.y(), rgba);

    // Trapezoids between consecutive curve points
    for (int i = 1; i < curveCount; ++i)
    {
        QPointF c0 = polygon[i];     // curve point i-1 (1-indexed in polygon)
        QPointF c1 = polygon[i + 1]; // curve point i
        QPointF p0 = projectToBaseline(c0);
        QPointF p1 = projectToBaseline(c1);

        // Quad: c0, c1, p1, p0
        pushQuad(out, c0, c1, p1, p0, rgba);
    }

    // Last cap triangle: curve[last] → base1 → projected(curve[last])
    QPointF projLast = projectToBaseline(polygon[polygon.size() - 2]);
    pushVertex(out, polygon[polygon.size() - 2].x(), polygon[polygon.size() - 2].y(), rgba);
    pushVertex(out, base1.x(), base1.y(), rgba);
    pushVertex(out, projLast.x(), projLast.y(), rgba);

    return out;
}

} // namespace QCPLineExtruder
```

- [ ] **Step 7: Build and run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: all `TestLineExtruder` tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/painting/line-extruder.h src/painting/line-extruder.cpp \
    tests/auto/test-line-extruder/test-line-extruder.h \
    tests/auto/test-line-extruder/test-line-extruder.cpp \
    tests/auto/meson.build tests/auto/autotest.cpp meson.build
git commit -m "feat: add line extruder for GPU polyline/fill tessellation

Pure geometry module that extrudes polylines into triangle-list
vertex buffers (miter joins, bevel fallback, NaN gaps) and
tessellates baseline fill polygons via trapezoid decomposition."
```

---

### Task 2: Fill tessellation tests

**Files:**
- Modify: `tests/auto/test-line-extruder/test-line-extruder.h`
- Modify: `tests/auto/test-line-extruder/test-line-extruder.cpp`

- [ ] **Step 1: Add fill tessellation test slots to header**

Add to `TestLineExtruder` class:
```cpp
    void fillHorizontalBaseline();
    void fillVerticalBaseline();
    void fillTooFewPoints();
    void fillMinimalTrapezoid();
```

- [ ] **Step 2: Write fill tests**

Append to `test-line-extruder.cpp`:

```cpp
void TestLineExtruder::fillHorizontalBaseline()
{
    // Polygon: base0(0,10), curve(0,0), curve(5,0), curve(10,0), base1(10,10)
    // Horizontal baseline at y=10, curve at y=0
    QPolygonF poly;
    poly << QPointF(0, 10) << QPointF(0, 0) << QPointF(5, 0) << QPointF(10, 0) << QPointF(10, 10);
    QColor color(0, 0, 255, 255);

    auto verts = QCPLineExtruder::tessellateFillPolygon(poly, color);

    // 3 curve points → first cap (3 verts) + 2 trapezoid quads (12 verts) + last cap (3 verts) = 18
    QCOMPARE(verts.size(), 18 * 6);
}

void TestLineExtruder::fillVerticalBaseline()
{
    // Vertical baseline at x=10
    QPolygonF poly;
    poly << QPointF(10, 0) << QPointF(0, 0) << QPointF(0, 5) << QPointF(0, 10) << QPointF(10, 10);
    QColor color(255, 0, 0, 255);

    auto verts = QCPLineExtruder::tessellateFillPolygon(poly, color);

    QCOMPARE(verts.size(), 18 * 6);
}

void TestLineExtruder::fillTooFewPoints()
{
    QPolygonF poly;
    poly << QPointF(0, 0) << QPointF(5, 5) << QPointF(10, 0);
    auto verts = QCPLineExtruder::tessellateFillPolygon(poly, Qt::red);
    QVERIFY(verts.isEmpty()); // < 4 points
}

void TestLineExtruder::fillMinimalTrapezoid()
{
    // 4 points: base0, single curve point, base1 — but that's only 3...
    // Actually 4 points: base0, curve0, curve1=base1 is ambiguous.
    // With exactly 4 points: base0, c0, c1, base1 → 2 curve points
    QPolygonF poly;
    poly << QPointF(0, 10) << QPointF(0, 0) << QPointF(10, 0) << QPointF(10, 10);
    auto verts = QCPLineExtruder::tessellateFillPolygon(poly, Qt::red);

    // 2 curve points → first cap (3) + 1 quad (6) + last cap (3) = 12
    QCOMPARE(verts.size(), 12 * 6);
}
```

- [ ] **Step 3: Build and run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: all tests pass.

- [ ] **Step 4: Commit**

```bash
git add tests/auto/test-line-extruder/
git commit -m "test: add fill tessellation tests for line extruder"
```

---

## Chunk 2: Plottable Shaders and Build Integration

### Task 3: Plottable shaders

**Files:**
- Create: `src/painting/shaders/plottable.vert`
- Create: `src/painting/shaders/plottable.frag`
- Modify: `src/painting/shaders/embed_shaders.py`
- Modify: `meson.build`

- [ ] **Step 1: Create vertex shader**

Create `src/painting/shaders/plottable.vert`:

```glsl
#version 440

layout(location = 0) in vec2 position;
layout(location = 1) in vec4 color;

layout(location = 0) out vec4 v_color;

layout(std140, binding = 0) uniform ViewportParams {
    float width;
    float height;
    float yFlip;
    float dpr;
} pc;

void main()
{
    float ndcX = (position.x * pc.dpr / pc.width) * 2.0 - 1.0;
    float ndcY = pc.yFlip * ((position.y * pc.dpr / pc.height) * 2.0 - 1.0);
    gl_Position = vec4(ndcX, ndcY, 0.0, 1.0);
    v_color = color;
}
```

- [ ] **Step 2: Create fragment shader**

Create `src/painting/shaders/plottable.frag`:

```glsl
#version 440

layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = v_color;
}
```

- [ ] **Step 3: Update embed_shaders.py to accept variable number of shaders**

Replace `src/painting/shaders/embed_shaders.py`:

```python
#!/usr/bin/env python3
"""Convert .qsb shader files to a C++ header with embedded byte arrays."""
import sys
import os

def embed(path, var_name):
    with open(path, 'rb') as f:
        data = f.read()
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        lines.append('    ' + ', '.join(f'0x{b:02x}' for b in chunk))
    hex_body = ',\n'.join(lines)
    return (f'static constexpr unsigned char {var_name}[] = {{\n'
            f'{hex_body}\n'
            f'}};\n'
            f'static constexpr unsigned int {var_name}_len = {len(data)};\n')

if __name__ == '__main__':
    # Usage: embed_shaders.py output.h name1:input1.qsb name2:input2.qsb ...
    output_path = sys.argv[1]
    pairs = sys.argv[2:]
    with open(output_path, 'w') as out:
        out.write('#pragma once\n\n')
        out.write('// Auto-generated from .qsb shader files. Do not edit.\n\n')
        for pair in pairs:
            var_name, input_path = pair.split(':', 1)
            out.write(embed(input_path, var_name))
            out.write('\n')
```

- [ ] **Step 4: Update meson.build shader targets**

Add the plottable shader compile targets after the existing composite shader targets. Update the `embedded_shaders` custom_target to include all four shaders with the new embed_shaders.py calling convention.

In `meson.build`, after the existing `composite_frag_qsb` target, add:

```meson
plottable_vert_qsb = custom_target('plottable_vert_qsb',
    input: 'src/painting/shaders/plottable.vert',
    output: 'plottable.vert.qsb',
    command: [qsb, '--qt6', '-o', '@OUTPUT@', '@INPUT@'])

plottable_frag_qsb = custom_target('plottable_frag_qsb',
    input: 'src/painting/shaders/plottable.frag',
    output: 'plottable.frag.qsb',
    command: [qsb, '--qt6', '-o', '@OUTPUT@', '@INPUT@'])
```

Replace the existing `embedded_shaders` custom_target with:

```meson
embedded_shaders = custom_target('embedded_shaders',
    input: [composite_vert_qsb, composite_frag_qsb, plottable_vert_qsb, plottable_frag_qsb],
    output: 'embedded_shaders.h',
    command: [python3, files('src/painting/shaders/embed_shaders.py'),
              '@OUTPUT@',
              'composite_vert_qsb_data:@INPUT0@',
              'composite_frag_qsb_data:@INPUT1@',
              'plottable_vert_qsb_data:@INPUT2@',
              'plottable_frag_qsb_data:@INPUT3@'])
```

- [ ] **Step 5: Build to verify shaders compile**

Run: `meson setup --wipe build && meson compile -C build`
Expected: builds successfully, `embedded_shaders.h` now contains all 4 shader byte arrays.

- [ ] **Step 6: Commit**

```bash
git add src/painting/shaders/plottable.vert src/painting/shaders/plottable.frag \
    src/painting/shaders/embed_shaders.py meson.build
git commit -m "feat: add plottable vertex/fragment shaders and build targets

Simple position+color shaders with UBO-driven pixel-to-NDC
transform. Updated embed_shaders.py to accept variable shader pairs."
```

---

## Chunk 3: QCPPlottableRhiLayer — GPU Resource Management

### Task 4: QCPPlottableRhiLayer class

Manages per-layer GPU resources: vertex buffer, pipeline, draw entries with scissor rects.

**Files:**
- Create: `src/painting/plottable-rhi-layer.h`
- Create: `src/painting/plottable-rhi-layer.cpp`
- Modify: `meson.build`

- [ ] **Step 1: Create header**

Create `src/painting/plottable-rhi-layer.h`:

```cpp
#pragma once

#include <QColor>
#include <QPointF>
#include <QRect>
#include <QVector>
#include <rhi/qrhi.h>

class QCPPlottableRhiLayer
{
public:
    struct DrawEntry
    {
        int fillOffset = 0;
        int fillVertexCount = 0;
        int strokeOffset = 0;
        int strokeVertexCount = 0;
        QRect scissorRect; // in physical pixels, Y-flipped for Y-up backends
    };

    explicit QCPPlottableRhiLayer(QRhi* rhi);
    ~QCPPlottableRhiLayer();

    // Geometry accumulation (called during replot)
    void clear();
    DrawEntry addPlottable(const QVector<float>& fillVerts,
                           const QVector<float>& strokeVerts,
                           const QRect& clipRect, double dpr,
                           int outputHeight, bool isYUpInNDC);

    // GPU resource management
    void invalidatePipeline(); // call on resize (render pass descriptor change)
    bool ensurePipeline(QRhiRenderPassDescriptor* rpDesc, int sampleCount);
    void uploadResources(QRhiResourceUpdateBatch* updates,
                          const QSize& outputSize, float dpr, bool isYUpInNDC);
    void render(QRhiCommandBuffer* cb, const QSize& outputSize);

    bool isDirty() const { return mDirty; }
    bool hasGeometry() const { return !mDrawEntries.isEmpty(); }

private:
    QRhi* mRhi;
    QVector<float> mStagingVertices;
    QVector<DrawEntry> mDrawEntries;

    QRhiBuffer* mVertexBuffer = nullptr;
    QRhiBuffer* mUniformBuffer = nullptr;
    QRhiShaderResourceBindings* mSrb = nullptr;
    QRhiGraphicsPipeline* mPipeline = nullptr;
    int mVertexBufferSize = 0;
    int mLastSampleCount = 0;
    bool mDirty = false;
};
```

- [ ] **Step 2: Create implementation**

Create `src/painting/plottable-rhi-layer.cpp`:

```cpp
#include "plottable-rhi-layer.h"
#include "Profiling.hpp"

QCPPlottableRhiLayer::QCPPlottableRhiLayer(QRhi* rhi)
    : mRhi(rhi)
{
}

QCPPlottableRhiLayer::~QCPPlottableRhiLayer()
{
    delete mPipeline;
    delete mSrb;
    delete mUniformBuffer;
    delete mVertexBuffer;
}

void QCPPlottableRhiLayer::invalidatePipeline()
{
    delete mPipeline;
    mPipeline = nullptr;
    delete mSrb;
    mSrb = nullptr;
    delete mUniformBuffer;
    mUniformBuffer = nullptr;
}

void QCPPlottableRhiLayer::clear()
{
    mStagingVertices.clear();
    mDrawEntries.clear();
    mDirty = true;
}

QCPPlottableRhiLayer::DrawEntry
QCPPlottableRhiLayer::addPlottable(const QVector<float>& fillVerts,
                                    const QVector<float>& strokeVerts,
                                    const QRect& clipRect, double dpr,
                                    int outputHeight, bool isYUpInNDC)
{
    DrawEntry entry;
    int sx = clipRect.x() * dpr;
    int sy = clipRect.y() * dpr;
    int sw = clipRect.width() * dpr;
    int sh = clipRect.height() * dpr;
    // QRhi scissor Y origin is bottom for Y-up backends (OpenGL)
    if (isYUpInNDC)
        sy = outputHeight - sy - sh;
    entry.scissorRect = QRect(sx, sy, sw, sh);

    if (!fillVerts.isEmpty())
    {
        entry.fillOffset = mStagingVertices.size() / 6; // vertex index
        entry.fillVertexCount = fillVerts.size() / 6;
        mStagingVertices.append(fillVerts);
    }

    if (!strokeVerts.isEmpty())
    {
        entry.strokeOffset = mStagingVertices.size() / 6;
        entry.strokeVertexCount = strokeVerts.size() / 6;
        mStagingVertices.append(strokeVerts);
    }

    mDrawEntries.append(entry);
    mDirty = true;
    return entry;
}

bool QCPPlottableRhiLayer::ensurePipeline(QRhiRenderPassDescriptor* rpDesc,
                                           int sampleCount)
{
    if (mPipeline && mLastSampleCount == sampleCount)
        return true;

    // Rebuild pipeline if sample count changed or first creation
    invalidatePipeline();

    // Load shaders from embedded data
    extern const unsigned char plottable_vert_qsb_data[];
    extern const unsigned int plottable_vert_qsb_data_len;
    extern const unsigned char plottable_frag_qsb_data[];
    extern const unsigned int plottable_frag_qsb_data_len;

    QShader vertShader = QShader::fromSerialized(QByteArray::fromRawData(
        reinterpret_cast<const char*>(plottable_vert_qsb_data), plottable_vert_qsb_data_len));
    QShader fragShader = QShader::fromSerialized(QByteArray::fromRawData(
        reinterpret_cast<const char*>(plottable_frag_qsb_data), plottable_frag_qsb_data_len));

    if (!vertShader.isValid() || !fragShader.isValid())
    {
        qDebug() << "Failed to load plottable shaders";
        return false;
    }

    // Create uniform buffer for viewport params (16 bytes: width, height, yFlip, dpr)
    delete mUniformBuffer;
    mUniformBuffer = mRhi->newBuffer(QRhiBuffer::Dynamic,
                                      QRhiBuffer::UniformBuffer, 16);
    mUniformBuffer->create();

    // Create SRB binding the uniform buffer at binding 0
    delete mSrb;
    mSrb = mRhi->newShaderResourceBindings();
    mSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage, mUniformBuffer)
    });
    mSrb->create();

    mPipeline = mRhi->newGraphicsPipeline();
    mPipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vertShader},
        {QRhiShaderStage::Fragment, fragShader}
    });

    // Vertex layout: (x, y) float2 + (r, g, b, a) float4
    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({{6 * sizeof(float)}});
    inputLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},
        {0, 1, QRhiVertexInputAttribute::Float4, 2 * sizeof(float)}
    });
    mPipeline->setVertexInputLayout(inputLayout);

    // Premultiplied alpha blending
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::One;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    mPipeline->setTargetBlends({blend});

    mPipeline->setFlags(QRhiGraphicsPipeline::UsesScissor);
    mPipeline->setTopology(QRhiGraphicsPipeline::Triangles);
    mPipeline->setSampleCount(sampleCount);
    mPipeline->setRenderPassDescriptor(rpDesc);
    mPipeline->setShaderResourceBindings(mSrb);

    if (!mPipeline->create())
    {
        qDebug() << "Failed to create plottable pipeline";
        delete mPipeline;
        mPipeline = nullptr;
        return false;
    }

    mLastSampleCount = sampleCount;
    return true;
}

void QCPPlottableRhiLayer::uploadResources(QRhiResourceUpdateBatch* updates,
                                            const QSize& outputSize, float dpr,
                                            bool isYUpInNDC)
{
    // Upload uniform buffer (viewport params) — always, since output size may change
    if (mUniformBuffer)
    {
        struct { float width, height, yFlip, dpr; } params = {
            float(outputSize.width()),
            float(outputSize.height()),
            isYUpInNDC ? -1.0f : 1.0f,
            dpr
        };
        updates->updateDynamicBuffer(mUniformBuffer, 0, 16, &params);
    }

    if (!mDirty || mStagingVertices.isEmpty())
        return;

    int requiredSize = mStagingVertices.size() * sizeof(float);

    if (!mVertexBuffer || mVertexBufferSize < requiredSize)
    {
        delete mVertexBuffer;
        mVertexBuffer = mRhi->newBuffer(QRhiBuffer::Dynamic,
                                         QRhiBuffer::VertexBuffer,
                                         requiredSize);
        mVertexBuffer->create();
        mVertexBufferSize = requiredSize;
    }

    updates->updateDynamicBuffer(mVertexBuffer, 0, requiredSize,
                                  mStagingVertices.constData());
    mDirty = false;
}

void QCPPlottableRhiLayer::render(QRhiCommandBuffer* cb,
                                   const QSize& outputSize)
{
    PROFILE_HERE_N("QCPPlottableRhiLayer::render");

    if (!mPipeline || !mVertexBuffer || !mSrb || mDrawEntries.isEmpty())
        return;

    cb->setGraphicsPipeline(mPipeline);
    cb->setViewport({0, 0, float(outputSize.width()), float(outputSize.height())});
    cb->setShaderResources(mSrb);

    const QRhiCommandBuffer::VertexInput vbufBinding(mVertexBuffer, 0);
    cb->setVertexInput(0, 1, &vbufBinding);

    for (const auto& entry : mDrawEntries)
    {
        cb->setScissor({entry.scissorRect.x(), entry.scissorRect.y(),
                        entry.scissorRect.width(), entry.scissorRect.height()});

        if (entry.fillVertexCount > 0)
            cb->draw(entry.fillVertexCount, 1, entry.fillOffset, 0);
        if (entry.strokeVertexCount > 0)
            cb->draw(entry.strokeVertexCount, 1, entry.strokeOffset, 0);
    }
}

- [ ] **Step 3: Add to meson.build**

Add `'src/painting/plottable-rhi-layer.cpp'` to the `static_library('NeoQCP', ...)` source list.
Add `'src/painting/plottable-rhi-layer.h'` to `extra_files` in the same target.

- [ ] **Step 4: Build**

Run: `meson compile -C build`
Expected: compiles successfully.

- [ ] **Step 5: Commit**

```bash
git add src/painting/plottable-rhi-layer.h src/painting/plottable-rhi-layer.cpp meson.build
git commit -m "feat: add QCPPlottableRhiLayer for GPU plottable rendering

Per-layer GPU resource manager: accumulates extruded geometry,
manages dynamic vertex buffer, creates plottable pipeline with
scissor support, renders with UBO-driven pixel-to-NDC transform."
```

---

## Chunk 4: Integration — Wire Up Replot and Render

### Task 5: Add plottable RHI layer management to QCustomPlot

Connect `QCPPlottableRhiLayer` into the `initialize()` / `render()` / `releaseResources()` lifecycle.

**Files:**
- Modify: `src/core.h`
- Modify: `src/core.cpp`

- [ ] **Step 1: Add includes and members to core.h**

Add forward declaration after the existing QRhi forward declarations (around line 49):
```cpp
class QCPPlottableRhiLayer;
```

Add member in the `// RHI compositing resources:` section (after line 316):
```cpp
QMap<QCPLayer*, QCPPlottableRhiLayer*> mPlottableRhiLayers;
```

- [ ] **Step 2: Update core.cpp includes**

Add at top of `core.cpp` (after the existing includes around line 40):
```cpp
#include "painting/plottable-rhi-layer.h"
#include "painting/line-extruder.h"
```

- [ ] **Step 3: Update releaseResources()**

In `QCustomPlot::releaseResources()`, add cleanup of plottable RHI layers **before** `mPaintBuffers.clear()`:

```cpp
qDeleteAll(mPlottableRhiLayers);
mPlottableRhiLayers.clear();
```

- [ ] **Step 4: Update destructor**

In `QCustomPlot::~QCustomPlot()`, add cleanup of plottable RHI layers **before** `mPaintBuffers.clear()`:

```cpp
qDeleteAll(mPlottableRhiLayers);
mPlottableRhiLayers.clear();
```

- [ ] **Step 5: Update initialize() resize path**

In the `mRhiInitialized` early-return branch of `initialize()` (line 2237), also invalidate plottable pipelines (not full destruction — vertex buffers survive resize):

```cpp
for (auto* prl : mPlottableRhiLayers)
    prl->invalidatePipeline();
```

- [ ] **Step 6: Update render() to draw plottable geometry**

In `render()`, after the existing layer compositing loop (after the `cb->endPass()` at line 2417), restructure the render pass to interleave plottable geometry. The modified render loop becomes:

Replace the section from `cb->beginPass(...)` through `cb->endPass()` with:

```cpp
cb->beginPass(renderTarget(), clearColor, {1.0f, 0}, updates);

cb->setGraphicsPipeline(mCompositePipeline);
cb->setViewport({0, 0, float(outputSize.width()), float(outputSize.height())});
const QRhiCommandBuffer::VertexInput vbufBinding(mQuadVertexBuffer, 0);
cb->setVertexInput(0, 1, &vbufBinding, mQuadIndexBuffer, 0, QRhiCommandBuffer::IndexUInt16);

// Iterate over layers (not paint buffers) — mPaintBuffers and mLayers are NOT 1:1.
// Multiple logical layers share a single paint buffer. Track which buffers we've
// already composited to avoid double-drawing.
QSet<QCPAbstractPaintBuffer*> compositedBuffers;

for (QCPLayer* layer : mLayers)
{
    // Composite the layer's paint buffer texture (if not already done)
    if (auto pb = layer->mPaintBuffer.toStrongRef())
    {
        if (!compositedBuffers.contains(pb.data()))
        {
            compositedBuffers.insert(pb.data());
            if (auto* rhiBuffer = dynamic_cast<QCPPaintBufferRhi*>(pb.data()))
            {
                if (rhiBuffer->texture())
                {
                    if (!rhiBuffer->srb() || !rhiBuffer->srbMatchesTexture())
                    {
                        auto* srb = mRhi->newShaderResourceBindings();
                        srb->setBindings({
                            QRhiShaderResourceBinding::sampledTexture(
                                1, QRhiShaderResourceBinding::FragmentStage,
                                rhiBuffer->texture(), mSampler)
                        });
                        srb->create();
                        rhiBuffer->setSrb(srb, rhiBuffer->texture());
                    }
                    cb->setGraphicsPipeline(mCompositePipeline);
                    cb->setShaderResources(rhiBuffer->srb());
                    cb->setVertexInput(0, 1, &vbufBinding, mQuadIndexBuffer, 0,
                                       QRhiCommandBuffer::IndexUInt16);
                    cb->drawIndexed(6);
                }
            }
        }
    }

    // Draw GPU plottable geometry for this layer (after its paint buffer)
    if (auto* prl = mPlottableRhiLayers.value(layer, nullptr))
    {
        if (prl->hasGeometry())
        {
            prl->render(cb, outputSize);

            // Restore composite pipeline state for next layer
            cb->setGraphicsPipeline(mCompositePipeline);
            cb->setViewport({0, 0, float(outputSize.width()), float(outputSize.height())});
            cb->setVertexInput(0, 1, &vbufBinding, mQuadIndexBuffer, 0,
                               QRhiCommandBuffer::IndexUInt16);
        }
    }
}

cb->endPass();
```

- [ ] **Step 7: Upload plottable vertex data in render()**

In `render()`, after uploading QPainter staging textures but before `cb->beginPass(...)`, add:

```cpp
for (auto* prl : mPlottableRhiLayers)
{
    // Ensure pipeline + UBO exist before uploading uniform data
    prl->ensurePipeline(renderTarget()->renderPassDescriptor(), sampleCount());
    prl->uploadResources(updates, outputSize, mBufferDevicePixelRatio,
                          mRhi->isYUpInNDC());
}
```

- [ ] **Step 8: Build and run existing tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: all existing tests still pass (no behavioral change yet — plottable RHI layers are empty).

- [ ] **Step 9: Commit**

```bash
git add src/core.h src/core.cpp
git commit -m "feat: wire QCPPlottableRhiLayer into RHI lifecycle

Add per-layer plottable GPU resource management to QCustomPlot.
Plottable geometry is rendered interleaved with layer texture
compositing during render(). No plottables use the GPU path yet."
```

---

### Task 6: Hook drawPolyline and drawFill into GPU path

The key integration: when RHI is active and pen is solid, `drawPolyline()` writes extruded geometry to the layer's `QCPPlottableRhiLayer` instead of painting to QPainter.

**Files:**
- Modify: `src/plottables/plottable1d.h` (drawPolyline)
- Modify: `src/plottables/plottable-graph.cpp` (drawFill, draw)
- Modify: `src/core.h` (expose rhi(), plottable RHI layer accessor, rhiOutputSize)
- Modify: `src/core.cpp` (plottable RHI layer accessor + creation during replot)

- [ ] **Step 1: Add accessor to QCustomPlot for plottable RHI layers**

In `core.h`, add public methods:
```cpp
QRhi* rhi() const { return mRhi; } // expose cached RHI pointer (QRhiWidget::rhi() is protected)
QCPPlottableRhiLayer* plottableRhiLayer(QCPLayer* layer);
QSize rhiOutputSize() const; // physical pixel size of render target
```

In `core.cpp`, implement:
```cpp
QCPPlottableRhiLayer* QCustomPlot::plottableRhiLayer(QCPLayer* layer)
{
    if (!mRhi)
        return nullptr;
    if (!mPlottableRhiLayers.contains(layer))
    {
        auto* prl = new QCPPlottableRhiLayer(mRhi);
        mPlottableRhiLayers[layer] = prl;
    }
    return mPlottableRhiLayers[layer];
}

QSize QCustomPlot::rhiOutputSize() const
{
    return renderTarget() ? renderTarget()->pixelSize() : QSize();
}
```

- [ ] **Step 2: Clear plottable RHI layers during replot**

In `QCustomPlot::replot()`, after `setupPaintBuffers()` (line 2900) and before the layer draw loop, add:

```cpp
for (auto* prl : mPlottableRhiLayers)
    prl->clear();
```

- [ ] **Step 3: No separate `useGpuRendering()` helper needed**

The GPU branch in `drawPolyline()` and `drawFill()` checks `mParentPlot->rhi()` directly via the new public accessor, using a C++17 init-statement in `if` to store the pointer locally. This avoids a trivially thin helper and eliminates double-dereferencing the same field.

- [ ] **Step 4: Modify drawPolyline() in plottable1d.h**

At the top of `QCPAbstractPlottable1D<DataType>::drawPolyline()`, before the cosmetic pen logic (line 604), add the GPU branch:

```cpp
// GPU rendering path: extrude polyline into triangle strip
if (auto* rhi = mParentPlot ? mParentPlot->rhi() : nullptr;
    rhi && !painter->modes().testFlag(QCPPainter::pmVectorized)
        && painter->pen().style() == Qt::SolidLine)
{
    auto* prl = mParentPlot->plottableRhiLayer(mLayer);
    if (prl)
    {
        QColor penColor = painter->pen().color();
        // Premultiply alpha for GPU blending (src=One, dst=OneMinusSrcAlpha)
        float a = penColor.alphaF();
        QColor premul = QColor::fromRgbF(penColor.redF() * a,
                                          penColor.greenF() * a,
                                          penColor.blueF() * a, a);

        // Guard against zero-width / cosmetic pens (default width 0 means 1px)
        float penWidth = qMax(1.0f, static_cast<float>(painter->pen().widthF()));
        auto strokeVerts = QCPLineExtruder::extrudePolyline(
            lineData, penWidth, premul);
        if (!strokeVerts.isEmpty())
        {
            const double dpr = mParentPlot->bufferDevicePixelRatio();
            const QSize outputSize = mParentPlot->rhiOutputSize();
            prl->addPlottable({}, strokeVerts, clipRect(), dpr,
                               outputSize.height(), rhi->isYUpInNDC());
        }
        return;
    }
}
```

Add the necessary includes at the top of `plottable1d.h`:
```cpp
#include "painting/line-extruder.h"
#include "painting/plottable-rhi-layer.h"
```

- [ ] **Step 5: Modify drawFill() in plottable-graph.cpp**

In `QCPGraph::drawFill()`, after the `mChannelFillGraph` check but inside the baseline fill branch (line 1006-1010), add the GPU path:

```cpp
if (!mChannelFillGraph)
{
    if (auto* rhi = mParentPlot ? mParentPlot->rhi() : nullptr;
        rhi && !painter->modes().testFlag(QCPPainter::pmVectorized))
    {
        auto* prl = mParentPlot->plottableRhiLayer(mLayer);
        if (prl)
        {
            QColor brushColor = painter->brush().color();
            // Premultiply alpha for GPU blending (src=One, dst=OneMinusSrcAlpha)
            float a = brushColor.alphaF();
            QColor premul = QColor::fromRgbF(brushColor.redF() * a,
                                              brushColor.greenF() * a,
                                              brushColor.blueF() * a, a);

            const double dpr = mParentPlot->bufferDevicePixelRatio();
            const QSize outputSize = mParentPlot->rhiOutputSize();
            const int outHeight = outputSize.height();
            const bool yUp = rhi->isYUpInNDC();

            foreach (QCPDataRange segment, segments)
            {
                auto fillVerts = QCPLineExtruder::tessellateFillPolygon(
                    getFillPolygon(lines, segment), premul);
                if (!fillVerts.isEmpty())
                {
                    prl->addPlottable(fillVerts, {}, clipRect(), dpr,
                                       outHeight, yUp);
                }
            }
            return;
        }
    }
    // Existing QPainter fallback:
    foreach (QCPDataRange segment, segments)
        painter->drawPolygon(getFillPolygon(lines, segment));
}
```

Add includes at the top of `plottable-graph.cpp`:
```cpp
#include "painting/line-extruder.h"
#include "painting/plottable-rhi-layer.h"
```

- [ ] **Step 6: Build and run all tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: all tests pass. The GPU path is now active but the visual result needs manual verification.

- [ ] **Step 7: Commit**

```bash
git add src/plottables/plottable1d.h src/plottables/plottable-graph.cpp \
    src/core.h src/core.cpp
git commit -m "feat: wire Graph/Curve drawing into GPU rendering path

drawPolyline() and drawFill() now branch to GPU extrusion when
QRhi is active and pen is solid. Fills use trapezoid tessellation.
Falls back to QPainter for exports, dashed lines, and channel fills."
```

---

## Chunk 5: MSAA and Manual Testing

### Task 7: Enable MSAA

**Files:**
- Modify: `src/core.cpp`

- [ ] **Step 1: Set sample count in constructor**

In `QCustomPlot::QCustomPlot()`, after line 414 (`setAttribute(Qt::WA_NoMousePropagation)`), add:

```cpp
setSampleCount(4); // 4x MSAA for smooth GPU-rendered geometry
```

- [ ] **Step 2: Set sample count on composite pipeline**

In `QCustomPlot::render()`, in the lazy `mCompositePipeline` creation block (after `mCompositePipeline = mRhi->newGraphicsPipeline()`), add before `mCompositePipeline->create()`:

```cpp
mCompositePipeline->setSampleCount(sampleCount());
```

Every pipeline rendering into the MSAA render target must match its sample count, otherwise pipeline creation fails on Metal/D3D12 and triggers Vulkan validation errors.

- [ ] **Step 3: Build and run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: all tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/core.cpp
git commit -m "feat: enable 4x MSAA for GPU plottable antialiasing"
```

---

### Task 8: Manual visual verification

**Files:**
- Modify: `tests/manual/mainwindow.cpp` (if needed)

- [ ] **Step 1: Build and run the manual test app**

Run: `meson compile -C build && ./build/tests/manual/manual`

Verify visually:
- Lines render correctly (no gaps, proper width, smooth joins)
- Fills appear correctly (no holes, proper color)
- Axes, grid, and legend are unaffected
- Resizing works (geometry regenerated)
- Zooming/panning works (replot regenerates geometry)
- Compare with a reference QPainter render (temporarily disable GPU path by checking export mode)
- If available, test on HiDPI display (DPR > 1) — scissor rects and vertex positions must scale correctly
- Check console output for any RHI warnings about pipeline creation, sample count, or validation errors

- [ ] **Step 2: Fix any visual issues found**

This step is iterative. Common issues to watch for:
- Y-flip direction wrong (upside-down rendering)
- Scissor rect off by DPR factor
- Premultiplied alpha incorrect (colors look wrong)
- Miter join artifacts at very sharp angles
- Uniform buffer not being uploaded (geometry renders at wrong position)

- [ ] **Step 3: Run automated tests one final time**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: all tests pass.

- [ ] **Step 4: Commit any fixes**

```bash
git add -u
git commit -m "fix: visual corrections for GPU plottable rendering"
```

---

## Summary

| Chunk | Tasks | What it delivers |
|-------|-------|-----------------|
| 1 | Tasks 1-2 | Line extruder: pure geometry, fully tested, no QRhi dependency |
| 2 | Task 3 | Plottable shaders compiled and embedded in binary |
| 3 | Task 4 | QCPPlottableRhiLayer: GPU resource management class |
| 4 | Tasks 5-6 | Full integration: replot generates geometry, render draws it |
| 5 | Tasks 7-8 | MSAA + manual visual verification and fixes |

Each chunk is independently buildable and testable. Chunks 1-2 have zero risk to existing functionality. Chunk 3 adds infrastructure but no behavioral change. Chunk 4 activates the GPU path. Chunk 5 polishes.

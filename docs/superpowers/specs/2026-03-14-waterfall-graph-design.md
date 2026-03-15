# QCPWaterfallGraph Design Spec

## Problem

Seismographs and space physics instruments produce multi-channel time series where each trace should be displayed at a vertical offset proportional to a physical quantity (e.g., station distance, sensor position). This is called a "waterfall plot" or "record section." NeoQCP has no support for this; QCPMultiGraph draws all components at their absolute y-values, causing overlap.

## Goals

- Add a `QCPWaterfallGraph` class for waterfall/record-section plots
- Support uniform and data-driven vertical offsets
- Support optional per-trace amplitude normalization with configurable gain
- Y-axis shows offset values (distance, index), not signal amplitude
- Trace identification on hover/click (inherited from QCPMultiGraph)
- Manual test with a realistic seismograph setup

## Non-Goals (v1)

- Wiggle fill (positive/negative half-fill)
- Amplitude scale bar
- Horizontal waterfall (traces offset along x-axis)
- Vertical key axis support (key axis must be horizontal in v1)
- `lsImpulse` line style (impulse baselines would need per-component offset; deferred)

## Architecture

### Class Hierarchy

```
QCPAbstractPlottable
  └── QCPMultiGraph              (private → protected for data members)
        └── QCPWaterfallGraph    (new)

QCPAbstractMultiDataSource
  └── QCPWaterfallDataAdapter    (new, internal to waterfall)
```

### Key Insight: Data Source Adapter

`QCPAbstractMultiDataSource::getOptimizedLineData()` and `getLines()` both return pixel-space data — they call `coordToPixel` internally. There is no batch coordinate-space API.

Rather than fighting this, QCPWaterfallGraph interposes a **data source adapter** (`QCPWaterfallDataAdapter`) that wraps the user's original data source and transforms values in coordinate space *before* the pixel conversion pipeline. This means:

- Adaptive sampling works correctly (min/max computed on transformed values)
- `valueAt()`, `getLines()`, `getOptimizedLineData()` all return offset-adjusted data
- Parent's `selectTest()`, `selectEvent()`, `draw()` all work with transformed coordinates — many overrides become unnecessary
- `valueRange()` returns the offset-adjusted range automatically

### Files

| File | Change |
|------|--------|
| `src/plottables/plottable-multigraph.h` | `private:` → `protected:` for data members and helpers |
| `src/plottables/plottable-waterfall.h` | New: `QCPWaterfallDataAdapter` + `QCPWaterfallGraph` |
| `src/plottables/plottable-waterfall.cpp` | New implementation |
| `src/qcp.h` | Add `#include` for new header |
| `src/meson.build` | Add new source file |
| `tests/manual/mainwindow.h` | Add `setupWaterfallTest` declaration |
| `tests/manual/mainwindow.cpp` | Add `setupWaterfallTest` implementation |

### QCPMultiGraph Changes

Move from `private` to `protected`:

- `mDataSource`, `mComponents`, `mLineStyle`, `mAdaptiveSampling`, `mScatterSkip`
- `syncComponentCount()`, `updateBaseSelection()`
- Line-style transforms: `toStepLeftLines`, `toStepRightLines`, `toStepCenterLines` (static), `toImpulseLines` (non-static, accesses value axis)

No API or behavioral changes. Existing code is unaffected.

## QCPWaterfallDataAdapter

A `QCPAbstractMultiDataSource` wrapper that delegates to the original data source while transforming values in coordinate space.

### Method Categories

**Pure delegation (no transformation):**
- `columnCount()` → `mSource->columnCount()`
- `size()` → `mSource->size()`
- `empty()` → `mSource->empty()`
- `keyAt(i)` → `mSource->keyAt(i)`
- `keyRange(...)` → `mSource->keyRange(...)`
- `findBegin(...)` → `mSource->findBegin(...)`
- `findEnd(...)` → `mSource->findEnd(...)`

**Value transformation:**
- `valueAt(c, i)` → `offset[c] + mSource->valueAt(c, i) * normFactor[c] * gain`
- `valueRange(c, ...)` → transformed range (apply offset + scale to source's range)

**Line data (builds transformed value buffer, then calls algo functions):**

`getLines()` and `getOptimizedLineData()` cannot simply delegate to `mSource->getLines(...)` because those return pixel-space data with the source's raw values already baked in. Instead, the adapter:

1. Builds a `std::vector<double>` of transformed values for the `[begin, end)` range:
   ```
   for i in [begin, end):
       transformed[i-begin] = offset[c] + mSource->valueAt(c, i) * normFactor[c] * gain
   ```
2. Calls `qcp::algo::linesToPixels(sourceKeys, transformed, ...)` or `qcp::algo::optimizedLineData(sourceKeys, transformed, ...)` with the transformed buffer.

This reuses the existing adaptive sampling algorithm on transformed values. The allocation is only for the visible range (`[begin, end)`), not the full dataset.

To access the source's key container for the algo call, the adapter stores a reference to the keys obtained via `keyAt()` iteration into a pre-allocated buffer, or alternatively the adapter can implement the algo loop directly (iterate indices, call `keyAt`/`valueAt`, build QPointF via `coordToPixel`). The second approach avoids a key copy at the cost of not reusing the template algo — but the algo is simple enough that this is acceptable.

### API

```cpp
class QCPWaterfallDataAdapter : public QCPAbstractMultiDataSource {
public:
    explicit QCPWaterfallDataAdapter(std::shared_ptr<QCPAbstractMultiDataSource> source);

    void setSource(std::shared_ptr<QCPAbstractMultiDataSource> source);
    QCPAbstractMultiDataSource* source() const;

    // Transform parameters (set by QCPWaterfallGraph before each draw)
    void setOffsets(const QVector<double>& offsets);
    void setNormFactors(const QVector<double>& factors);
    void setGain(double gain);

    // QCPAbstractMultiDataSource interface — all overrides
    int columnCount() const override;     // delegation
    int size() const override;            // delegation
    double keyAt(int i) const override;   // delegation
    QCPRange keyRange(...) const override; // delegation
    int findBegin(...) const override;    // delegation
    int findEnd(...) const override;      // delegation

    double valueAt(int column, int i) const override;          // transformed
    QCPRange valueRange(int column, ...) const override;       // transformed

    QVector<QPointF> getLines(int column, int begin, int end,
                               QCPAxis* keyAxis, QCPAxis* valueAxis) const override;  // transformed + pixel
    QVector<QPointF> getOptimizedLineData(int column, int begin, int end, int pixelWidth,
                                           QCPAxis* keyAxis, QCPAxis* valueAxis) const override;  // transformed + pixel

private:
    std::shared_ptr<QCPAbstractMultiDataSource> mSource;
    QVector<double> mOffsets;
    QVector<double> mNormFactors;
    double mGain = 1.0;

    double transform(int column, double rawValue) const;
};
```

## QCPWaterfallGraph API

```cpp
class QCPWaterfallGraph : public QCPMultiGraph {
    Q_OBJECT
public:
    enum OffsetMode { omUniform, omCustom };
    Q_ENUM(OffsetMode)

    explicit QCPWaterfallGraph(QCPAxis* keyAxis, QCPAxis* valueAxis);

    // Original data source (user-facing)
    void setDataSource(std::unique_ptr<QCPAbstractMultiDataSource> source);
    void setDataSource(std::shared_ptr<QCPAbstractMultiDataSource> source);

    // Offset configuration
    OffsetMode offsetMode() const;
    void setOffsetMode(OffsetMode mode);
    void setUniformSpacing(double spacing);   // used when omUniform
    double uniformSpacing() const;
    void setOffsets(const QVector<double>& offsets);  // used when omCustom
    QVector<double> offsets() const;

    // Normalization
    bool normalize() const;
    void setNormalize(bool enabled);
    double gain() const;
    void setGain(double gain);  // scales normalized amplitude

    // Force recomputation of normalization factors (call after mutating data in-place)
    void invalidateNormalization();

protected:
    void draw(QCPPainter* painter) override;

    // Override getValueRange for proper auto-scaling margin
    QCPRange getValueRange(bool& foundRange,
                           QCP::SignDomain inSignDomain = QCP::sdBoth,
                           const QCPRange& inKeyRange = QCPRange()) const override;

private:
    OffsetMode mOffsetMode = omUniform;
    double mUniformSpacing = 1.0;
    QVector<double> mUserOffsets;
    bool mNormalize = true;
    double mGain = 1.0;
    bool mNormDirty = true;
    QVector<double> mCachedNormFactors;

    std::shared_ptr<QCPAbstractMultiDataSource> mOriginalSource;
    std::shared_ptr<QCPWaterfallDataAdapter> mAdapter;

    double effectiveOffset(int component) const;
    void updateAdapter();
    void recomputeNormFactors();
};
```

### setDataSource override

When the user sets a data source, QCPWaterfallGraph stores it as `mOriginalSource`, wraps it in `mAdapter`, and passes the adapter to the parent's `mDataSource`. The parent never sees the raw data — only the transformed view. Also marks norm factors dirty.

### draw() override

Calls `updateAdapter()` to refresh transform parameters (offsets, norm factors, gain), then delegates to the parent's `draw()`. The parent draws the already-transformed data normally.

```
draw():
    1. updateAdapter()    — push current offsets/normFactors/gain to adapter
    2. QCPMultiGraph::draw(painter)  — parent draws transformed data as-is
```

Note: `lsImpulse` line style is not supported in v1 (see Non-Goals). If `mLineStyle == lsImpulse`, the waterfall should fall back to `lsLine` or warn.

### No selectTest/selectEvent overrides needed

Because the adapter transforms all data at the source level, the parent's `selectTest()`, `selectTestRect()`, and `selectEvent()` all operate on offset-adjusted coordinates automatically. Hit testing, rect selection, and `valueAt()` comparisons all match the visual positions.

## Offset Model

Each trace `c` has a baseline at `effectiveOffset(c)`:

- **omUniform**: `effectiveOffset(c) = c * mUniformSpacing`
- **omCustom**: `effectiveOffset(c) = mUserOffsets[c]` if `c < mUserOffsets.size()`, otherwise falls back to `c * mUniformSpacing`

The adapter transforms each value as:

```
transformedValue(c, i) = effectiveOffset(c) + originalValue(c, i) * normFactor(c) * mGain
```

Where `normFactor(c)`:
- If `mNormalize == true`: `1.0 / max(|value|)` across **all** values in component `c` of the original source (full dataset, not visible range). Cached and recomputed only when dirty.
- If `mNormalize == false`: `1.0`

The gain controls how much each trace wiggles around its baseline. A gain of `0.5 * uniformSpacing` means traces fill half the space between baselines.

### Cache Invalidation

Norm factors are marked dirty and recomputed on next `updateAdapter()` call when:
- The data source changes (via `setDataSource`)
- Normalization is toggled (via `setNormalize`)
- The component count changes
- The user explicitly calls `invalidateNormalization()` (for mutable data sources where underlying data changes in-place)

Offsets and gain are always pushed to the adapter since they're cheap to set.

## Axis Integration

### getValueRange() override

Returns the range spanning all effective offsets, with margin for trace amplitude. Respects `inSignDomain` and `inKeyRange` parameters:

```cpp
double margin = mNormalize ? mGain : maxRawAmplitude * mGain;
double lo = min(effectiveOffset(c) for all c) - margin;
double hi = max(effectiveOffset(c) for all c) + margin;

// Apply sign domain filtering
if (inSignDomain == QCP::sdPositive) lo = qMax(lo, 0.0);
if (inSignDomain == QCP::sdNegative) hi = qMin(hi, 0.0);
foundRange = (lo < hi);
```

Where `maxRawAmplitude` is the maximum absolute value across all traces in the original source. When normalizing, the max amplitude after transform is exactly `mGain`, so the margin is `mGain`.

If `inKeyRange` is non-default, delegate to the adapter's `valueRange()` for each component and merge the ranges (the adapter already returns transformed values).

### getKeyRange()

Inherited from QCPMultiGraph via the adapter (pure delegation) — unchanged.

### Y-axis ticks

The user attaches a `QCPAxisTickerFixed` or `QCPAxisTickerText` with labels at offset values. The waterfall does not force a ticker. Documentation and the manual test show the recommended setup.

### Interface1D

All Interface1D methods (`dataMainValue`, `dataPixelPosition`, `dataValueRange`) are inherited from QCPMultiGraph. They read from the adapter (which returns transformed values), so they automatically return offset-adjusted coordinates. No overrides needed.

Note: `dataMainValue(index)` uses component 0, consistent with QCPMultiGraph. Tracers will track component 0's offset.

### Legend icon

The parent's `drawLegendIcon()` is reused as-is — it draws per-component colored line segments.

## Manual Test: setupWaterfallTest

A realistic seismograph record section:

- **8 traces** representing seismic stations at distances 20, 45, 70, 95, 120, 150, 185, 220 km
- **Synthetic seismograms**: damped sinusoid with arrival time proportional to distance (simulating P-wave at ~6 km/s), plus low-amplitude noise
- **Custom offsets** set to station distances (omCustom mode)
- **Normalization enabled** with gain tuned so traces don't overlap excessively
- **Y-axis**: label "Epicentral Distance (km)", ticks at station distances using `QCPAxisTickerText`
- **X-axis**: label "Time (s)", range 0–40s
- **Per-trace colors**: using the default MultiGraph palette
- **Interactive**: drag to pan, scroll to zoom (standard `presetInteractive`)
- **Component names**: "Station A" through "Station H" for legend/tooltip identification

This test goes into `mainwindow.h/cpp` following the existing `setupXxxTest` pattern.

## Testing Strategy

- **Manual test** (described above): visual verification of offset, normalization, axis ticks, interaction
- **Unit tests** (deferred to implementation plan): `effectiveOffset()` correctness, normalization factor computation, `getValueRange()` bounds, adapter `valueAt()` transform correctness, adapter `valueRange()` bounds, sign domain filtering

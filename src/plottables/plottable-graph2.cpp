#include "plottable-graph2.h"
#include "plottable-l1-cache.h"
#include "plottable-linestyle.h"
#include "Profiling.hpp"
#include "../datasource/graph-resampler.h"

#include "../axis/axis.h"
#include "../core.h"
#include "../layoutelements/layoutelement-axisrect.h"
#include "../painting/line-extruder.h"
#include "../painting/painter.h"
#include "../painting/plottable-rhi-layer.h"
#include "../painting/viewport-offset.h"
#include "../vector2d.h"

QCPGraph2::QCPGraph2(QCPAxis* keyAxis, QCPAxis* valueAxis)
    : QCPAbstractPlottable(keyAxis, valueAxis)
    , mPipeline(parentPlot() ? parentPlot()->pipelineScheduler() : nullptr, this)
{
    setPen(QPen(Qt::blue, 0));
    setBrush(Qt::NoBrush);

    if (keyAxis)
    {
        connect(keyAxis, QOverload<const QCPRange&>::of(&QCPAxis::rangeChanged),
                this, &QCPGraph2::onViewportChanged);
        connect(keyAxis, &QCPAxis::scaleTypeChanged,
                this, [this] { mLineCacheDirty = true; mCachedLines.clear(); });
    }
    if (valueAxis)
    {
        connect(valueAxis, QOverload<const QCPRange&>::of(&QCPAxis::rangeChanged),
                this, &QCPGraph2::onViewportChanged);
        connect(valueAxis, &QCPAxis::scaleTypeChanged,
                this, [this] { mLineCacheDirty = true; mCachedLines.clear(); });
    }

    connect(&mPipeline, &QCPGraphPipeline::finished,
            this, [this](uint64_t) { onL1Ready(); });
    connect(&mPipeline, &QCPGraphPipeline::busyChanged,
            this, [this](bool) { updateEffectiveBusy(); });
}

QCPGraph2::~QCPGraph2() = default;

static void ensureL1Transform(QCPGraphPipeline& pipeline, int sourceSize)
{
    if (sourceSize >= qcp::algo::kResampleThreshold)
    {
        if (!pipeline.hasTransform())
        {
            // Pipeline only builds L1 cache — L2 is done synchronously
            pipeline.setTransform(TransformKind::ViewportIndependent,
                [](const QCPAbstractDataSource& src,
                   const ViewportParams& vp,
                   std::any& cache) -> std::shared_ptr<QCPAbstractDataSource> {
                    return qcp::algo::buildL1Cache(src, vp, cache);
                });
        }
    }
    else if (pipeline.hasTransform())
    {
        pipeline.clearTransform();
    }
}

void QCPGraph2::setDataSource(std::unique_ptr<QCPAbstractDataSource> source)
{
    setDataSource(std::shared_ptr<QCPAbstractDataSource>(std::move(source)));
}

void QCPGraph2::setDataSource(std::shared_ptr<QCPAbstractDataSource> source)
{
    mDataSource = std::move(source);
    mL1Cache.reset();
    mL2Result.reset();
    mLineCacheDirty = true;
    mCachedLines.clear();
    mL2Dirty = false;
    mNeedsResampling = mDataSource && mDataSource->size() >= qcp::algo::kResampleThreshold;
    if (mDataSource)
        ensureL1Transform(mPipeline, mDataSource->size());
    mPipeline.setSource(mDataSource);
}

void QCPGraph2::dataChanged()
{
    mLineCacheDirty = true;
    mCachedLines.clear();

    bool wasResampling = mNeedsResampling;
    mNeedsResampling = mDataSource && mDataSource->size() >= qcp::algo::kResampleThreshold;

    if (mNeedsResampling && !wasResampling && mDataSource)
        ensureL1Transform(mPipeline, mDataSource->size());

    if (mPipeline.hasTransform())
    {
        mL1Cache.reset();
        mL2Result.reset();
        mL2Dirty = false;
        mPipeline.onDataChanged();
    }
    else if (mParentPlot)
        mParentPlot->replot();
}

void QCPGraph2::onL1Ready()
{
    PROFILE_HERE_N("QCPGraph2::onL1Ready");
    qcp::extractL1Cache<qcp::algo::GraphResamplerCache>(mPipeline.cache(), mL1Cache, mL2Dirty);
    if (parentPlot())
        parentPlot()->replot(QCustomPlot::rpQueuedReplot);
}

void QCPGraph2::rebuildL2(const ViewportParams& vp)
{
    PROFILE_HERE_N("QCPGraph2::rebuildL2");
    if (!mL1Cache) return;
    if (vp.keyLogScale)
    {
        mL2Result.reset(); // L2 not supported for log scale — fall back to raw data
        return;
    }
    mL2Result = qcp::algo::resampleL2(*mL1Cache, vp);
}

// --- QCPPlottableInterface1D ---

int QCPGraph2::dataCount() const
{
    return mDataSource ? mDataSource->size() : 0;
}

double QCPGraph2::dataMainKey(int index) const
{
    return mDataSource ? mDataSource->keyAt(index) : 0.0;
}

double QCPGraph2::dataSortKey(int index) const
{
    return mDataSource ? mDataSource->keyAt(index) : 0.0;
}

double QCPGraph2::dataMainValue(int index) const
{
    return mDataSource ? mDataSource->valueAt(index) : 0.0;
}

QCPRange QCPGraph2::dataValueRange(int index) const
{
    if (!mDataSource)
        return QCPRange(0, 0);
    double v = mDataSource->valueAt(index);
    return QCPRange(v, v);
}

QPointF QCPGraph2::dataPixelPosition(int index) const
{
    if (!mDataSource || !mKeyAxis || !mValueAxis)
        return {};
    return coordsToPixels(mDataSource->keyAt(index), mDataSource->valueAt(index));
}

int QCPGraph2::findBegin(double sortKey, bool expandedRange) const
{
    return mDataSource ? mDataSource->findBegin(sortKey, expandedRange) : 0;
}

int QCPGraph2::findEnd(double sortKey, bool expandedRange) const
{
    return mDataSource ? mDataSource->findEnd(sortKey, expandedRange) : 0;
}

QCPDataSelection QCPGraph2::selectTestRect(const QRectF& rect, bool onlySelectable) const
{
    QCPDataSelection result;
    if ((onlySelectable && mSelectable == QCP::stNone) || !mDataSource || mDataSource->empty())
        return result;
    if (!mKeyAxis || !mValueAxis)
        return result;

    double key1, value1, key2, value2;
    pixelsToCoords(rect.topLeft(), key1, value1);
    pixelsToCoords(rect.bottomRight(), key2, value2);
    if (key1 > key2) qSwap(key1, key2);
    if (value1 > value2) qSwap(value1, value2);
    QCPRange keyRange(key1, key2);
    QCPRange valueRange(value1, value2);

    int begin = mDataSource->findBegin(keyRange.lower, false);
    int end = mDataSource->findEnd(keyRange.upper, false);

    int currentSegmentBegin = -1;
    for (int i = begin; i < end; ++i)
    {
        double k = mDataSource->keyAt(i);
        double v = mDataSource->valueAt(i);
        if (currentSegmentBegin == -1)
        {
            if (keyRange.contains(k) && valueRange.contains(v))
                currentSegmentBegin = i;
        }
        else if (!keyRange.contains(k) || !valueRange.contains(v))
        {
            result.addDataRange(QCPDataRange(currentSegmentBegin, i), false);
            currentSegmentBegin = -1;
        }
    }
    if (currentSegmentBegin != -1)
        result.addDataRange(QCPDataRange(currentSegmentBegin, end), false);

    result.simplify();
    return result;
}

// --- QCPAbstractPlottable ---

double QCPGraph2::selectTest(const QPointF& pos, bool onlySelectable, QVariant* details) const
{
    if ((onlySelectable && mSelectable == QCP::stNone) || !mDataSource || mDataSource->empty())
        return -1;
    if (!mKeyAxis || !mValueAxis)
        return -1;

    // Fast path: when details aren't needed (e.g. wheel events), just check
    // if the mouse is inside the clip rect rather than doing per-point testing.
    if (!details)
    {
        auto* axisRect = mKeyAxis->axisRect();
        if (axisRect && axisRect->rect().contains(pos.toPoint()))
            return mParentPlot->selectionTolerance() * 0.99;
        return -1;
    }

    // Use resampled data for hit testing when available (O(k) instead of O(n))
    const QCPAbstractDataSource* ds = mL2Result ? mL2Result.get() : mDataSource.get();

    double posKeyMin, posKeyMax, dummy;
    pixelsToCoords(
        pos - QPointF(mParentPlot->selectionTolerance(), mParentPlot->selectionTolerance()),
        posKeyMin, dummy);
    pixelsToCoords(
        pos + QPointF(mParentPlot->selectionTolerance(), mParentPlot->selectionTolerance()),
        posKeyMax, dummy);
    if (posKeyMin > posKeyMax)
        qSwap(posKeyMin, posKeyMax);

    int begin = ds->findBegin(posKeyMin, true);
    int end = ds->findEnd(posKeyMax, true);
    if (begin == end)
        return -1;

    double minDistSqr = (std::numeric_limits<double>::max)();
    int minDistIndex = -1;
    QCPRange keyRange(mKeyAxis->range());
    QCPRange valueRange(mValueAxis->range());

    for (int i = begin; i < end; ++i)
    {
        double k = ds->keyAt(i);
        double v = ds->valueAt(i);
        if (keyRange.contains(k) && valueRange.contains(v))
        {
            double distSqr = QCPVector2D(coordsToPixels(k, v) - pos).lengthSquared();
            if (distSqr < minDistSqr)
            {
                minDistSqr = distSqr;
                minDistIndex = i;
            }
        }
    }

    QCPDataSelection selectionResult;
    if (minDistIndex >= 0)
        selectionResult.addDataRange(QCPDataRange(minDistIndex, minDistIndex + 1), false);
    selectionResult.simplify();
    if (details)
        details->setValue(selectionResult);
    return minDistIndex >= 0 ? qSqrt(minDistSqr) : -1;
}

QCPRange QCPGraph2::getKeyRange(bool& foundRange, QCP::SignDomain inSignDomain) const
{
    PROFILE_HERE_N("QCPGraph2::getKeyRange");
    if (!mDataSource || mDataSource->empty())
    {
        foundRange = false;
        return {};
    }
    return mDataSource->keyRange(foundRange, inSignDomain);
}

QCPRange QCPGraph2::getValueRange(bool& foundRange, QCP::SignDomain inSignDomain,
                                   const QCPRange& inKeyRange) const
{
    PROFILE_HERE_N("QCPGraph2::getValueRange");
    if (!mDataSource || mDataSource->empty())
    {
        foundRange = false;
        return {};
    }
    return mDataSource->valueRange(foundRange, inSignDomain, inKeyRange);
}

// --- Drawing ---

QPointF QCPGraph2::stallPixelOffset() const
{
    if (mHasRenderedRange && !mCachedLines.isEmpty())
        return qcp::computeViewportOffset(mKeyAxis.data(), mValueAxis.data(),
                                          mRenderedRange.key, mRenderedRange.value);
    return {};
}

void QCPGraph2::draw(QCPPainter* painter)
{
    PROFILE_HERE_N("QCPGraph2::draw");
    if (!mKeyAxis || !mValueAxis || !mDataSource)
        return;

    // Lazy L2 rebuild: coalesces all viewport changes since last draw into one rebuild
    if (mL2Dirty && mL1Cache)
    {
        auto* axisRect = mKeyAxis->axisRect();
        if (axisRect)
        {
            ViewportParams vp;
            vp.keyRange = mKeyAxis->range();
            vp.valueRange = mValueAxis->range();
            vp.plotWidthPx = axisRect->width();
            vp.plotHeightPx = axisRect->height();
            vp.keyLogScale = (mKeyAxis->scaleType() == QCPAxis::stLogarithmic);
            vp.valueLogScale = (mValueAxis->scaleType() == QCPAxis::stLogarithmic);
            rebuildL2(vp);
            mLineCacheDirty = true; // L2 data changed → cached pixel-space lines are stale
        }
        mL2Dirty = false;
    }

    // When the async pipeline is active but hasn't delivered results yet,
    // skip drawing raw data.  Drawing 100K+ points through QPainter is
    // extremely slow on macOS (Core Graphics), causing multi-second stalls.
    // The pipeline will trigger a replot when L1 is ready.
    // Exception: log-scale axes never produce L2 results (rebuildL2 resets
    // mL2Result), so we must not skip or the graph stays permanently blank.
    // Exception: export mode (pmNoCaching) must always draw.
    if (mNeedsResampling && !mL2Result
        && !painter->modes().testFlag(QCPPainter::pmNoCaching)
        && mKeyAxis->scaleType() != QCPAxis::stLogarithmic)
        return;

    // Use L2 resampled data if available, otherwise fall back to raw data.
    // Raw data is used for small datasets (below threshold), log-scale keys,
    // or while L1 is still building asynchronously.
    const QCPAbstractDataSource* ds = mL2Result ? mL2Result.get() : mDataSource.get();
    if (!ds || ds->empty())
        return;

    PROFILE_PASS_VALUE(ds->size());

    if (mKeyAxis->range().size() <= 0)
        return;
    if (mLineStyle == lsNone && mScatterStyle.isNone())
        return;

    const QCPRange keyRange = mKeyAxis->range();
    int begin = ds->findBegin(keyRange.lower);
    int end = ds->findEnd(keyRange.upper);
    if (begin >= end)
        return;

    const bool keyIsVertical = mKeyAxis->orientation() == Qt::Vertical;

    // --- Line caching with GPU translation ---
    const QSize currentPlotSize(mKeyAxis->axisRect()->width(), mKeyAxis->axisRect()->height());
    const bool isExportMode = painter->modes().testFlag(QCPPainter::pmNoCaching)
                            || painter->modes().testFlag(QCPPainter::pmVectorized);
    auto [needFreshLines, gpuOffset] = qcp::evaluateLineCache(
        mLineCacheDirty, mCachedLines.isEmpty(),
        currentPlotSize, mCachedPlotSize,
        mHasRenderedRange, mRenderedRange.key, mRenderedRange.value,
        mKeyAxis.data(), mValueAxis.data(), isExportMode);

    QVector<QPointF> lines;
    if (needFreshLines)
    {
        // Expand data range by 50% on each side so GPU-translated pans
        // don't expose uncovered edges before the rebuild threshold triggers.
        const double margin = keyRange.size() * 0.5;
        int cacheBegin = ds->findBegin(keyRange.lower - margin);
        int cacheEnd = ds->findEnd(keyRange.upper + margin);

        if (mAdaptiveSampling)
        {
            const int pixDim = keyIsVertical
                ? static_cast<int>(mKeyAxis->axisRect()->height())
                : static_cast<int>(mKeyAxis->axisRect()->width());
            lines = ds->getOptimizedLineData(
                cacheBegin, cacheEnd, pixDim, mKeyAxis.data(), mValueAxis.data());
        }
        else
        {
            lines = ds->getLines(cacheBegin, cacheEnd, mKeyAxis.data(), mValueAxis.data());
        }

        // Cache for reuse (but not during export)
        if (!isExportMode)
        {
            mCachedLines = lines;
            mRenderedRange = {mKeyAxis->range(), mValueAxis->range()};
            mHasRenderedRange = true;
            mLineCacheDirty = false;
            mCachedPlotSize = currentPlotSize;
            gpuOffset = {};
        }
    }
    else
    {
        lines = mCachedLines;
        // gpuOffset already computed above
    }

    if (lines.isEmpty())
        return;

    // Draw lines
    if (mLineStyle != lsNone && mPen.style() != Qt::NoPen && mPen.color().alpha() != 0)
    {
        // Helper: try GPU path for a polyline, fall back to QPainter.
        // Disabled for export: pmVectorized (SVG/PDF) and pmNoCaching (raster
        // export via toPixmap/saveRastered) don't composite plottable RHI layers.
        auto drawPoly = [&](const QVector<QPointF>& pts) {
            if (auto* rhi = mParentPlot ? mParentPlot->rhi() : nullptr;
                rhi && !painter->modes().testFlag(QCPPainter::pmVectorized)
                    && !painter->modes().testFlag(QCPPainter::pmNoCaching)
                    && mPen.style() == Qt::SolidLine)
            {
                if (auto* prl = mParentPlot->plottableRhiLayer(mLayer))
                {
                    // Pre-translate cached points instead of setting layer-wide offset,
                    // so other plottables on the same layer are unaffected.
                    QVector<QPointF> translated;
                    const QVector<QPointF>& src = (!gpuOffset.isNull()) ?
                        [&]() -> const QVector<QPointF>& {
                            translated.resize(pts.size());
                            for (int i = 0; i < pts.size(); ++i)
                                translated[i] = pts[i] + gpuOffset;
                            return translated;
                        }() : pts;

                    const QColor penColor = mPen.color();
                    const double dpr = mParentPlot->bufferDevicePixelRatio();
                    const float penWidth = (mPen.isCosmetic() || qFuzzyIsNull(mPen.widthF()))
                        ? static_cast<float>(1.0 / dpr)
                        : qMax(1.0f, static_cast<float>(mPen.widthF()));
                    auto strokeVerts = QCPLineExtruder::extrudePolyline(src, penWidth, penColor);
                    if (!strokeVerts.isEmpty())
                    {
                        const QSize outputSize = mParentPlot->rhiOutputSize();
                        prl->addPlottable({}, strokeVerts, clipRect(), dpr,
                                           outputSize.height(), rhi->isYUpInNDC());
                        return;
                    }
                }
            }
            // Software fallback
            applyDefaultAntialiasingHint(painter);
            painter->setPen(mPen);
            painter->setBrush(Qt::NoBrush);
            if (!gpuOffset.isNull())
            {
                painter->translate(gpuOffset);
                painter->drawPolyline(pts.constData(), pts.size());
                painter->translate(-gpuOffset);
            }
            else
            {
                painter->drawPolyline(pts.constData(), pts.size());
            }
        };

        switch (mLineStyle)
        {
            case lsNone:
                break;
            case lsLine:
                drawPoly(lines);
                break;
            case lsStepLeft:
                drawPoly(qcp::toStepLeftLines(lines, keyIsVertical));
                break;
            case lsStepRight:
                drawPoly(qcp::toStepRightLines(lines, keyIsVertical));
                break;
            case lsStepCenter:
                drawPoly(qcp::toStepCenterLines(lines, keyIsVertical));
                break;
            case lsImpulse:
            {
                auto impulse = qcp::toImpulseLines(lines, keyIsVertical, mValueAxis->coordToPixel(0));
                applyDefaultAntialiasingHint(painter);
                QPen impulsePen = mPen;
                impulsePen.setCapStyle(Qt::FlatCap);
                painter->setPen(impulsePen);
                painter->drawLines(impulse);
                break;
            }
        }
    }

    // Draw scatters
    if (!mScatterStyle.isNone())
    {
        applyScattersAntialiasingHint(painter);
        mScatterStyle.applyTo(painter, mPen);
        const int skip = mScatterSkip + 1;
        for (int i = 0; i < lines.size(); i += skip)
            mScatterStyle.drawShape(painter, lines[i].x(), lines[i].y());
    }
}

void QCPGraph2::drawLegendIcon(QCPPainter* painter, const QRectF& rect) const
{
    applyDefaultAntialiasingHint(painter);

    // Draw line sample
    if (mLineStyle != lsNone)
    {
        painter->setPen(mPen);
        painter->drawLine(QLineF(rect.left(), rect.center().y(),
                                  rect.right() + 5, rect.center().y()));
    }

    // Draw scatter sample
    if (!mScatterStyle.isNone())
    {
        applyScattersAntialiasingHint(painter);
        QCPScatterStyle iconStyle = mScatterStyle;
        iconStyle.setSize(qMin(mScatterStyle.size(), rect.height() * 0.8));
        iconStyle.applyTo(painter, mPen);
        iconStyle.drawShape(painter, rect.center().x(), rect.center().y());
    }
}

void QCPGraph2::onViewportChanged()
{
    // Just mark L2 as needing rebuild — actual rebuild happens lazily in draw()
    // to coalesce multiple rangeChanged signals per frame (pan fires many mouse moves)
    if (mL1Cache)
        mL2Dirty = true;
}

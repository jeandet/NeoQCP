#include "plottable-multigraph.h"
#include "../axis/axis.h"
#include "../core.h"
#include "../datasource/graph-resampler.h"
#include "../datasource/resampled-multi-datasource.h"
#include "../layoutelements/layoutelement-axisrect.h"
#include "../layoutelements/layoutelement-legend-group.h"
#include "../painting/line-extruder.h"
#include "../painting/painter.h"
#include "../painting/plottable-rhi-layer.h"
#include "../painting/viewport-offset.h"
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
    , mPipeline(parentPlot() ? parentPlot()->pipelineScheduler() : nullptr, this)
{
    // Base constructor auto-adds a QCPPlottableLegendItem (vtable not yet set up
    // for virtual addToLegend). Replace it with our group legend item.
    if (mParentPlot && mParentPlot->autoAddPlottableToLegend() && mParentPlot->legend) {
        QCPAbstractPlottable::removeFromLegend(mParentPlot->legend);
        addToLegend();
    }

    if (keyAxis)
    {
        connect(keyAxis, QOverload<const QCPRange&>::of(&QCPAxis::rangeChanged),
                this, &QCPMultiGraph::onViewportChanged);
        connect(keyAxis, &QCPAxis::scaleTypeChanged,
                this, [this] { mLineCacheDirty = true; mCachedLines.clear(); });
    }
    if (valueAxis)
    {
        connect(valueAxis, &QCPAxis::scaleTypeChanged,
                this, [this] { mLineCacheDirty = true; mCachedLines.clear(); });
    }

    connect(&mPipeline, &QCPMultiGraphPipeline::finished,
            this, [this](uint64_t) { onL1Ready(); });
    connect(&mPipeline, &QCPMultiGraphPipeline::busyChanged,
            this, [this](bool) { updateEffectiveBusy(); });
}

QCPMultiGraph::~QCPMultiGraph() = default;

void QCPMultiGraph::setDataSource(std::unique_ptr<QCPAbstractMultiDataSource> source)
{
    setDataSource(std::shared_ptr<QCPAbstractMultiDataSource>(std::move(source)));
}

static void ensureL1TransformMulti(QCPMultiGraphPipeline& pipeline, int sourceSize, int colCount)
{
    const bool needsResampling = colCount > 0 && sourceSize >= 100'000
        && static_cast<int64_t>(sourceSize) * colCount >= qcp::algo::kResampleThreshold;
    if (needsResampling)
    {
        if (!pipeline.hasTransform())
        {
            pipeline.setTransform(TransformKind::ViewportIndependent,
                [](const QCPAbstractMultiDataSource& src,
                   const ViewportParams& vp,
                   std::any& cache) -> std::shared_ptr<QCPAbstractMultiDataSource> {
                    return qcp::algo::buildL1CacheMulti(src, vp, cache);
                });
        }
    }
    else if (pipeline.hasTransform())
    {
        pipeline.clearTransform();
    }
}

void QCPMultiGraph::setDataSource(std::shared_ptr<QCPAbstractMultiDataSource> source)
{
    mDataSource = std::move(source);
    syncComponentCount();
    mL1Cache.reset();
    mL2Result.reset();
    mL2Dirty = false;
    mLineCacheDirty = true;
    mCachedLines.clear();

    if (mDataSource)
    {
        mNeedsResampling = mDataSource->columnCount() > 0
            && mDataSource->size() >= 100'000
            && static_cast<int64_t>(mDataSource->size()) * mDataSource->columnCount()
               >= qcp::algo::kResampleThreshold;
        ensureL1TransformMulti(mPipeline, mDataSource->size(), mDataSource->columnCount());
    }
    else
    {
        mNeedsResampling = false;
    }
    mPipeline.setSource(mDataSource);
}

void QCPMultiGraph::dataChanged()
{
    mLineCacheDirty = true;
    mCachedLines.clear();
    if (mDataSource)
    {
        bool wasResampling = mNeedsResampling;
        mNeedsResampling = mDataSource->columnCount() > 0
            && mDataSource->size() >= 100'000
            && static_cast<int64_t>(mDataSource->size()) * mDataSource->columnCount()
               >= qcp::algo::kResampleThreshold;

        if (mNeedsResampling != wasResampling)
            ensureL1TransformMulti(mPipeline, mDataSource->size(), mDataSource->columnCount());
    }

    mL1Cache.reset();
    mL2Result.reset();
    mL2Dirty = false;

    if (mPipeline.hasTransform())
        mPipeline.onDataChanged();
    else if (mParentPlot)
        mParentPlot->replot();
}

void QCPMultiGraph::onL1Ready()
{
    auto& pipelineCache = mPipeline.cache();
    auto* c = std::any_cast<qcp::algo::MultiGraphResamplerCache>(&pipelineCache);
    if (c && c->sourceSize > 0)
    {
        mL1Cache = std::make_shared<qcp::algo::MultiGraphResamplerCache>(std::move(*c));
        pipelineCache = std::any{};
        mL2Dirty = true;
    }
    if (parentPlot())
        parentPlot()->replot(QCustomPlot::rpQueuedReplot);
}

void QCPMultiGraph::rebuildL2(const ViewportParams& vp)
{
    if (!mL1Cache) return;
    mL2Result = qcp::algo::resampleL2Multi(*mL1Cache, vp);
}

void QCPMultiGraph::onViewportChanged()
{
    if (mL1Cache)
        mL2Dirty = true;
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

// --- Selection ---

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
    mLastRectSelections.clear();
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

    mLastRectSelections.resize(mComponents.size());
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
        mLastRectSelections[c] = colSel;
        for (int r = 0; r < colSel.dataRangeCount(); ++r)
            unionResult.addDataRange(colSel.dataRange(r), false);
    }
    unionResult.simplify();
    return unionResult;
}

void QCPMultiGraph::selectEvent(QMouseEvent* event, bool additive, const QVariant& details,
                                 bool* selectionStateChanged)
{
    Q_UNUSED(event);

    if (!additive) {
        for (auto& c : mComponents)
            c.selection = QCPDataSelection();
    }

    if (details.canConvert<QCPDataSelection>()) {
        // Rect selection: use per-component selections computed by selectTestRect,
        // which already filtered by the rect's key and value ranges.
        if (!mLastRectSelections.isEmpty()) {
            for (int c = 0; c < mComponents.size() && c < mLastRectSelections.size(); ++c)
                mComponents[c].selection = mLastRectSelections[c];
            mLastRectSelections.clear();
        }
    } else {
        // Point selection: details is a QVariantMap with componentIndex/dataIndex
        auto map = details.toMap();
        int compIdx = map.value("componentIndex", -1).toInt();
        int dataIdx = map.value("dataIndex", -1).toInt();
        if (compIdx < 0 || compIdx >= mComponents.size() || dataIdx < 0)
            return;
        mComponents[compIdx].selection.addDataRange(QCPDataRange(dataIdx, dataIdx + 1), false);
        mComponents[compIdx].selection.simplify();
    }

    updateBaseSelection();
    if (selectionStateChanged)
        *selectionStateChanged = true;
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

// --- Drawing ---

QPointF QCPMultiGraph::stallPixelOffset() const
{
    if (mHasRenderedRange && !mCachedLines.isEmpty())
        return qcp::computeViewportOffset(mKeyAxis.data(), mValueAxis.data(),
                                          mRenderedRange.key, mRenderedRange.value);
    return {};
}

void QCPMultiGraph::draw(QCPPainter* painter)
{
    if (!mKeyAxis || !mValueAxis || !mDataSource || mDataSource->empty())
        return;
    if (mKeyAxis->range().size() <= 0)
        return;

    // Lazy L2 rebuild from L1 cache
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
            rebuildL2(vp);
        }
        mL2Dirty = false;
    }

    // Export path: synchronous fallback when no L2 result yet
    if (!mL2Result && mNeedsResampling
        && painter->modes().testFlag(QCPPainter::pmNoCaching))
    {
        ViewportParams vp;
        vp.keyRange = mKeyAxis->range();
        vp.valueRange = mValueAxis->range();
        auto* axisRect = mKeyAxis->axisRect();
        vp.plotWidthPx = axisRect ? axisRect->width() : 800;
        vp.plotHeightPx = axisRect ? axisRect->height() : 600;
        vp.keyLogScale = (mKeyAxis->scaleType() == QCPAxis::stLogarithmic);
        mPipeline.runSynchronously(vp);
        onL1Ready();
        if (mL1Cache)
            rebuildL2(vp);
    }

    // When the async pipeline is active but hasn't delivered results yet,
    // skip drawing raw data.  Drawing 100K+ points through QPainter is
    // extremely slow on macOS (Core Graphics), causing multi-second stalls.
    // The pipeline will trigger a replot when L1 is ready.
    // Exception: log-scale axes never produce L2 results, so we must not skip.
    // Exception: export mode (pmNoCaching) must always draw.
    if (mNeedsResampling && !mL2Result
        && !painter->modes().testFlag(QCPPainter::pmNoCaching)
        && mKeyAxis->scaleType() != QCPAxis::stLogarithmic)
        return;

    const QCPAbstractMultiDataSource* ds = mL2Result ? mL2Result.get() : mDataSource.get();

    const QCPRange keyRange = mKeyAxis->range();
    int begin = ds->findBegin(keyRange.lower);
    int end = ds->findEnd(keyRange.upper);
    if (begin >= end)
        return;

    const bool keyIsVertical = mKeyAxis->orientation() == Qt::Vertical;
    const int pixelWidth = keyIsVertical
        ? static_cast<int>(mKeyAxis->axisRect()->height())
        : static_cast<int>(mKeyAxis->axisRect()->width());

    // --- Line caching with GPU translation ---
    bool needFreshLines = mLineCacheDirty || mCachedLines.isEmpty();

    QSize currentPlotSize(mKeyAxis->axisRect()->width(), mKeyAxis->axisRect()->height());
    if (currentPlotSize != mCachedPlotSize)
        needFreshLines = true;

    if (!needFreshLines && mHasRenderedRange)
    {
        double keyRatio = mKeyAxis->range().size() / mRenderedRange.key.size();
        double valRatio = mValueAxis->range().size() / mRenderedRange.value.size();
        if (qAbs(keyRatio - 1.0) > 0.01 || qAbs(valRatio - 1.0) > 0.01)
            needFreshLines = true;
    }

    QPointF gpuOffset;
    if (!needFreshLines && mHasRenderedRange)
    {
        gpuOffset = qcp::computeViewportOffset(mKeyAxis.data(), mValueAxis.data(),
                                               mRenderedRange.key, mRenderedRange.value);
        const double keyDim = keyIsVertical
            ? mKeyAxis->axisRect()->height()
            : mKeyAxis->axisRect()->width();
        const double valDim = keyIsVertical
            ? mKeyAxis->axisRect()->width()
            : mKeyAxis->axisRect()->height();
        const double keyOff = qAbs(keyIsVertical ? gpuOffset.y() : gpuOffset.x());
        const double valOff = qAbs(keyIsVertical ? gpuOffset.x() : gpuOffset.y());
        if (keyOff > keyDim * 0.5 || valOff > valDim * 0.5)
            needFreshLines = true;
    }

    const bool isExportMode = painter->modes().testFlag(QCPPainter::pmNoCaching)
                           || painter->modes().testFlag(QCPPainter::pmVectorized);
    if (isExportMode)
        needFreshLines = true;

    QVector<QVector<QPointF>> exportLines;
    auto& linesTarget = isExportMode ? exportLines : mCachedLines;

    if (needFreshLines)
    {
        // Expand data range by 50% on each side so GPU-translated pans
        // don't expose uncovered edges before the rebuild threshold triggers.
        const double margin = keyRange.size() * 0.5;
        int cacheBegin = ds->findBegin(keyRange.lower - margin);
        int cacheEnd = ds->findEnd(keyRange.upper + margin);

        linesTarget.resize(mComponents.size());
        for (int c = 0; c < mComponents.size(); ++c)
        {
            if (!mComponents[c].visible) { linesTarget[c].clear(); continue; }
            if (mL2Result)
                linesTarget[c] = ds->getLines(c, cacheBegin, cacheEnd, mKeyAxis.data(), mValueAxis.data());
            else if (mAdaptiveSampling)
                linesTarget[c] = ds->getOptimizedLineData(c, cacheBegin, cacheEnd, pixelWidth,
                                                           mKeyAxis.data(), mValueAxis.data());
            else
                linesTarget[c] = ds->getLines(c, cacheBegin, cacheEnd, mKeyAxis.data(), mValueAxis.data());
        }
        if (!isExportMode)
        {
            mRenderedRange = {mKeyAxis->range(), mValueAxis->range()};
            mHasRenderedRange = true;
            mLineCacheDirty = false;
            mCachedPlotSize = currentPlotSize;
            gpuOffset = {};
        }
    }

    for (int c = 0; c < mComponents.size(); ++c) {
        const auto& comp = mComponents[c];
        if (!comp.visible) continue;
        if (mLineStyle == lsNone && comp.scatterStyle.isNone()) continue;

        if (c >= linesTarget.size()) continue;
        QVector<QPointF> lines = linesTarget[c];
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
            const QPen& activePen = comp.selection.isEmpty() ? comp.pen : comp.selectedPen;
            if (mLineStyle == lsImpulse) {
                applyDefaultAntialiasingHint(painter);
                QPen impulsePen = activePen;
                impulsePen.setCapStyle(Qt::FlatCap);
                painter->setPen(impulsePen);
                painter->drawLines(lines);
            } else {
                // Try GPU path, fall back to QPainter.
                // Disabled for export: pmVectorized (SVG/PDF) and pmNoCaching (raster
                // export via toPixmap/saveRastered) don't composite plottable RHI layers.
                auto drawPoly = [&](const QVector<QPointF>& pts, const QPen& pen) {
                    if (auto* rhi = mParentPlot ? mParentPlot->rhi() : nullptr;
                        rhi && !painter->modes().testFlag(QCPPainter::pmVectorized)
                            && !painter->modes().testFlag(QCPPainter::pmNoCaching)
                            && pen.style() == Qt::SolidLine)
                    {
                        if (auto* prl = mParentPlot->plottableRhiLayer(mLayer))
                        {
                            prl->setPixelOffset(gpuOffset);
                            const double dpr = mParentPlot->bufferDevicePixelRatio();
                            // Cosmetic pens (widthF==0) = 1 device pixel, independent of DPR
                            const float penWidth = (pen.isCosmetic() || qFuzzyIsNull(pen.widthF()))
                                ? static_cast<float>(1.0 / dpr)
                                : qMax(1.0f, static_cast<float>(pen.widthF()));
                            auto strokeVerts = QCPLineExtruder::extrudePolyline(pts, penWidth, pen.color());
                            if (!strokeVerts.isEmpty())
                            {
                                const QSize outputSize = mParentPlot->rhiOutputSize();
                                prl->addPlottable({}, strokeVerts, clipRect(), dpr,
                                                   outputSize.height(), rhi->isYUpInNDC());
                                return;
                            }
                        }
                    }
                    applyDefaultAntialiasingHint(painter);
                    painter->setPen(pen);
                    painter->setBrush(Qt::NoBrush);
                    painter->drawPolyline(pts.constData(), pts.size());
                };
                drawPoly(lines, activePen);
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

// --- Legend ---

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
    for (int i = 0; i < legend->itemCount(); ++i)
        if (auto* gi = qobject_cast<QCPGroupLegendItem*>(legend->item(i)))
            if (gi->multiGraph() == this)
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

// --- Line style transforms ---

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

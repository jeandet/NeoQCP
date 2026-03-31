#pragma once
#include "abstract-multi-datasource.h"
#include "graph-resampler.h"
#include "algorithms.h"
#include "../Profiling.hpp"
#include <algorithm>
#include <cmath>

class QCPResampledMultiDataSource final : public QCPAbstractMultiDataSource {
public:
    explicit QCPResampledMultiDataSource(qcp::algo::MultiColumnBinResult bins)
        : mBins(std::move(bins)) {}

    int columnCount() const override { return mBins.numColumns; }
    int size() const override { return static_cast<int>(mBins.keys.size()); }

    double keyAt(int i) const override { return mBins.keys[i]; }

    double valueAt(int column, int i) const override
    {
        return mBins.values[column * mBins.stride() + i];
    }

    QCPRange keyRange(bool& found, QCP::SignDomain sd = QCP::sdBoth) const override
    {
        return qcp::algo::keyRange(mBins.keys, found, sd);
    }

    QCPRange valueRange(int column, bool& found, QCP::SignDomain sd = QCP::sdBoth,
                        const QCPRange& inKeyRange = QCPRange()) const override
    {
        found = false;
        if (column < 0 || column >= mBins.numColumns) return {};
        int s = mBins.stride();
        const bool filterByKey = (inKeyRange.lower != 0 || inKeyRange.upper != 0);
        double lo = std::numeric_limits<double>::max();
        double hi = std::numeric_limits<double>::lowest();
        for (int i = 0; i < s; ++i)
        {
            if (filterByKey && !inKeyRange.contains(mBins.keys[i])) continue;
            double v = mBins.values[column * s + i];
            if (std::isnan(v)) continue;
            if (sd == QCP::sdPositive && v <= 0) continue;
            if (sd == QCP::sdNegative && v >= 0) continue;
            if (v < lo) lo = v;
            if (v > hi) hi = v;
            found = true;
        }
        return found ? QCPRange(lo, hi) : QCPRange();
    }

    int findBegin(double sortKey, bool expandedRange = true) const override
    {
        return qcp::algo::findBegin(mBins.keys, sortKey, expandedRange);
    }

    int findEnd(double sortKey, bool expandedRange = true) const override
    {
        return qcp::algo::findEnd(mBins.keys, sortKey, expandedRange);
    }

    QVector<QPointF> getOptimizedLineData(int column, int begin, int end, int pixelWidth,
                                           QCPAxis* keyAxis, QCPAxis* valueAxis) const override
    {
        return getLines(column, begin, end, keyAxis, valueAxis);
    }

    QVector<QPointF> getLines(int column, int begin, int end,
                               QCPAxis* keyAxis, QCPAxis* valueAxis) const override
    {
        if (column < 0 || column >= mBins.numColumns) return {};
        int s = mBins.stride();
        const bool keyIsVertical = keyAxis->orientation() == Qt::Vertical;
        const int count = end - begin;
        auto gaps = qcp::algo::detectKeyGaps(mBins.keys, begin, end);
        const auto nanPt = QPointF(qQNaN(), qQNaN());

        QVector<QPointF> lines;
        lines.reserve(count + count / 10);
        for (int i = begin; i < end; ++i)
        {
            if (gaps[i - begin])
                lines.append(nanPt);
            double v = mBins.values[column * s + i];
            if (std::isnan(v)) continue;
            double keyPx = keyAxis->coordToPixel(mBins.keys[i]);
            double valPx = valueAxis->coordToPixel(v);
            lines.append(keyIsVertical ? QPointF(valPx, keyPx) : QPointF(keyPx, valPx));
        }
        return lines;
    }

private:
    qcp::algo::MultiColumnBinResult mBins;
};

namespace qcp::algo {

inline std::shared_ptr<QCPResampledMultiDataSource> resampleL2Multi(
    const MultiGraphResamplerCache& l1Cache,
    const ViewportParams& vp)
{
    PROFILE_HERE_N("resampleL2Multi");
    if (vp.keyLogScale)
        return nullptr;

    int l2Bins = vp.plotWidthPx * kLevel2PixelMultiplier;
    if (l2Bins <= 0) l2Bins = 3200;

    const auto& l1 = l1Cache.level1;
    int l1Size = static_cast<int>(l1.keys.size());
    if (l1Size == 0 || l1.numColumns == 0) return nullptr;

    auto [l1Begin, l1End] = l1ViewportBounds(l1.keys, l1Size, vp.keyRange);
    if (l1End <= l1Begin)
        return nullptr;

    // Skip L2 binning when visible points are sparse enough to draw directly
    if (l1End - l1Begin <= l2Bins)
        return nullptr;

    int N = l1.numColumns;
    int l1Stride = l1.stride();

    MultiColumnBinResult l2;
    l2.numColumns = N;

    const double binWidth = vp.keyRange.size() / l2Bins;
    const double halfWidth = binWidth * 0.5;
    const double keyLo = vp.keyRange.lower;
    int l2Stride = l2Bins * 2;

    l2.keys.resize(l2Stride);
    l2.values.resize(N * l2Stride);

    // Use +inf/-inf sentinels for min/max instead of NaN — eliminates 2 isnan
    // calls per iteration in the hot loop below.
    constexpr double posInf = std::numeric_limits<double>::infinity();
    constexpr double negInf = -std::numeric_limits<double>::infinity();
    for (int c = 0; c < N; ++c)
    {
        double* col = l2.values.data() + c * l2Stride;
        for (int b = 0; b < l2Bins; ++b)
        {
            col[b * 2 + 0] = posInf;   // min slot
            col[b * 2 + 1] = negInf;   // max slot
        }
    }

    for (int b = 0; b < l2Bins; ++b)
    {
        double binCenter = keyLo + (b + 0.5) * binWidth;
        l2.keys[b * 2 + 0] = binCenter;
        l2.keys[b * 2 + 1] = binCenter + halfWidth;
    }

    // Pre-compute bin indices (keys shared across columns)
    int l1Count = l1End - l1Begin;
    std::vector<int> binIdx(l1Count);
    const int* binIdxPtr = binIdx.data();
    for (int i = 0; i < l1Count; ++i)
    {
        double k = l1.keys[l1Begin + i];
        binIdx[i] = std::clamp(static_cast<int>((k - keyLo) / binWidth), 0, l2Bins - 1);
    }

    // Outer loop over columns for cache-friendly output access
    for (int c = 0; c < N; ++c)
    {
        const double* colIn = l1.values.data() + c * l1Stride;
        double* colOut = l2.values.data() + c * l2Stride;
        for (int i = 0; i < l1Count; ++i)
        {
            double v = colIn[l1Begin + i];
            if (v != v) continue;  // NaN check (v != v iff NaN)

            int bin = binIdxPtr[i];
            double& mn = colOut[bin * 2 + 0];
            double& mx = colOut[bin * 2 + 1];
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
    }

    // Compact in-place: move non-empty entries to the front of l2
    int outSize = 0;
    for (int i = 0; i < l2Stride; ++i)
    {
        bool hasData = false;
        for (int c = 0; c < N; ++c)
        {
            double mn = l2.values[c * l2Stride + i];
            if (mn != posInf)
            {
                hasData = true;
                break;
            }
        }
        if (!hasData) continue;

        l2.keys[outSize] = l2.keys[i];
        for (int c = 0; c < N; ++c)
        {
            double v = l2.values[c * l2Stride + i];
            // Convert remaining sentinels back to NaN for columns with no data in this bin
            if (v == posInf || v == negInf)
                v = std::numeric_limits<double>::quiet_NaN();
            l2.values[c * l2Stride + outSize] = v;
        }
        ++outSize;
    }
    if (outSize == 0) return nullptr;

    l2.keys.resize(outSize);
    // Compact column data: shift each column's data to final stride.
    // dest (c*outSize) <= source (c*l2Stride) for all c, so forward copy is safe.
    for (int c = 1; c < N; ++c)
        for (int i = 0; i < outSize; ++i)
            l2.values[c * outSize + i] = l2.values[c * l2Stride + i];
    l2.values.resize(N * outSize);

    return std::make_shared<QCPResampledMultiDataSource>(std::move(l2));
}

} // namespace qcp::algo

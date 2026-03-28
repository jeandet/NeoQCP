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
        QVector<QPointF> lines;
        lines.reserve(end - begin);
        for (int i = begin; i < end; ++i)
        {
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

    int N = l1.numColumns;
    int l1Stride = l1.stride();

    MultiColumnBinResult l2;
    l2.numColumns = N;

    const double binWidth = vp.keyRange.size() / l2Bins;
    const double halfWidth = binWidth * 0.5;
    const double keyLo = vp.keyRange.lower;
    int l2Stride = l2Bins * 2;

    l2.keys.resize(l2Stride);
    l2.values.resize(N * l2Stride, std::numeric_limits<double>::quiet_NaN());

    for (int b = 0; b < l2Bins; ++b)
    {
        double binCenter = keyLo + (b + 0.5) * binWidth;
        l2.keys[b * 2 + 0] = binCenter;
        l2.keys[b * 2 + 1] = binCenter + halfWidth;
    }

    // Pre-compute bin indices (keys shared across columns)
    int l1Count = l1End - l1Begin;
    std::vector<int> binIdx(l1Count);
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
            if (std::isnan(v)) continue;

            int bin = binIdx[i];
            double& mn = colOut[bin * 2 + 0];
            double& mx = colOut[bin * 2 + 1];
            if (std::isnan(mn) || v < mn) mn = v;
            if (std::isnan(mx) || v > mx) mx = v;
        }
    }

    // Compact in-place: move non-empty entries to the front of l2
    int outSize = 0;
    for (int i = 0; i < l2Stride; ++i)
    {
        bool hasData = false;
        for (int c = 0; c < N; ++c)
        {
            if (!std::isnan(l2.values[c * l2Stride + i]))
            {
                hasData = true;
                break;
            }
        }
        if (!hasData) continue;

        l2.keys[outSize] = l2.keys[i];
        for (int c = 0; c < N; ++c)
            l2.values[c * l2Stride + outSize] = l2.values[c * l2Stride + i];
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

inline std::shared_ptr<QCPResampledMultiDataSource> buildPreviewMulti(
    const QCPAbstractMultiDataSource& src)
{
    PROFILE_HERE_N("buildPreviewMulti");
    const int srcSize = src.size();
    const int N = src.columnCount();
    if (srcSize < kPreviewThreshold || N <= 0)
        return nullptr;

    bool foundRange = false;
    QCPRange fullKeyRange = src.keyRange(foundRange);
    if (!foundRange || fullKeyRange.size() <= 0)
        return nullptr;

    int numBins = std::min(kPreviewBins, srcSize / 2);
    auto bins = binMinMaxMultiParallel(src, 0, srcSize, fullKeyRange, numBins);

    int stride = static_cast<int>(bins.keys.size());
    std::vector<bool> keep(stride, false);
    for (int i = 0; i < stride; ++i)
    {
        for (int c = 0; c < N; ++c)
        {
            if (!std::isnan(bins.values[c * stride + i]))
            {
                keep[i] = true;
                break;
            }
        }
    }

    int outSize = 0;
    for (bool k : keep) outSize += k;
    if (outSize == 0) return nullptr;

    MultiColumnBinResult out;
    out.numColumns = N;
    out.keys.reserve(outSize);
    out.values.resize(N * outSize);
    int idx = 0;
    for (int i = 0; i < stride; ++i)
    {
        if (!keep[i]) continue;
        out.keys.push_back(bins.keys[i]);
        for (int c = 0; c < N; ++c)
            out.values[c * outSize + idx] = bins.values[c * stride + i];
        ++idx;
    }

    return std::make_shared<QCPResampledMultiDataSource>(std::move(out));
}

} // namespace qcp::algo

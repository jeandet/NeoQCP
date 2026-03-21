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

    auto beginIt = std::lower_bound(l1.keys.begin(), l1.keys.end(), vp.keyRange.lower);
    auto endIt = std::upper_bound(l1.keys.begin(), l1.keys.end(), vp.keyRange.upper);
    int l1Begin = std::max(0, static_cast<int>(beginIt - l1.keys.begin()) - 1);
    int l1End = std::min(l1Size, static_cast<int>(endIt - l1.keys.begin()) + 1);

    l1Begin = l1Begin & ~1;
    l1End = (l1End + 1) & ~1;
    l1End = std::min(l1End, l1Size);

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

    for (int i = l1Begin; i < l1End; ++i)
    {
        double k = l1.keys[i];
        int bin = static_cast<int>((k - keyLo) / binWidth);
        bin = std::clamp(bin, 0, l2Bins - 1);

        for (int c = 0; c < N; ++c)
        {
            double v = l1.values[c * l1Stride + i];
            if (std::isnan(v)) continue;

            double& mn = l2.values[c * l2Stride + bin * 2 + 0];
            double& mx = l2.values[c * l2Stride + bin * 2 + 1];
            if (std::isnan(mn) || v < mn) mn = v;
            if (std::isnan(mx) || v > mx) mx = v;
        }
    }

    MultiColumnBinResult out;
    out.numColumns = N;
    std::vector<bool> keep(l2Stride, false);
    for (int i = 0; i < l2Stride; ++i)
    {
        for (int c = 0; c < N; ++c)
        {
            if (!std::isnan(l2.values[c * l2Stride + i]))
            {
                keep[i] = true;
                break;
            }
        }
    }

    int outSize = 0;
    for (bool k : keep) outSize += k;
    if (outSize == 0) return nullptr;

    out.keys.reserve(outSize);
    out.values.resize(N * outSize);
    int idx = 0;
    for (int i = 0; i < l2Stride; ++i)
    {
        if (!keep[i]) continue;
        out.keys.push_back(l2.keys[i]);
        for (int c = 0; c < N; ++c)
            out.values[c * outSize + idx] = l2.values[c * l2Stride + i];
        ++idx;
    }

    return std::make_shared<QCPResampledMultiDataSource>(std::move(out));
}

} // namespace qcp::algo

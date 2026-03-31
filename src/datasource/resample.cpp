#include "resample.h"
#include "abstract-datasource-2d.h"
#include "Profiling.hpp"
#include <axis/range.h>
#include <plottables/plottable-colormap.h> // for QCPColorMapData
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace qcp::algo2d {

namespace {

std::vector<double> generateRange(double start, double end, int n, bool log)
{
    std::vector<double> range(n);
    if (n <= 1)
    {
        if (n == 1)
            range[0] = start;
        return range;
    }
    if (log && start > 0 && end > 0)
    {
        double logStart = std::log10(start);
        double step = (std::log10(end) - logStart) / (n - 1);
        for (int i = 0; i < n; ++i)
            range[i] = std::pow(10.0, logStart + i * step);
    }
    else
    {
        double step = (end - start) / (n - 1);
        for (int i = 0; i < n; ++i)
            range[i] = start + i * step;
    }
    return range;
}

// Generate bin edges (n+1 values) for n bins. Each bin center is xAxis[i],
// edges are midpoints between consecutive centers.
std::vector<double> generateBinEdges(const std::vector<double>& centers)
{
    int n = static_cast<int>(centers.size());
    std::vector<double> edges(n + 1);
    if (n == 0) return edges;
    if (n == 1)
    {
        edges[0] = centers[0] - 0.5;
        edges[1] = centers[0] + 0.5;
        return edges;
    }
    edges[0] = centers[0] - (centers[1] - centers[0]) * 0.5;
    for (int i = 1; i < n; ++i)
        edges[i] = (centers[i - 1] + centers[i]) * 0.5;
    edges[n] = centers[n - 1] + (centers[n - 1] - centers[n - 2]) * 0.5;
    return edges;
}

// Find the first source index >= value using binary search on sorted source X.
int lowerBoundSrc(const QCPAbstractDataSource2D& src, int begin, int end, double value)
{
    int lo = begin, hi = end;
    while (lo < hi)
    {
        int mid = lo + (hi - lo) / 2;
        if (src.xAt(mid) < value)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

int findBin(double value, const std::vector<double>& axis)
{
    auto it = std::lower_bound(axis.begin(), axis.end(), value);
    if (it == axis.end())
        return static_cast<int>(axis.size()) - 1;
    int idx = static_cast<int>(std::distance(axis.begin(), it));
    if (idx > 0 && (value - axis[idx - 1]) < (axis[idx] - value))
        --idx;
    return std::clamp(idx, 0, static_cast<int>(axis.size()) - 1);
}

int findBinFloor(double value, const std::vector<double>& axis)
{
    auto it = std::lower_bound(axis.begin(), axis.end(), value);
    if (it == axis.end())
        return static_cast<int>(axis.size()) - 1;
    int idx = static_cast<int>(std::distance(axis.begin(), it));
    if (idx > 0 && (value - axis[idx - 1]) <= (axis[idx] - value))
        --idx;
    return std::clamp(idx, 0, static_cast<int>(axis.size()) - 1);
}

} // anonymous namespace

QCPColorMapData* resample(
    const QCPAbstractDataSource2D& src,
    int xBegin, int xEnd,
    const QCPRange& xRange, const QCPRange& yRange,
    int targetWidth, int targetHeight,
    bool yLogScale,
    double gapThreshold)
{
    PROFILE_HERE_N("resample");
    int srcCount = xEnd - xBegin;
    if (srcCount < 2 || targetWidth < 1 || targetHeight < 1)
        return nullptr;
    if (xRange.lower >= xRange.upper || yRange.lower >= yRange.upper)
        return nullptr;

    int nx = targetWidth;
    int ny = targetHeight;

    auto xAxis = generateRange(xRange.lower, xRange.upper, nx, false);
    auto yAxis = generateRange(yRange.lower, yRange.upper, ny, yLogScale);
    int ys = src.ySize();

    auto* data = new QCPColorMapData(nx, ny, {xAxis.front(), xAxis.back()},
                                              {yAxis.front(), yAxis.back()});

    std::vector<double> accum(nx * ny, 0.0);
    std::vector<uint32_t> counts(nx * ny, 0);

    // Extend the source range by 1 on each side as context for gap detection.
    int ctxBegin = std::max(0, xBegin - 1);
    int ctxEnd = std::min(src.xSize(), xEnd + 1);
    int ctxCount = ctxEnd - ctxBegin;
    // Mark gaps between consecutive context columns using local reference dx.
    std::vector<bool> gapBetween(ctxCount, false);
    if (gapThreshold > 0 && ctxCount > 2)
    {
        for (int i = 0; i < ctxCount - 1; ++i)
        {
            double dx = src.xAt(ctxBegin + i + 1) - src.xAt(ctxBegin + i);
            double refDx = std::numeric_limits<double>::max();
            if (i > 0)
                refDx = std::min(refDx, src.xAt(ctxBegin + i) - src.xAt(ctxBegin + i - 1));
            if (i + 2 < ctxCount)
                refDx = std::min(refDx, src.xAt(ctxBegin + i + 2) - src.xAt(ctxBegin + i + 1));
            if (refDx < std::numeric_limits<double>::max() && dx > gapThreshold * refDx)
                gapBetween[i] = true;
        }
    }

    // Y bin ranges (precomputed once for non-variable Y)
    struct BinRange { int lo, hi; };
    bool variableY = src.yIs2D();

    auto computeYBinRanges = [&](int col, std::vector<BinRange>& ranges) {
        for (int yj = 0; yj < ys; ++yj)
        {
            double yVal = src.yAt(col, yj);
            double yLo, yHi;
            if (ys == 1)
            {
                yLo = yHi = yVal;
            }
            else if (yLogScale && yVal > 0)
            {
                double prev = (yj > 0) ? src.yAt(col, yj - 1) : 0;
                double next = (yj < ys - 1) ? src.yAt(col, yj + 1) : 0;
                yLo = (yj > 0 && prev > 0) ? std::sqrt(prev * yVal) : yVal;
                yHi = (yj < ys - 1 && next > 0) ? std::sqrt(yVal * next) : yVal;
            }
            else
            {
                double halfBelow = (yj > 0) ? (yVal - src.yAt(col, yj - 1)) * 0.5 : 0;
                double halfAbove = (yj < ys - 1) ? (src.yAt(col, yj + 1) - yVal) * 0.5 : 0;
                if (yj == 0) halfBelow = halfAbove;
                if (yj == ys - 1) halfAbove = halfBelow;
                double halfSpacing = std::min(halfBelow, halfAbove);
                yLo = yVal - halfSpacing;
                yHi = yVal + halfSpacing;
            }
            ranges[yj].lo = findBin(yLo, yAxis);
            ranges[yj].hi = findBinFloor(yHi, yAxis);
        }
    };

    std::vector<BinRange> yBinRanges(ys);
    if (!variableY)
        computeYBinRanges(xBegin, yBinRanges);

    // --- Bin-driven X iteration ---
    // Instead of iterating all srcCount source columns (O(srcCount × ys)),
    // iterate output X bins and find which source columns fall in each bin.
    // Since source X is sorted, we sweep once through the source range.
    auto xEdges = generateBinEdges(xAxis);

    // For each output X bin, find the source columns that contribute to it.
    // A source column contributes to bin b if its fill range overlaps [xEdges[b], xEdges[b+1]).
    // We use the sweep approach: advance a cursor through sorted source columns.
    int srcCursor = xBegin;

    for (int xb = 0; xb < nx; ++xb)
    {
        double binLo = xEdges[xb];
        double binHi = xEdges[xb + 1];

        // Find source columns whose X value falls near this bin.
        // We need columns whose fill range overlaps [binLo, binHi).
        // Conservative approach: find columns with xVal in [binLo - maxSpacing, binHi + maxSpacing].
        // Simpler: find columns with xVal in the bin range, expanded by one source spacing on each side.

        // Advance cursor to first source column >= binLo (minus one spacing for fill overlap)
        // Since source is sorted, we can binary search from current cursor position.
        int colBegin = lowerBoundSrc(src, srcCursor, xEnd, binLo);
        // Back up one to catch columns whose fill extends into this bin
        if (colBegin > xBegin) --colBegin;

        // Find end: first source column > binHi
        int colEnd = lowerBoundSrc(src, colBegin, xEnd, binHi);
        // Include one extra to catch columns whose fill extends back into this bin
        if (colEnd < xEnd) ++colEnd;

        // Process each source column in this range
        for (int xi = colBegin; xi < colEnd; ++xi)
        {
            int ci = xi - ctxBegin; // context index
            double xVal = src.xAt(xi);

            // Compute X fill range (same logic as before)
            bool gapLeft = (ci > 0) && gapBetween[ci - 1];
            bool gapRight = (ci < ctxCount - 1) && gapBetween[ci];

            double leftDist = (!gapLeft && ci > 0)
                ? xVal - src.xAt(ctxBegin + ci - 1) : std::numeric_limits<double>::max();
            double rightDist = (!gapRight && ci < ctxCount - 1)
                ? src.xAt(ctxBegin + ci + 1) - xVal : std::numeric_limits<double>::max();

            if (leftDist == std::numeric_limits<double>::max() && rightDist < std::numeric_limits<double>::max())
                leftDist = rightDist;
            if (rightDist == std::numeric_limits<double>::max() && leftDist < std::numeric_limits<double>::max())
                rightDist = leftDist;

            double xHalfSpacing = (leftDist < std::numeric_limits<double>::max())
                ? std::min(leftDist, rightDist) * 0.5 : 0;

            double fillLo = xVal - xHalfSpacing;
            double fillHi = xVal + xHalfSpacing;

            // Check this source column's fill actually overlaps this output bin
            if (fillHi < binLo || fillLo > binHi)
                continue;

            // Find the range of output X bins this source column fills
            int xBinLo = findBin(fillLo, xAxis);
            int xBinHi = findBinFloor(fillHi, xAxis);

            if (variableY)
                computeYBinRanges(xi, yBinRanges);

            for (int yj = 0; yj < ys; ++yj)
            {
                double yVal = src.yAt(xi, yj);
                if (std::isnan(yVal))
                    continue;
                double zVal = src.zAt(xi, yj);
                if (std::isnan(zVal))
                    continue;

                int yLo = yBinRanges[yj].lo;
                int yHi = yBinRanges[yj].hi;

                for (int ob = std::max(xBinLo, xb); ob <= std::min(xBinHi, xb); ++ob)
                    for (int yb = yLo; yb <= yHi; ++yb)
                    {
                        int idx = ob * ny + yb;
                        accum[idx] += zVal;
                        counts[idx] += 1;
                    }
            }
        }

        // Advance cursor for next bin (bins are monotonic)
        srcCursor = colBegin;
    }

    for (int i = 0; i < nx; ++i)
    {
        for (int j = 0; j < ny; ++j)
        {
            int idx = i * ny + j;
            data->setCell(i, j, counts[idx] > 0 ? accum[idx] / counts[idx]
                                                 : std::nan(""));
        }
    }

    data->recalculateDataBounds();
    return data;
}

} // namespace qcp::algo2d

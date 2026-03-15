#pragma once

class QCPAbstractDataSource2D;
class QCPColorMapData;
class QCPRange;

namespace qcp::algo2d {

// Core resampling algorithm (ported from SciQLopPlots).
// Output grid is locked to the viewport (xRange/yRange) at pixel resolution
// (targetWidth x targetHeight). Returns a new QCPColorMapData*. Caller owns it.
// Returns nullptr if input is insufficient (srcCount < 2, zero target size, etc.).
QCPColorMapData* resample(
    const QCPAbstractDataSource2D& src,
    int xBegin, int xEnd,
    const QCPRange& xRange, const QCPRange& yRange,
    int targetWidth, int targetHeight,
    bool yLogScale,
    double gapThreshold);

} // namespace qcp::algo2d

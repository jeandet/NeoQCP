#pragma once

#include <QtGlobal>

namespace qcp {

/// Small epsilon added to mTickCount when computing tick steps, to prevent
/// jitter that would arise from exact integer rounding (e.g. a range exactly
/// divisible by the tick count).
inline constexpr double kTickCountEpsilon = 1e-10;

/// Returns the hand-chosen sub-tick count for minute/hour tick intervals
/// (5 min through 24 h). Returns -1 if tickStep doesn't match any known interval.
inline int minuteHourSubTickCount(double tickStep)
{
    switch (qRound(tickStep))
    {
        case 5 * 60:    return 4;
        case 10 * 60:   return 1;
        case 15 * 60:   return 2;
        case 30 * 60:   return 1;
        case 60 * 60:   return 3;
        case 3600 * 2:  return 3;
        case 3600 * 3:  return 2;
        case 3600 * 6:  return 1;
        case 3600 * 12: return 3;
        case 3600 * 24: return 3;
        default:        return -1;
    }
}

} // namespace qcp

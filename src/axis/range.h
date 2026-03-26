/***************************************************************************
**                                                                        **
**  QCustomPlot, an easy to use, modern plotting widget for Qt            **
**  Copyright (C) 2011-2022 Emanuel Eichhammer                            **
**                                                                        **
**  This program is free software: you can redistribute it and/or modify  **
**  it under the terms of the GNU General Public License as published by  **
**  the Free Software Foundation, either version 3 of the License, or     **
**  (at your option) any later version.                                   **
**                                                                        **
**  This program is distributed in the hope that it will be useful,       **
**  but WITHOUT ANY WARRANTY; without even the implied warranty of        **
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         **
**  GNU General Public License for more details.                          **
**                                                                        **
**  You should have received a copy of the GNU General Public License     **
**  along with this program.  If not, see http://www.gnu.org/licenses/.   **
**                                                                        **
****************************************************************************
**           Author: Emanuel Eichhammer                                   **
**  Website/Contact: https://www.qcustomplot.com/                         **
**             Date: 06.11.22                                             **
**          Version: 2.1.1                                                **
****************************************************************************/

#ifndef QCP_RANGE_H
#define QCP_RANGE_H

#include "../global.h"

class QCP_LIB_DECL QCPRange
{
public:
    double lower, upper;

    constexpr QCPRange() : lower(0), upper(0) {}
    constexpr QCPRange(double lower, double upper) : lower(lower), upper(upper) { normalize(); }

    [[nodiscard]] constexpr bool operator==(const QCPRange& other) const
    {
        return lower == other.lower && upper == other.upper;
    }

    [[nodiscard]] constexpr bool operator!=(const QCPRange& other) const { return !(*this == other); }

    constexpr QCPRange& operator+=(const double& value)
    {
        lower += value;
        upper += value;
        return *this;
    }

    constexpr QCPRange& operator-=(const double& value)
    {
        lower -= value;
        upper -= value;
        return *this;
    }

    constexpr QCPRange& operator*=(const double& value)
    {
        lower *= value;
        upper *= value;
        return *this;
    }

    constexpr QCPRange& operator/=(const double& value)
    {
        lower /= value;
        upper /= value;
        return *this;
    }

    friend inline constexpr QCPRange operator+(const QCPRange&, double);
    friend inline constexpr QCPRange operator+(double, const QCPRange&);
    friend inline constexpr QCPRange operator-(const QCPRange& range, double value);
    friend inline constexpr QCPRange operator*(const QCPRange& range, double value);
    friend inline constexpr QCPRange operator*(double value, const QCPRange& range);
    friend inline constexpr QCPRange operator/(const QCPRange& range, double value);

    [[nodiscard]] constexpr double size() const { return upper - lower; }

    [[nodiscard]] constexpr double center() const { return (upper + lower) * 0.5; }

    constexpr void normalize()
    {
        if (lower > upper)
        {
            double tmp = lower;
            lower = upper;
            upper = tmp;
        }
    }

    void expand(const QCPRange& otherRange);
    void expand(double includeCoord);
    [[nodiscard]] QCPRange expanded(const QCPRange& otherRange) const;
    [[nodiscard]] QCPRange expanded(double includeCoord) const;
    [[nodiscard]] QCPRange bounded(double lowerBound, double upperBound) const;
    [[nodiscard]] QCPRange sanitizedForLogScale() const;
    [[nodiscard]] QCPRange sanitizedForLinScale() const;

    [[nodiscard]] constexpr bool contains(double value) const { return value >= lower && value <= upper; }

    [[nodiscard]] static bool validRange(double lower, double upper);
    [[nodiscard]] static bool validRange(const QCPRange& range);
    static const double minRange;
    static const double maxRange;
};

Q_DECLARE_TYPEINFO(QCPRange, Q_MOVABLE_TYPE);

/*! \relates QCPRange

  Prints \a range in a human readable format to the qDebug output.
*/
inline QDebug operator<<(QDebug d, const QCPRange& range)
{
    d.nospace() << "QCPRange(" << range.lower << ", " << range.upper << ")";
    return d.space();
}

/*!
  Adds \a value to both boundaries of the range.
*/
inline constexpr QCPRange operator+(const QCPRange& range, double value)
{
    return QCPRange(range.lower + value, range.upper + value);
}

/*!
  Adds \a value to both boundaries of the range.
*/
inline constexpr QCPRange operator+(double value, const QCPRange& range)
{
    return QCPRange(range.lower + value, range.upper + value);
}

/*!
  Subtracts \a value from both boundaries of the range.
*/
inline constexpr QCPRange operator-(const QCPRange& range, double value)
{
    return QCPRange(range.lower - value, range.upper - value);
}

/*!
  Multiplies both boundaries of the range by \a value.
*/
inline constexpr QCPRange operator*(const QCPRange& range, double value)
{
    return QCPRange(range.lower * value, range.upper * value);
}

/*!
  Multiplies both boundaries of the range by \a value.
*/
inline constexpr QCPRange operator*(double value, const QCPRange& range)
{
    return QCPRange(range.lower * value, range.upper * value);
}

/*!
  Divides both boundaries of the range by \a value.
*/
inline constexpr QCPRange operator/(const QCPRange& range, double value)
{
    return QCPRange(range.lower / value, range.upper / value);
}

#endif // QCP_RANGE_H

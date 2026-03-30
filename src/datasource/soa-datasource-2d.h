#pragma once
#include "abstract-datasource-2d.h"
#include "algorithms-2d.h"
#include <memory>
#include <ranges>
#include <span>

template <IndexableNumericRange XC, IndexableNumericRange YC, IndexableNumericRange ZC>
class QCPSoADataSource2D final : public QCPAbstractDataSource2D
{
public:
    using X = std::ranges::range_value_t<XC>;
    using Y = std::ranges::range_value_t<YC>;
    using Z = std::ranges::range_value_t<ZC>;

    QCPSoADataSource2D(XC x, YC y, ZC z, std::shared_ptr<const void> dataGuard = {})
        : mX(std::move(x)), mY(std::move(y)), mZ(std::move(z)),
          mDataGuard(std::move(dataGuard))
    {
        auto nx = std::ranges::size(mX);
        auto ny = std::ranges::size(mY);
        auto nz = std::ranges::size(mZ);
        Q_ASSERT(nx > 0 && nz > 0 && nz % nx == 0);
        mYSize = static_cast<int>(nz / nx);
        mYIs2D = (ny == nz);
        Q_ASSERT(ny == nz || ny == static_cast<decltype(ny)>(mYSize));
    }

    const XC& x() const { return mX; }
    const YC& y() const { return mY; }
    const ZC& z() const { return mZ; }

    int xSize() const override { return static_cast<int>(std::ranges::size(mX)); }
    int ySize() const override { return mYSize; }
    bool yIs2D() const override { return mYIs2D; }

    double xAt(int i) const override { return static_cast<double>(mX[i]); }

    double yAt(int i, int j) const override
    {
        return mYIs2D ? static_cast<double>(mY[i * mYSize + j])
                      : static_cast<double>(mY[j]);
    }

    double zAt(int i, int j) const override
    {
        return static_cast<double>(mZ[i * mYSize + j]);
    }

    QCPRange xRange(bool& found, QCP::SignDomain sd = QCP::sdBoth) const override
    {
        return qcp::algo2d::xRange(mX, found, sd);
    }

    QCPRange yRange(bool& found, QCP::SignDomain sd = QCP::sdBoth) const override
    {
        return qcp::algo2d::yRange(mY, found, sd);
    }

    QCPRange zRange(bool& found, int xBegin = 0, int xEnd = -1) const override
    {
        return qcp::algo2d::zRange(mZ, mYSize, found, xBegin, xEnd);
    }

    int findXBegin(double sortKey) const override
    {
        return qcp::algo2d::findXBegin(mX, sortKey);
    }

    int findXEnd(double sortKey) const override
    {
        return qcp::algo2d::findXEnd(mX, sortKey);
    }

private:
    XC mX;
    YC mY;
    ZC mZ;
    int mYSize;
    bool mYIs2D;
    std::shared_ptr<const void> mDataGuard;
};

#pragma once
#include "abstract-multi-datasource.h"
#include "algorithms.h"
#include <QtGlobal>
#include <memory>
#include <vector>

template <IndexableNumericRange KeyContainer, IndexableNumericRange ValueContainer>
class QCPSoAMultiDataSource final : public QCPAbstractMultiDataSource {
public:
    using K = std::ranges::range_value_t<KeyContainer>;
    using V = std::ranges::range_value_t<ValueContainer>;

    QCPSoAMultiDataSource(KeyContainer keys, std::vector<ValueContainer> valueColumns,
                           std::shared_ptr<const void> dataGuard = {})
        : mKeys(std::move(keys)), mValues(std::move(valueColumns)),
          mDataGuard(std::move(dataGuard))
    {
        for (const auto& col : mValues)
            Q_ASSERT(std::ranges::size(col) == std::ranges::size(mKeys));
    }

    int columnCount() const override { return static_cast<int>(mValues.size()); }
    int size() const override { return static_cast<int>(std::ranges::size(mKeys)); }

    double keyAt(int i) const override
    {
        Q_ASSERT(i >= 0 && i < size());
        return static_cast<double>(mKeys[i]);
    }

    double valueAt(int column, int i) const override
    {
        Q_ASSERT(column >= 0 && column < columnCount());
        Q_ASSERT(i >= 0 && i < size());
        return static_cast<double>(mValues[column][i]);
    }

    QCPRange keyRange(bool& found, QCP::SignDomain sd = QCP::sdBoth) const override
    {
        return qcp::algo::keyRange(mKeys, found, sd);
    }

    QCPRange valueRange(int column, bool& found, QCP::SignDomain sd = QCP::sdBoth,
                        const QCPRange& inKeyRange = QCPRange()) const override
    {
        Q_ASSERT(column >= 0 && column < columnCount());
        return qcp::algo::valueRange(mKeys, mValues[column], found, sd, inKeyRange);
    }

    int findBegin(double sortKey, bool expandedRange = true) const override
    {
        return qcp::algo::findBegin(mKeys, sortKey, expandedRange);
    }

    int findEnd(double sortKey, bool expandedRange = true) const override
    {
        return qcp::algo::findEnd(mKeys, sortKey, expandedRange);
    }

    QVector<QPointF> getOptimizedLineData(int column, int begin, int end, int pixelWidth,
                                           QCPAxis* keyAxis, QCPAxis* valueAxis) const override
    {
        Q_ASSERT(column >= 0 && column < columnCount());
        return qcp::algo::optimizedLineData(mKeys, mValues[column], begin, end, pixelWidth,
                                             keyAxis, valueAxis);
    }

    QVector<QPointF> getLines(int column, int begin, int end,
                               QCPAxis* keyAxis, QCPAxis* valueAxis) const override
    {
        Q_ASSERT(column >= 0 && column < columnCount());
        return qcp::algo::linesToPixels(mKeys, mValues[column], begin, end, keyAxis, valueAxis);
    }

private:
    KeyContainer mKeys;
    std::vector<ValueContainer> mValues;
    std::shared_ptr<const void> mDataGuard;
};

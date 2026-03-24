#pragma once
#include "abstract-multi-datasource.h"
#include "algorithms.h"
#include <QtGlobal>
#include <cstddef>
#include <span>

// Lightweight non-owning view of a single column in a row-major 2D array.
// Satisfies IndexableNumericRange (random_access_range + arithmetic value_type).
template <typename V>
class StridedColumnView {
public:
    StridedColumnView(const V* base, int count, int stride)
        : mBase(base), mCount(count), mStride(stride) {}

    struct Iterator {
        using iterator_category = std::random_access_iterator_tag;
        using value_type = V;
        using difference_type = std::ptrdiff_t;
        using pointer = const V*;
        using reference = const V&;

        const V* ptr = nullptr;
        int stride = 0;

        const V& operator*() const { return *ptr; }
        Iterator& operator++() { ptr += stride; return *this; }
        Iterator operator++(int) { auto t = *this; ++*this; return t; }
        Iterator& operator--() { ptr -= stride; return *this; }
        Iterator operator--(int) { auto t = *this; --*this; return t; }
        Iterator& operator+=(difference_type n) { ptr += n * stride; return *this; }
        Iterator& operator-=(difference_type n) { ptr -= n * stride; return *this; }
        Iterator operator+(difference_type n) const { return {ptr + n * stride, stride}; }
        Iterator operator-(difference_type n) const { return {ptr - n * stride, stride}; }
        difference_type operator-(const Iterator& o) const { return (ptr - o.ptr) / stride; }
        const V& operator[](difference_type n) const { return *(ptr + n * stride); }
        auto operator<=>(const Iterator& o) const = default;
        bool operator==(const Iterator& o) const = default;
    };

    friend Iterator operator+(typename Iterator::difference_type n, const Iterator& it)
    { return it + n; }

    Iterator begin() const { return {mBase, mStride}; }
    Iterator end() const { return {mBase + static_cast<std::ptrdiff_t>(mCount) * mStride, mStride}; }
    int size() const { return mCount; }
    const V& operator[](int i) const { return mBase[static_cast<std::ptrdiff_t>(i) * mStride]; }

private:
    const V* mBase;
    int mCount;
    int mStride;
};

// Non-owning multi-column data source for row-major (C-order) 2D arrays.
// Data layout: values[row * stride + col], where stride >= columnCount.
// Zero-copy: wraps the caller's buffer directly.
template <typename K, typename V>
class QCPRowMajorMultiDataSource final : public QCPAbstractMultiDataSource {
public:
    QCPRowMajorMultiDataSource(std::span<const K> keys,
                                const V* values,
                                int rows,
                                int columns,
                                int stride)
        : mKeys(keys), mValues(values), mRows(rows), mColumns(columns), mStride(stride)
    {
        Q_ASSERT(static_cast<int>(keys.size()) == rows);
        Q_ASSERT(stride >= columns);
    }

    int columnCount() const override { return mColumns; }
    int size() const override { return mRows; }

    double keyAt(int i) const override
    {
        Q_ASSERT(i >= 0 && i < mRows);
        return static_cast<double>(mKeys[i]);
    }

    double valueAt(int column, int i) const override
    {
        Q_ASSERT(column >= 0 && column < mColumns);
        Q_ASSERT(i >= 0 && i < mRows);
        return static_cast<double>(mValues[i * mStride + column]);
    }

    QCPRange keyRange(bool& found, QCP::SignDomain sd = QCP::sdBoth) const override
    {
        return qcp::algo::keyRange(mKeys, found, sd);
    }

    QCPRange valueRange(int column, bool& found, QCP::SignDomain sd = QCP::sdBoth,
                        const QCPRange& inKeyRange = QCPRange()) const override
    {
        Q_ASSERT(column >= 0 && column < mColumns);
        found = false;
        const bool filterByKey = (inKeyRange.lower != 0 || inKeyRange.upper != 0);
        double lo = std::numeric_limits<double>::max();
        double hi = std::numeric_limits<double>::lowest();
        for (int i = 0; i < mRows; ++i)
        {
            if (filterByKey && !inKeyRange.contains(static_cast<double>(mKeys[i])))
                continue;
            double v = static_cast<double>(mValues[i * mStride + column]);
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
        return qcp::algo::findBegin(mKeys, sortKey, expandedRange);
    }

    int findEnd(double sortKey, bool expandedRange = true) const override
    {
        return qcp::algo::findEnd(mKeys, sortKey, expandedRange);
    }

    QVector<QPointF> getOptimizedLineData(int column, int begin, int end, int pixelWidth,
                                           QCPAxis* keyAxis, QCPAxis* valueAxis) const override
    {
        Q_ASSERT(column >= 0 && column < mColumns);
        StridedColumnView<V> colView(mValues + column, mRows, mStride);
        return qcp::algo::optimizedLineData(mKeys, colView, begin, end, pixelWidth,
                                             keyAxis, valueAxis);
    }

    QVector<QPointF> getLines(int column, int begin, int end,
                               QCPAxis* keyAxis, QCPAxis* valueAxis) const override
    {
        Q_ASSERT(column >= 0 && column < mColumns);
        StridedColumnView<V> colView(mValues + column, mRows, mStride);
        return qcp::algo::linesToPixels(mKeys, colView, begin, end, keyAxis, valueAxis);
    }

private:
    std::span<const K> mKeys;
    const V* mValues;
    int mRows;
    int mColumns;
    int mStride;
};

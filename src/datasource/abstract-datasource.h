#pragma once
#include "global.h"
#include "axis/range.h"
#include <QPointF>
#include <QVector>
#include <ranges>
#include <type_traits>

class QCPAxis;

// Concepts for data source container requirements
template <typename C>
concept IndexableNumericRange = std::ranges::random_access_range<C>
    && std::is_arithmetic_v<std::ranges::range_value_t<C>>;

template <typename C>
concept ContiguousNumericRange = IndexableNumericRange<C>
    && std::ranges::contiguous_range<C>;

// Non-templated abstract base class for all data sources.
// QCPGraph2 holds a pointer to this; virtual dispatch happens once per render.
class QCPAbstractDataSource {
public:
    virtual ~QCPAbstractDataSource() = default;

    virtual int size() const = 0;
    virtual bool empty() const { return size() == 0; }

    // Range queries (for axis auto-scaling)
    virtual QCPRange keyRange(bool& foundRange,
                              QCP::SignDomain sd = QCP::sdBoth) const = 0;
    virtual QCPRange valueRange(bool& foundRange,
                                QCP::SignDomain sd = QCP::sdBoth,
                                const QCPRange& inKeyRange = QCPRange()) const = 0;

    // Binary search on sorted keys.
    // expandedRange=true includes one extra point beyond the boundary
    // (needed for correct line rendering at viewport edges).
    virtual int findBegin(double sortKey, bool expandedRange = true) const = 0;
    virtual int findEnd(double sortKey, bool expandedRange = true) const = 0;

    // Per-element access (slow path: selection, tooltips)
    virtual double keyAt(int i) const = 0;
    virtual double valueAt(int i) const = 0;

    // Processed outputs -- implementations run native-type algorithms internally,
    // cast to double/QPointF only at the pixel-coordinate output step.
    virtual QVector<QPointF> getOptimizedLineData(
        int begin, int end, int pixelWidth,
        QCPAxis* keyAxis, QCPAxis* valueAxis) const = 0;

    virtual QVector<QPointF> getLines(
        int begin, int end,
        QCPAxis* keyAxis, QCPAxis* valueAxis) const = 0;
};

#ifndef QCP_DATA_LOCATOR_H
#define QCP_DATA_LOCATOR_H

#include "global.h"
#include <limits>

class QCPAbstractPlottable;

class QCP_LIB_DECL QCPDataLocator
{
public:
    QCPDataLocator() = default;

    void setPlottable(QCPAbstractPlottable* plottable);
    bool locate(const QPointF& pixelPos);

    bool isValid() const { return mValid; }
    double key() const { return mKey; }
    double value() const { return mValue; }
    double data() const { return mData; }
    int dataIndex() const { return mDataIndex; }
    QCPAbstractPlottable* plottable() const { return mPlottable; }
    QCPAbstractPlottable* hitPlottable() const { return mHitPlottable; }

private:
    QCPAbstractPlottable* mPlottable = nullptr;
    QCPAbstractPlottable* mHitPlottable = nullptr;
    bool mValid = false;
    double mKey = 0;
    double mValue = 0;
    double mData = std::numeric_limits<double>::quiet_NaN();
    int mDataIndex = -1;

    bool locateGraph(const QPointF& pixelPos);
    bool locateGraph2(const QPointF& pixelPos);
    bool locateCurve(const QPointF& pixelPos);
    bool locateColorMap(const QPointF& pixelPos);
    bool locateColorMap2(const QPointF& pixelPos);
    bool locateHistogram2D(const QPointF& pixelPos);
    bool locateMultiGraph(const QPointF& pixelPos);
};

#endif // QCP_DATA_LOCATOR_H

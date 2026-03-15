#include "data-locator.h"
#include <algorithm>
#include <cmath>
#include "plottables/plottable-graph.h"
#include "plottables/plottable-graph2.h"
#include "plottables/plottable-curve.h"
#include "plottables/plottable-colormap.h"
#include "plottables/plottable-colormap2.h"
#include "plottables/plottable-multigraph.h"
#include "selection.h"

void QCPDataLocator::setPlottable(QCPAbstractPlottable* plottable)
{
    mPlottable = plottable;
    mHitPlottable = nullptr;
    mValid = false;
    mKey = 0;
    mValue = 0;
    mData = std::numeric_limits<double>::quiet_NaN();
    mDataIndex = -1;
}

bool QCPDataLocator::locate(const QPointF& pixelPos)
{
    mValid = false;
    if (!mPlottable)
        return false;

    // Try specific types in order (derived before base where needed)
    if (qobject_cast<QCPGraph2*>(mPlottable))
        return locateGraph2(pixelPos);
    if (qobject_cast<QCPGraph*>(mPlottable))
        return locateGraph(pixelPos);
    if (qobject_cast<QCPCurve*>(mPlottable))
        return locateCurve(pixelPos);
    if (qobject_cast<QCPColorMap2*>(mPlottable))
        return locateColorMap2(pixelPos);
    if (qobject_cast<QCPColorMap*>(mPlottable))
        return locateColorMap(pixelPos);
    if (qobject_cast<QCPMultiGraph*>(mPlottable))
        return locateMultiGraph(pixelPos);

    return false;
}

bool QCPDataLocator::locateGraph(const QPointF& pixelPos)
{
    auto* graph = qobject_cast<QCPGraph*>(mPlottable);
    QVariant details;
    double dist = graph->selectTest(pixelPos, false, &details);
    if (dist < 0)
        return false;

    auto sel = details.value<QCPDataSelection>();
    if (sel.dataRangeCount() == 0)
        return false;

    int index = sel.dataRange(0).begin();
    auto it = graph->data()->at(index);
    mKey = it->key;
    mValue = it->value;
    mDataIndex = index;
    mHitPlottable = mPlottable;
    mValid = true;
    return true;
}

bool QCPDataLocator::locateGraph2(const QPointF& pixelPos)
{
    auto* g2 = qobject_cast<QCPGraph2*>(mPlottable);
    QVariant details;
    double dist = g2->selectTest(pixelPos, false, &details);
    if (dist < 0)
        return false;

    auto sel = details.value<QCPDataSelection>();
    if (sel.dataRangeCount() == 0)
        return false;

    int index = sel.dataRange(0).begin();
    auto* src = g2->dataSource();
    if (!src || index < 0 || index >= src->size())
        return false;

    mKey = src->keyAt(index);
    mValue = src->valueAt(index);
    mDataIndex = index;
    mHitPlottable = mPlottable;
    mValid = true;
    return true;
}

bool QCPDataLocator::locateCurve(const QPointF& pixelPos)
{
    auto* curve = qobject_cast<QCPCurve*>(mPlottable);
    QVariant details;
    double dist = curve->selectTest(pixelPos, false, &details);
    if (dist < 0)
        return false;

    auto sel = details.value<QCPDataSelection>();
    if (sel.dataRangeCount() == 0)
        return false;

    int index = sel.dataRange(0).begin();
    auto it = curve->data()->at(index);
    mKey = it->key;
    mValue = it->value;
    mDataIndex = index;
    mHitPlottable = mPlottable;
    mValid = true;
    return true;
}

bool QCPDataLocator::locateColorMap(const QPointF& pixelPos)
{
    auto* cm = qobject_cast<QCPColorMap*>(mPlottable);
    double key, value;
    cm->pixelsToCoords(pixelPos, key, value);

    auto* mapData = cm->data();
    if (!mapData || mapData->isEmpty())
        return false;

    int keyIndex, valueIndex;
    mapData->coordToCell(key, value, &keyIndex, &valueIndex);

    if (keyIndex < 0 || keyIndex >= mapData->keySize()
        || valueIndex < 0 || valueIndex >= mapData->valueSize())
        return false;

    mKey = key;
    mValue = value;
    mData = mapData->cell(keyIndex, valueIndex);
    mDataIndex = keyIndex * mapData->valueSize() + valueIndex;
    mHitPlottable = mPlottable;
    mValid = true;
    return true;
}

bool QCPDataLocator::locateColorMap2(const QPointF& pixelPos)
{
    auto* cm2 = qobject_cast<QCPColorMap2*>(mPlottable);
    double key, value;
    cm2->pixelsToCoords(pixelPos, key, value);

    auto* src = cm2->dataSource();
    if (!src || src->xSize() == 0 || src->ySize() == 0)
        return false;

    // findXBegin uses expandedRange (for viewport rendering), so it may be one
    // index before the actual nearest. Compare with neighbor to find true nearest.
    int xi = std::clamp(src->findXBegin(key), 0, src->xSize() - 1);
    if (xi + 1 < src->xSize()
        && std::abs(src->xAt(xi + 1) - key) < std::abs(src->xAt(xi) - key))
        ++xi;
    int yj = 0;
    double bestDy = std::numeric_limits<double>::max();
    for (int j = 0; j < src->ySize(); ++j)
    {
        double dy = std::abs(src->yAt(xi, j) - value);
        if (dy < bestDy) { bestDy = dy; yj = j; }
    }

    mKey = key;
    mValue = value;
    mData = src->zAt(xi, yj);
    mDataIndex = xi * src->ySize() + yj;
    mHitPlottable = mPlottable;
    mValid = true;
    return true;
}

bool QCPDataLocator::locateMultiGraph(const QPointF& pixelPos)
{
    auto* mg = qobject_cast<QCPMultiGraph*>(mPlottable);
    QVariant details;
    double dist = mg->selectTest(pixelPos, false, &details);
    if (dist < 0)
        return false;

    auto map = details.toMap();
    if (!map.contains("dataIndex"))
        return false;

    int index = map["dataIndex"].toInt();
    int component = map.value("componentIndex", 0).toInt();
    auto* src = mg->dataSource();
    if (!src || index < 0 || index >= mg->dataCount())
        return false;

    mKey = mg->dataMainKey(index);
    mValue = src->valueAt(component, index);
    mDataIndex = index;
    mHitPlottable = mPlottable;
    mValid = true;
    return true;
}

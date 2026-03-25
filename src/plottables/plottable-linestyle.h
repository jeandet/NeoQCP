#pragma once

#include <QPointF>
#include <QVector>

namespace qcp {

inline QVector<QPointF> toStepLeftLines(const QVector<QPointF>& lines, bool keyIsVertical)
{
    if (lines.size() < 2)
        return lines;

    QVector<QPointF> result;
    result.resize(lines.size() * 2);

    if (keyIsVertical)
    {
        double lastValue = lines.first().x();
        for (int i = 0; i < lines.size(); ++i)
        {
            const double key = lines[i].y();
            result[i * 2 + 0] = QPointF(lastValue, key);
            lastValue = lines[i].x();
            result[i * 2 + 1] = QPointF(lastValue, key);
        }
    }
    else
    {
        double lastValue = lines.first().y();
        for (int i = 0; i < lines.size(); ++i)
        {
            const double key = lines[i].x();
            result[i * 2 + 0] = QPointF(key, lastValue);
            lastValue = lines[i].y();
            result[i * 2 + 1] = QPointF(key, lastValue);
        }
    }
    return result;
}

inline QVector<QPointF> toStepRightLines(const QVector<QPointF>& lines, bool keyIsVertical)
{
    if (lines.size() < 2)
        return lines;

    QVector<QPointF> result;
    result.resize(lines.size() * 2);

    if (keyIsVertical)
    {
        double lastKey = lines.first().y();
        for (int i = 0; i < lines.size(); ++i)
        {
            const double value = lines[i].x();
            result[i * 2 + 0] = QPointF(value, lastKey);
            lastKey = lines[i].y();
            result[i * 2 + 1] = QPointF(value, lastKey);
        }
    }
    else
    {
        double lastKey = lines.first().x();
        for (int i = 0; i < lines.size(); ++i)
        {
            const double value = lines[i].y();
            result[i * 2 + 0] = QPointF(lastKey, value);
            lastKey = lines[i].x();
            result[i * 2 + 1] = QPointF(lastKey, value);
        }
    }
    return result;
}

inline QVector<QPointF> toStepCenterLines(const QVector<QPointF>& lines, bool keyIsVertical)
{
    if (lines.size() < 2)
        return lines;

    QVector<QPointF> result;
    result.resize(lines.size() * 2);

    if (keyIsVertical)
    {
        double lastKey = lines.first().y();
        double lastValue = lines.first().x();
        result[0] = QPointF(lastValue, lastKey);
        for (int i = 1; i < lines.size(); ++i)
        {
            const double midKey = (lines[i].y() + lastKey) * 0.5;
            result[i * 2 - 1] = QPointF(lastValue, midKey);
            lastValue = lines[i].x();
            lastKey = lines[i].y();
            result[i * 2 + 0] = QPointF(lastValue, midKey);
        }
        result[lines.size() * 2 - 1] = QPointF(lastValue, lastKey);
    }
    else
    {
        double lastKey = lines.first().x();
        double lastValue = lines.first().y();
        result[0] = QPointF(lastKey, lastValue);
        for (int i = 1; i < lines.size(); ++i)
        {
            const double midKey = (lines[i].x() + lastKey) * 0.5;
            result[i * 2 - 1] = QPointF(midKey, lastValue);
            lastValue = lines[i].y();
            lastKey = lines[i].x();
            result[i * 2 + 0] = QPointF(midKey, lastValue);
        }
        result[lines.size() * 2 - 1] = QPointF(lastKey, lastValue);
    }
    return result;
}

inline QVector<QPointF> toImpulseLines(const QVector<QPointF>& lines, bool keyIsVertical, double zeroPixel)
{
    QVector<QPointF> result;
    result.resize(lines.size() * 2);

    if (keyIsVertical)
    {
        for (int i = 0; i < lines.size(); ++i)
        {
            const double key = lines[i].y();
            result[i * 2 + 0] = QPointF(zeroPixel, key);
            result[i * 2 + 1] = QPointF(lines[i].x(), key);
        }
    }
    else
    {
        for (int i = 0; i < lines.size(); ++i)
        {
            const double key = lines[i].x();
            result[i * 2 + 0] = QPointF(key, zeroPixel);
            result[i * 2 + 1] = QPointF(key, lines[i].y());
        }
    }
    return result;
}

} // namespace qcp

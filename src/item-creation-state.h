#pragma once
#include "global.h"
#include <QObject>
#include <functional>

class QCustomPlot;
class QCPAbstractItem;
class QCPAxis;
class QCPAxisRect;

using ItemCreator = std::function<QCPAbstractItem*(QCustomPlot* plot, QCPAxis* keyAxis, QCPAxis* valueAxis)>;

class QCPItemCreationState : public QObject {
    Q_OBJECT
public:
    enum State { Idle, Drawing };

    explicit QCPItemCreationState(QCustomPlot* plot);

    State state() const { return mState; }

    bool handleMousePress(QMouseEvent* event);
    bool handleMouseMove(QMouseEvent* event);
    bool handleKeyPress(QKeyEvent* event);

    void cancel();

signals:
    void itemCreated(QCPAbstractItem* item);
    void itemCanceled();

private:
    QCustomPlot* mPlot;
    State mState = Idle;
    QCPAbstractItem* mCurrentItem = nullptr;
    QCPAxis* mKeyAxis = nullptr;
    QCPAxis* mValueAxis = nullptr;

    void initItemPosition(double key, double value);
    void commitItem();
    void cancelItem();
    void updateItemPosition(const QPointF& pixelPos);
    QCPAxisRect* axisRectAt(const QPointF& pos) const;
};

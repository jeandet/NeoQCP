#pragma once
#include "layer.h"

class QCPAbstractItem;
class QCPAxis;
class QCPAxisRect;
class QCPPainter;

class QCPItemCreationState : public QCPLayerable {
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

protected:
    void applyDefaultAntialiasingHint(QCPPainter* painter) const override;
    void draw(QCPPainter* painter) override;

private:
    QCustomPlot* mPlot;
    State mState = Idle;
    QCPAbstractItem* mCurrentItem = nullptr;
    QCPAxis* mKeyAxis = nullptr;
    QCPAxis* mValueAxis = nullptr;
    double mAnchorKey = 0;
    double mAnchorValue = 0;

    void initItemPosition(double key, double value);
    void commitItem();
    void cancelItem();
    void updateItemPosition(const QPointF& pixelPos);
    void drawBadge(QCPPainter* painter, const QRect& axisRectArea);
    static void rebindPositions(QCPAbstractItem* item, QCPAxis* keyAxis,
                                QCPAxis* valueAxis, QCPAxisRect* axisRect);
    QCPAxisRect* axisRectAt(const QPointF& pos) const;
};

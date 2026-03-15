#ifndef QCP_ITEM_VSPAN_H
#define QCP_ITEM_VSPAN_H

#include "../global.h"
#include "item.h"

class QCPPainter;
class QCustomPlot;

class QCP_LIB_DECL QCPItemVSpan : public QCPAbstractItem
{
    Q_OBJECT
    Q_PROPERTY(QPen pen READ pen WRITE setPen)
    Q_PROPERTY(QPen selectedPen READ selectedPen WRITE setSelectedPen)
    Q_PROPERTY(QBrush brush READ brush WRITE setBrush)
    Q_PROPERTY(QBrush selectedBrush READ selectedBrush WRITE setSelectedBrush)
    Q_PROPERTY(QPen borderPen READ borderPen WRITE setBorderPen)
    Q_PROPERTY(QPen selectedBorderPen READ selectedBorderPen WRITE setSelectedBorderPen)
    Q_PROPERTY(bool movable READ movable WRITE setMovable)

public:
    explicit QCPItemVSpan(QCustomPlot* parentPlot);
    virtual ~QCPItemVSpan() override;

    QPen pen() const { return mPen; }
    QPen selectedPen() const { return mSelectedPen; }
    QBrush brush() const { return mBrush; }
    QBrush selectedBrush() const { return mSelectedBrush; }
    QPen borderPen() const { return mBorderPen; }
    QPen selectedBorderPen() const { return mSelectedBorderPen; }
    bool movable() const { return mMovable; }
    QCPRange range() const;

    void setPen(const QPen& pen);
    void setSelectedPen(const QPen& pen);
    void setBrush(const QBrush& brush);
    void setSelectedBrush(const QBrush& brush);
    void setBorderPen(const QPen& pen);
    void setSelectedBorderPen(const QPen& pen);
    void setMovable(bool movable);
    void setRange(const QCPRange& range);

    virtual double selectTest(const QPointF& pos, bool onlySelectable,
                              QVariant* details = nullptr) const override;

    QCPItemPosition* const lowerEdge;
    QCPItemPosition* const upperEdge;
    QCPItemAnchor* const center;

    // hit detail: which part was grabbed (public for test and external use)
    enum HitPart { hpNone = -2, hpFill = -1, hpLowerEdge = 0, hpUpperEdge = 1 };

signals:
    void rangeChanged(const QCPRange& newRange);

protected:
    enum AnchorIndex { aiCenter };

    QPen mPen, mSelectedPen;
    QBrush mBrush, mSelectedBrush;
    QPen mBorderPen, mSelectedBorderPen;
    bool mMovable = true;

    HitPart mDragPart = hpNone;
    double mDragStartLower = 0.0;
    double mDragStartUpper = 0.0;

    virtual void draw(QCPPainter* painter) override;
    virtual QPointF anchorPixelPosition(int anchorId) const override;
    virtual void mousePressEvent(QMouseEvent* event, const QVariant& details) override;
    virtual void mouseMoveEvent(QMouseEvent* event, const QPointF& startPos) override;
    virtual void mouseReleaseEvent(QMouseEvent* event, const QPointF& startPos) override;

    QPen mainPen() const;
    QBrush mainBrush() const;
    QPen mainBorderPen() const;
};

#endif // QCP_ITEM_VSPAN_H

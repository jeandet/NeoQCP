#pragma once
#include "layoutelement-legend.h"

class QCPMultiGraph;

class QCP_LIB_DECL QCPGroupLegendItem : public QCPAbstractLegendItem
{
    Q_OBJECT
public:
    QCPGroupLegendItem(QCPLegend* parent, QCPMultiGraph* multiGraph);

    QCPMultiGraph* multiGraph() const { return mMultiGraph; }
    bool expanded() const { return mExpanded; }
    void setExpanded(bool expanded);
    int selectedComponent() const { return mSelectedComponent; }

    QSize minimumOuterSizeHint() const override;
    double selectTest(const QPointF& pos, bool onlySelectable, QVariant* details = nullptr) const override;

signals:
    void componentClicked(int componentIndex);

protected:
    void draw(QCPPainter* painter) override;
    void selectEvent(QMouseEvent* event, bool additive, const QVariant& details, bool* selectionStateChanged) override;

private:
    int rowHeight() const;
    QString headerName() const;
    QCPMultiGraph* mMultiGraph;
    bool mExpanded = false;
    int mSelectedComponent = -1;
};

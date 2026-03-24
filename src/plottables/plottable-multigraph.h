#pragma once
#include "plottable.h"
#include "plottable1d.h"
#include "datasource/abstract-multi-datasource.h"
#include "datasource/soa-multi-datasource.h"
#include "datasource/row-major-multi-datasource.h"
#include "datasource/async-pipeline.h"
#include "datasource/graph-resampler.h"
#include <memory>
#include <span>

struct QCP_LIB_DECL QCPGraphComponent {
    QString name;
    QPen pen;
    QPen selectedPen;
    QCPScatterStyle scatterStyle;
    QCPDataSelection selection;
    bool visible = true;
};

class QCP_LIB_DECL QCPMultiGraph : public QCPAbstractPlottable, public QCPPlottableInterface1D {
    Q_OBJECT
public:
    enum LineStyle { lsNone, lsLine, lsStepLeft, lsStepRight, lsStepCenter, lsImpulse };
    Q_ENUM(LineStyle)

    explicit QCPMultiGraph(QCPAxis* keyAxis, QCPAxis* valueAxis);
    ~QCPMultiGraph() override;

    // Data source
    void setDataSource(std::unique_ptr<QCPAbstractMultiDataSource> source);
    virtual void setDataSource(std::shared_ptr<QCPAbstractMultiDataSource> source);
    QCPAbstractMultiDataSource* dataSource() const { return mDataSource.get(); }
    void dataChanged();

    QCPMultiGraphPipeline& pipeline() { return mPipeline; }
    const QCPMultiGraphPipeline& pipeline() const { return mPipeline; }
    bool hasRenderedRange() const { return mHasRenderedRange; }
    QPointF stallPixelOffset() const override;

    // Convenience: owning
    template <IndexableNumericRange KC, IndexableNumericRange VC>
    void setData(KC&& keys, std::vector<VC>&& valueColumns)
    {
        using KD = std::decay_t<KC>;
        using VD = std::decay_t<VC>;
        setDataSource(std::make_shared<QCPSoAMultiDataSource<KD, VD>>(
            std::forward<KC>(keys), std::forward<std::vector<VC>>(valueColumns)));
    }

    // Convenience: non-owning spans (column-major / Fortran-order)
    template <typename K, typename V>
    void viewData(std::span<const K> keys, std::vector<std::span<const V>> valueColumns)
    {
        setDataSource(std::make_shared<
            QCPSoAMultiDataSource<std::span<const K>, std::span<const V>>>(
            keys, std::move(valueColumns)));
    }

    // Convenience: non-owning row-major (C-order) 2D array
    template <typename K, typename V>
    void viewRowMajorData(std::span<const K> keys, const V* values,
                           int rows, int columns, int stride)
    {
        setDataSource(std::make_shared<QCPRowMajorMultiDataSource<K, V>>(
            keys, values, rows, columns, stride));
    }

    // Components
    int componentCount() const { return mComponents.size(); }
    QCPGraphComponent& component(int index) { return mComponents[index]; }
    const QCPGraphComponent& component(int index) const { return mComponents[index]; }
    void setComponentNames(const QStringList& names);
    void setComponentColors(const QList<QColor>& colors);
    void setComponentPens(const QList<QPen>& pens);

    // Per-component value access (for tracers/tooltips)
    double componentValueAt(int column, int index) const;

    // Shared style
    LineStyle lineStyle() const { return mLineStyle; }
    void setLineStyle(LineStyle style) { mLineStyle = style; }
    bool adaptiveSampling() const { return mAdaptiveSampling; }
    void setAdaptiveSampling(bool enabled) { if (mAdaptiveSampling != enabled) { mAdaptiveSampling = enabled; mLineCacheDirty = true; mCachedLines.clear(); } }
    int scatterSkip() const { return mScatterSkip; }
    void setScatterSkip(int skip) { mScatterSkip = qMax(0, skip); }

    // Per-component selection
    QCPDataSelection componentSelection(int index) const;
    void setComponentSelection(int index, const QCPDataSelection& sel);

    // QCPPlottableInterface1D
    int dataCount() const override;
    double dataMainKey(int index) const override;
    double dataSortKey(int index) const override;
    double dataMainValue(int index) const override;
    QCPRange dataValueRange(int index) const override;
    QPointF dataPixelPosition(int index) const override;
    bool sortKeyIsMainKey() const override { return true; }
    QCPDataSelection selectTestRect(const QRectF& rect, bool onlySelectable) const override;
    int findBegin(double sortKey, bool expandedRange = true) const override;
    int findEnd(double sortKey, bool expandedRange = true) const override;

    // QCPAbstractPlottable
    QCPPlottableInterface1D* interface1D() override { return this; }
    double selectTest(const QPointF& pos, bool onlySelectable,
                      QVariant* details = nullptr) const override;
    QCPRange getKeyRange(bool& foundRange,
                         QCP::SignDomain inSignDomain = QCP::sdBoth) const override;
    QCPRange getValueRange(bool& foundRange,
                           QCP::SignDomain inSignDomain = QCP::sdBoth,
                           const QCPRange& inKeyRange = QCPRange()) const override;

    // Legend
    using QCPAbstractPlottable::addToLegend;
    using QCPAbstractPlottable::removeFromLegend;
    bool addToLegend(QCPLegend* legend) override;
    bool removeFromLegend(QCPLegend* legend) const override;

protected:
    void draw(QCPPainter* painter) override;
    void drawLegendIcon(QCPPainter* painter, const QRectF& rect) const override;

    void selectEvent(QMouseEvent* event, bool additive, const QVariant& details,
                     bool* selectionStateChanged) override;
    void deselectEvent(bool* selectionStateChanged) override;

    friend class TestMultiGraph;
    friend class TestPipeline;

protected:
    std::shared_ptr<QCPAbstractMultiDataSource> mDataSource;
    QVector<QCPGraphComponent> mComponents;
    mutable QVector<QCPDataSelection> mLastRectSelections; // per-component selections from selectTestRect
    LineStyle mLineStyle = lsLine;
    bool mAdaptiveSampling = true;
    int mScatterSkip = 0;
    QCPMultiGraphPipeline mPipeline;
    std::shared_ptr<qcp::algo::MultiGraphResamplerCache> mL1Cache;
    std::shared_ptr<QCPAbstractMultiDataSource> mL2Result;
    bool mL2Dirty = false;
    bool mNeedsResampling = false;
    struct { QCPRange key, value; } mRenderedRange {};
    bool mHasRenderedRange = false;
    // Line cache: per-component cached lines, reused with GPU offset
    QVector<QVector<QPointF>> mCachedLines;
    bool mLineCacheDirty = true;
    QSize mCachedPlotSize;

    void onL1Ready();
    void rebuildL2(const ViewportParams& vp);
    void onViewportChanged();
    bool pipelineBusy() const override { return mPipeline.isBusy(); }

    void syncComponentCount();
    void updateBaseSelection();

    // Line style transforms (pixel-space in, pixel-space out)
    static QVector<QPointF> toStepLeftLines(const QVector<QPointF>& lines, bool keyIsVertical);
    static QVector<QPointF> toStepRightLines(const QVector<QPointF>& lines, bool keyIsVertical);
    static QVector<QPointF> toStepCenterLines(const QVector<QPointF>& lines, bool keyIsVertical);
    QVector<QPointF> toImpulseLines(const QVector<QPointF>& lines, bool keyIsVertical) const;
};

#pragma once
#include "plottable-multigraph.h"
#include <memory>

class QCPWaterfallDataAdapter : public QCPAbstractMultiDataSource {
public:
    explicit QCPWaterfallDataAdapter(std::shared_ptr<QCPAbstractMultiDataSource> source);

    void setSource(std::shared_ptr<QCPAbstractMultiDataSource> source);
    QCPAbstractMultiDataSource* source() const { return mSource.get(); }

    void setOffsets(const QVector<double>& offsets) { mOffsets = offsets; }
    void setNormFactors(const QVector<double>& factors) { mNormFactors = factors; }
    void setGain(double gain) { mGain = gain; }

    int columnCount() const override;
    int size() const override;
    double keyAt(int i) const override;
    QCPRange keyRange(bool& found, QCP::SignDomain sd = QCP::sdBoth) const override;
    int findBegin(double sortKey, bool expandedRange = true) const override;
    int findEnd(double sortKey, bool expandedRange = true) const override;

    double valueAt(int column, int i) const override;
    QCPRange valueRange(int column, bool& found, QCP::SignDomain sd = QCP::sdBoth,
                        const QCPRange& inKeyRange = QCPRange()) const override;
    QVector<QPointF> getLines(int column, int begin, int end,
                               QCPAxis* keyAxis, QCPAxis* valueAxis) const override;
    QVector<QPointF> getOptimizedLineData(int column, int begin, int end, int pixelWidth,
                                           QCPAxis* keyAxis, QCPAxis* valueAxis) const override;

private:
    std::shared_ptr<QCPAbstractMultiDataSource> mSource;
    QVector<double> mOffsets;
    QVector<double> mNormFactors;
    double mGain = 1.0;

    double transform(int column, double rawValue) const;
};

class QCP_LIB_DECL QCPWaterfallGraph : public QCPMultiGraph {
    Q_OBJECT
public:
    enum OffsetMode { omUniform, omCustom };
    Q_ENUM(OffsetMode)

    explicit QCPWaterfallGraph(QCPAxis* keyAxis, QCPAxis* valueAxis);

    void setDataSource(std::shared_ptr<QCPAbstractMultiDataSource> source) override;

    OffsetMode offsetMode() const { return mOffsetMode; }
    void setOffsetMode(OffsetMode mode);
    double uniformSpacing() const { return mUniformSpacing; }
    void setUniformSpacing(double spacing);
    QVector<double> offsets() const { return mUserOffsets; }
    void setOffsets(const QVector<double>& offsets);

    bool normalize() const { return mNormalize; }
    void setNormalize(bool enabled);
    double gain() const { return mGain; }
    void setGain(double gain);

    void invalidateNormalization();

protected:
    void dataChanged() override;
    void draw(QCPPainter* painter) override;
    QCPRange getValueRange(bool& foundRange,
                           QCP::SignDomain inSignDomain = QCP::sdBoth,
                           const QCPRange& inKeyRange = QCPRange()) const override;

private:
    OffsetMode mOffsetMode = omUniform;
    double mUniformSpacing = 1.0;
    QVector<double> mUserOffsets;
    bool mNormalize = true;
    double mGain = 1.0;
    mutable bool mNormDirty = true;
    mutable QVector<double> mCachedNormFactors;

    std::shared_ptr<QCPAbstractMultiDataSource> mOriginalSource;
    std::shared_ptr<QCPWaterfallDataAdapter> mAdapter;

    double effectiveOffset(int component) const;
    void updateAdapter() const;
    void recomputeNormFactors() const;

    friend class TestWaterfall;
};

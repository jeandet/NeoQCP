#pragma once
#include <QtTest/QtTest>

class QCustomPlot;

class TestPipeline : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    // Scheduler tests
    void schedulerSubmitHeavy();
    void schedulerSubmitFast();
    void schedulerFastPriority();

    // Pipeline base tests
    void pipelinePassthrough();
    void pipelineTransformRuns();
    void pipelineCoalescing();
    void pipelineViewportIndependentSkipsViewport();
    void pipelineViewportDependentRuns();
    void pipelineCachePreservedOnViewport();
    void pipelineCacheClearedOnDataChange();
    void pipelineInterimResult();
    void pipelineDestructionWhileRunning();

    // QCPGraph2 pipeline integration
    void graph2PipelinePassthrough();
    void graph2PipelineTransform();

    // QCPColorMap2 pipeline integration
    void colormap2PipelineDefault();
    void colormap2PipelineResample();

    // End-to-end
    void graph2DataFromExternalThread();
    void colormap2DataFromExternalThread();

    // Race condition reproducers
    void pipelineSourceReplacedDuringJob();
    void pipelineRapidFireDeliverResult();

    // Graph resampler
    void graphResamplerBinMinMax();
    void graphResamplerLevel1AndLevel2();
    void graphResamplerCacheReuse();
    void graphResamplerNaNSkipped();
    void graphResamplerEmptyBinsProduceNaN();
    void graph2HierarchicalResamplingActivates();
    void graph2SmallDataNoResampling();
    void graph2LargeToSmallDataFallback();
    void graphResamplerBinMinMaxKeyPositions();
    void graphResamplerBinMinMaxZeroBins();
    void graphResamplerNonFiniteKeysSkipped();
    void graphResamplerParallelMatchesSingleThreaded();

    // Multi-column resampler
    void multiGraphBinMinMaxMulti();
    void multiGraphBinMinMaxMultiNaN();
    void multiGraphBinMinMaxMultiParallelMatchesSingleThreaded();
    void resampledMultiDataSourceInterface();
    void multiGraphL1AndL2();

    // QCPMultiGraph pipeline integration
    void multiGraphSmallDataNoResampling();
    void multiGraphLargeDataL1L2();
    void multiGraphThresholdScalesWithColumnCount();
    void multiGraphRapidSetDataSource();
    void multiGraphExportSynchronousFallback();
    void multiGraphLogScaleFallback();
    void multiGraphDataChangedInvalidatesL1();
    void multiGraphHiddenComponentsStillResampled();

    // L2 lazy rebuild (deferred to draw)
    void graph2L2RebuildDeferredToDraw();
    void graph2L2CoalescesMultipleViewportChanges();
    void graph2L2DirtyAfterL1Ready();

    // Histogram binner
    void bin2dBasicCounts();
    void bin2dNaNSkipped();
    void bin2dEmptyInput();
    void bin2dSingleBin();

    // QCPHistogram2D
    void histogram2dPipelineBins();
    void histogram2dNormalizationColumn();
    void histogram2dNormalizationToggleNoRebind();
    void histogram2dRenderSmokeTest();

private:
    QCustomPlot* mPlot = nullptr;
};

#include "test-qcustomplot/test-qcustomplot.h"
#include "test-qcpgraph/test-qcpgraph.h"
#include "test-qcpcurve/test-qcpcurve.h"
#include "test-qcpbars/test-qcpbars.h"
#include "test-qcpfinancial/test-qcpfinancial.h"
#include "test-colormap/test-colormap.h"
#include "test-qcplayout/test-qcplayout.h"
#include "test-qcplegend/test-qcplegend.h"
#include "test-qcpaxisrect/test-qcpaxisrect.h"
#include "test-datacontainer/test-datacontainer.h"
#include "test-line-extruder/test-line-extruder.h"
#include "test-datasource/test-datasource.h"
#include "test-datasource2d/test-datasource2d.h"
#include "test-theme/test-theme.h"
#include "test-paintbuffer/test-paintbuffer.h"
#include "test-multi-datasource/test-multi-datasource.h"
#include "test-multigraph/test-multigraph.h"
#include "test-pipeline/test-pipeline.h"
#include "test-waterfall/test-waterfall.h"
#include "test-hspan/test-hspan.h"
#include "test-rspan/test-rspan.h"
#include "test-vspan/test-vspan.h"
#include "test-richtext/test-richtext.h"
#include "test-data-locator/test-data-locator.h"
#include "test-overlay/test-overlay.h"

#define QCPTEST(t) t t##instance; QTest::qExec(&t##instance)

int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  
  QCPTEST(TestQCustomPlot);
  QCPTEST(TestQCPGraph);
  QCPTEST(TestQCPCurve);
  QCPTEST(TestQCPBars);
  QCPTEST(TestQCPFinancial);
  QCPTEST(TestColorMap);
  QCPTEST(TestQCPLayout);
  QCPTEST(TestQCPLegend);
  QCPTEST(TestQCPAxisRect);
  QCPTEST(TestDatacontainer);
  QCPTEST(TestLineExtruder);
  QCPTEST(TestDataSource);
  QCPTEST(TestDataSource2D);
  QCPTEST(TestTheme);
  QCPTEST(TestPaintBuffer);
  QCPTEST(TestMultiDataSource);
  QCPTEST(TestMultiGraph);
  QCPTEST(TestDataLocator);
  QCPTEST(TestHSpan);
  QCPTEST(TestRSpan);
  QCPTEST(TestVSpan);
  QCPTEST(TestWaterfall);
  QCPTEST(TestPipeline);
  QCPTEST(TestRichText);
  QCPTEST(TestOverlay);

  return 0;
}

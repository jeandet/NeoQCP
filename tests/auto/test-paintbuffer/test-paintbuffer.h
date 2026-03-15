#include <QtTest/QtTest>
#include <QtWidgets/qtestsupport_widgets.h>
#include "qcustomplot.h"
#include "painting/paintbuffer-pixmap.h"

class TestPaintBuffer : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void contentDirty_newBufferIsDirty();
    void contentDirty_resetAfterReplot();
    void contentDirty_setOnResize();
    void contentDirty_setOnInvalidation();
    void contentDirty_markDirtyLayer();
    void contentDirty_fallbackMarksAllDirty();
    void contentDirty_incrementalReplotSkipsCleanBuffers();
    void contentDirty_incrementalReplotPreservesContent();
    void replotAndExport_smokeTest();
    void replotOnFirstShow_tabWidget();

private:
    QCustomPlot* mPlot;
};

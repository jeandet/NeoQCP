#include "test-rspan.h"
#include "../../../src/qcp.h"

void TestRSpan::init()
{
    mPlot = new QCustomPlot();
    mPlot->resize(400, 300);
    mPlot->xAxis->setRange(0, 10);
    mPlot->yAxis->setRange(0, 10);
    mPlot->replot();
}

void TestRSpan::cleanup()
{
    delete mPlot;
    mPlot = nullptr;
}

void TestRSpan::createAndRanges()
{
    auto* span = new QCPItemRSpan(mPlot);
    span->setKeyRange(QCPRange(2, 8));
    span->setValueRange(QCPRange(3, 7));

    QCOMPARE(span->keyRange().lower, 2.0);
    QCOMPARE(span->keyRange().upper, 8.0);
    QCOMPARE(span->valueRange().lower, 3.0);
    QCOMPARE(span->valueRange().upper, 7.0);
}

void TestRSpan::selectTestEdges()
{
    auto* span = new QCPItemRSpan(mPlot);
    span->setKeyRange(QCPRange(2, 8));
    span->setValueRange(QCPRange(2, 8));
    mPlot->replot();

    const double midY = (mPlot->yAxis->coordToPixel(2) + mPlot->yAxis->coordToPixel(8)) * 0.5;
    const double midX = (mPlot->xAxis->coordToPixel(2) + mPlot->xAxis->coordToPixel(8)) * 0.5;

    // left edge
    {
        QVariant details;
        double dist = span->selectTest(
            QPointF(mPlot->xAxis->coordToPixel(2), midY), false, &details);
        QVERIFY(dist >= 0);
        QCOMPARE(details.toInt(), static_cast<int>(QCPItemRSpan::hpLeft));
    }

    // right edge
    {
        QVariant details;
        double dist = span->selectTest(
            QPointF(mPlot->xAxis->coordToPixel(8), midY), false, &details);
        QVERIFY(dist >= 0);
        QCOMPARE(details.toInt(), static_cast<int>(QCPItemRSpan::hpRight));
    }

    // top edge
    {
        QVariant details;
        double dist = span->selectTest(
            QPointF(midX, mPlot->yAxis->coordToPixel(8)), false, &details);
        QVERIFY(dist >= 0);
        QCOMPARE(details.toInt(), static_cast<int>(QCPItemRSpan::hpTop));
    }

    // bottom edge
    {
        QVariant details;
        double dist = span->selectTest(
            QPointF(midX, mPlot->yAxis->coordToPixel(2)), false, &details);
        QVERIFY(dist >= 0);
        QCOMPARE(details.toInt(), static_cast<int>(QCPItemRSpan::hpBottom));
    }
}

void TestRSpan::selectTestFill()
{
    auto* span = new QCPItemRSpan(mPlot);
    span->setKeyRange(QCPRange(2, 8));
    span->setValueRange(QCPRange(2, 8));
    mPlot->replot();

    const double midX = mPlot->xAxis->coordToPixel(5);
    const double midY = mPlot->yAxis->coordToPixel(5);

    QVariant details;
    double dist = span->selectTest(QPointF(midX, midY), false, &details);
    QVERIFY(dist >= 0);
    QCOMPARE(details.toInt(), static_cast<int>(QCPItemRSpan::hpFill));
}

void TestRSpan::drawDoesNotCrash()
{
    auto* span = new QCPItemRSpan(mPlot);
    span->setKeyRange(QCPRange(1, 9));
    span->setValueRange(QCPRange(1, 9));
    mPlot->replot();
    QVERIFY(true);
}

void TestRSpan::exportFallbackRendersSpan()
{
    auto* span = new QCPItemRSpan(mPlot);
    span->setKeyRange(QCPRange(3, 7));
    span->setValueRange(QCPRange(3, 7));
    span->setBrush(QBrush(QColor(0, 0, 255, 128)));
    span->setPen(Qt::NoPen);
    span->setBorderPen(Qt::NoPen);
    mPlot->replot();
    QPixmap pm = mPlot->toPixmap(400, 300);
    QVERIFY(!pm.isNull());
    QImage img = pm.toImage();
    int centerX = static_cast<int>(mPlot->xAxis->coordToPixel(5));
    int centerY = static_cast<int>(mPlot->yAxis->coordToPixel(5));
    centerX = qBound(0, centerX, img.width() - 1);
    centerY = qBound(0, centerY, img.height() - 1);
    QColor c = img.pixelColor(centerX, centerY);
    QVERIFY2(c.blue() > 50, qPrintable(QString("Expected blue > 50, got %1").arg(c.blue())));
}

void TestRSpan::dirtyTrackingReplots()
{
    auto* span = new QCPItemRSpan(mPlot);
    span->setKeyRange(QCPRange(2, 8));
    span->setValueRange(QCPRange(2, 8));
    mPlot->replot();

    span->setBrush(QBrush(Qt::blue));
    mPlot->replot();
    span->setPen(QPen(Qt::green));
    mPlot->replot();
    span->setBorderPen(QPen(Qt::red, 3));
    mPlot->replot();
    span->setKeyRange(QCPRange(1, 9));
    mPlot->replot();
    span->setValueRange(QCPRange(1, 9));
    mPlot->replot();
    QVERIFY(true);
}

void TestRSpan::drawOnLogAxis()
{
    mPlot->xAxis->setScaleType(QCPAxis::stLogarithmic);
    mPlot->xAxis->setRange(1, 1000);
    mPlot->yAxis->setScaleType(QCPAxis::stLogarithmic);
    mPlot->yAxis->setRange(1, 1000);
    auto* span = new QCPItemRSpan(mPlot);
    span->setKeyRange(QCPRange(10, 100));
    span->setValueRange(QCPRange(10, 100));
    mPlot->replot();
    QVERIFY(true);
}

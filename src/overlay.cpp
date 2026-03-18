#include "overlay.h"
#include "core.h"
#include "painting/painter.h"
#include <QApplication>

QCPOverlay::QCPOverlay(QCustomPlot* parentPlot)
    : QCPLayerable(parentPlot)
{
    mFont = QApplication::font();
    setVisible(false);
}

void QCPOverlay::showMessage(const QString& text, Level level,
                              SizeMode sizeMode, Position position)
{
    mText = text;
    mLevel = level;
    mSizeMode = sizeMode;
    mPosition = position;
    setVisible(!text.isEmpty());
    emit messageChanged(mText, mLevel);
    if (mParentPlot)
        mParentPlot->replot(QCustomPlot::rpQueuedReplot);
}

void QCPOverlay::clearMessage()
{
    mText.clear();
    setVisible(false);
    emit messageChanged(mText, mLevel);
    if (mParentPlot)
        mParentPlot->replot(QCustomPlot::rpQueuedReplot);
}

void QCPOverlay::setCollapsible(bool enabled)
{
    mCollapsible = enabled;
}

void QCPOverlay::setCollapsed(bool collapsed)
{
    if (mCollapsed == collapsed)
        return;
    mCollapsed = collapsed;
    emit collapsedChanged(mCollapsed);
    if (mParentPlot)
        mParentPlot->replot(QCustomPlot::rpQueuedReplot);
}

void QCPOverlay::setOpacity(qreal opacity)
{
    mOpacity = qBound(0.0, opacity, 1.0);
}

void QCPOverlay::setFont(const QFont& font)
{
    mFont = font;
}

QColor QCPOverlay::levelColor() const
{
    switch (mLevel) {
        case Info:    return QColor(46, 139, 87);
        case Warning: return QColor(204, 153, 0);
        case Error:   return QColor(178, 34, 34);
    }
    return QColor(46, 139, 87);
}

QRect QCPOverlay::overlayRect() const
{
    return computeRect();
}

QRect QCPOverlay::computeRect() const
{
    return {}; // stub — implemented in Task 3
}

QRect QCPOverlay::collapseHandleRect() const
{
    return {}; // stub — implemented in Task 4
}

void QCPOverlay::applyDefaultAntialiasingHint(QCPPainter* painter) const
{
    painter->setAntialiasing(true);
}

void QCPOverlay::draw(QCPPainter*)
{
    // stub — implemented in Task 3
}

double QCPOverlay::selectTest(const QPointF&, bool, QVariant*) const
{
    return -1; // stub — implemented in Task 4
}

void QCPOverlay::mousePressEvent(QMouseEvent*, const QVariant&)
{
    // stub — implemented in Task 4
}

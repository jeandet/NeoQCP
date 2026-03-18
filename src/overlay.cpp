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
    if (mText.isEmpty() || !mParentPlot)
        return {};

    const QRect viewport = mParentPlot->rect();
    const QFontMetrics fm(mFont);
    constexpr int pad = 4;
    const bool horizontal = (mPosition == Top || mPosition == Bottom);

    if (mSizeMode == FullWidget)
        return viewport;

    int contentSize = 0;
    if (mSizeMode == Compact || mCollapsed) {
        contentSize = fm.height() + 2 * pad;
    } else { // FitContent
        if (horizontal) {
            QRect textBounds = fm.boundingRect(
                QRect(0, 0, viewport.width() - 2 * pad, 0),
                Qt::AlignLeft | Qt::TextWordWrap, mText);
            contentSize = textBounds.height() + 2 * pad;
        } else {
            QRect textBounds = fm.boundingRect(
                QRect(0, 0, viewport.height() - 2 * pad, 0),
                Qt::AlignLeft | Qt::TextWordWrap, mText);
            contentSize = textBounds.height() + 2 * pad;
        }
    }

    switch (mPosition) {
        case Top:
            return QRect(viewport.left(), viewport.top(),
                         viewport.width(), contentSize);
        case Bottom:
            return QRect(viewport.left(), viewport.bottom() - contentSize + 1,
                         viewport.width(), contentSize);
        case Left:
            return QRect(viewport.left(), viewport.top(),
                         contentSize, viewport.height());
        case Right:
            return QRect(viewport.right() - contentSize + 1, viewport.top(),
                         contentSize, viewport.height());
    }
    return {};
}

QRect QCPOverlay::collapseHandleRect() const
{
    if (!mCollapsible)
        return {};
    const QRect rect = computeRect();
    if (rect.isEmpty())
        return {};

    constexpr int handleSize = 20;
    const bool horizontal = (mPosition == Top || mPosition == Bottom);
    if (horizontal) {
        return QRect(rect.right() - handleSize, rect.top(),
                     handleSize, rect.height());
    } else {
        return QRect(rect.left(), rect.bottom() - handleSize,
                     rect.width(), handleSize);
    }
}

void QCPOverlay::applyDefaultAntialiasingHint(QCPPainter* painter) const
{
    painter->setAntialiasing(true);
}

void QCPOverlay::draw(QCPPainter* painter)
{
    if (mText.isEmpty())
        return;
    // Skip during export
    if (painter->modes().testFlag(QCPPainter::pmNoCaching))
        return;

    const QRect rect = computeRect();
    if (rect.isEmpty())
        return;

    painter->save();

    // Background
    QColor bg = levelColor();
    bg.setAlphaF(mOpacity);
    painter->setPen(Qt::NoPen);
    painter->setBrush(bg);
    painter->drawRoundedRect(rect, 4, 4);

    // Text
    constexpr int pad = 4;
    painter->setPen(Qt::white);
    painter->setFont(mFont);

    const bool horizontal = (mPosition == Top || mPosition == Bottom);
    const QString displayText = mCollapsed ? mText.section('\n', 0, 0) : mText;

    if (horizontal) {
        QRect textRect = rect.adjusted(pad, pad, -pad, -pad);
        int flags = Qt::AlignLeft | Qt::AlignVCenter;
        if (mSizeMode != Compact && !mCollapsed)
            flags = Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap;
        painter->drawText(textRect, flags, displayText);
    } else {
        // Rotated text for Left/Right
        painter->translate(rect.center());
        if (mPosition == Left)
            painter->rotate(-90); // bottom-to-top
        else
            painter->rotate(90);  // top-to-bottom
        QRect textRect(-rect.height() / 2, -rect.width() / 2,
                       rect.height(), rect.width());
        textRect.adjust(pad, pad, -pad, -pad);
        int flags = Qt::AlignLeft | Qt::AlignVCenter;
        if (mSizeMode != Compact && !mCollapsed)
            flags = Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap;
        painter->drawText(textRect, flags, displayText);
    }

    // Collapse handle
    if (mCollapsible) {
        QRect handleRect = collapseHandleRect();
        painter->setPen(QPen(Qt::white, 1.5));
        painter->setBrush(Qt::NoBrush);
        QPointF center = handleRect.center();
        qreal sz = 4.0;
        if (horizontal) {
            if (mCollapsed) {
                painter->drawLine(QPointF(center.x() - sz, center.y() - sz/2),
                                  QPointF(center.x(), center.y() + sz/2));
                painter->drawLine(QPointF(center.x(), center.y() + sz/2),
                                  QPointF(center.x() + sz, center.y() - sz/2));
            } else {
                painter->drawLine(QPointF(center.x() - sz, center.y() + sz/2),
                                  QPointF(center.x(), center.y() - sz/2));
                painter->drawLine(QPointF(center.x(), center.y() - sz/2),
                                  QPointF(center.x() + sz, center.y() + sz/2));
            }
        } else {
            if (mCollapsed) {
                qreal dir = (mPosition == Left) ? 1.0 : -1.0;
                painter->drawLine(QPointF(center.x() - dir*sz/2, center.y() - sz),
                                  QPointF(center.x() + dir*sz/2, center.y()));
                painter->drawLine(QPointF(center.x() + dir*sz/2, center.y()),
                                  QPointF(center.x() - dir*sz/2, center.y() + sz));
            } else {
                qreal dir = (mPosition == Left) ? -1.0 : 1.0;
                painter->drawLine(QPointF(center.x() - dir*sz/2, center.y() - sz),
                                  QPointF(center.x() + dir*sz/2, center.y()));
                painter->drawLine(QPointF(center.x() + dir*sz/2, center.y()),
                                  QPointF(center.x() - dir*sz/2, center.y() + sz));
            }
        }
    }

    painter->restore();
}

double QCPOverlay::selectTest(const QPointF& pos, bool /*onlySelectable*/,
                               QVariant* /*details*/) const
{
    if (mText.isEmpty() || !visible())
        return -1;

    const QRect rect = computeRect();
    if (!rect.contains(pos.toPoint()))
        return -1;

    if (mSizeMode == FullWidget)
        return 0;

    if (mCollapsible && collapseHandleRect().contains(pos.toPoint()))
        return 0;

    return -1;
}

void QCPOverlay::mousePressEvent(QMouseEvent* event, const QVariant& /*details*/)
{
    if (mCollapsible && collapseHandleRect().contains(event->pos()))
        setCollapsed(!mCollapsed);
}

#include "theme.h"

QCPTheme::QCPTheme(QObject* parent)
    : QObject(parent)
    , mBackground(Qt::white)
    , mForeground(Qt::black)
    , mGrid(200, 200, 200)
    , mSubGrid(220, 220, 220)
    , mSelection(Qt::blue)
    , mLegendBackground(Qt::white)
    , mLegendBorder(Qt::black)
{
}

void QCPTheme::setBackground(const QColor& color)
{
    if (mBackground != color) { mBackground = color; emit changed(); }
}

void QCPTheme::setForeground(const QColor& color)
{
    if (mForeground != color) { mForeground = color; emit changed(); }
}

void QCPTheme::setGrid(const QColor& color)
{
    if (mGrid != color) { mGrid = color; emit changed(); }
}

void QCPTheme::setSubGrid(const QColor& color)
{
    if (mSubGrid != color) { mSubGrid = color; emit changed(); }
}

void QCPTheme::setSelection(const QColor& color)
{
    if (mSelection != color) { mSelection = color; emit changed(); }
}

void QCPTheme::setLegendBackground(const QColor& color)
{
    if (mLegendBackground != color) { mLegendBackground = color; emit changed(); }
}

void QCPTheme::setLegendBorder(const QColor& color)
{
    if (mLegendBorder != color) { mLegendBorder = color; emit changed(); }
}

QCPTheme* QCPTheme::light(QObject* parent)
{
    return new QCPTheme(parent);
}

QCPTheme* QCPTheme::dark(QObject* parent)
{
    auto* theme = new QCPTheme(parent);
    // Block signals to avoid emitting changed() for each setter during batch init
    theme->blockSignals(true);
    theme->setBackground(QColor("#1e1e1e"));
    theme->setForeground(QColor("#e0e0e0"));
    theme->setGrid(QColor("#3a3a3a"));
    theme->setSubGrid(QColor("#2a2a2a"));
    theme->setSelection(QColor("#4a9eff"));
    theme->setLegendBackground(QColor("#2a2a2a"));
    theme->setLegendBorder(QColor("#555555"));
    theme->blockSignals(false);
    return theme;
}

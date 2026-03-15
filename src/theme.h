#ifndef QCP_THEME_H
#define QCP_THEME_H

#include "global.h"
#include <QColor>
#include <QObject>

class QCP_LIB_DECL QCPTheme : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QColor background READ background WRITE setBackground NOTIFY changed)
    Q_PROPERTY(QColor foreground READ foreground WRITE setForeground NOTIFY changed)
    Q_PROPERTY(QColor grid READ grid WRITE setGrid NOTIFY changed)
    Q_PROPERTY(QColor subGrid READ subGrid WRITE setSubGrid NOTIFY changed)
    Q_PROPERTY(QColor selection READ selection WRITE setSelection NOTIFY changed)
    Q_PROPERTY(QColor legendBackground READ legendBackground WRITE setLegendBackground NOTIFY changed)
    Q_PROPERTY(QColor legendBorder READ legendBorder WRITE setLegendBorder NOTIFY changed)

public:
    explicit QCPTheme(QObject* parent = nullptr);

    QColor background() const { return mBackground; }
    QColor foreground() const { return mForeground; }
    QColor grid() const { return mGrid; }
    QColor subGrid() const { return mSubGrid; }
    QColor selection() const { return mSelection; }
    QColor legendBackground() const { return mLegendBackground; }
    QColor legendBorder() const { return mLegendBorder; }

    void setBackground(const QColor& color);
    void setForeground(const QColor& color);
    void setGrid(const QColor& color);
    void setSubGrid(const QColor& color);
    void setSelection(const QColor& color);
    void setLegendBackground(const QColor& color);
    void setLegendBorder(const QColor& color);

    static QCPTheme* light(QObject* parent = nullptr);
    static QCPTheme* dark(QObject* parent = nullptr);

signals:
    void changed();

private:
    QColor mBackground;
    QColor mForeground;
    QColor mGrid;
    QColor mSubGrid;
    QColor mSelection;
    QColor mLegendBackground;
    QColor mLegendBorder;
};

#endif // QCP_THEME_H

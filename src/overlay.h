#ifndef QCP_OVERLAY_H
#define QCP_OVERLAY_H

#include "global.h"
#include "layer.h"

class QCPPainter;

class QCP_LIB_DECL QCPOverlay : public QCPLayerable
{
    Q_OBJECT
public:
    enum Level { Info, Warning, Error };
    Q_ENUM(Level)
    enum SizeMode { Compact, FitContent, FullWidget };
    Q_ENUM(SizeMode)
    enum Position { Top, Bottom, Left, Right };
    Q_ENUM(Position)

    explicit QCPOverlay(QCustomPlot* parentPlot);
    ~QCPOverlay() override = default;

    // getters:
    QString text() const { return mText; }
    Level level() const { return mLevel; }
    SizeMode sizeMode() const { return mSizeMode; }
    Position position() const { return mPosition; }
    bool isCollapsible() const { return mCollapsible; }
    bool isCollapsed() const { return mCollapsed; }
    qreal opacity() const { return mOpacity; }
    QFont font() const { return mFont; }
    QRect overlayRect() const;

    // actions:
    void showMessage(const QString& text, Level level = Info,
                     SizeMode sizeMode = Compact, Position position = Top);
    void clearMessage();

    // setters:
    void setCollapsible(bool enabled);
    void setCollapsed(bool collapsed);
    void setOpacity(qreal opacity);
    void setFont(const QFont& font);

signals:
    void messageChanged(const QString& text, QCPOverlay::Level level);
    void collapsedChanged(bool collapsed);

protected:
    void applyDefaultAntialiasingHint(QCPPainter* painter) const override;
    void draw(QCPPainter* painter) override;
    double selectTest(const QPointF& pos, bool onlySelectable,
                      QVariant* details = nullptr) const override;
    void mousePressEvent(QMouseEvent* event, const QVariant& details) override;

private:
    QString mText;
    Level mLevel = Info;
    SizeMode mSizeMode = Compact;
    Position mPosition = Top;
    bool mCollapsible = false;
    bool mCollapsed = false;
    qreal mOpacity = 1.0;
    QFont mFont;

    QColor levelColor() const;
    QRect computeRect() const;
    QRect collapseHandleRect() const;

    Q_DISABLE_COPY(QCPOverlay)
};

#endif // QCP_OVERLAY_H

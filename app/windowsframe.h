#pragma once

#include <QAbstractNativeEventFilter>
#include <QFont>
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <array>

// QML owns the adapter; the OS owns move/resize/snap and system-menu tracking.
// Unsupported platforms/backends keep an ordinary Qt system-decorated window.
class WindowsFrame : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
    Q_PROPERTY(QQuickWindow* window READ window WRITE setWindow NOTIFY configurationChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY configurationChanged)
    Q_PROPERTY(bool blocked MEMBER m_blocked NOTIFY configurationChanged)
    Q_PROPERTY(QQuickItem* caption MEMBER m_caption NOTIFY configurationChanged)
    Q_PROPERTY(QQuickItem* minimizeButton MEMBER m_minimize NOTIFY configurationChanged)
    Q_PROPERTY(QQuickItem* maximizeButton MEMBER m_maximize NOTIFY configurationChanged)
    Q_PROPERTY(QQuickItem* closeButton MEMBER m_close NOTIFY configurationChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QString fallbackReason READ fallbackReason NOTIFY activeChanged)
    Q_PROPERTY(int hoveredButton READ hoveredButton NOTIFY interactionChanged)
    Q_PROPERTY(int pressedButton READ pressedButton NOTIFY interactionChanged)
    Q_PROPERTY(QFont captionFont READ captionFont CONSTANT)
public:
    explicit WindowsFrame(QObject* parent = nullptr);
    ~WindowsFrame() override;
    QQuickWindow* window() const { return m_window; }
    void setWindow(QQuickWindow* window);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    bool active() const { return m_active; }
    QString fallbackReason() const { return m_fallbackReason; }
    int hoveredButton() const { return m_hovered; }
    int pressedButton() const { return m_pressed; }
    QFont captionFont() const;
    Q_INVOKABLE void invoke(int button);

    // Native client pixels, not global desktop pixels: each HWND has its own DPI.
    static QPointF logicalPoint(QPointF clientPixels, qreal devicePixelRatio);
    enum Hit { Client, Caption, Minimize, Maximize, Close,
               Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight };
    static Hit hitTest(QPointF point, QSizeF size, qreal border, bool maximized,
                       QRectF caption, const std::array<QRectF, 3>& buttons);
    bool nativeEventFilter(const QByteArray&, void*, qintptr*) override;
signals:
    void configurationChanged();
    void activeChanged();
    void interactionChanged();
protected:
    bool eventFilter(QObject*, QEvent*) override;
private:
    void attach();
    void detach();
    void fail(const QString& reason);
    void setInteraction(int hovered, int pressed);
    Hit hitAtNativePoint(long screenX, long screenY) const;
    QPointer<QQuickWindow> m_window;
    // QML items are children of the same window and outlive native teardown.
    QQuickItem* m_caption = nullptr;
    QQuickItem* m_minimize = nullptr;
    QQuickItem* m_maximize = nullptr;
    QQuickItem* m_close = nullptr;
    bool m_enabled = false;
    bool m_blocked = false;
    bool m_active = false;
    bool m_attaching = false;
    quintptr m_handle = 0;
    int m_hovered = 0;
    int m_pressed = 0;
    QString m_fallbackReason;
};

void registerWindowsFrameType();

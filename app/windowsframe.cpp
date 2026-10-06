#include "windowsframe.h"
#include "maprenderitem.h"
#include "gpumapitem.h"
#include "referenceimageitem.h"
#include "geographicimageitem.h"
#include "terrainlandmaskitem.h"
#include "referenceimagelibrary.h"
#include <QFontDatabase>
#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QQmlEngine>
#include <QScopedValueRollback>
#include <QTimer>
#include <algorithm>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#endif

void registerWindowsFrameType()
{
    qmlRegisterType<WindowsFrame>("Pandoeditor.Windowing", 1, 0, "WindowsFrame");
    qmlRegisterType<MapRenderItem>("Pandoeditor.Windowing", 1, 0, "MapRenderItem");
    qmlRegisterType<GpuMapItem>("Pandoeditor.Windowing", 1, 0, "GpuMapItem");
    qmlRegisterType<ReferenceImageItem>("Pandoeditor.Windowing", 1, 0, "ReferenceImageItem");
    qmlRegisterType<GeographicImageItem>("Pandoeditor.Windowing", 1, 0, "GeographicImageItem");
    qmlRegisterType<TerrainLandMaskItem>("Pandoeditor.Windowing", 1, 0, "TerrainLandMaskItem");
    qmlRegisterType<ReferenceImageLibrary>("Pandoeditor.Windowing", 1, 0, "ReferenceImageLibrary");
}

WindowsFrame::WindowsFrame(QObject* parent) : QObject(parent)
{
    qApp->installNativeEventFilter(this);
}

WindowsFrame::~WindowsFrame()
{
    detach();
    qApp->removeNativeEventFilter(this);
}

QFont WindowsFrame::captionFont() const
{
    return QFontDatabase::systemFont(QFontDatabase::TitleFont);
}

void WindowsFrame::setWindow(QQuickWindow* window)
{
    if (m_window == window) return;
    detach();
    if (m_window) m_window->removeEventFilter(this);
    m_window = window;
    if (m_window) m_window->installEventFilter(this);
    emit configurationChanged();
    // All QML geometry references must be assigned before native creation.
    QTimer::singleShot(0, this, &WindowsFrame::attach);
}

void WindowsFrame::setEnabled(bool enabled)
{
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    if (!enabled) detach();
    else QTimer::singleShot(0, this, &WindowsFrame::attach);
    emit configurationChanged();
}

bool WindowsFrame::eventFilter(QObject* object, QEvent* event)
{
    if (object == m_window && event->type() == QEvent::PlatformSurface) {
        auto surface = static_cast<QPlatformSurfaceEvent*>(event);
        if (surface->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
            detach();
        else QTimer::singleShot(0, this, &WindowsFrame::attach);
    } else if (object == m_window && event->type() == QEvent::WinIdChange) {
        QTimer::singleShot(0, this, &WindowsFrame::attach);
    }
    return QObject::eventFilter(object, event);
}

void WindowsFrame::fail(const QString& reason)
{
    detach();
    m_fallbackReason = reason;
    qWarning().noquote() << "WindowsFrame: using system frame:" << reason;
    emit activeChanged();
}

void WindowsFrame::attach()
{
    if (!m_enabled || !m_window || m_attaching) return;
    if (QGuiApplication::platformName() != "windows") {
        m_fallbackReason = QStringLiteral("Native Windows backend unavailable");
        return;
    }
#ifdef Q_OS_WIN
    // A queued SurfaceCreated/WinIdChange callback may outlive destroy().
    // Never let winId() resurrect a deliberately destroyed native surface.
    if (!m_window->handle()) return;
    QScopedValueRollback<bool> guard(m_attaching, true);
    const auto hwnd = reinterpret_cast<HWND>(m_window->winId());
    if (m_active && m_handle == reinterpret_cast<quintptr>(hwnd)) return;
    detach();
    BOOL composition = FALSE;
    if (!hwnd || FAILED(DwmIsCompositionEnabled(&composition)) || !composition) {
        fail(QStringLiteral("DWM composition unavailable"));
        return;
    }
    // Keep Qt's native overlapped window styles. No FramelessWindowHint: this
    // preserves native snap eligibility, system menu and fallback decoration.
    m_handle = reinterpret_cast<quintptr>(hwnd);
    const MARGINS margins{1, 1, 1, 1};
    if (FAILED(DwmExtendFrameIntoClientArea(hwnd, &margins))) {
        fail(QStringLiteral("DwmExtendFrameIntoClientArea failed"));
        return;
    }
    m_active = true;
    m_fallbackReason.clear();
    // DWMWA_WINDOW_CORNER_PREFERENCE is Win11-only. Older Windows safely
    // ignores it; corner policy failure is not a frame initialization failure.
    const DWORD round = 2;
    DwmSetWindowAttribute(hwnd, 33, &round, sizeof(round));
    emit activeChanged();
    if (!SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED))
        fail(QStringLiteral("Native frame refresh failed"));
#endif
}

void WindowsFrame::detach()
{
    const auto handle = m_handle;
    const bool wasActive = m_active;
    m_handle = 0;
    m_active = false;
    setInteraction(0, 0);
#ifdef Q_OS_WIN
    auto hwnd = reinterpret_cast<HWND>(handle);
    if (hwnd && IsWindow(hwnd)) {
        if (GetCapture() == hwnd) ReleaseCapture();
        const MARGINS margins{};
        DwmExtendFrameIntoClientArea(hwnd, &margins);
        const DWORD defaultCorners = 0;
        DwmSetWindowAttribute(hwnd, 33, &defaultCorners, sizeof(defaultCorners));
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
#else
    Q_UNUSED(handle);
#endif
    if (wasActive) emit activeChanged();
}

void WindowsFrame::setInteraction(int hovered, int pressed)
{
    if (m_hovered == hovered && m_pressed == pressed) return;
    m_hovered = hovered;
    m_pressed = pressed;
    emit interactionChanged();
}

void WindowsFrame::invoke(int button)
{
    if (!m_window || m_blocked) return;
    switch (button) {
    case 1: m_window->showMinimized(); break;
    case 2:
        if (m_window->windowState() == Qt::WindowMaximized) m_window->showNormal();
        else m_window->showMaximized();
        break;
    case 3: m_window->close(); break; // Always dispatches QML onClosing.
    }
}

QPointF WindowsFrame::logicalPoint(QPointF point, qreal ratio)
{
    return point / (ratio > 0 ? ratio : 1);
}

WindowsFrame::Hit WindowsFrame::hitTest(QPointF p, QSizeF size, qreal border,
                                       bool maximized, QRectF caption,
                                       const std::array<QRectF, 3>& buttons)
{
    if (!QRectF(QPointF(), size).contains(p)) return Client;
    if (!maximized) {
        const bool left = p.x() < border, right = p.x() >= size.width() - border;
        const bool top = p.y() < border, bottom = p.y() >= size.height() - border;
        if (top && left) return TopLeft;
        if (top && right) return TopRight;
        if (bottom && left) return BottomLeft;
        if (bottom && right) return BottomRight;
        if (left) return Left;
        if (right) return Right;
        if (top) return Top;
        if (bottom) return Bottom;
    }
    if (buttons[0].contains(p)) return Minimize;
    if (buttons[1].contains(p)) return Maximize;
    if (buttons[2].contains(p)) return Close;
    return caption.contains(p) ? Caption : Client;
}

WindowsFrame::Hit WindowsFrame::hitAtNativePoint(long screenX, long screenY) const
{
#ifdef Q_OS_WIN
    if (!m_window || m_blocked) return Client;
    auto hwnd = reinterpret_cast<HWND>(m_handle);
    POINT point{screenX, screenY};
    ScreenToClient(hwnd, &point);
    const qreal ratio = m_window->devicePixelRatio();
    const UINT dpi = GetDpiForWindow(hwnd);
    const qreal border = (GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi)
                         + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi)) / ratio;
    auto rect = [](QQuickItem* item) {
        return item && item->isVisible() && item->isEnabled()
            ? item->mapRectToScene(QRectF(0, 0, item->width(), item->height())) : QRectF();
    };
    return hitTest(logicalPoint(QPointF(point.x, point.y), ratio), m_window->size(),
                   border, IsZoomed(hwnd), rect(m_caption),
                   {rect(m_minimize), rect(m_maximize), rect(m_close)});
#else
    Q_UNUSED(screenX); Q_UNUSED(screenY);
    return Client;
#endif
}

bool WindowsFrame::nativeEventFilter(const QByteArray&, void* message, qintptr* result)
{
#ifdef Q_OS_WIN
    // Qt's Win32 dispatcher passes nullptr for queued input, and deliberately
    // skips the global filter for that input in the HWND procedure. Consuming
    // it here is the single input path; non-input HWND messages have a result.
    qintptr unusedResult = 0;
    if (!result) result = &unusedResult;
    const auto msg = static_cast<MSG*>(message);
    auto hwnd = reinterpret_cast<HWND>(m_handle);
    if (!m_active || msg->hwnd != hwnd) return false;
    auto buttonAt = [this](POINT point) {
        switch (hitAtNativePoint(point.x, point.y)) {
        case Minimize: return 1;
        case Maximize: return 2;
        case Close: return 3;
        default: return 0;
        }
    };
    switch (msg->message) {
    case WM_NCCALCSIZE: {
        // All normal-window pixels belong to Qt; DWM supplies the outside
        // border/shadow. Maximized client area is clipped to this monitor's
        // work area, including taskbars docked on any side.
        auto rect = msg->wParam
            ? &reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam)->rgrc[0]
            : reinterpret_cast<RECT*>(msg->lParam);
        if (IsZoomed(hwnd)) {
            MONITORINFO monitor{sizeof(MONITORINFO)};
            if (GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
                rect->left = std::max(rect->left, monitor.rcWork.left);
                rect->top = std::max(rect->top, monitor.rcWork.top);
                rect->right = std::min(rect->right, monitor.rcWork.right);
                rect->bottom = std::min(rect->bottom, monitor.rcWork.bottom);
            }
        }
        *result = 0;
        return true;
    }
    case WM_NCHITTEST: {
        static const int codes[]{HTCLIENT, HTCAPTION, HTMINBUTTON, HTMAXBUTTON, HTCLOSE,
                                HTLEFT, HTRIGHT, HTTOP, HTBOTTOM,
                                HTTOPLEFT, HTTOPRIGHT, HTBOTTOMLEFT, HTBOTTOMRIGHT};
        *result = codes[hitAtNativePoint(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam))];
        return true;
    }
    case WM_NCMOUSEMOVE: {
        POINT point{GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};
        setInteraction(buttonAt(point), m_pressed);
        TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE | TME_NONCLIENT, hwnd, 0};
        TrackMouseEvent(&tracking);
        // Qt may translate frame-strut mouse events instead of forwarding
        // them. Deliver native hover explicitly so DWM can open Snap Layouts.
        LRESULT dwmResult = 0;
        *result = DwmDefWindowProc(hwnd, msg->message, msg->wParam, msg->lParam, &dwmResult)
            ? dwmResult : DefWindowProc(hwnd, msg->message, msg->wParam, msg->lParam);
        return true;
    }
    case WM_NCMOUSELEAVE: {
        setInteraction(0, m_pressed);
        LRESULT dwmResult = 0;
        *result = DwmDefWindowProc(hwnd, msg->message, msg->wParam, msg->lParam, &dwmResult)
            ? dwmResult : DefWindowProc(hwnd, msg->message, msg->wParam, msg->lParam);
        return true;
    }
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK: {
        const int button = buttonAt({GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)});
        if (!button) break; // Caption/resize tracking belongs entirely to Windows.
        setInteraction(button, button);
        SetCapture(hwnd);
        *result = 0;
        return true;
    }
    case WM_MOUSEMOVE:
        if (m_pressed) {
            POINT point{GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};
            ClientToScreen(hwnd, &point);
            setInteraction(buttonAt(point), m_pressed);
            *result = 0; return true;
        }
        setInteraction(0, 0);
        break;
    case WM_LBUTTONUP:
    case WM_NCLBUTTONUP:
        if (m_pressed) {
            POINT point{GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};
            if (msg->message == WM_LBUTTONUP) ClientToScreen(hwnd, &point);
            const int button = m_pressed;
            const bool activate = buttonAt(point) == button;
            setInteraction(activate ? button : 0, 0);
            ReleaseCapture();
            if (activate) QTimer::singleShot(0, this, [this, button] { invoke(button); });
            *result = 0; return true;
        }
        break;
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE: setInteraction(0, 0); break;
    case WM_SYSCOMMAND:
        if ((msg->wParam & 0xfff0) == SC_CLOSE) {
            QTimer::singleShot(0, this, [this] { invoke(3); });
            *result = 0; return true;
        }
        break;
    case WM_DWMCOMPOSITIONCHANGED: {
        BOOL composition = FALSE;
        if (FAILED(DwmIsCompositionEnabled(&composition)) || !composition) {
            QTimer::singleShot(0, this, [this] { fail(QStringLiteral("DWM composition lost")); });
        }
        break;
    }
    default: break;
    }
#else
    Q_UNUSED(message); Q_UNUSED(result);
#endif
    return false;
}

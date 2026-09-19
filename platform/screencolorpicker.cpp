#include "screencolorpicker.h"
#include <QBackingStore>
#include <QGuiApplication>
#include <QCursor>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QWindow>
#include <algorithm>
#include <functional>
#include <vector>

namespace {
class CaptureWindow final : public QWindow {
public:
    QImage captured;
    QBackingStore store{this};
    std::function<void(const QString&)> picked;
    std::function<void()> abort;
    CaptureWindow(QScreen* screen,QImage image):QWindow(screen),captured(std::move(image)) {
        setObjectName("screenColorPickerOverlay");
        setSurfaceType(QSurface::RasterSurface);
        setFlags(Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint);
        setGeometry(screen->geometry());setCursor(Qt::CrossCursor);
    }
    void paint() {
        if(!isExposed())return;
        store.resize(size());const QRect bounds(QPoint(),size());store.beginPaint(bounds);
        {QPainter p(store.paintDevice());p.drawImage(bounds,captured);}
        store.endPaint();store.flush(bounds);
    }
protected:
    void exposeEvent(QExposeEvent*) override{paint();}
    void resizeEvent(QResizeEvent*) override{paint();}
    void mouseReleaseEvent(QMouseEvent* event) override {
        if(event->button()!=Qt::LeftButton)return;
        const auto pos=event->position();
        if(width()<=0||height()<=0||pos.x()<0||pos.y()<0||pos.x()>=width()||pos.y()>=height())return;
        const int x=std::clamp(int(pos.x()*captured.width()/width()),0,captured.width()-1);
        const int y=std::clamp(int(pos.y()*captured.height()/height()),0,captured.height()-1);
        picked(captured.pixelColor(x,y).name(QColor::HexRgb));
    }
    void keyPressEvent(QKeyEvent* event) override {
        if(event->key()==Qt::Key_Escape||event->key()==Qt::Key_Back){abort();event->accept();}
    }
};
}
struct ScreenColorPicker::Impl {
    std::vector<std::unique_ptr<CaptureWindow>> windows;
    std::vector<QMetaObject::Connection> screenConnections;
    quint64 request=0;
    bool active=false;
    void clear() {
        active=false;
        for(const auto& connection:screenConnections)QObject::disconnect(connection);
        screenConnections.clear();windows.clear();
    }
};
ScreenColorPicker::ScreenColorPicker(QObject* parent):QObject(parent),impl_(std::make_unique<Impl>()) {
    if(auto app=qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
        connect(app,&QGuiApplication::screenRemoved,this,[this](QScreen*){cancel();});
}
ScreenColorPicker::~ScreenColorPicker()=default;
bool ScreenColorPicker::available() const {
    if(!qobject_cast<QGuiApplication*>(QCoreApplication::instance()))return false;
    const auto platform=QGuiApplication::platformName();
    return platform=="xcb" || platform=="windows";
}
bool ScreenColorPicker::busy() const{return impl_->active;}
void ScreenColorPicker::cancel() {
    if(!impl_->active)return;
    ++impl_->request;impl_->clear();emit busyChanged();emit cancelled();
}
bool ScreenColorPicker::start() {
    if(!available()||busy())return false;
    try {
        // Capture every screen BEFORE showing any overlay. Device pixel ratio is
        // retained in each image; local logical positions map through image size.
        std::vector<std::pair<QScreen*,QImage>> captures;
        for(auto screen:QGuiApplication::screens()) {
            auto image=screen->grabWindow(0).toImage();
            if(image.isNull()){emit failed(QStringLiteral("화면의 색상을 가져오지 못했습니다."));return false;}
            captures.emplace_back(screen,std::move(image));
        }
        if(captures.empty())return false;
        const auto request=++impl_->request;
        for(auto& [screen,image]:captures) {
            auto window=std::make_unique<CaptureWindow>(screen,std::move(image));
            window->picked=[this,request](const QString& color){
                // Queue completion so no event handler destroys its own window.
                QTimer::singleShot(0,this,[this,request,color]{
                    if(!impl_->active || impl_->request!=request)return;
                    impl_->clear();emit busyChanged();emit colorSelected(color);
                });
            };
            window->abort=[this,request]{QTimer::singleShot(0,this,[this,request]{if(impl_->request==request)cancel();});};
            impl_->screenConnections.push_back(connect(screen,&QScreen::geometryChanged,this,[this](const QRect&){cancel();}));
            impl_->windows.push_back(std::move(window));
        }
        impl_->active=true;emit busyChanged();
        CaptureWindow* active=nullptr;
        for(auto& window:impl_->windows){window->show();if(window->geometry().contains(QCursor::pos()))active=window.get();}
        if(!active)active=impl_->windows.front().get();
        active->raise();active->requestActivate();return true;
    }catch(const std::exception&){impl_->clear();emit busyChanged();emit failed(QStringLiteral("색상 추출을 시작하지 못했습니다."));return false;}
}

#include "gpumapitem.h"
#include "mapviewstate.h"
#include <QFile>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QMetaObject>
#include <QResource>
#include <cmath>
#include <chrono>

namespace {
qint64 monotonicNanoseconds() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

GpuMapItem::GpuMapItem(QQuickItem* parent):QQuickItem(parent) {
    Q_INIT_RESOURCE(m73_map_shaders);
    setFlag(ItemHasContents,true);
    connect(this,&QQuickItem::windowChanged,this,&GpuMapItem::attachWindow);
    connect(this,&GpuMapItem::viewportChanged,this,&GpuMapItem::update);
    connect(this,&QQuickItem::widthChanged,this,[this] {evaluateBackend();update();});
    connect(this,&QQuickItem::heightChanged,this,[this] {evaluateBackend();update();});
}
bool GpuMapItem::forcedGpu() const {
    return qgetenv("PANDOEDITOR_MAP_RENDERER").trimmed().toLower()=="gpu";
}
void GpuMapItem::setSceneBridge(QObject* value) {
    auto* next=qobject_cast<MapSceneBridge*>(value);
    if(bridge_==next)return;
    if(bridge_)disconnect(bridge_,nullptr,this,nullptr);
    bridge_=next;
    if(bridge_) {
        connect(bridge_,&MapSceneBridge::sceneChanged,this,[this] {evaluateBackend();update();});
        connect(bridge_,&MapSceneBridge::viewChanged,this,&GpuMapItem::update);
        connect(bridge_,&QObject::destroyed,this,[this]{bridge_=nullptr;setStatus(false,QStringLiteral("Scene bridge unavailable"));});
    }
    evaluateBackend();emit sceneBridgeChanged();update();
}
void GpuMapItem::setContentReady(bool value) {
    if(contentReady_==value)return;
    contentReady_=value;
    emit rendererReadyChanged();update();
}
#define MAP_VIEW_SETTER(Name,Member) \
void GpuMapItem::set##Name(double value) { \
    if(!std::isfinite(value)||flat_.Member==value)return; \
    flat_.Member=float(value);emit viewportChanged(); \
}
MAP_VIEW_SETTER(OriginX,originX)
MAP_VIEW_SETTER(OriginY,originY)
MAP_VIEW_SETTER(MapScale,mapScale)
MAP_VIEW_SETTER(MapCosLatitude,cosLatitude)
MAP_VIEW_SETTER(MapMinX,minX)
MAP_VIEW_SETTER(MapMaxLatitude,maxLatitude)
#undef MAP_VIEW_SETTER

void GpuMapItem::setStatus(bool ready,const QString& reason) {
    if(ready_==ready&&diagnostic_==reason)return;
    ready_=ready;diagnostic_=reason;
    emit rendererReadyChanged();update();
}
void GpuMapItem::attachWindow(QQuickWindow* next) {
    if(connectedWindow_)disconnect(connectedWindow_,nullptr,this,nullptr);
    connectedWindow_=next;
    frameStartNs_.store(0);
    setStatus(false,QStringLiteral("Waiting for Qt Quick scene graph"));
    if(!next)return;
    connect(next,&QQuickWindow::sceneGraphInitialized,this,
            &GpuMapItem::evaluateBackend,Qt::QueuedConnection);
    connect(next,&QQuickWindow::sceneGraphInvalidated,this,[this] {
        setStatus(false,QStringLiteral("Scene graph invalidated"));
    },Qt::QueuedConnection);
    connect(next,&QQuickWindow::sceneGraphError,this,[this](QQuickWindow::SceneGraphError,
                                                            const QString& message) {
        setStatus(false,QStringLiteral("GPU scene graph error: ")+message);
    },Qt::QueuedConnection);
    connect(next,&QQuickWindow::frameSwapped,this,[this] {
        const auto started=frameStartNs_.exchange(0);
        if(!rendererReady()||!isVisible()||started<=0)return;
        const auto elapsed=double(monotonicNanoseconds()-started)/1000000.;
        if(elapsed>0&&elapsed<500)emit frameSampled(elapsed);
    },Qt::QueuedConnection);
    evaluateBackend();
}
void GpuMapItem::evaluateBackend() {
    const auto policy=qgetenv("PANDOEDITOR_MAP_RENDERER").trimmed().toLower();
    if(policy=="cpu") {setStatus(false,QStringLiteral("CPU renderer forced"));return;}
    if(!window()||!bridge_||!bridge_->sceneSnapshot()) {
        setStatus(false,QStringLiteral("Waiting for map scene"));return;
    }
    for(const char* name:{"fill","stroke","point"}) {
        if(!QFile::exists(QStringLiteral(":/m73/shaders/")+name+".vert.qsb")||
           !QFile::exists(QStringLiteral(":/m73/shaders/")+name+".frag.qsb")) {
            setStatus(false,QStringLiteral("GPU shader resources unavailable"));return;
        }
    }
    const auto api=window()->rendererInterface()->graphicsApi();
    if(api==QSGRendererInterface::Software||api==QSGRendererInterface::Unknown) {
        setStatus(false,policy=="gpu"?
            QStringLiteral("GPU renderer forced but no graphics backend is available"):
            QStringLiteral("Qt Quick software backend; CPU fallback"));return;
    }
    setStatus(true,QString());
}
QSGNode* GpuMapItem::updatePaintNode(QSGNode* previous,UpdatePaintNodeData*) {
    frameStartNs_.store(monotonicNanoseconds());
    if(!rendererReady()||!bridge_||!bridge_->sceneSnapshot()) {
        delete previous;return nullptr;
    }
    auto* node=previous?static_cast<MapSceneNode*>(previous):new MapSceneNode;
    auto view=bridge_->viewState();
    // The engine-owned camera snapshot is authoritative. Qt only contributes
    // the actual framebuffer DPR for backend-specific rasterization.
    view.devicePixelRatio=window()?window()->devicePixelRatio():view.devicePixelRatio;
    if(!validMapViewState(view)) {
        delete node;
        QMetaObject::invokeMethod(this,[this] {
            setStatus(false,QStringLiteral("Invalid GPU map viewport"));
        },Qt::QueuedConnection);
        return nullptr;
    }
    node->sync(bridge_->sceneSnapshot(),view,flat_,renderStats_,uploadBudgetBytes_);
    if(renderStats_.uploadsPending)
        QMetaObject::invokeMethod(this,[this] {update();},Qt::QueuedConnection);
    const auto current=renderStats_;
    QMetaObject::invokeMethod(this,[this,current] {publishedStats_=current;emit statsChanged();},Qt::QueuedConnection);
    return node;
}

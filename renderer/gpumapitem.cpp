#include "gpumapitem.h"
#include <pandoeditor/map/mapviewstate.h>
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
    if(window())attachWindow(window());
}
bool GpuMapItem::forcedGpu() const {
    return qgetenv("PANDOEDITOR_MAP_RENDERER").trimmed().toLower()=="gpu";
}
GpuMapItem::~GpuMapItem() {
    // QQuickItem's base destructor can emit windowChanged after this class is gone.
    disconnect(this,nullptr,this,nullptr);
    if(connectedWindow_)disconnect(connectedWindow_,nullptr,this,nullptr);
    if(bridge_)disconnect(bridge_,nullptr,this,nullptr);
    bridgeGeneration_.fetch_add(1);windowGeneration_.fetch_add(1);resourceGeneration_.fetch_add(1);
    uploadContinuationQueued_.store(false);frameStartNs_.store(0);
}
void GpuMapItem::setSceneBridge(QObject* value) {
    auto* next=qobject_cast<MapSceneBridge*>(value);
    if(bridge_==next)return;
    if(bridge_)disconnect(bridge_,nullptr,this,nullptr);
    bridge_=next;
    const auto bridgeGeneration=bridgeGeneration_.fetch_add(1)+1;
    resourceGeneration_.fetch_add(1);
    uploadContinuationQueued_.store(false);frameStartNs_.store(0);
    publishedStats_={};emit statsChanged();
    if(bridge_) {
        connect(bridge_,&MapSceneBridge::sceneChanged,this,[this] {evaluateBackend();update();});
        connect(bridge_,&MapSceneBridge::viewChanged,this,&GpuMapItem::update);
        connect(bridge_,&QObject::destroyed,this,[this,bridgeGeneration]{
            if(bridgeGeneration_.load()!=bridgeGeneration)return;
            resourceGeneration_.fetch_add(1);uploadContinuationQueued_.store(false);
            bridge_=nullptr;publishedStats_={};emit statsChanged();
            setStatus(false,QStringLiteral("Scene bridge unavailable"));
        });
    }
    evaluateBackend();emit sceneBridgeChanged();update();
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
    const auto windowGeneration=windowGeneration_.fetch_add(1)+1;
    resourceGeneration_.fetch_add(1);uploadContinuationQueued_.store(false);
    publishedStats_={};emit statsChanged();
    frameStartNs_.store(0);
    setStatus(false,QStringLiteral("Waiting for Qt Quick scene graph"));
    if(!next)return;
    connect(next,&QQuickWindow::sceneGraphInitialized,this,[this,windowGeneration] {
        if(windowGeneration_.load()!=windowGeneration)return;
        const auto generation=resourceGeneration_.fetch_add(1)+1;
        uploadContinuationQueued_.store(false);frameStartNs_.store(0);
        QMetaObject::invokeMethod(this,[this,windowGeneration,generation] {
            if(windowGeneration_.load()!=windowGeneration||resourceGeneration_.load()!=generation)return;
            publishedStats_={};emit statsChanged();evaluateBackend();update();
        },Qt::QueuedConnection);
    },Qt::DirectConnection);
    connect(next,&QQuickWindow::sceneGraphInvalidated,this,[this,windowGeneration] {
        if(windowGeneration_.load()!=windowGeneration)return;
        const auto generation=resourceGeneration_.fetch_add(1)+1;
        uploadContinuationQueued_.store(false);frameStartNs_.store(0);
        QMetaObject::invokeMethod(this,[this,windowGeneration,generation] {
            if(windowGeneration_.load()!=windowGeneration||resourceGeneration_.load()!=generation)return;
            publishedStats_={};emit statsChanged();setStatus(false,QStringLiteral("Scene graph invalidated"));
        },Qt::QueuedConnection);
    },Qt::DirectConnection);
    connect(next,&QQuickWindow::sceneGraphError,this,[this,windowGeneration](QQuickWindow::SceneGraphError,
                                                            const QString& message) {
        if(windowGeneration_.load()!=windowGeneration)return;
        const auto generation=resourceGeneration_.load();
        QMetaObject::invokeMethod(this,[this,windowGeneration,generation,message] {
            if(windowGeneration_.load()!=windowGeneration||resourceGeneration_.load()!=generation)return;
            setStatus(false,QStringLiteral("GPU scene graph error: ")+message);
        },Qt::QueuedConnection);
    },Qt::DirectConnection);
    connect(next,&QQuickWindow::frameSwapped,this,[this,windowGeneration] {
        if(windowGeneration_.load()!=windowGeneration)return;
        const auto generation=resourceGeneration_.load();
        const auto started=frameStartNs_.exchange(0);
        if(started<=0)return;
        const auto elapsed=double(monotonicNanoseconds()-started)/1000000.;
        QMetaObject::invokeMethod(this,[this,windowGeneration,generation,elapsed] {
            if(windowGeneration_.load()!=windowGeneration||resourceGeneration_.load()!=generation)return;
            if(rendererReady()&&isVisible()&&elapsed>0)emit frameSampled(elapsed);
        },Qt::QueuedConnection);
    },Qt::DirectConnection);
    evaluateBackend();
}
void GpuMapItem::evaluateBackend() {
    const auto policy=qgetenv("PANDOEDITOR_MAP_RENDERER").trimmed().toLower();
    if(policy=="cpu") {setStatus(false,QStringLiteral("CPU renderer forced"));return;}
    if(!window()||!bridge_||!bridge_->sceneSnapshot()) {
        setStatus(false,QStringLiteral("Waiting for map scene"));return;
    }
    const auto scene=bridge_->sceneSnapshot();
    if(scene)for(const auto& draw:scene->drawSequence)
        if((draw.primitive==PrimitiveKind::Polygon||draw.primitive==PrimitiveKind::WorldFill)&&
           draw.layerOpacity<.999f) {
            if(policy=="gpu") {
                setStatus(false,QStringLiteral("GPU renderer forced but layer group compositing requires CPU fallback"));
            } else {
                setStatus(false,QStringLiteral("Layer group compositing uses CPU fallback"));
            }
            return;
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
    const auto generation=resourceGeneration_.load();
    if(renderGeneration_!=generation) {
        delete previous;previous=nullptr;renderStats_={};renderGeneration_=generation;
    }
    frameStartNs_.store(monotonicNanoseconds());
    const auto retireCurrent=[&](QSGNode* node) {
        if(node)renderStats_.resourceRetirementCount+=std::size_t(node->childCount());
        delete node;
        renderStats_.sceneRevision=0;renderStats_.geometryBytes=0;renderStats_.strokeBytes=0;
        renderStats_.drawNodes=0;renderStats_.visibleCountryCount=0;
        renderStats_.drawIndexCount=0;renderStats_.fullIndexCount=0;
        renderStats_.liveResourceCount=0;renderStats_.liveResourceBytes=0;
        renderStats_.uploadBytesThisFrame=0;renderStats_.uploadsPending=false;
        uploadContinuationQueued_.store(false);
    };
    const auto frame=bridge_?bridge_->frameSnapshot():nullptr;
    if(!rendererReady()||!frame||!frame->scene) {
        retireCurrent(previous);
        const auto cleared=renderStats_;
        QMetaObject::invokeMethod(this,[this,generation,cleared] {
            if(resourceGeneration_.load()!=generation)return;
            publishedStats_=cleared;emit statsChanged();
        },Qt::QueuedConnection);
        return nullptr;
    }
    auto* node=previous?static_cast<MapSceneNode*>(previous):new MapSceneNode;
    auto view=frame->view;
    // The engine-owned camera snapshot is authoritative. Qt only contributes
    // the actual framebuffer DPR for backend-specific rasterization.
    view.devicePixelRatio=window()?window()->devicePixelRatio():view.devicePixelRatio;
    if(!validMapViewState(view)) {
        retireCurrent(node);
        const auto cleared=renderStats_;
        QMetaObject::invokeMethod(this,[this,generation,cleared] {
            if(resourceGeneration_.load()!=generation)return;
            publishedStats_=cleared;emit statsChanged();
            setStatus(false,QStringLiteral("Invalid GPU map viewport"));
        },Qt::QueuedConnection);
        return nullptr;
    }
    node->sync(frame->scene,view,flat_,renderStats_,uploadBudgetBytes_,frame->worldPlan.get());
    if(renderStats_.uploadsPending&&!uploadContinuationQueued_.exchange(true))
        QMetaObject::invokeMethod(this,[this,generation] {
            if(resourceGeneration_.load()!=generation)return;
            uploadContinuationQueued_.store(false);
            if(!rendererReady())return;
            ++uploadContinuations_;update();
        },Qt::QueuedConnection);
    const auto current=renderStats_;
    QMetaObject::invokeMethod(this,[this,current,generation] {
        if(resourceGeneration_.load()!=generation)return;
        publishedStats_=current;emit statsChanged();
    },Qt::QueuedConnection);
    return node;
}

QVariantMap GpuMapItem::resourceCacheStats() const {
    return {{"liveResourceCount",qulonglong(publishedStats_.liveResourceCount)},
        {"liveResourceBytes",qulonglong(publishedStats_.liveResourceBytes)},
        {"resourceCreationCount",qulonglong(publishedStats_.resourceCreationCount)},
        {"resourceRetirementCount",qulonglong(publishedStats_.resourceRetirementCount)},
        {"uploadsPending",publishedStats_.uploadsPending},
        {"geometryUploadCount",qulonglong(publishedStats_.geometryUploadCount)},
        {"uploadBytesThisFrame",qulonglong(publishedStats_.uploadBytesThisFrame)}};
}

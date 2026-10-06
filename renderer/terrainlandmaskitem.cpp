#include "terrainlandmaskitem.h"
#include "scenegraph/mapmaterial.h"
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGTexture>
#include <QSGTextureProvider>
#include <QMetaObject>
#include <QResource>
#include <algorithm>
#include <cmath>
#include <climits>
#include <limits>
#include <map>
#include <stdexcept>

namespace {
bool sameView(const MapViewState& a,const MapViewState& b) {
    return a.revision==b.revision&&advanceViewRevision(a,b).revision==a.revision;
}
bool sameFlat(const MapFlatViewport& a,const MapFlatViewport& b) {
    return a.originX==b.originX&&a.originY==b.originY&&a.mapScale==b.mapScale&&
        a.cosLatitude==b.cosLatitude&&a.minX==b.minX&&a.maxLatitude==b.maxLatitude;
}
bool sameFrame(const TerrainLandMaskFrame& a,const TerrainLandMaskFrame& b) {
    return a.ready==b.ready&&a.viewRevision==b.viewRevision&&a.sceneRevision==b.sceneRevision&&
        a.resourceGeneration==b.resourceGeneration&&a.logicalSize==b.logicalSize&&
        a.textureSize==b.textureSize&&a.uvTransform==b.uvTransform;
}
struct MaskBuffer {
    std::shared_ptr<const CountryBaseMesh> base;
    std::vector<std::size_t> baseSlots;
    std::shared_ptr<const std::vector<float>> positions;
    std::shared_ptr<const std::vector<std::uint32_t>> flatIndices,globeIndices;
    std::shared_ptr<QSGGeometry> flat,globe;
    std::size_t bytes=0;
};
std::shared_ptr<QSGGeometry> geometry(const std::vector<float>& positions,
                                     const std::vector<std::uint32_t>& indices) {
    if(positions.size()%2||positions.size()/2>std::size_t(INT_MAX)||indices.size()>std::size_t(INT_MAX))
        throw std::invalid_argument("invalid physical mask buffer size");
    const auto vertices=positions.size()/2;
    for(const auto index:indices)if(index>=vertices)throw std::invalid_argument("physical mask index out of range");
    auto result=std::make_shared<QSGGeometry>(QSGGeometry::defaultAttributes_Point2D(),
        int(vertices),int(indices.size()),QSGGeometry::UnsignedIntType);
    result->setDrawingMode(QSGGeometry::DrawTriangles);
    result->setVertexDataPattern(QSGGeometry::StaticPattern);
    result->setIndexDataPattern(QSGGeometry::StaticPattern);
    auto* target=result->vertexDataAsPoint2D();
    for(std::size_t i=0;i<vertices;++i)target[i].set(positions[i*2],positions[i*2+1]);
    std::copy(indices.begin(),indices.end(),result->indexDataAsUInt());
    return result;
}
struct MaskDrawNode final : QSGGeometryNode {
    std::shared_ptr<MaskBuffer> retained;
    explicit MaskDrawNode(std::shared_ptr<MaskBuffer> buffer):retained(std::move(buffer)) {
        setMaterial(new MapMaterial(MapPrimitive::Fill,BlendMode::Normal));setFlag(OwnsMaterial,true);
    }
};
struct MaskRoot final : QSGNode {
    std::map<std::string,std::shared_ptr<MaskBuffer>> buffers;
    std::map<std::string,MaskDrawNode*> draws;
    std::size_t bytes=0;
    qulonglong uploads=0;
    void sync(const PhysicalLandMaskPacket& packet,const MapViewState& view,
              const MapFlatViewport& flat,const std::vector<double>& offsets) {
        std::map<std::string,std::shared_ptr<MaskBuffer>> next;
        if(packet.worldBase&&packet.worldBase->mesh&&!packet.baseSlots.empty()) {
            const auto mesh=packet.worldBase->mesh;
            const auto old=buffers.find("base");
            std::shared_ptr<MaskBuffer> buffer;
            if(old!=buffers.end()&&old->second->base==mesh&&old->second->baseSlots==packet.baseSlots)
                buffer=old->second;
            else {
                buffer=std::make_shared<MaskBuffer>();buffer->base=mesh;buffer->baseSlots=packet.baseSlots;
                std::vector<float> positions;positions.reserve(mesh->positionsMicrodegrees.size());
                for(const auto coordinate:mesh->positionsMicrodegrees)positions.push_back(float(coordinate)/1000000.f);
                std::vector<std::uint32_t> indices;
                for(const auto slot:packet.baseSlots) {
                    if(slot*2+1>=mesh->countryTriangleRanges.size())throw std::invalid_argument("physical mask slot out of range");
                    const auto first=mesh->countryTriangleRanges[slot*2],count=mesh->countryTriangleRanges[slot*2+1];
                    if(std::size_t(first)+count>mesh->triangleIndices.size())throw std::invalid_argument("physical mask range out of bounds");
                    indices.insert(indices.end(),mesh->triangleIndices.begin()+first,mesh->triangleIndices.begin()+first+count);
                }
                buffer->flat=geometry(positions,indices);buffer->globe=buffer->flat;++uploads;
                buffer->bytes=positions.size()*sizeof(float)+indices.size()*sizeof(std::uint32_t);
            }
            next.emplace("base",std::move(buffer));
        }
        for(const auto& polygon:packet.polygons) {
            const auto& source=polygon.geometryPacket;
            if(!source.positions||!source.indices||!source.globeIndices)throw std::invalid_argument("incomplete physical polygon packet");
            const auto key="polygon/"+polygon.key;
            const auto old=buffers.find(key);std::shared_ptr<MaskBuffer> buffer;
            if(old!=buffers.end()&&old->second->positions==source.positions&&
               old->second->flatIndices==source.indices&&old->second->globeIndices==source.globeIndices)
                buffer=old->second;
            else {
                buffer=std::make_shared<MaskBuffer>();buffer->positions=source.positions;
                buffer->flatIndices=source.indices;buffer->globeIndices=source.globeIndices;
                buffer->flat=geometry(*source.positions,*source.indices);++uploads;
                buffer->bytes=source.positions->size()*sizeof(float)+source.indices->size()*sizeof(std::uint32_t);
                if(source.indices==source.globeIndices)buffer->globe=buffer->flat;
                else {
                    buffer->globe=geometry(*source.positions,*source.globeIndices);++uploads;
                    buffer->bytes+=source.positions->size()*sizeof(float)+source.globeIndices->size()*sizeof(std::uint32_t);
                }
            }
            next.emplace(key,std::move(buffer));
        }
        std::map<std::string,MaskDrawNode*> active;
        const RenderStyle white{0xffffff,1};
        for(const auto& entry:next)for(const auto offset:offsets) {
            const auto key=entry.first+"/"+std::to_string(offset);
            const auto found=draws.find(key);
            MaskDrawNode* node=found!=draws.end()?found->second:nullptr;
            if(node&&node->retained!=entry.second) {removeChildNode(node);delete node;node=nullptr;}
            if(!node) {node=new MaskDrawNode(entry.second);appendChildNode(node);}
            auto* target=(view.mode==ProjectionMode::Globe?entry.second->globe:entry.second->flat).get();
            if(node->geometry()!=target) {node->setGeometry(target);node->markDirty(QSGNode::DirtyGeometry);}
            auto* material=static_cast<MapMaterial*>(node->material());
            material->setStyle(white);
            material->setView(view,flat.originX,flat.originY,flat.mapScale,flat.cosLatitude,
                              flat.minX,flat.maxLatitude,float(offset));
            node->markDirty(QSGNode::DirtyMaterial);active.emplace(key,node);
        }
        for(const auto& entry:draws)if(!active.count(entry.first)) {removeChildNode(entry.second);delete entry.second;}
        draws=std::move(active);buffers=std::move(next);bytes=0;
        for(const auto& entry:buffers)bytes+=entry.second->bytes;
    }
};
}

struct TerrainLandMaskItem::RenderState {
    qulonglong generation=0;
    std::shared_ptr<const MapFrame> submitted;
    MapFlatViewport flat;
    QSizeF logicalSize;
    QVector4D uvTransform;
    double dpr=1;
    std::shared_ptr<const PhysicalLandMaskPacket> authenticatedPacket;
    MapViewState authenticatedView;
    MapFlatViewport authenticatedFlat;
    QSizeF authenticatedSize;
    double authenticatedDpr=0;
    QPointer<QSGTextureProvider> provider;
    QSGTexture* texture=nullptr;
    TerrainLandMaskFrame authenticated;
};

TerrainLandMaskItem::TerrainLandMaskItem(QQuickItem* parent):QQuickItem(parent) {
    Q_INIT_RESOURCE(m73_map_shaders);
    setFlag(ItemHasContents,true);
    connect(this,&QQuickItem::windowChanged,this,&TerrainLandMaskItem::attachWindow);
    connect(this,&TerrainLandMaskItem::viewportChanged,this,&TerrainLandMaskItem::update);
    connect(this,&QQuickItem::widthChanged,this,&TerrainLandMaskItem::update);
    connect(this,&QQuickItem::heightChanged,this,&TerrainLandMaskItem::update);
    if(window())attachWindow(window());
}
TerrainLandMaskItem::~TerrainLandMaskItem() {
    disconnect(this,nullptr,this,nullptr);
    if(connectedWindow_)disconnect(connectedWindow_,nullptr,this,nullptr);
    if(bridge_)disconnect(bridge_,nullptr,this,nullptr);
    if(textureSource_)disconnect(textureSource_,nullptr,this,nullptr);
    resourceGeneration_.fetch_add(1);
}
void TerrainLandMaskItem::invalidateResources() {
    resourceGeneration_.fetch_add(1);geometryUploadCount_.store(0);geometryBytes_.store(0);
    publishPreparedFrame({});
    QMetaObject::invokeMethod(this,[this]{emit statsChanged();update();},Qt::QueuedConnection);
}
void TerrainLandMaskItem::attachWindow(QQuickWindow* next) {
    if(connectedWindow_)disconnect(connectedWindow_,nullptr,this,nullptr);
    connectedWindow_=next;invalidateResources();
    if(!next)return;
    connect(next,&QQuickWindow::sceneGraphInvalidated,this,&TerrainLandMaskItem::invalidateResources,Qt::DirectConnection);
    connect(next,&QQuickWindow::sceneGraphInitialized,this,&TerrainLandMaskItem::invalidateResources,Qt::DirectConnection);
}
void TerrainLandMaskItem::setSceneBridge(QObject* object) {
    auto* next=qobject_cast<MapSceneBridge*>(object);if(bridge_==next)return;
    if(bridge_)disconnect(bridge_,nullptr,this,nullptr);
    bridge_=next;invalidateResources();
    if(bridge_) {
        connect(bridge_,&MapSceneBridge::sceneChanged,this,&TerrainLandMaskItem::update);
        connect(bridge_,&MapSceneBridge::viewChanged,this,&TerrainLandMaskItem::update);
        connect(bridge_,&QObject::destroyed,this,[this]{bridge_=nullptr;invalidateResources();});
    }
    emit sceneBridgeChanged();update();
}
void TerrainLandMaskItem::setTextureSource(QQuickItem* source) {
    if(textureSource_==source)return;
    if(textureSource_)disconnect(textureSource_,nullptr,this,nullptr);
    textureSource_=source;invalidateResources();
    if(textureSource_)connect(textureSource_,&QObject::destroyed,this,[this] {
        textureSource_=nullptr;invalidateResources();emit textureSourceChanged();
    });
    emit textureSourceChanged();update();
}
bool TerrainLandMaskItem::isTextureProvider() const {
    return textureSource_&&textureSource_->isTextureProvider();
}
QSGTextureProvider* TerrainLandMaskItem::textureProvider() const {
    return isTextureProvider()?textureSource_->textureProvider():nullptr;
}
#define MASK_VIEW_SETTER(Name,Member) \
void TerrainLandMaskItem::set##Name(double value) { \
    if(!std::isfinite(value)||!std::isfinite(float(value))||flat_.Member==float(value))return; \
    flat_.Member=float(value);emit viewportChanged(); \
}
MASK_VIEW_SETTER(OriginX,originX)
MASK_VIEW_SETTER(OriginY,originY)
MASK_VIEW_SETTER(MapScale,mapScale)
MASK_VIEW_SETTER(MapCosLatitude,cosLatitude)
MASK_VIEW_SETTER(MapMinX,minX)
MASK_VIEW_SETTER(MapMaxLatitude,maxLatitude)
#undef MASK_VIEW_SETTER
void TerrainLandMaskItem::setMaskUvTransform(QVector4D value) {
    for(int i=0;i<4;++i)if(!std::isfinite(value[i]))return;
    if(value==uvTransform_)return;uvTransform_=value;emit viewportChanged();
}
void TerrainLandMaskItem::publishPreparedFrame(const TerrainLandMaskFrame& frame) {
    const auto before=std::atomic_load(&publishedFrame_);
    if(before&&sameFrame(*before,frame))return;
    std::atomic_store(&publishedFrame_,std::make_shared<const TerrainLandMaskFrame>(frame));
    QMetaObject::invokeMethod(this,[this]{emit maskReadyChanged();},Qt::QueuedConnection);
}
bool TerrainLandMaskItem::maskReady() const {const auto f=std::atomic_load(&publishedFrame_);return f&&f->ready;}
qulonglong TerrainLandMaskItem::maskViewRevision() const {const auto f=std::atomic_load(&publishedFrame_);return f?f->viewRevision:0;}
qulonglong TerrainLandMaskItem::maskSceneRevision() const {const auto f=std::atomic_load(&publishedFrame_);return f?f->sceneRevision:0;}
QSize TerrainLandMaskItem::maskPixelSize() const {const auto f=std::atomic_load(&publishedFrame_);return f?f->textureSize:QSize{};}

QSGNode* TerrainLandMaskItem::updatePaintNode(QSGNode* previous,UpdatePaintNodeData*) {
    const auto generation=resourceGeneration_.load();
    if(!renderState_||renderState_->generation!=generation) {
        delete previous;previous=nullptr;renderState_=std::make_unique<RenderState>();renderState_->generation=generation;
    }
    const auto frame=bridge_?bridge_->frameSnapshot():nullptr;
    const auto api=window()?window()->rendererInterface()->graphicsApi():QSGRendererInterface::Unknown;
    if(!window()||window()->format().samples()>1||api==QSGRendererInterface::Software||api==QSGRendererInterface::Unknown||
       !frame||!frame->scene||!frame->scene->physicalLandMask||width()<=0||height()<=0||
       std::abs(width()-frame->view.viewportWidth)>1e-6||std::abs(height()-frame->view.viewportHeight)>1e-6) {
        delete previous;renderState_->submitted.reset();renderState_->authenticated.ready=false;
        geometryBytes_.store(0);publishPreparedFrame({});return nullptr;
    }
    auto* root=previous?static_cast<MaskRoot*>(previous):new MaskRoot;
    auto view=frame->view;view.devicePixelRatio=window()->devicePixelRatio();
    const auto offsets=frame->worldPlan?frame->worldPlan->worldOffsets:visibleFlatWorldOffsets(view);
    try {root->sync(*frame->scene->physicalLandMask,view,flat_,offsets);}
    catch(const std::exception&) {
        delete root;renderState_->submitted.reset();renderState_->authenticated.ready=false;
        geometryBytes_.store(0);publishPreparedFrame({});return nullptr;
    }
    renderState_->submitted=frame;renderState_->flat=flat_;
    renderState_->logicalSize=QSizeF(width(),height());renderState_->uvTransform=uvTransform_;
    renderState_->dpr=view.devicePixelRatio;
    const auto oldUploads=geometryUploadCount_.exchange(root->uploads),oldBytes=geometryBytes_.exchange(root->bytes);
    if(oldUploads!=root->uploads||oldBytes!=root->bytes)
        QMetaObject::invokeMethod(this,[this,generation] {
            if(resourceGeneration_.load()==generation)emit statsChanged();
        },Qt::QueuedConnection);
    return root;
}

void TerrainLandMaskItem::noteRenderThreadTextureUse(QSGTextureProvider* provider,QSGTexture* texture,
                                                     bool updatedThisFrame) {
    if(!renderState_)return;
    auto& state=*renderState_;const auto frame=state.submitted;
    const auto current=bridge_?bridge_->frameSnapshot():nullptr;
    const bool currentSource=state.generation==resourceGeneration_.load()&&frame&&current&&
        frame->scene==current->scene&&sameView(frame->view,current->view)&&
        provider&&texture&&provider==textureProvider()&&texture==provider->texture()&&
        texture->rhiTexture()&&texture->textureSize().width()>0&&texture->textureSize().height()>0;
    const bool clean=currentSource&&state.authenticated.ready&&state.provider==provider&&state.texture==texture&&
        state.authenticatedPacket==frame->scene->physicalLandMask&&sameView(state.authenticatedView,frame->view)&&
        sameFlat(state.authenticatedFlat,state.flat)&&state.authenticatedSize==state.logicalSize&&
        state.authenticatedDpr==state.dpr&&state.authenticated.textureSize==texture->textureSize()&&
        state.authenticated.resourceGeneration==state.generation;
    if(!currentSource||(!updatedThisFrame&&!clean)) {
        state.authenticated.ready=false;publishPreparedFrame({});return;
    }
    state.authenticatedPacket=frame->scene->physicalLandMask;state.authenticatedView=frame->view;
    state.authenticatedFlat=state.flat;state.authenticatedSize=state.logicalSize;state.authenticatedDpr=state.dpr;
    state.provider=provider;state.texture=texture;
    auto& ready=state.authenticated;ready.ready=true;ready.viewRevision=frame->view.revision;
    ready.sceneRevision=frame->scene->revision;ready.resourceGeneration=state.generation;
    ready.logicalSize=state.logicalSize;ready.textureSize=texture->textureSize();
    const auto rect=texture->normalizedTextureSubRect();const auto uv=state.uvTransform;
    ready.uvTransform={float(rect.width())*uv.x(),float(rect.height())*uv.y(),
        float(rect.x()+rect.width()*uv.z()),float(rect.y()+rect.height()*uv.w())};
    publishPreparedFrame(ready);
}
TerrainLandMaskFrame TerrainLandMaskItem::renderThreadMaskFrame() const {
    if(!renderState_)return {};
    const auto& state=*renderState_;const auto current=bridge_?bridge_->frameSnapshot():nullptr;
    if(!state.authenticated.ready||state.generation!=resourceGeneration_.load()||!current||
       !state.submitted||current->scene!=state.submitted->scene||
       !sameView(current->view,state.submitted->view)||!state.provider||!state.texture||
       state.provider->texture()!=state.texture||
       state.authenticatedPacket!=current->scene->physicalLandMask)return {};
    return state.authenticated;
}

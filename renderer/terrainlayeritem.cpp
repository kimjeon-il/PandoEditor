#include "terrainlayeritem.h"
#include "terrainrenderowner.h"
#include "terraingridmesh.h"
#include "scenegraph/terrainmaterial.h"
#include "terrainlandmaskitem.h"
#include "terrainrendercontract.h"
#include <pandoeditor/map/projectionengine.h>
#include <QSGGeometryNode>
#include <QSGTextureProvider>
#include <QQuickWindow>
#include <algorithm>
#include <map>
#include <memory>
#include <tuple>
#include <limits>
#include <rhi/qrhi.h>

namespace {
std::atomic<quint64> nextWindowEpoch{1};
using DrawKey=std::tuple<QString,QString,double,bool,bool,int,int>;
bool actualTextureReady(QSGTexture* texture) {
    auto* backing=texture?texture->rhiTexture():nullptr;
    return backing&&backing->nativeTexture().object!=0&&backing->pixelSize()==texture->textureSize();
}
class DrawNode final : public QObject,public QSGGeometryNode {
public:
    DrawNode() {
        setFlag(QSGNode::UsePreprocess,true);
        setGeometry(new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(),0,0,QSGGeometry::UnsignedIntType));
        setFlag(QSGNode::OwnsGeometry,true);setMaterial(new TerrainMaterial);setFlag(QSGNode::OwnsMaterial,true);
    }
    ~DrawNode() override{disconnect(maskChanged);disconnect(maskDestroyed);}
    TerrainMaterial* terrainMaterial() const{return static_cast<TerrainMaterial*>(material());}
    QPointer<TerrainLandMaskItem> mask;
    QPointer<QSGTextureProvider> provider;
    QMetaObject::Connection maskChanged,maskDestroyed;
    quint64 viewRevision=0,sceneRevision=0;
    QSizeF logical;
    bool requiresMask=false,probe=false,underlay=false;
    bool currentMask() const{return !requiresMask||terrainMaterial()->maskState.x()>0.5f;}
    void preprocess() override {
        auto* m=terrainMaterial();m->setLandMask(nullptr,logical,{1,1,0,0},false);
        if(probe||!requiresMask)return;
        auto* next=mask?mask->textureProvider():nullptr;
        if(next!=provider) {
            disconnect(maskChanged);disconnect(maskDestroyed);provider=next;
            if(next) {
                maskChanged=connect(next,&QSGTextureProvider::textureChanged,this,[this]{markDirty(QSGNode::DirtyMaterial);},Qt::DirectConnection);
                maskDestroyed=connect(next,&QObject::destroyed,this,[this]{provider=nullptr;
                    terrainMaterial()->setLandMask(nullptr,logical,{1,1,0,0},false);markDirty(QSGNode::DirtyMaterial);},Qt::DirectConnection);
            }
        }
        if(provider&&mask) {
            auto* texture=provider->texture();bool updated=false;
            if(auto* dynamic=qobject_cast<QSGDynamicTexture*>(texture))updated=dynamic->updateTexture();
            texture=provider->texture();mask->noteRenderThreadTextureUse(provider,texture,updated);
            const auto frame=mask->renderThreadMaskFrame();
            const bool ready=TerrainRenderContract::maskCurrent(frame.ready,actualTextureReady(texture),
                {frame.viewRevision,frame.sceneRevision,frame.logicalSize,frame.textureSize},
                {viewRevision,sceneRevision,logical,texture?texture->textureSize():QSize{}});
            m->setLandMask(ready?texture:nullptr,logical,frame.uvTransform,ready);
        }
        markDirty(QSGNode::DirtyMaterial);
    }
};
class LayerNode final : public QSGNode {
public:
    LayerNode(){setFlag(QSGNode::UsePreprocess,true);}
    std::unique_ptr<TerrainRenderOwner> owner;
    TerrainRenderOwnerSnapshot identity;
    QPointer<TerrainImageBridge> bridge;
    std::map<DrawKey,DrawNode*> nodes;
    quint64 meshBytes=0;
    std::vector<DrawNode*> underlays;
    QPointer<TerrainLandMaskItem> mask;
    bool candidateNeedsMask=false;
    quint64 viewRevision=0,sceneRevision=0;
    QSizeF logical;
    void preprocess() override {
        bool ready=owner&&owner->candidateSelected();
        if(ready&&candidateNeedsMask) {
            auto* provider=mask?mask->textureProvider():nullptr;auto* texture=provider?provider->texture():nullptr;
            bool updated=false;
            if(auto* dynamic=qobject_cast<QSGDynamicTexture*>(texture))updated=dynamic->updateTexture();
            texture=provider?provider->texture():nullptr;
            if(mask&&provider)mask->noteRenderThreadTextureUse(provider,texture,updated);
            const auto frame=mask?mask->renderThreadMaskFrame():TerrainLandMaskFrame{};
            ready=TerrainRenderContract::maskCurrent(frame.ready,actualTextureReady(texture),
                {frame.viewRevision,frame.sceneRevision,frame.logicalSize,frame.textureSize},
                {viewRevision,sceneRevision,logical,texture?texture->textureSize():QSize{}});
        }
        // Suppress old physical output in the same frame that a current gray
        // mask can draw; its intentionally discarded sea must show map ocean.
        for(auto* node:underlays)if(node->underlay) {
            node->terrainMaterial()->setDrawSuppressed(ready);node->markDirty(QSGNode::DirtyMaterial);
        }
    }
    // Destroy geometry/material borrowers before their render-owner textures.
    ~LayerNode() override {while(firstChild()){auto* child=firstChild();removeChildNode(child);delete child;}nodes.clear();owner.reset();}
};
void makeMesh(DrawNode* node,const TerrainDisplayDraw& draw,bool probe,bool globe,double physicalScale) {
    TerrainGridMesh mesh;
    if(probe) {
        mesh.vertices={0,0,0,0,1,0,1,0,0,1,0,1,1,1,1,1};mesh.indices={0,2,1,1,2,3};
    } else {
        mesh=buildTerrainGridMesh(draw.bounds.west,draw.bounds.north,draw.bounds.east,draw.bounds.south,globe,physicalScale);
    }
    auto* geometry=node->geometry();geometry->allocate(int(mesh.vertices.size()/4),int(mesh.indices.size()));
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    auto* vertices=geometry->vertexDataAsTexturedPoint2D();
    for(std::size_t i=0;i<mesh.vertices.size()/4;++i)
        vertices[i].set(mesh.vertices[i*4],mesh.vertices[i*4+1],mesh.vertices[i*4+2],mesh.vertices[i*4+3]);
    std::copy(mesh.indices.begin(),mesh.indices.end(),geometry->indexDataAsUInt());
    node->markDirty(QSGNode::DirtyGeometry);
}
}
TerrainLayerItem::TerrainLayerItem(QQuickItem* parent):QQuickItem(parent) {
    Q_INIT_RESOURCE(m73_map_shaders);
    setFlag(ItemHasContents,true);connect(this,&TerrainLayerItem::changed,this,&TerrainLayerItem::update);
    connect(this,&QQuickItem::windowChanged,this,&TerrainLayerItem::attachWindow);
    if(window())attachWindow(window());
}
TerrainLayerItem::~TerrainLayerItem() {
    disconnect(this,nullptr,this,nullptr);if(connectedWindow_)disconnect(connectedWindow_,nullptr,this,nullptr);
}
void TerrainLayerItem::setSceneBridge(QObject* object) {
    auto* next=qobject_cast<MapSceneBridge*>(object);if(scene_==next)return;
    if(scene_)disconnect(scene_,nullptr,this,nullptr);scene_=next;
    if(next) {connect(next,&MapSceneBridge::viewChanged,this,&TerrainLayerItem::update);
        connect(next,&MapSceneBridge::sceneChanged,this,&TerrainLayerItem::update);}
    emit changed();
}
void TerrainLayerItem::setTerrainBridge(QObject* object) {
    auto* next=qobject_cast<TerrainImageBridge*>(object);if(terrain_==next)return;
    if(terrain_)disconnect(terrain_,nullptr,this,nullptr);terrain_=next;
    if(next) {connect(next,&TerrainImageBridge::renderInputChanged,this,&TerrainLayerItem::update);
        connect(next,&TerrainImageBridge::renderStyleChanged,this,&TerrainLayerItem::update);
        publishOwner(graphAlive_.load());}
    emit changed();
}
void TerrainLayerItem::setLandMaskSource(QQuickItem* value) {if(mask_==value)return;mask_=value;emit changed();}
void TerrainLayerItem::publishOwner(bool alive) {
    if(terrain_)terrain_->observeOwner({windowEpoch_.load(),contextEpoch_.load(),alive});
}
void TerrainLayerItem::attachWindow(QQuickWindow* window) {
    if(connectedWindow_)disconnect(connectedWindow_,nullptr,this,nullptr);connectedWindow_=window;
    windowEpoch_.store(nextWindowEpoch.fetch_add(1));contextEpoch_.store(0);graphAlive_.store(false);publishOwner(false);
    if(!window)return;
    connect(window,&QQuickWindow::sceneGraphInitialized,this,[this] {
        contextEpoch_.fetch_add(1);graphAlive_.store(true);publishOwner(true);
        QMetaObject::invokeMethod(this,[this]{update();},Qt::QueuedConnection);
    },Qt::DirectConnection);
    connect(window,&QQuickWindow::sceneGraphInvalidated,this,[this] {
        contextEpoch_.fetch_add(1);graphAlive_.store(false);publishOwner(false);
    },Qt::DirectConnection);
    // A layer attached after graph initialization still needs a context owner.
    if(window->isSceneGraphInitialized()){contextEpoch_.store(1);graphAlive_.store(true);publishOwner(true);}
}
QSGNode* TerrainLayerItem::updatePaintNode(QSGNode* previous,UpdatePaintNodeData*) {
    auto* root=static_cast<LayerNode*>(previous);
    if(!window()||!scene_||!terrain_||!graphAlive_.load()) {delete root;return nullptr;}
    const auto input=terrain_->renderInput();if(!input){delete root;return nullptr;}
    const TerrainRenderOwnerSnapshot identity{windowEpoch_.load(),contextEpoch_.load(),true};
    if(!root||root->identity.windowEpoch!=identity.windowEpoch||root->identity.contextEpoch!=identity.contextEpoch||root->bridge!=terrain_) {
        delete root;root=new LayerNode;root->identity=identity;
        root->bridge=terrain_;
        root->owner=std::make_unique<TerrainRenderOwner>(window(),terrain_,identity);
    }
    root->owner->synchronize(input);
    root->underlays.clear();root->mask=qobject_cast<TerrainLandMaskItem*>(mask_.data());
    root->viewRevision=input->view.revision;root->sceneRevision=input->sceneRevision;
    root->logical={input->view.viewportWidth,input->view.viewportHeight};root->candidateNeedsMask=false;
    if(root->owner->candidateSelected())for(const auto& draw:root->owner->draws()) {
        const auto* resource=root->owner->resource({draw.resource.key,draw.resource.contentKey});
        root->candidateNeedsMask=root->candidateNeedsMask||(resource&&(resource->frame.dem||resource->gray));
    }
    std::map<DrawKey,DrawNode*> next;quint64 meshBytes=0;
    const auto configure=[&](const TerrainDisplayDraw& draw,bool probe,std::size_t index,bool underlay=false) {
        const TerrainDisplayResourceId id{draw.resource.key,draw.resource.contentKey};
        auto* texture=root->owner->texture(id);if(!texture)return;
        const auto* resource=root->owner->resource(id);if(!probe&&!resource)return;
        const bool globe=input->view.mode==ProjectionMode::Globe;
        const double physicalScale=input->meshPhysicalScale>0?input->meshPhysicalScale:
            input->view.scale*std::max(1.,input->view.devicePixelRatio);
        const auto shape=probe?TerrainGridShape{}:terrainGridShape(draw.bounds.east-draw.bounds.west,
            draw.bounds.north-draw.bounds.south,globe,physicalScale);
        const DrawKey key{draw.resource.key,draw.resource.contentKey,draw.worldOffset,probe,globe,shape.columns,shape.rows};
        const auto already=next.find(key);const auto old=root->nodes.find(key);
        auto* node=already!=next.end()?already->second:old==root->nodes.end()?new DrawNode:old->second;
        if(already==next.end()&&old==root->nodes.end())makeMesh(node,draw,probe,globe,physicalScale);
        else root->removeChildNode(node);
        root->appendChildNode(node);next[key]=node;
        auto* material=node->terrainMaterial();material->terrainTexture=texture;
        material->tintTexture=probe?texture:root->owner->tintTextureFor(*resource);
        material->emptyMaskTexture=root->owner->emptyMaskTexture();
        node->mask=qobject_cast<TerrainLandMaskItem*>(mask_.data());node->probe=probe;node->underlay=underlay;
        if(underlay)root->underlays.push_back(node);
        node->requiresMask=!probe&&(resource->frame.dem||resource->gray);
        node->viewRevision=input->view.revision;node->sceneRevision=input->sceneRevision;
        node->logical={input->view.viewportWidth,input->view.viewportHeight};
        const auto view=input->view;constexpr float radians=0.01745329251994329577f;
        material->setView(view,float(view.translateX),float(view.translateY),float(view.scale)*radians,1,
            float(view.centerLongitude),float(view.centerLatitude));
        const auto size=texture->textureSize();
        material->setTile({draw.bounds.west,draw.bounds.south,draw.bounds.east,draw.bounds.north},size,
            resource?resource->frame.levelSize:size,resource?resource->frame.gutter:0);
        material->setOptions(!probe&&resource->frame.dem,probe||!(resource->frame.dem?input->gray:resource->gray),
            terrain_->darkTheme(),terrain_->shadeBlend(),false,!probe);
        material->setUploadProbe(probe);
        material->setDrawSuppressed(false);
        material->commitObserver=[owner=root->owner.get()](QSGTexture* committed,double millis,bool valid){owner->noteTextureCommit(committed,millis,valid);};
        const auto sceneRevision=scene_->sceneSnapshot()?scene_->sceneSnapshot()->revision:0;
        const bool currentView=scene_->viewState().revision==input->view.revision&&sceneRevision==input->sceneRevision;
        material->drawObserver=probe?std::function<void()>{}:std::function<void()>{[owner=root->owner.get(),node,index,draw,currentView]{
            if(currentView&&node->currentMask())owner->noteDraw(index,draw);
        }};
        node->markDirty(QSGNode::DirtyMaterial);
    };
    std::size_t index=0;
    if(input->enabled&&root->owner->candidateSelected()&&root->owner->needsFallbackUnderlay())
        for(const auto& draw:root->owner->fallbackDraws())configure(draw,false,std::numeric_limits<std::size_t>::max(),true);
    if(input->enabled)for(const auto& draw:root->owner->draws())configure(draw,false,index++);
    if(input->enabled)for(const auto& id:root->owner->pendingProbes()) {
        const auto* data=root->owner->resource(id);
        const TerrainDisplayResource resource=data?data->resource:TerrainDisplayResource{id.key,id.contentKey,0,{-180,90,180,-90}};
        configure({resource,resource.interior,0},true,0);
    }
    for(const auto& old:root->nodes)if(!next.count(old.first)){root->removeChildNode(old.second);delete old.second;}
    for(const auto& entry:next)meshBytes+=quint64(entry.second->geometry()->vertexCount())*sizeof(QSGGeometry::TexturedPoint2D)+
        quint64(entry.second->geometry()->indexCount())*sizeof(quint32);
    root->nodes=std::move(next);root->owner->setMeshBytes(meshBytes);return root;
}

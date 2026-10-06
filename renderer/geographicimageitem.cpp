#include "geographicimageitem.h"
#include "geographicimagemesh.h"
#include "scenegraph/terrainmaterial.h"
#include "terrainlandmaskitem.h"
#include "terrainrendercontract.h"
#include <QImageReader>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGTextureMaterial>
#include <QSGTextureProvider>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>

namespace {
class GeographicImageNode final : public QObject,public QSGGeometryNode {
public:
    GeographicImageNode(){setFlag(QSGNode::UsePreprocess,true);}
    ~GeographicImageNode() override {
        QObject::disconnect(maskChanged);QObject::disconnect(maskDestroyed);
        delete texture;delete emptyMaskTexture;
    }
    QSGTexture* texture=nullptr;
    std::shared_ptr<QSGTexture> tint;
    qint64 imageKey=0,tintKey=0;
    bool terrain=false;
    bool requiresMask=false;
    QSGTexture* emptyMaskTexture=nullptr;
    QPointer<TerrainLandMaskItem> maskSource;
    QPointer<QSGTextureProvider> maskProvider;
    QMetaObject::Connection maskChanged,maskDestroyed;
    quint64 requestedViewRevision=0,requestedSceneRevision=0;
    QSizeF requestedLogicalSize;
    void preprocess() override {
        if(!terrain)return;
        auto* m=static_cast<TerrainMaterial*>(material());
        // Reset every frame: a previous-view texture can never remain authorized.
        m->setLandMask(nullptr,requestedLogicalSize,{1,1,0,0},false);
        if(!requiresMask)return; // Physical raster draws full opaque tiles.
        auto* next=maskSource?maskSource->textureProvider():nullptr;
        if(next!=maskProvider) {
            QObject::disconnect(maskChanged);QObject::disconnect(maskDestroyed);
            maskProvider=next;
            if(next) {
                maskChanged=QObject::connect(next,&QSGTextureProvider::textureChanged,this,[this]{
                    markDirty(QSGNode::DirtyMaterial);
                },Qt::DirectConnection);
                maskDestroyed=QObject::connect(next,&QObject::destroyed,this,[this]{
                    maskProvider=nullptr;
                    if(terrain)static_cast<TerrainMaterial*>(material())->setLandMask(nullptr,requestedLogicalSize,{1,1,0,0},false);
                    markDirty(QSGNode::DirtyMaterial);
                },Qt::DirectConnection);
            }
        }
        if(maskProvider&&maskSource) {
            auto* mask=maskProvider->texture();bool updated=false;
            if(auto* dynamic=qobject_cast<QSGDynamicTexture*>(mask))updated=dynamic->updateTexture();
            // Dynamic providers can replace the underlying texture during update.
            mask=maskProvider->texture();
            maskSource->noteRenderThreadTextureUse(maskProvider,mask,updated);
            const auto frame=maskSource->renderThreadMaskFrame();
            const bool ready=TerrainRenderContract::maskCurrent(frame.ready,mask&&mask->rhiTexture()!=nullptr,
                {frame.viewRevision,frame.sceneRevision,frame.logicalSize,frame.textureSize},
                {requestedViewRevision,requestedSceneRevision,requestedLogicalSize,mask?mask->textureSize():QSize{}});
            m->setLandMask(ready?mask:nullptr,requestedLogicalSize,frame.uvTransform,ready);
        }
        markDirty(QSGNode::DirtyMaterial);
    }
};
// One tint upload per Qt window/image, shared by its visible terrain tiles.
// Weak entries own no GPU resources; the last render-thread node releases it.
std::shared_ptr<QSGTexture> sharedTint(QQuickWindow* window,const QImage& image) {
    using Key=std::pair<QQuickWindow*,qint64>;
    static std::mutex mutex;
    static std::map<Key,std::weak_ptr<QSGTexture>> cache;
    std::lock_guard lock(mutex);
    for(auto it=cache.begin();it!=cache.end();) {
        if(it->second.expired())it=cache.erase(it);else ++it;
    }
    const Key key{window,image.cacheKey()};
    if(auto old=cache[key].lock())return old;
    auto texture=std::shared_ptr<QSGTexture>(window->createTextureFromImage(image));
    if(texture) {
        texture->setFiltering(QSGTexture::Linear);
        texture->setHorizontalWrapMode(QSGTexture::ClampToEdge);
        texture->setVerticalWrapMode(QSGTexture::ClampToEdge);
        cache[key]=texture;
    }
    return texture;
}
}

GeographicImageItem::GeographicImageItem(QQuickItem* parent):QQuickItem(parent)
{
    setFlag(ItemHasContents,true);connect(this,&GeographicImageItem::changed,this,&GeographicImageItem::update);
}
GeographicImageItem::~GeographicImageItem() {
    if(terrainBridge_)terrainBridge_->releaseDisplayBacking(this);
}
void GeographicImageItem::setSceneBridge(QObject* value) {
    auto* next=qobject_cast<MapSceneBridge*>(value);if(bridge_==next)return;
    if(bridge_)disconnect(bridge_,nullptr,this,nullptr);bridge_=next;
    if(bridge_) {
        connect(bridge_,&MapSceneBridge::viewChanged,this,&GeographicImageItem::update);
        connect(bridge_,&MapSceneBridge::sceneChanged,this,&GeographicImageItem::update);
    }
    emit changed();
}
void GeographicImageItem::setTerrainBridge(QObject* value) {
    auto* next=qobject_cast<TerrainImageBridge*>(value);if(next==terrainBridge_)return;
    if(terrainBridge_) {
        terrainBridge_->releaseDisplayBacking(this);
        disconnect(terrainBridge_,nullptr,this,nullptr);
    }
    terrainBridge_=next;image_={};terrainFrame_={};rasterDisplayImage_={};rasterSourceKey_=0;
    if(next)connect(next,&TerrainImageBridge::sourceChanged,this,[this]{image_={};refresh();emit changed();});
    if(next)connect(next,&TerrainImageBridge::renderStyleChanged,this,&GeographicImageItem::update);
    refresh();emit changed();
}
void GeographicImageItem::setTerrainTile(QVariantMap value) {
    if(terrainTile_==value)return;terrainTile_=std::move(value);image_={};refresh();emit changed();
}
void GeographicImageItem::setSource(QUrl value){if(source_==value)return;source_=std::move(value);refresh();emit changed();}
#define GEO_SETTER(Name,member) void GeographicImageItem::set##Name(double value){if(!std::isfinite(value)||member==value)return;member=value;emit changed();}
GEO_SETTER(West,west_)
GEO_SETTER(South,south_)
GEO_SETTER(East,east_)
GEO_SETTER(North,north_)
#undef GEO_SETTER
void GeographicImageItem::setColorMode(QString value){value=value.toLower();if(value!="gray")value="color";if(colorMode_==value)return;colorMode_=value;if(!terrainBridge_||!terrainFrame_.dem)refresh();emit changed();}
void GeographicImageItem::setSmooth(bool value){if(smooth_==value)return;smooth_=value;emit changed();}
void GeographicImageItem::setLandPass(bool value){if(landPass_==value)return;landPass_=value;emit changed();}
void GeographicImageItem::setLandMaskSource(QQuickItem* value) {
    if(landMaskSource_==value)return;
    if(landMaskSource_)disconnect(landMaskSource_,nullptr,this,nullptr);
    landMaskSource_=value;
    if(value)connect(value,&QObject::destroyed,this,&GeographicImageItem::update);
    emit changed();
}
void GeographicImageItem::refresh() {
    if(terrainBridge_) {
        if(!terrainTile_.contains("level")||!terrainTile_.contains("column")||!terrainTile_.contains("row")) {
            terrainFrame_={};image_={};rasterDisplayImage_={};rasterSourceKey_=0;
            terrainBridge_->releaseDisplayBacking(this);return;
        }
        bool l=false,c=false,r=false;
        const auto level=terrainTile_.value("level").toInt(&l),column=terrainTile_.value("column").toInt(&c),row=terrainTile_.value("row").toInt(&r);
        terrainFrame_=(l&&c&&r)?terrainBridge_->acquireFrame(level,column,row):TerrainImageFrame{};
        image_=terrainFrame_.image;
        if(!terrainFrame_.dem) {
            const bool gray=colorMode_=="gray";
            if(rasterDisplayImage_.isNull()||rasterSourceKey_!=image_.cacheKey()||rasterDisplayGray_!=gray) {
                rasterDisplayImage_=TerrainRenderContract::rasterDisplayImage(image_,gray);
                rasterSourceKey_=image_.cacheKey();rasterDisplayGray_=gray;
            }
        } else {rasterDisplayImage_={};rasterSourceKey_=0;}
        terrainBridge_->setDisplayBacking(this,rasterDisplayImage_);
        return;
    }
    if(source_.isEmpty()){image_={};return;}
    const auto path=source_.isLocalFile()?source_.toLocalFile():
        source_.scheme()=="qrc"?QStringLiteral(":")+source_.path():source_.toString();
    QImageReader reader(path);reader.setAutoTransform(true);image_=reader.read();
    if(colorMode_=="gray"&&!image_.isNull()) {
        auto gray=image_.convertToFormat(QImage::Format_ARGB32);
        for(int y=0;y<gray.height();++y)for(int x=0;x<gray.width();++x) {
            const auto pixel=gray.pixel(x,y);const auto value=qGray(pixel);
            gray.setPixel(x,y,qRgba(value,value,value,qAlpha(pixel)));
        }
        image_=std::move(gray);
    }
}
QSGNode* GeographicImageItem::updatePaintNode(QSGNode* previous,UpdatePaintNodeData*) {
    const auto discard=[&]() -> QSGNode* {delete previous;return nullptr;};
    const QImage& uploadImage=terrainBridge_&&!terrainFrame_.dem?rasterDisplayImage_:image_;
    if(!window()||!bridge_||uploadImage.isNull()||!(east_>west_)||!(north_>south_))return discard();
    GeographicImageMesh mesh;
    try {mesh=buildGeographicImageMesh({west_,south_,east_,north_},bridge_->viewState());}
    catch(...) {return discard();}
    if(mesh.indices.empty())return discard();
    const bool terrain=bool(terrainBridge_);
    auto* node=static_cast<GeographicImageNode*>(previous);
    if(node&&node->terrain!=terrain){delete node;node=nullptr;}
    if(!node) {
        node=new GeographicImageNode;node->terrain=terrain;
        auto* geometry=new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(),0,0,QSGGeometry::UnsignedShortType);
        node->setGeometry(geometry);node->setFlag(QSGNode::OwnsGeometry,true);
        node->setMaterial(terrain?static_cast<QSGMaterial*>(new TerrainMaterial):new QSGTextureMaterial);
        node->setFlag(QSGNode::OwnsMaterial,true);
    }
    auto* geometry=node->geometry();
    geometry->allocate(int(mesh.vertices.size()/4),int(mesh.indices.size()));
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    auto* vertices=geometry->vertexDataAsTexturedPoint2D();
    for(std::size_t i=0;i<mesh.vertices.size()/4;++i)
        vertices[i].set(mesh.vertices[i*4],mesh.vertices[i*4+1],mesh.vertices[i*4+2],mesh.vertices[i*4+3]);
    std::copy(mesh.indices.begin(),mesh.indices.end(),geometry->indexDataAsUShort());
    if(!node->texture||node->imageKey!=uploadImage.cacheKey()) {
        delete node->texture;node->texture=window()->createTextureFromImage(uploadImage);
        node->imageKey=uploadImage.cacheKey();
    }
    if(!node->texture){delete node;return nullptr;}
    // Both Web terrain representations use LINEAR sampling and their real gutter.
    node->texture->setFiltering(terrain?QSGTexture::Linear:
        smooth_?QSGTexture::Linear:QSGTexture::Nearest);
    node->texture->setHorizontalWrapMode(QSGTexture::ClampToEdge);
    node->texture->setVerticalWrapMode(QSGTexture::ClampToEdge);
    if(terrain) {
        if(node->tintKey!=terrainFrame_.tint.cacheKey()) {
            node->tint=terrainFrame_.tint.isNull()?nullptr:sharedTint(window(),terrainFrame_.tint);
            node->tintKey=terrainFrame_.tint.cacheKey();
        }
        auto* material=static_cast<TerrainMaterial*>(node->material());
        node->requiresMask=terrainFrame_.dem||colorMode_=="gray";
        material->terrainTexture=node->texture;material->tintTexture=node->tint.get();
        if(!node->emptyMaskTexture) {
            QImage empty(1,1,QImage::Format_RGBA8888);empty.fill(Qt::transparent);
            node->emptyMaskTexture=window()->createTextureFromImage(empty);
        }
        material->emptyMaskTexture=node->emptyMaskTexture;
        node->maskSource=qobject_cast<TerrainLandMaskItem*>(landMaskSource_.data());
        const auto view=bridge_->viewState();const auto scene=bridge_->sceneSnapshot();
        node->requestedViewRevision=view.revision;node->requestedSceneRevision=scene?scene->revision:0;
        node->requestedLogicalSize={view.viewportWidth,view.viewportHeight};
        material->setLandMask(nullptr,node->requestedLogicalSize,{1,1,0,0},false);
        material->setView(bridge_->viewState());
        material->setTile({west_,south_,east_,north_},uploadImage.size(),terrainFrame_.levelSize,terrainFrame_.gutter);
        material->setOptions(terrainFrame_.dem,colorMode_=="color",terrainBridge_->darkTheme(),
                             terrainBridge_->shadeBlend(),landPass_);
    } else {
        auto* material=static_cast<QSGTextureMaterial*>(node->material());
        material->setTexture(node->texture);material->setFiltering(smooth_?QSGTexture::Linear:QSGTexture::Nearest);
        material->setFlag(QSGMaterial::Blending,true);
    }
    node->markDirty(QSGNode::DirtyGeometry|QSGNode::DirtyMaterial);
    return node;
}

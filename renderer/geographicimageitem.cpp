#include "geographicimageitem.h"
#include "geographicimagemesh.h"
#include <QImageReader>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGTextureMaterial>
#include <algorithm>
#include <cmath>

namespace {
class GeographicImageNode final : public QSGGeometryNode {
public:
    ~GeographicImageNode() override {delete texture;}
    QSGTexture* texture=nullptr;
};
}

GeographicImageItem::GeographicImageItem(QQuickItem* parent):QQuickItem(parent)
{
    setFlag(ItemHasContents,true);connect(this,&GeographicImageItem::changed,this,&GeographicImageItem::update);
}
void GeographicImageItem::setSceneBridge(QObject* value) {
    auto* next=qobject_cast<MapSceneBridge*>(value);if(bridge_==next)return;
    if(bridge_)disconnect(bridge_,nullptr,this,nullptr);bridge_=next;
    if(bridge_)connect(bridge_,&MapSceneBridge::viewChanged,this,&GeographicImageItem::update);
    emit changed();
}
void GeographicImageItem::setTerrainBridge(QObject* value) {
    auto* next=qobject_cast<TerrainImageBridge*>(value);if(next==terrainBridge_)return;
    if(terrainBridge_)disconnect(terrainBridge_,nullptr,this,nullptr);
    terrainBridge_=next;image_={};
    if(next)connect(next,&TerrainImageBridge::sourceChanged,this,[this]{image_={};refresh();emit changed();});
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
void GeographicImageItem::setColorMode(QString value){value=value.toLower();if(value!="gray")value="color";if(colorMode_==value)return;colorMode_=value;refresh();emit changed();}
void GeographicImageItem::setSmooth(bool value){if(smooth_==value)return;smooth_=value;emit changed();}
void GeographicImageItem::refresh() {
    if(terrainBridge_) {
        if(!terrainTile_.contains("level")||!terrainTile_.contains("column")||!terrainTile_.contains("row"))return;
        bool l=false,c=false,r=false;
        const auto level=terrainTile_.value("level").toInt(&l),column=terrainTile_.value("column").toInt(&c),row=terrainTile_.value("row").toInt(&r);
        image_=(l&&c&&r)?terrainBridge_->acquire(level,column,row,colorMode_=="gray"):QImage{};
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
    delete previous;
    if(!window()||!bridge_||image_.isNull()||!(east_>west_)||!(north_>south_))return nullptr;
    GeographicImageMesh mesh;
    try {mesh=buildGeographicImageMesh({west_,south_,east_,north_},bridge_->viewState());}
    catch(...) {return nullptr;}
    if(mesh.indices.empty())return nullptr;
    auto* node=new GeographicImageNode;
    auto* geometry=new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(),
        int(mesh.vertices.size()/4),int(mesh.indices.size()),QSGGeometry::UnsignedShortType);
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    auto* vertices=geometry->vertexDataAsTexturedPoint2D();
    for(std::size_t i=0;i<mesh.vertices.size()/4;++i)
        vertices[i].set(mesh.vertices[i*4],mesh.vertices[i*4+1],mesh.vertices[i*4+2],mesh.vertices[i*4+3]);
    std::copy(mesh.indices.begin(),mesh.indices.end(),geometry->indexDataAsUShort());
    auto* material=new QSGTextureMaterial;
    node->texture=window()->createTextureFromImage(image_);
    material->setTexture(node->texture);material->setFiltering(smooth_?QSGTexture::Linear:QSGTexture::Nearest);
    material->setFlag(QSGMaterial::Blending,true);
    node->setGeometry(geometry);node->setFlag(QSGNode::OwnsGeometry,true);
    node->setMaterial(material);node->setFlag(QSGNode::OwnsMaterial,true);
    return node;
}

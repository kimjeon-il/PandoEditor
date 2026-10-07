#include "terrainmaterial.h"
#include "../terrainrendercontract.h"
#include <QSGMaterialShader>
#include <QMatrix4x4>
#include <QByteArray>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <QElapsedTimer>
#include <rhi/qrhi.h>

namespace {
QSGMaterialType materialType;
constexpr float radians=0.01745329251994329577f;
class Shader final : public QSGMaterialShader {
public:
    Shader() {
        setShaderFileName(VertexStage,QStringLiteral(":/m73/shaders/terrain.vert.qsb"));
        setShaderFileName(FragmentStage,QStringLiteral(":/m73/shaders/terrain.frag.qsb"));
    }
    bool updateUniformData(RenderState& state,QSGMaterial* current,QSGMaterial*) override {
        auto* bytes=state.uniformData();if(!bytes||bytes->size()<256)return false;
        const auto* m=static_cast<TerrainMaterial*>(current);
        if(m->drawObserver)m->drawObserver();
        const auto matrix=state.combinedMatrix();std::memcpy(bytes->data(),matrix.constData(),64);
        const QVector4D vectors[]{m->flat0,m->flat1,m->globe0,m->globe1,m->bounds,
            m->uvBounds,m->dimensions,m->options,m->effects,m->maskTransform,m->maskMetrics,m->maskState};
        static_assert(sizeof(QVector4D)==16);
        std::memcpy(bytes->data()+64,vectors,sizeof(vectors));
        const float opacity=state.opacity();std::memcpy(bytes->data()+64+8*16+8,&opacity,4);
        return true;
    }
    void updateSampledImage(RenderState& state,int binding,QSGTexture** texture,
                            QSGMaterial* current,QSGMaterial*) override {
        auto* m=static_cast<TerrainMaterial*>(current);
        *texture=binding==1?m->terrainTexture:binding==2?(m->tintTexture?m->tintTexture:m->terrainTexture):
            binding==3?(m->landMaskTexture?m->landMaskTexture:m->emptyMaskTexture):nullptr;
        if(*texture) {
            QElapsedTimer elapsed;elapsed.start();
            (*texture)->commitTextureOperations(state.rhi(),state.resourceUpdateBatch());
            if(m->commitObserver) {
                const auto* backing=(*texture)->rhiTexture();
                const bool valid=backing&&const_cast<QRhiTexture*>(backing)->nativeTexture().object!=0&&
                    backing->pixelSize()==(*texture)->textureSize();
                m->commitObserver(*texture,double(elapsed.nsecsElapsed())/1000000.,valid);
                if(!valid&&m->emptyMaskTexture&&m->emptyMaskTexture!=*texture) {
                    *texture=m->emptyMaskTexture;
                    (*texture)->commitTextureOperations(state.rhi(),state.resourceUpdateBatch());
                }
            }
        }
    }
};
}
TerrainMaterial::TerrainMaterial(){setFlag(Blending,true);setFlag(NoBatching,true);}
QSGMaterialType* TerrainMaterial::type() const{return &materialType;}
QSGMaterialShader* TerrainMaterial::createShader(QSGRendererInterface::RenderMode) const{return new Shader;}
int TerrainMaterial::compare(const QSGMaterial* other) const {
    if(this==other)return 0;return std::less<const void*>{}(this,other)?-1:1;
}
void TerrainMaterial::setView(const MapViewState& view,float originX,float originY,float mapScale,
                             float cosLatitude,float minX,float maxLatitude,float worldOffset) {
    flat0={originX,originY,mapScale*cosLatitude,mapScale};
    flat1={minX,maxLatitude,worldOffset,view.mode==ProjectionMode::Globe?1.f:0.f};
    globe0={float((view.centerLongitude+view.rotationLongitude)*radians),
        float(std::clamp(view.centerLatitude+view.rotationLatitude,-90.,90.)*radians),
        float(view.rotationRoll*radians),float(view.scale)};
    globe1={float(view.translateX),float(view.translateY),float(view.viewportWidth),float(view.viewportHeight)};
}
void TerrainMaterial::setTile(GeographicImageBounds tile,QSize texture,QSize level,int gutter) {
    bounds={float(tile.west),float(tile.north),float(tile.east),float(tile.south)};
    uvBounds=TerrainRenderContract::uvBounds(texture,gutter);
    dimensions={float(texture.width()),float(texture.height()),float(level.width()),float(level.height())};
    effects.setW(float(std::floor(((tile.west+tile.east)*0.5+180.0)/360.0)*360.0));
}
void TerrainMaterial::setOptions(bool dem,bool physical,bool dark,float blend,bool land,bool geographic) {
    // The shader decides missing-tint gray only after the real mask selects land.
    options={physical?1.f:0.f,dark?1.f:0.f,
        std::clamp(blend,0.f,1.f),land?1.f:0.f};
    effects={dem?1.f:0.f,geographic?1.f:0.f,1.f,effects.w()};
}
void TerrainMaterial::setLandMask(QSGTexture* texture,QSizeF logical,QVector4D transform,bool ready) {
    landMaskTexture=texture;
    const auto size=texture?texture->textureSize():QSize{};
    const bool valid=ready&&texture&&!size.isEmpty()&&logical.width()>0&&logical.height()>0&&
        std::isfinite(transform.x())&&std::isfinite(transform.y())&&std::isfinite(transform.z())&&
        std::isfinite(transform.w())&&transform.x()!=0&&transform.y()!=0;
    maskTransform=transform;
    maskMetrics={logical.width()>0?float(1/logical.width()):0.f,
        logical.height()>0?float(1/logical.height()):0.f,
        size.width()>0?1.f/size.width():0.f,size.height()>0?1.f/size.height():0.f};
    maskState={valid?1.f:0.f,tintTexture?1.f:0.f,maskState.z(),maskState.w()};
    if(texture) {
        texture->setFiltering(QSGTexture::Nearest);texture->setMipmapFiltering(QSGTexture::None);
        texture->setHorizontalWrapMode(QSGTexture::ClampToEdge);
        texture->setVerticalWrapMode(QSGTexture::ClampToEdge);
    }
}

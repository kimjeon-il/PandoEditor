#include "mapmaterial.h"
#include <QSGMaterialShader>
#include <QSGRendererInterface>
#include <QMatrix4x4>
#include <QByteArray>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace {
constexpr float radians=0.01745329251994329577f;
QSGMaterialType materialTypes[6];
int typeIndex(MapPrimitive primitive, BlendMode blend) {
    return int(primitive)*2+(blend==BlendMode::Multiply?1:0);
}
class Shader final : public QSGMaterialShader {
public:
    Shader(MapPrimitive primitive, BlendMode blend):blend_(blend) {
        const char* name=primitive==MapPrimitive::Fill?"fill":
            primitive==MapPrimitive::Stroke?"stroke":"point";
        setShaderFileName(VertexStage,QStringLiteral(":/m73/shaders/")+name+".vert.qsb");
        setShaderFileName(FragmentStage,QStringLiteral(":/m73/shaders/")+name+".frag.qsb");
        setFlag(UpdatesGraphicsPipelineState);
    }
    bool updateUniformData(RenderState& state,QSGMaterial* newMaterial,QSGMaterial*) override {
        auto* bytes=state.uniformData();
        if(!bytes||bytes->size()<144)return false;
        auto* material=static_cast<MapMaterial*>(newMaterial);
        const QMatrix4x4 matrix=state.combinedMatrix();
        std::memcpy(bytes->data(),matrix.constData(),64);
        static_assert(sizeof(QVector4D)==16,"std140 vec4 layout requires four floats");
        const QVector4D vectors[]{material->flat0,material->flat1,material->globe0,
            material->globe1,material->color,material->effects};
        std::memcpy(bytes->data()+64,vectors,
                    std::min<std::size_t>(std::size_t(bytes->size()-64),sizeof(vectors)));
        // Qt item opacity composes with per-packet alpha.
        const float alpha=vectors[4].w()*state.opacity();
        std::memcpy(bytes->data()+64+4*16+12,&alpha,sizeof(alpha));
        return true;
    }
    bool updateGraphicsPipelineState(RenderState&,GraphicsPipelineState* ps,
                                     QSGMaterial*,QSGMaterial*) override {
        ps->blendEnable=true;
        ps->separateBlendFactors=true;
        ps->srcColor=blend_==BlendMode::Multiply?GraphicsPipelineState::DstColor:
            GraphicsPipelineState::SrcAlpha;
        ps->dstColor=GraphicsPipelineState::OneMinusSrcAlpha;
        ps->srcAlpha=GraphicsPipelineState::One;
        ps->dstAlpha=GraphicsPipelineState::OneMinusSrcAlpha;
        return true;
    }
private:
    BlendMode blend_;
};
}

MapMaterial::MapMaterial(MapPrimitive kind,BlendMode mode):primitive(kind),blend(mode) {
    setFlag(Blending,true);
    setFlag(NoBatching,true); // Scene drawSequence is authoritative for order.
}
QSGMaterialType* MapMaterial::type() const {return &materialTypes[typeIndex(primitive,blend)];}
QSGMaterialShader* MapMaterial::createShader(QSGRendererInterface::RenderMode) const {
    return new Shader(primitive,blend);
}
int MapMaterial::compare(const QSGMaterial* other) const {
    if(this==other)return 0;
    // NoBatching means the scene graph never collapses distinct ordered packets.
    return std::less<const void*>{}(this,other)?-1:1;
}
void MapMaterial::setView(const MapViewState& view,float originX,float originY,
                          float mapScale,float cosLatitude,float minX,float maxLatitude,
                          float worldOffset) {
    flat0=QVector4D(originX,originY,mapScale*cosLatitude,mapScale);
    flat1=QVector4D(minX,maxLatitude,worldOffset,view.mode==ProjectionMode::Globe?1.f:0.f);
    const float lon=float((view.centerLongitude+view.rotationLongitude)*radians);
    const float lat=float(std::clamp(view.centerLatitude+view.rotationLatitude,-90.,90.)*radians);
    globe0=QVector4D(lon,lat,float(view.rotationRoll*radians),float(view.scale));
    globe1=QVector4D(float(view.translateX),float(view.translateY),
                     float(view.viewportWidth),float(view.viewportHeight));
}
void MapMaterial::setStyle(const RenderStyle& style,float pointRadius) {
    const float alpha=std::clamp(style.alpha*(primitive==MapPrimitive::Fill?style.fillAlpha:1.f),0.f,1.f);
    // Multiply uses DstColor as the RGB source factor. Attenuate its source
    // color here so opacity 0 is a no-op and opacity 1 is full multiply.
    const float weight=blend==BlendMode::Multiply?alpha:1.f;
    color=QVector4D(float((style.color>>16)&255)/255*weight,
                    float((style.color>>8)&255)/255*weight,
                    float(style.color&255)/255*weight,alpha);
    effects=QVector4D(std::max(0.f,style.width),style.dashOn,style.dashOff,pointRadius);
}

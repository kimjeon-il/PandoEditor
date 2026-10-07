#pragma once
#include "../geographicimagemesh.h"
#include <QSGMaterial>
#include <QSGRendererInterface>
#include <QSGTexture>
#include <QVector4D>
#include <QSize>
#include <functional>

// Render-thread only; the owner retains textures through camera/style updates.
// landPass is an authoritative world geometry pass, never an elevation test.
class TerrainMaterial final : public QSGMaterial {
public:
    TerrainMaterial();
    QSGMaterialType* type() const override;
    QSGMaterialShader* createShader(QSGRendererInterface::RenderMode) const override;
    int compare(const QSGMaterial*) const override;
    void setView(const MapViewState&,float originX=0,float originY=0,float mapScale=1,
                 float cosLatitude=1,float minX=0,float maxLatitude=0,float worldOffset=0);
    void setTile(GeographicImageBounds,QSize texture,QSize level,int gutter);
    void setOptions(bool dem,bool physical,bool dark,float shadeBlend,bool landPass,
                    bool geographicInput=false);
    void setLandMask(QSGTexture*,QSizeF logicalSize,QVector4D uvTransform,bool ready);
    void setUploadProbe(bool probe){maskState.setZ(probe?1.f:0.f);}
    void setDrawSuppressed(bool hidden){maskState.setW(hidden?1.f:0.f);}
    std::function<void(QSGTexture*,double,bool)> commitObserver;
    std::function<void()> drawObserver;
    QSGTexture* terrainTexture=nullptr;
    QSGTexture* tintTexture=nullptr;
    QSGTexture* landMaskTexture=nullptr; // Borrowed, never deleted by this material.
    QSGTexture* emptyMaskTexture=nullptr; // Owned by node; valid inert descriptor while missing.
    QVector4D flat0,flat1,globe0,globe1,bounds,uvBounds,dimensions,options,effects;
    QVector4D maskTransform,maskMetrics,maskState;
};

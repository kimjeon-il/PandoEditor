#pragma once

#include <pandoeditor/map/renderpacket.h>
#include <pandoeditor/map/mapviewstate.h>
#include <QSGMaterial>
#include <QSGRendererInterface>
#include <QVector4D>

enum class MapPrimitive { Fill, Stroke, Point };

// Render-thread only. Qt owns each material through its QSGGeometryNode.
class MapMaterial final : public QSGMaterial {
public:
    MapMaterial(MapPrimitive primitive, BlendMode blend);
    QSGMaterialType* type() const override;
    QSGMaterialShader* createShader(QSGRendererInterface::RenderMode) const override;
    int compare(const QSGMaterial* other) const override;

    void setView(const MapViewState& view, float originX, float originY,
                 float mapScale, float cosLatitude, float minX, float maxLatitude,
                 float worldOffset);
    void setStyle(const RenderStyle& style, float pointRadius=3);

    MapPrimitive primitive;
    BlendMode blend;
    QVector4D flat0;   // originX, originY, mapScale * cosLatitude, mapScale
    QVector4D flat1;   // minX, maxLatitude, worldOffset, globe mode
    QVector4D globe0;  // center longitude, center latitude, roll (radians), scale
    QVector4D globe1;  // translateX, translateY, viewport width, viewport height
    QVector4D color;
    QVector4D effects; // stroke width, dash on, dash off, point radius
    QVector4D strokeOptions; // join mode, round cap, AA CSS radius, miter limit
};

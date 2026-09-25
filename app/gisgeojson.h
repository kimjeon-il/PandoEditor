#pragma once
#include <pandoeditor/document.h>
#include <QByteArray>

namespace pandoeditor {
struct GisGeoJsonFeature {
    std::string id;
    Geometry geometry;
    std::string propertiesJson="{}";
};
struct GisGeoJsonCollection {
    std::vector<GisGeoJsonFeature> features;
};
// Strict local EPSG:4326 GeoJSON boundary. Unknown attributes remain lossless.
GisGeoJsonCollection parseGisGeoJson(const QByteArray&);
QByteArray exportGisGeoJson(const GisGeoJsonCollection&);
}

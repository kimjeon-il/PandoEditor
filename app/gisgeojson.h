#pragma once
#include <pandoeditor/document.h>
#include <pandoeditor/gisexchange.h>
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
// Explicit generic target only. Territorial and distribution files require
// their own mapping and relationship preview.
GisGenericImportPlan planGenericGeoJsonImport(const ProjectSnapshot&,const QByteArray&,
    std::string planId,std::string fileName);
}

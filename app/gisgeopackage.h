#pragma once
#include "gisgeojson.h"
#include <QByteArray>
#include <QString>
#include <string>
#include <vector>

namespace pandoeditor {
struct GisGeoPackageLayer {
    std::string tableName;
    std::string targetType;
    std::string geometryType;
    GisGeoJsonCollection collection;
    std::vector<std::string> propertyColumns;
};
struct GisGeoPackage {
    bool projectPackage=false;
    std::vector<GisGeoPackageLayer> layers;
};

// Local, read-only exchange boundary. Neither this function nor its caller
// mutates a ProjectDocument; mapping and confirmation are separate steps.
GisGeoPackage readGisGeoPackage(const QString& filePath);
// Project JSON is authoritative; every derived row and column must agree.
void validateProjectGeoPackageVectors(const GisGeoPackage&,
                                     const std::vector<GisGeoPackageLayer>& expected);
// GIS-only vector file; project settings and country assets belong to the
// separate project GeoPackage flow.
QByteArray exportGisGeoPackage(const ProjectDocument&,const std::vector<std::string>& selected,
                              bool metadataOnly=false);
}

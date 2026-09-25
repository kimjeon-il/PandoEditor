#pragma once
#include "gisgeojson.h"
#include <QString>
#include <string>
#include <vector>

namespace pandoeditor {
struct GisGeoPackageLayer {
    std::string tableName;
    std::string targetType;
    std::string geometryType;
    GisGeoJsonCollection collection;
};
struct GisGeoPackage {
    bool projectPackage=false;
    std::vector<GisGeoPackageLayer> layers;
};

// Local, read-only exchange boundary. Neither this function nor its caller
// mutates a ProjectDocument; mapping and confirmation are separate steps.
GisGeoPackage readGisGeoPackage(const QString& filePath);
}

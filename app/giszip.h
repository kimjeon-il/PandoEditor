#pragma once
#include "gisgeojson.h"
#include <QByteArray>
#include <string>
#include <vector>

namespace pandoeditor {
struct GisZipLayer {
    std::string path;
    std::string category;
    std::string targetType;
    std::string distributionType;
    GisGeoJsonCollection collection;
};
struct GisGeoJsonZip {
    bool webManifest=false;
    std::vector<GisZipLayer> layers;
};

// A read-only file boundary. Mapping and document mutation remain in later
// import-plan stages; no layer is silently attached to the active project.
GisGeoJsonZip parseGisGeoJsonZip(const QByteArray& bytes);
}

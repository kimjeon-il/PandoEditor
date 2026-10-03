#pragma once
#include "gisgeojson.h"

namespace pandoeditor {
// GIS-only view of a selected layer; materialization never changes the document.
GisGeoJsonCollection exportGisDocumentLayer(const ProjectDocument&,
                                            const std::string& layer);
struct GisExportLayer {
    std::string category,file,targetType;
    GisGeoJsonCollection collection;
};
std::vector<GisExportLayer> buildGisExportLayers(const ProjectDocument&,
    const std::vector<std::string>& selected);
QByteArray exportGisGeoJsonZip(const ProjectDocument&,const std::vector<std::string>& selected,
    const std::string& createdAt);
}

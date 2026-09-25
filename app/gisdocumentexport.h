#pragma once
#include "gisgeojson.h"

namespace pandoeditor {
// GIS-only view of a selected layer; materialization never changes the document.
GisGeoJsonCollection exportGisDocumentLayer(const ProjectDocument&,
                                            const std::string& layer);
}

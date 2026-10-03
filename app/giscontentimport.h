#pragma once
#include "gisgeojson.h"
#include "giszip.h"
#include "gisgeopackage.h"
#include <pandoeditor/project.h>
#include <variant>

namespace pandoeditor {
struct GisContentMapping {
    GisExchangeTarget target=GisExchangeTarget::Generic;
    std::string layerId,layerName;
    std::string idField="",nameField="name",layerIdField="layer_id",
        territoryField="territorial_unit_id",sourceModeField="source_mode";
};
using GisContentImport=std::variant<GisGenericImportPlan,GisDistributionImportPlan>;
GisContentImport planGisContentImport(const ProjectSnapshot&,
    const GisGeoJsonCollection&,std::string planId,GisSource,const GisContentMapping&);
GisContentImport planGisContentZipImport(const ProjectSnapshot&,const GisGeoJsonZip&,
    std::size_t layerIndex,std::string planId,GisSource,const GisContentMapping&);
GisContentImport planGisContentGeoPackageImport(const ProjectSnapshot&,const GisGeoPackage&,
    std::size_t layerIndex,std::string planId,GisSource,const GisContentMapping&);
}

#pragma once
#include "gisgeojson.h"
#include <pandoeditor/geometryoperations.h>
#include <QString>
#include <functional>

namespace pandoeditor {
// Every mapping is explicit. A common owner overrides source fields, including
// a canonical administrative parent. Political relation inputs are rejected.
struct GisTerritorialMapping {
    GisExchangeTarget target=GisExchangeTarget::Region;
    std::string idField="__fid__",nameField="name",sovereignField="sovereign_id",
                parentField="parent_id",validFromField="valid_from",validToField="valid_to",
                colorField="color";
    std::optional<ObjectRef> commonSovereign,commonParent;
    enum class CoastDecision { Reject, ImportedGeometry, CountryGeometry, Cancel };
    CoastDecision coast=CoastDecision::Reject;
};

// The caller runs this read-only work off the GUI thread and passes the same
// snapshot into CommandProcessor::prepare. All intersections/unions/differences
// use the pinned M4 calculator, with cancellation between every operation.
GisTerritorialImportPlan prepareGisTerritorialImport(const ProjectSnapshot&,
    const GisGeoJsonCollection&,std::string planId,GisSource,
    const GisTerritorialMapping&,const GeometryCalculator&,
    const GeometryCancellation& cancelled={});
GisTerritorialImportPlan prepareGisTerritorialGeoJsonImport(const ProjectSnapshot&,
    const QByteArray&,std::string planId,std::string fileName,
    const GisTerritorialMapping&,const GeometryCalculator&,
    const GeometryCancellation& cancelled={});
GisTerritorialImportPlan prepareGisTerritorialZipImport(const ProjectSnapshot&,
    const QByteArray&,std::size_t layerIndex,std::string planId,std::string fileName,
    const GisTerritorialMapping&,const GeometryCalculator&,
    const GeometryCancellation& cancelled={});
GisTerritorialImportPlan prepareGisTerritorialGeoPackageImport(const ProjectSnapshot&,
    const QString&,std::size_t layerIndex,std::string planId,
    const GisTerritorialMapping&,const GeometryCalculator&,
    const GeometryCancellation& cancelled={});
}

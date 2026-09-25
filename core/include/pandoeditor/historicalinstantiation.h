#pragma once
#include <pandoeditor/historicallibrary.h>
#include <pandoeditor/territorialmutation.h>
#include <cstdint>

namespace pandoeditor {
class ProjectSnapshot;

// All ownership is explicit. A catalog entry is read-only; the plan owns its
// materialized geometry and is bound to one project revision.
struct HistoricalAddRequest {
    std::string libraryId,referenceDate,geometryVersionId;
    std::optional<ObjectRef> parent,sovereign;
    bool approvePartial=false;
    // An unresolved historical subunit can explicitly become a country.
    bool asIndependentCountry=false;
    std::string countryName;
};
struct HistoricalAddition {
    HistoricalSelection selection;
    std::optional<std::string> referenceDate;
    std::optional<ObjectRef> parent,sovereign;
    bool partialApproved=false;
    bool asIndependentCountry=false;
    std::string countryName;
};
struct HistoricalInstantiationPlan {
    std::string projectInstanceId,documentId;
    std::uint64_t baseRevision=0;
    std::vector<HistoricalAddition> additions;
    // Computed by the M4 geometry worker against this exact revision.
    std::vector<GeometryReplacement> territoryReplacements;
    // Existing countries entirely absorbed by exactly one selected historical
    // country. Dependent typed references are redirected in the same ChangeSet.
    std::map<ObjectRef,ObjectRef> territoryTransfers;
    std::map<std::string,std::string> countryNameUpdates;
};
HistoricalInstantiationPlan planHistorical(const ProjectSnapshot&,
    const HistoricalLibrary&,const std::vector<HistoricalAddRequest>&,
    const std::vector<GeometryReplacement>& territoryReplacements={},
    const std::map<ObjectRef,ObjectRef>& territoryTransfers={});
HistoricalInstantiationPlan planIndependentHistorical(const ProjectSnapshot&,
    const HistoricalLibrary&,const std::vector<HistoricalAddRequest>&);
// A snapshot is a template. Missing refs and unresolved ownership stop the
// entire plan; no modern-boundary substitute is synthesized.
HistoricalInstantiationPlan planIndependentHistoricalSnapshot(const ProjectSnapshot&,
    const HistoricalLibrary&,const std::string& snapshotId,
    const std::vector<HistoricalAddRequest>& overrides={});
void applyHistoricalInstantiation(ProjectDocument&,const HistoricalInstantiationPlan&);
}

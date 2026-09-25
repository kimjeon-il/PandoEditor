#pragma once
#include <pandoeditor/historicallibrary.h>
#include <cstdint>

namespace pandoeditor {
class ProjectSnapshot;

// All ownership is explicit. A catalog entry is read-only; the plan owns its
// materialized geometry and is bound to one project revision.
struct HistoricalAddRequest {
    std::string libraryId,referenceDate,geometryVersionId;
    std::optional<ObjectRef> parent,sovereign;
};
struct HistoricalAddition {
    HistoricalSelection selection;
    std::optional<std::string> referenceDate;
    std::optional<ObjectRef> parent,sovereign;
};
struct HistoricalInstantiationPlan {
    std::string projectInstanceId,documentId;
    std::uint64_t baseRevision=0;
    std::vector<HistoricalAddition> additions;
};
HistoricalInstantiationPlan planIndependentHistorical(const ProjectSnapshot&,
    const HistoricalLibrary&,const std::vector<HistoricalAddRequest>&);
void applyHistoricalInstantiation(ProjectDocument&,const HistoricalInstantiationPlan&);
}

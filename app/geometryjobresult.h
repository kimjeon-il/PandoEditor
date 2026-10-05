#pragma once
#include "territorialgeometry.h"
#include "geometrysnap.h"
#include "territoryselection.h"
#include "riverselectionpreparation.h"
#include <string>
#include <variant>

// Application calculations carry their actual inert values, never an invented
// successful command preview or a second canonical Project. A stopped/stale job
// delivers monostate; exceptions escaping a calculation carry a typed failure.
struct GeometryJobFailure {
    pandoeditor::CommandError error=pandoeditor::CommandError::PrepareFailed;
    std::string detail;
};
using GeometryJobResult=std::variant<std::monostate,GeometryJobFailure,
    pandoeditor::TerritorySelectionDerivedResult,AnnexGeometryPreviewResult,SplitGeometryPreviewResult,
    pandoeditor::TerritorySelectionDraftResult,RiverSelectionPreparationResult,geometrysnap::CandidateBatch>;

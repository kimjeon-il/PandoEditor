#pragma once
#include <pandoeditor/commands.h>
#include <pandoeditor/jobs.h>
#include <pandoeditor/map/territoryselection.h>

namespace pandoeditor {
struct PreviewRiverNormalizationResult {
    GeometryOperationStatus status=GeometryOperationStatus::Failed;
    std::string detail;
    std::optional<Geometry> geometry;
    bool succeeded() const noexcept {return status==GeometryOperationStatus::Completed;}
};
struct PreviewAreaResult {
    GeometryOperationStatus status=GeometryOperationStatus::Failed;
    std::string detail;
    double areaKm2=0;
    bool succeeded() const noexcept {return status==GeometryOperationStatus::Completed;}
};
struct TerritorialPreviewCalculators {
    TerritorySelectionCalculators geometry;
    TerritoryGeometryTransform normalizeRaw;
    std::function<PreviewRiverNormalizationResult(const Geometry&,const GeometryCancellation&)> normalizeRiver;
    std::function<PreviewAreaResult(const Geometry&,const GeometryCancellation&)> areaKm2;
};
}

// Inert geometry values only. A preview may describe removals that would leave
// retained references dangling; only strict command preparation can admit it.
struct AnnexGeometryPreviewRequest {
    pandoeditor::ObjectRef target;
    std::vector<pandoeditor::ObjectRef> donors;
    pandoeditor::Geometry selection;
    std::vector<pandoeditor::TerritorySelectionRiverSliverContext> riverSliverContext;
};
struct AnnexGeometryRow {
    pandoeditor::ObjectRef owner;
    pandoeditor::Geometry before;
    std::optional<pandoeditor::Geometry> after;
};
struct AnnexGeometryValidationIssue {
    std::string kind;
    std::vector<pandoeditor::ObjectRef> objects;
    std::string detail;
    bool blocking=true;
};
struct AnnexGeometryPreviewResult {
    pandoeditor::GeometryOperationStatus status=pandoeditor::GeometryOperationStatus::Failed;
    pandoeditor::CommandError error=pandoeditor::CommandError::PrepareFailed;
    std::string detail;
    pandoeditor::Geometry transferredGeometry;
    std::vector<pandoeditor::ObjectRef> selectedDonors,affectedDonors,removedRoots;
    std::vector<AnnexGeometryRow> rows;
    std::vector<AnnexGeometryValidationIssue> issues;
    std::optional<pandoeditor::TerritorialMutationPlan> plan;
    pandoeditor::GeometryPatch patch;
    std::size_t autoIncludedSliverCount=0;
    double autoIncludedSliverAreaM2=0;
    double transferAreaKm2=0;
    bool ok() const;
    bool blocking() const;
};
AnnexGeometryPreviewResult calculateAnnexGeometryPreview(
    const pandoeditor::ProjectSnapshot&,const AnnexGeometryPreviewRequest&,
    const pandoeditor::TerritorialPreviewCalculators&,const pandoeditor::JobToken&);

// Inert split receipt: one fresh sibling receives the selected union. Existing
// display rows follow the root/child preview owner order; the commit patch also
// owns all affected descendants. An exhausted root has a valid inert preview,
// but prepareSplitGeometryCommit rejects it under bounded creation rules.
struct SplitGeometryPreviewResult {
    pandoeditor::GeometryOperationStatus status=pandoeditor::GeometryOperationStatus::Failed;
    pandoeditor::CommandError error=pandoeditor::CommandError::PrepareFailed;
    std::string detail;
    pandoeditor::Geometry transferredGeometry,remainingGeometry;
    std::vector<AnnexGeometryRow> rows;
    std::vector<AnnexGeometryValidationIssue> issues;
    std::optional<pandoeditor::TerritorialMutationPlan> plan;
    pandoeditor::GeometryPatch patch;
    bool ok() const;
    bool blocking() const;
};
SplitGeometryPreviewResult calculateSplitGeometryPreview(
    const pandoeditor::ProjectSnapshot&,const pandoeditor::SplitTerritorialIntent&,
    const pandoeditor::TerritorialPreviewCalculators&,const pandoeditor::JobToken&);

// Detached boundary receipt. Rows include actual changed, reparented and removed
// owners; patch covers the full planned hierarchy without publishing anything.
struct BoundaryParentChange {
    pandoeditor::ObjectRef owner,from,to;
};
// Only the calculator may seal the canonical plan and patch. Public rows and
// diagnostics are inspectable presentation values and cannot authorize a commit.
class BoundaryGeometryPreviewResult {
public:
    pandoeditor::GeometryOperationStatus status=pandoeditor::GeometryOperationStatus::Failed;
    pandoeditor::CommandError error=pandoeditor::CommandError::PrepareFailed;
    std::string detail;
    std::vector<AnnexGeometryRow> rows;
    std::vector<AnnexGeometryValidationIssue> issues;
    std::vector<BoundaryParentChange> reparented;
    const std::optional<pandoeditor::TerritorialMutationPlan>& plan() const noexcept {return plan_;}
    const pandoeditor::GeometryPatch& patch() const noexcept {return patch_;}
    bool ok() const;
    bool blocking() const;
private:
    std::optional<pandoeditor::TerritorialMutationPlan> plan_;
    pandoeditor::GeometryPatch patch_;
    bool validated_=false;
    friend BoundaryGeometryPreviewResult calculateBoundaryGeometryPreview(
        const pandoeditor::ProjectSnapshot&,const pandoeditor::SharedBoundaryIntent&,const pandoeditor::TerritorialPreviewCalculators&,const pandoeditor::JobToken&);
};

BoundaryGeometryPreviewResult calculateBoundaryGeometryPreview(
    const pandoeditor::ProjectSnapshot&,const pandoeditor::SharedBoundaryIntent&,
    const pandoeditor::TerritorialPreviewCalculators&,const pandoeditor::JobToken&);

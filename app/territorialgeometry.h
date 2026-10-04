#pragma once
#include <pandoeditor/commands.h>
#include <pandoeditor/jobs.h>
#include <pandoeditor/geometryoperations.h>

// Worker entry point: calculation and candidate preparation only, never commit.
pandoeditor::PrepareResult prepareTerritorialGeometry(
    const pandoeditor::ProjectSnapshot&,const pandoeditor::TerritorialMutationPlan&,
    const pandoeditor::JobToken&);

// Drawn selection entry point, matching web selection preprocessing before the
// strict raw annex command. This does not publish or commit the candidate.
pandoeditor::PrepareResult prepareDrawnTerritoryAnnex(
    const pandoeditor::ProjectSnapshot&,const pandoeditor::AnnexTerritoryIntent&,
    const pandoeditor::JobToken&);

// Inert geometry values only. A preview may describe removals that would leave
// retained references dangling; only strict command preparation can admit it.
struct AnnexGeometryPreviewRequest {
    pandoeditor::ObjectRef target;
    std::vector<pandoeditor::ObjectRef> donors;
    pandoeditor::Geometry selection;
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
    bool ok() const;
    bool blocking() const;
};
AnnexGeometryPreviewResult calculateAnnexGeometryPreview(
    const pandoeditor::ProjectSnapshot&,const AnnexGeometryPreviewRequest&,
    const pandoeditor::JobToken&);
pandoeditor::PrepareResult prepareAnnexGeometryCommit(
    const pandoeditor::ProjectSnapshot&,const AnnexGeometryPreviewResult&,
    const pandoeditor::JobToken&);

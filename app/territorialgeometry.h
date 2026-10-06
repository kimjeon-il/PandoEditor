#pragma once
#include <pandoeditor/commands.h>
#include <pandoeditor/jobs.h>
#include <pandoeditor/geometryoperations.h>
#include <pandoeditor/map/territorialpreview.h>

// Worker entry point: calculation and candidate preparation only, never commit.
pandoeditor::PrepareResult prepareTerritorialGeometry(
    const pandoeditor::ProjectSnapshot&,const pandoeditor::TerritorialMutationPlan&,
    const pandoeditor::JobToken&);

// Drawn selection entry point, matching web selection preprocessing before the
// strict raw annex command. This does not publish or commit the candidate.
pandoeditor::PrepareResult prepareDrawnTerritoryAnnex(
    const pandoeditor::ProjectSnapshot&,const pandoeditor::AnnexTerritoryIntent&,
    const pandoeditor::JobToken&);

pandoeditor::PrepareResult prepareAnnexGeometryCommit(
    const pandoeditor::ProjectSnapshot&,const AnnexGeometryPreviewResult&,
    const pandoeditor::JobToken&);
pandoeditor::PrepareResult prepareSplitGeometryCommit(
    const pandoeditor::ProjectSnapshot&,const SplitGeometryPreviewResult&,
    const pandoeditor::JobToken&);
pandoeditor::PrepareResult prepareBoundaryGeometryCommit(
    const pandoeditor::ProjectSnapshot&,const BoundaryGeometryPreviewResult&,
    const pandoeditor::JobToken&);

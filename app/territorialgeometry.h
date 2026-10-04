#pragma once
#include <pandoeditor/commands.h>
#include <pandoeditor/jobs.h>

// Worker entry point: calculation and candidate preparation only, never commit.
pandoeditor::PrepareResult prepareTerritorialGeometry(
    const pandoeditor::ProjectSnapshot&,const pandoeditor::TerritorialMutationPlan&,
    const pandoeditor::JobToken&);

// Drawn selection entry point, matching web selection preprocessing before the
// strict raw annex command. This does not publish or commit the candidate.
pandoeditor::PrepareResult prepareDrawnTerritoryAnnex(
    const pandoeditor::ProjectSnapshot&,const pandoeditor::AnnexTerritoryIntent&,
    const pandoeditor::JobToken&);

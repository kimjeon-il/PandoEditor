#pragma once
#include <pandoeditor/commands.h>
#include <pandoeditor/jobs.h>

// Worker entry point: calculation and candidate preparation only, never commit.
pandoeditor::PrepareResult prepareTerritorialGeometry(
    const pandoeditor::ProjectSnapshot&,const pandoeditor::TerritorialMutationPlan&,
    const pandoeditor::JobToken&);

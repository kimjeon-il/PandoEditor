#pragma once
#include "riverpartitioncalculator.h"
// An owned calculation value for the existing job runner, never a second session.
struct RiverSelectionPreparationResult {
    bool kernelInvoked=false;
    HydroRiverSourceResult source;
    pandoeditor::RiverPartitionResult partition;
};

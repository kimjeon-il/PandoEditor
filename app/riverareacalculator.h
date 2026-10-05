#pragma once
#include "riverpartitioncalculator.h"

namespace pandoeditor {
struct RiverAreaResult {
    RiverPartitionStatus status=RiverPartitionStatus::Failed;
    QString detail;
    double areaKm2=0;
    bool succeeded() const noexcept {return status==RiverPartitionStatus::Completed;}
};
// Receipt/display metric only. Executes byte-verified pinned web D3 3.5.6 with
// a reviewed hoist-only Qt adapter in a fresh private engine in the calling worker thread; never normalizes input.
// Synchronous JS is checked for cancellation before/after load, call and decode.
RiverAreaResult calculateRiverAreaKm2(const Geometry&,
    const GeometryCancellation& cancelled={});
}

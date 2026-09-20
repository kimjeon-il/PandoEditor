#pragma once
#include <pandoeditor/geometryoperations.h>
#include <array>

namespace pandoeditor {
// Creates the engine in the calling thread. No QObject or JS value crosses
// threads; callers dispatch this synchronous function through the job runner.
GeometryOperationResult calculateGeometry(const GeometryOperationRequest& request,
    const GeometryCancellation& cancelled={});
struct SplitGeometryResult {
    GeometryOperationStatus status=GeometryOperationStatus::Failed;
    std::array<Geometry,2> candidates;
    std::size_t componentIndex=0;
    std::string detail;
    bool succeeded() const noexcept{return status==GeometryOperationStatus::Completed;}
};
SplitGeometryResult splitGeometryByLine(const Geometry&,const Ring&,const GeometryCancellation& cancelled={});
}

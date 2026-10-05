#pragma once
#include <pandoeditor/geometryoperations.h>

namespace pandoeditor {
// Creates the engine in the calling thread. No QObject or JS value crosses
// threads; callers dispatch this synchronous function through the job runner.
GeometryOperationResult calculateGeometry(const GeometryOperationRequest& request,
    const GeometryCancellation& cancelled={});
}

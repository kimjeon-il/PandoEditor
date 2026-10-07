#pragma once
#include <pandoeditor/geometryoperations.h>

namespace pandoeditor {
// Creates the engine in the calling thread. No QObject or JS value crosses
// threads; callers dispatch this synchronous function through the job runner.
GeometryOperationResult calculateGeometry(const GeometryOperationRequest& request,
    const GeometryCancellation& cancelled={});
// Strict canonical inputs and the same pinned kernel. Only the scalar area
// leaves this function; numerical intersection remnants are never stored.
GeometryOperationAreaResult calculateGeometryArea(const GeometryOperationRequest& request,
    const GeometryCancellation& cancelled={});
// Create, use and destroy in one calling thread. The pinned production kernel
// is loaded lazily once and released when this bounded transaction ends.
GeometryCalculator makeTransactionGeometryCalculator();
}

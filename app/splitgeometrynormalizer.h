#pragma once
#include <pandoeditor/geometryoperations.h>
class QJSEngine;
namespace pandoeditor {
struct SplitGeometryNormalizationResult {
    GeometryOperationStatus status=GeometryOperationStatus::Failed;
    Geometry geometry;
    std::string detail;
    bool inputUnchanged=false;
    bool succeeded() const noexcept {
        return status==GeometryOperationStatus::Completed || status==GeometryOperationStatus::Empty;
    }
};
// Evaluate the complete, hash-verified approved 07d3e20 helper in an engine owned
// by the caller. The caller loads polygon clipping; this never replaces it.
void loadApprovedSplitPolygonGeometry(QJSEngine& engine);
// Fresh private engine per call. Source coordinates and component order are
// retained exactly. Raw cut-worker input must not be prewrapped through this API.
// Empty is successful, not a canonical replacement. Synchronous JS cancellation
// wins at call boundaries; callers retain cancellation/stale-result checks.
SplitGeometryNormalizationResult wrapSplitGeometry(const Geometry& geometry,
    const GeometryCancellation& cancelled={});
// Ordinary entity-store normalization: keep raw geographic edges unsegmented.
SplitGeometryNormalizationResult normalizeSplitRawGeometry(const Geometry& geometry,
    const GeometryCancellation& cancelled={});
SplitGeometryNormalizationResult normalizeSplitClippedGeometry(const Geometry& geometry,
    const GeometryCancellation& cancelled={});
}

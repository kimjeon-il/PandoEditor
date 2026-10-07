#pragma once
#include <pandoeditor/document.h>
#include <functional>
#include <vector>

namespace pandoeditor {
enum class GeometryOperation { Union, Difference, Intersection };
struct GeometryOperationRequest {
    GeometryOperation operation;
    Geometry left, right;
    // When populated this is the complete ordered operand list.  It permits
    // one union/intersection call to match the web worker without repeatedly
    // rounding intermediate coordinates.  Legacy two-operand callers keep
    // using left/right.
    std::vector<Geometry> operands;
};
enum class GeometryOperationStatus { Completed, Empty, Cancelled, Failed };
struct GeometryOperationResult {
    GeometryOperationStatus status=GeometryOperationStatus::Failed;
    Geometry geometry;
    std::string detail;
    bool succeeded() const noexcept {
        return status==GeometryOperationStatus::Completed || status==GeometryOperationStatus::Empty;
    }
};
// The callback must be thread-safe. Empty is a successful calculation, not a
// valid replacement: the territorial plan owns object lifetime decisions.
using GeometryCancellation=std::function<bool()>;
using GeometryCalculator=std::function<GeometryOperationResult(
    const GeometryOperationRequest&, const GeometryCancellation&)>;
// A scalar predicate result cannot be used as a replacement or archive entry.
struct GeometryOperationAreaResult {
    GeometryOperationStatus status=GeometryOperationStatus::Failed;
    double area=0;
    std::string detail;
    bool succeeded() const noexcept {
        return status==GeometryOperationStatus::Completed||status==GeometryOperationStatus::Empty;
    }
};
}

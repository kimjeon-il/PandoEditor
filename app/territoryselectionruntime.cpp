#include "territoryselectionruntime.h"
#include "geometrycalculator.h"
#include "geometryruntime_p.h"
#include "splitgeometrynormalizer.h"
#include <utility>

namespace pandoeditor {
const TerritorySelectionCalculators& territorySelectionCalculators() {
    static const TerritorySelectionCalculators calculators{
        [](const GeometryOperationRequest& request,const GeometryCancellation& cancelled) {
            return calculateGeometry(request,cancelled);
        },
        [](const GeometryOperationRequest& request,const GeometryCancellation& cancelled) {
            auto result=calculateRiverGeometryIntermediate(request,cancelled);
            return GeometryOperationResult{result.status,std::move(result.geometry),std::move(result.detail)};
        },
        [](const Geometry& geometry,const GeometryCancellation& cancelled) {
            auto result=wrapSplitGeometry(geometry,cancelled);
            return GeometryOperationResult{result.status,std::move(result.geometry),std::move(result.detail)};
        },
        [](const Geometry& geometry,const GeometryCancellation& cancelled) {
            auto result=normalizeSplitClippedGeometry(geometry,cancelled);
            return GeometryOperationResult{result.status,std::move(result.geometry),std::move(result.detail)};
        }
    };
    return calculators;
}
}

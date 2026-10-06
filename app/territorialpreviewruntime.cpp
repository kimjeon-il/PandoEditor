#include "territorialpreviewruntime.h"
#include "territoryselectionruntime.h"
#include "splitgeometrynormalizer.h"
#include "riverpartitioncalculator.h"
#include "riverareacalculator.h"
#include <utility>

namespace pandoeditor {
const TerritorialPreviewCalculators& territorialPreviewCalculators() {
    static const TerritorialPreviewCalculators calculators{
        territorySelectionCalculators(),
        [](const Geometry& geometry,const GeometryCancellation& cancelled) {
            auto result=normalizeSplitRawGeometry(geometry,cancelled);
            return GeometryOperationResult{result.status,std::move(result.geometry),std::move(result.detail)};
        },
        [](const Geometry& geometry,const GeometryCancellation& cancelled) {
            auto result=normalizeRiverGeometry(geometry,cancelled);
            GeometryOperationStatus status=GeometryOperationStatus::Failed;
            switch(result.status) {
            case RiverPartitionStatus::Completed:status=GeometryOperationStatus::Completed;break;
            case RiverPartitionStatus::Cancelled:status=GeometryOperationStatus::Cancelled;break;
            case RiverPartitionStatus::Failed:status=GeometryOperationStatus::Failed;break;
            }
            return PreviewRiverNormalizationResult{status,result.detail.toStdString(),std::move(result.geometry)};
        },
        [](const Geometry& geometry,const GeometryCancellation& cancelled) {
            const auto result=calculateRiverAreaKm2(geometry,cancelled);
            GeometryOperationStatus status=GeometryOperationStatus::Failed;
            switch(result.status) {
            case RiverPartitionStatus::Completed:status=GeometryOperationStatus::Completed;break;
            case RiverPartitionStatus::Cancelled:status=GeometryOperationStatus::Cancelled;break;
            case RiverPartitionStatus::Failed:status=GeometryOperationStatus::Failed;break;
            }
            return PreviewAreaResult{status,result.detail.toStdString(),result.areaKm2};
        }
    };
    return calculators;
}
}

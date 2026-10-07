#pragma once
#include <pandoeditor/historicalinstantiation.h>
#include <pandoeditor/geometryoperations.h>

namespace pandoeditor {
// Read-only worker step. The output is bound to the snapshot revision and is
// subsequently applied through one CommandProcessor ChangeSet.
HistoricalInstantiationPlan prepareHistoricalTransaction(
    const ProjectSnapshot&,const HistoricalLibrary&,
    const std::vector<HistoricalAddRequest>&,
    const GeometryCalculator&,const GeometryCancellation& cancelled={});
HistoricalInstantiationPlan prepareHistoricalTransaction(
    const ProjectSnapshot&,std::vector<HistoricalAddition>,
    const GeometryCalculator&,const GeometryCancellation& cancelled={});
}

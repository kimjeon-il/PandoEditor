#pragma once
#include <pandoeditor/map/mapviewstate.h>
#include <pandoeditor/map/territoryselection.h>

namespace pandoeditor {
struct TerritoryCutRequest {
    Geometry source;
    Ring coordinates;
    MapViewState view;
    bool coarsePointer=false;
};
// Application transport into the unchanged fresh cut worker. Decoder exceptions
// escape to the existing GeometryJobFailure boundary.
TerritorySelectionDraftResult prepareTerritoryLineCandidates(const TerritoryCutRequest&,
    const GeometryCancellation& cancelled={});
}

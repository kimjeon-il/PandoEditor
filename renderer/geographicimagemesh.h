#pragma once
#include <pandoeditor/map/mapviewstate.h>
#include <cstdint>
#include <vector>

struct GeographicImageBounds {
    double west=-180,south=-90,east=180,north=90;
};

struct GeographicImageMesh {
    // Interleaved screen x/y and normalized texture u/v.
    std::vector<float> vertices;
    std::vector<std::uint16_t> indices;
};

GeographicImageMesh buildGeographicImageMesh(const GeographicImageBounds& bounds,
                                               const MapViewState& view,
                                               int columns=32,int rows=16);

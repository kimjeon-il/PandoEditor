#pragma once
#include <pandoeditor/document.h>
#include <cstdint>
#include <vector>

namespace pandoeditor {
// Standard GeoPackageBinary, SRS 4326, 2D simple features. Unsupported
// dimensions/CRS and any trailing or truncated bytes are rejected.
std::vector<std::uint8_t> encodeGeoPackageGeometry(const Geometry&);
Geometry decodeGeoPackageGeometry(const std::vector<std::uint8_t>&);
}

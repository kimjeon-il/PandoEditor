#pragma once
#include <QByteArray>
#include <cstdint>
#include <memory>
#include <vector>

struct CountryBaseMesh {
    bool preview=false;
    std::uint32_t sourceCoordinateCount=0;
    std::vector<std::int32_t> positionsMicrodegrees;
    std::vector<std::uint16_t> countryIndices;
    std::vector<std::uint32_t> triangleIndices,lineIndices;
    std::vector<std::uint32_t> countryTriangleRanges,countryBoundaryRanges;
    std::vector<std::int32_t> countryBounds;
    std::vector<std::uint32_t> countryBoundsFlags;
};

std::shared_ptr<const CountryBaseMesh> decodeCountryBaseMesh(const QByteArray& bytes,bool preview);

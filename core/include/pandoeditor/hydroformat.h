#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pandoeditor {
struct HydroByteView { const std::uint8_t* data=nullptr; std::size_t size=0; };
struct HydroTileKey {
    std::uint8_t stage=0;
    std::uint16_t x=0,y=0;
    bool operator<(const HydroTileKey& rhs) const {
        if(stage!=rhs.stage)return stage<rhs.stage;
        if(x!=rhs.x)return x<rhs.x;
        return y<rhs.y;
    }
};
struct HydroPackSpec {
    std::uint32_t id=0,offset=0,length=0;
    std::uint16_t shard=0;
    std::uint8_t stage=0;
};
struct HydroIndex {
    std::map<HydroTileKey,std::vector<std::uint32_t>> tilePacks;
    std::map<std::uint32_t,std::vector<std::uint32_t>> logicalPacks;
    std::map<std::uint32_t,HydroPackSpec> packSpecs;
};
struct HydroPoint {std::int32_t longitude=0,latitude=0;};
using HydroLine = std::vector<HydroPoint>;
using HydroPolygon = std::vector<HydroLine>;
struct HydroDecodedGeometry {
    std::uint8_t kind=0;
    std::vector<HydroLine> lines;
    std::vector<HydroPolygon> polygons;
};
struct HydroPhysicalFeature {
    std::uint32_t fid=0,logicalFid=0;
    std::uint8_t kind=0,stage=0,flags=0;
    std::uint16_t fragmentIndex=0,fragmentCount=0;
    float strokeWidth=0;
    std::int32_t bounds[4]{};
    HydroDecodedGeometry geometry;
    std::vector<std::vector<double>> widths;
};
struct HydroPack {
    std::uint8_t stage=0;
    std::vector<HydroPhysicalFeature> features;
};
struct HydroRiverSegment {
    std::uint32_t fid=0,logicalFid=0;
    HydroPoint start,end;
    double startWidth=0,endWidth=0;
    bool borderAligned=false;
};
struct HydroLakeShape {
    std::uint32_t fid=0,logicalFid=0;
    std::vector<HydroPolygon> polygons;
};
struct HydroRenderPacket {
    std::vector<HydroRiverSegment> rivers;
    std::vector<HydroLakeShape> lakes;
};
HydroIndex decodeHydroIndex(HydroByteView bytes,
                            const std::vector<std::uint64_t>& shardLengths);
HydroPack decodeHydroPack(HydroByteView bytes,std::uint32_t packId,
                          const std::map<std::uint32_t,std::uint32_t>& metadataLogicalIds);
HydroRenderPacket buildHydroRenderPacket(const HydroPack& pack);
}

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
HydroIndex decodeHydroIndex(HydroByteView bytes,
                            const std::vector<std::uint64_t>& shardLengths);
}

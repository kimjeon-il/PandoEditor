#pragma once

#include <pandoeditor/map/renderpacket.h>
#include <array>
#include <memory>
#include <string>

// CPU-side identity of one uploaded QSGGeometry. Owners prevent pointer reuse
// while an Entry still refers to immutable input buffers; this is not a cache.
struct MapResourceIdentity {
    pandoeditor::ObjectRef object;
    pandoeditor::GeometryRef geometry;
    std::uint64_t revision=0;
    int lod=2;
    ProjectionPreparationPolicy preparation=ProjectionPreparationPolicy::Geographic;
    std::array<std::shared_ptr<const void>,3> buffers;
    std::array<std::size_t,3> counts{};
    bool manualPosition=false;
    std::array<double,2> position{};
    std::string slice;

    bool operator==(const MapResourceIdentity& other) const {
        return object==other.object&&geometry==other.geometry&&revision==other.revision&&
            lod==other.lod&&preparation==other.preparation&&buffers==other.buffers&&
            counts==other.counts&&manualPosition==other.manualPosition&&
            position==other.position&&slice==other.slice;
    }
    bool operator!=(const MapResourceIdentity& other) const {return !(*this==other);}
};

template<class Draw> MapResourceIdentity mapDrawResourceIdentity(const Draw& draw) {
    MapResourceIdentity identity;
    identity.object=draw.object;identity.geometry=draw.geometry;
    identity.revision=draw.geometryRevision;identity.lod=draw.lod;
    identity.preparation=draw.preparationPolicy;
    return identity;
}

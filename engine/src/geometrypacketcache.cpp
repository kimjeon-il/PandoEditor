#include <pandoeditor/map/geometrypacketcache.h>
#include <stdexcept>
#include <optional>

namespace {
void requireLod(int lod) {if(lod<0||lod>2)throw std::invalid_argument("invalid packet LOD");}
template<class T> std::size_t bytes(const std::shared_ptr<const std::vector<T>>& value) {
    return value?value->capacity()*sizeof(T):0;
}
std::size_t packetBytes(const PolygonGeometryPacket& packet) {
    return bytes(packet.positions)+bytes(packet.unitSpherePositions)+bytes(packet.indices)+
        bytes(packet.globeIndices)+bytes(packet.ringOffsets)+bytes(packet.polygonOffsets);
}
std::size_t packetBytes(const StrokeGeometryPacket& packet) {
    return bytes(packet.startsEnds)+bytes(packet.unitSphereStartsEnds);
}
std::size_t packetBytes(const PointGeometryPacket& packet) {
    return bytes(packet.positions)+bytes(packet.unitSpherePositions);
}
RenderLod level(int lod) {return lod==0?RenderLod::Coarse:lod==1?RenderLod::Medium:RenderLod::High;}
}

void GeometryPacketCache::setBudget(std::size_t bytes) {budget_=bytes;trim();}
void GeometryPacketCache::protect(std::set<pandoeditor::ObjectRef> objects) {
    protected_=std::move(objects);trim();
}
void GeometryPacketCache::trim() {
    while(stats_.residentBytes>budget_) {
        auto victim=packets_.end();
        for(auto it=packets_.begin();it!=packets_.end();++it) {
            if(protected_.count(it->first.object))continue;
            if(victim==packets_.end()||it->second.used<victim->second.used)victim=it;
        }
        if(victim==packets_.end())break; // Live selected/edit packets may exceed the soft budget.
        stats_.residentBytes-=victim->second.bytes;packets_.erase(victim);++stats_.evictions;
    }
}
void GeometryPacketCache::discardOldVersions(const GeometryPacketCacheKey& key) {
    for(auto it=packets_.begin();it!=packets_.end();) {
        const auto& current=it->first;
        if(current.object==key.object&&current.geometry.id==key.geometry.id&&
           current.geometry.version!=key.geometry.version) {
            stats_.residentBytes-=it->second.bytes;it=packets_.erase(it);++stats_.evictions;
        }else ++it;
    }
}

PolygonGeometryPacket GeometryPacketCache::polygon(const pandoeditor::ObjectRef& object,
    const pandoeditor::GeometryRef& ref,const pandoeditor::Geometry& geometry,int lod,
    ProjectionPreparationPolicy policy) {
    requireLod(lod);
    GeometryPacketCacheKey key{object,ref,PrimitiveKind::Polygon,lod,policy};
    if(const auto it=packets_.find(key);it!=packets_.end()) {
        ++stats_.hits;it->second.used=++clock_;return std::get<PolygonGeometryPacket>(it->second.packet);
    }
    std::optional<pandoeditor::Geometry> derived;
    if(lod!=2)derived=prepareRenderGeometry(geometry,level(lod),LodPolicy::Independent,
        false,policy==ProjectionPreparationPolicy::GlobeReady);
    auto packet=makePolygonGeometryPacket(derived?*derived:geometry);
    const auto size=packetBytes(packet);
    discardOldVersions(key);
    packets_.emplace(std::move(key),Entry{packet,size,++clock_});
    stats_.residentBytes+=size;++stats_.builds;trim();return packet;
}

StrokeGeometryPacket GeometryPacketCache::stroke(const pandoeditor::ObjectRef& object,
    const pandoeditor::GeometryRef& ref,const pandoeditor::Geometry& geometry,int lod,
    ProjectionPreparationPolicy policy) {
    requireLod(lod);
    GeometryPacketCacheKey key{object,ref,PrimitiveKind::Stroke,lod,policy};
    if(const auto it=packets_.find(key);it!=packets_.end()) {
        ++stats_.hits;it->second.used=++clock_;return std::get<StrokeGeometryPacket>(it->second.packet);
    }
    std::optional<pandoeditor::Geometry> derived;
    if(lod!=2)derived=prepareRenderGeometry(geometry,level(lod),LodPolicy::Independent,
        false,policy==ProjectionPreparationPolicy::GlobeReady);
    auto packet=makeStrokeGeometryPacket(derived?*derived:geometry);
    const auto size=packetBytes(packet);
    discardOldVersions(key);
    packets_.emplace(std::move(key),Entry{packet,size,++clock_});
    stats_.residentBytes+=size;++stats_.builds;trim();return packet;
}

PointGeometryPacket GeometryPacketCache::point(const pandoeditor::ObjectRef& object,
    const pandoeditor::GeometryRef& ref,const pandoeditor::Geometry& geometry,int lod,
    ProjectionPreparationPolicy policy) {
    requireLod(lod);
    GeometryPacketCacheKey key{object,ref,PrimitiveKind::Point,lod,policy};
    if(const auto it=packets_.find(key);it!=packets_.end()) {
        ++stats_.hits;it->second.used=++clock_;return std::get<PointGeometryPacket>(it->second.packet);
    }
    auto packet=makePointGeometryPacket(geometry);
    const auto size=packetBytes(packet);
    discardOldVersions(key);
    packets_.emplace(std::move(key),Entry{packet,size,++clock_});
    stats_.residentBytes+=size;++stats_.builds;trim();return packet;
}

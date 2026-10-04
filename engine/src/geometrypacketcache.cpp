#include <pandoeditor/map/geometrypacketcache.h>
#include <pandoeditor/map/renderscene.h>
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

void GeometryPacketCache::setBudget(std::size_t bytes) {budget_=bytes;policy_.setBudget(bytes);trim();}
void GeometryPacketCache::protect(std::set<pandoeditor::ObjectRef> objects) {
    protectReasons(std::move(objects),{});
}
void GeometryPacketCache::protectReasons(std::set<pandoeditor::ObjectRef> selected,
                                        std::set<pandoeditor::ObjectRef> editing) {
    protected_=std::move(selected);editing_=std::move(editing);
    for(const auto& entry:packets_) {
        policy_.setProtection(entry.first,pandoeditor::ResourceProtection::Selected,
                              protected_.count(entry.first.object)!=0);
        policy_.setProtection(entry.first,pandoeditor::ResourceProtection::Editing,
                              editing_.count(entry.first.object)!=0);
    }
    trim();
}
void GeometryPacketCache::trim() {
    for(const auto& key:policy_.trim()){packets_.erase(key);++stats_.evictions;}
    stats_.residentBytes=policy_.snapshot().residentBytes;
}
void GeometryPacketCache::admit(const GeometryPacketCacheKey& key,Packet packet,std::size_t size) {
    auto inserted=packets_.emplace(key,Entry{std::move(packet),size});
    try {
        if(!policy_.admit(key,size)){packets_.erase(inserted.first);return;}
    } catch(...) {packets_.erase(inserted.first);throw;}
    policy_.setProtection(key,pandoeditor::ResourceProtection::Selected,protected_.count(key.object)!=0);
    policy_.setProtection(key,pandoeditor::ResourceProtection::Editing,editing_.count(key.object)!=0);
    discardOldVersions(key);
    stats_.residentBytes=policy_.snapshot().residentBytes;
    trim();
}
void GeometryPacketCache::discardOldVersions(const GeometryPacketCacheKey& key) {
    for(auto it=packets_.begin();it!=packets_.end();) {
        const auto& current=it->first;
        if(current.object==key.object&&current.geometry.id==key.geometry.id&&
           current.geometry.version!=key.geometry.version) {
            policy_.invalidate(current);it=packets_.erase(it);++stats_.evictions;
        }else ++it;
    }
}

PolygonGeometryPacket GeometryPacketCache::polygon(const pandoeditor::ObjectRef& object,
    const pandoeditor::GeometryRef& ref,const pandoeditor::Geometry& geometry,int lod,
    ProjectionPreparationPolicy policy) {
    requireLod(lod);
    GeometryPacketCacheKey key{object,ref,PrimitiveKind::Polygon,lod,policy};
    if(const auto it=packets_.find(key);it!=packets_.end()) {
        ++stats_.hits;policy_.touch(key);return std::get<PolygonGeometryPacket>(it->second.packet);
    }
    policy_.touch(key); // Record a miss without changing packet preparation.
    std::optional<pandoeditor::Geometry> derived;
    if(lod!=2)derived=prepareRenderGeometry(geometry,level(lod),LodPolicy::Independent,
        false,policy==ProjectionPreparationPolicy::GlobeReady);
    auto packet=makePolygonGeometryPacket(derived?*derived:geometry);
    const auto size=packetBytes(packet);
    ++stats_.builds;admit(key,packet,size);return packet;
}

StrokeGeometryPacket GeometryPacketCache::stroke(const pandoeditor::ObjectRef& object,
    const pandoeditor::GeometryRef& ref,const pandoeditor::Geometry& geometry,int lod,
    ProjectionPreparationPolicy policy) {
    requireLod(lod);
    GeometryPacketCacheKey key{object,ref,PrimitiveKind::Stroke,lod,policy};
    if(const auto it=packets_.find(key);it!=packets_.end()) {
        ++stats_.hits;policy_.touch(key);return std::get<StrokeGeometryPacket>(it->second.packet);
    }
    policy_.touch(key); // Record a miss without changing packet preparation.
    std::optional<pandoeditor::Geometry> derived;
    if(lod!=2)derived=prepareRenderGeometry(geometry,level(lod),LodPolicy::Independent,
        false,policy==ProjectionPreparationPolicy::GlobeReady);
    auto packet=makeStrokeGeometryPacket(derived?*derived:geometry);
    const auto size=packetBytes(packet);
    ++stats_.builds;admit(key,packet,size);return packet;
}

PointGeometryPacket GeometryPacketCache::point(const pandoeditor::ObjectRef& object,
    const pandoeditor::GeometryRef& ref,const pandoeditor::Geometry& geometry,int lod,
    ProjectionPreparationPolicy policy) {
    requireLod(lod);
    GeometryPacketCacheKey key{object,ref,PrimitiveKind::Point,lod,policy};
    if(const auto it=packets_.find(key);it!=packets_.end()) {
        ++stats_.hits;policy_.touch(key);return std::get<PointGeometryPacket>(it->second.packet);
    }
    policy_.touch(key);
    auto packet=makePointGeometryPacket(geometry);
    const auto size=packetBytes(packet);
    ++stats_.builds;admit(key,packet,size);return packet;
}

void GeometryPacketCache::setActiveScene(const std::shared_ptr<const RenderScene>& scene) {
    if(const auto old=activeScene_.lock();old&&scene&&scene->preparationIdentity&&old->preparationIdentity==scene->preparationIdentity&&old->geometrySignature==scene->geometrySignature) {
        activeScene_=scene;return;
    }
    std::map<GeometryPacketCacheKey,const void*> active;
    if(scene) {
        for(const auto& p:scene->polygons)active[{p.object,p.geometry,PrimitiveKind::Polygon,p.lod,p.preparationPolicy}]=p.geometryPacket.positions.get();
        for(const auto& p:scene->strokes)active[{p.object,p.geometry,PrimitiveKind::Stroke,p.lod,p.preparationPolicy}]=p.geometryPacket.startsEnds.get();
        for(const auto& p:scene->points)active[{p.object,p.geometry,PrimitiveKind::Point,p.lod,p.preparationPolicy}]=p.geometryPacket.positions.get();
    }
    activePackets_.swap(active);activeScene_=scene;
}
pandoeditor::ResourceCacheSnapshot GeometryPacketCache::resourceCacheSnapshot() const {
    auto result=policy_.snapshot();result.activeBytes=0;
    if(activeScene_.expired())return result;
    for(const auto& entry:packets_)if(const auto active=activePackets_.find(entry.first);active!=activePackets_.end()) {
        const auto identity=std::visit([](const auto& packet)->const void* {
            using T=std::decay_t<decltype(packet)>;
            if constexpr(std::is_same_v<T,StrokeGeometryPacket>)return packet.startsEnds.get();
            else return packet.positions.get();
        },entry.second.packet);
        if(identity==active->second)result.activeBytes+=entry.second.bytes;
    }
    return result;
}

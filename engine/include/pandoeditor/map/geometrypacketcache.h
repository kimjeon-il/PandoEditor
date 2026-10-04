#pragma once
#include <pandoeditor/map/renderpacket.h>
#include <pandoeditor/map/resourcecachepolicy.h>
#include <pandoeditor/map/renderlod.h>
#include <map>
#include <set>
#include <tuple>
#include <variant>

struct RenderScene;

struct GeometryPacketCacheKey {
    pandoeditor::ObjectRef object;
    pandoeditor::GeometryRef geometry;
    PrimitiveKind primitive=PrimitiveKind::Polygon;
    int lod=0;
    ProjectionPreparationPolicy policy=ProjectionPreparationPolicy::Geographic;
    bool operator<(const GeometryPacketCacheKey& other) const {
        return std::tie(object,geometry,primitive,lod,policy)<
               std::tie(other.object,other.geometry,other.primitive,other.lod,other.policy);
    }
};

struct GeometryPacketCacheStats { std::size_t builds=0,hits=0,evictions=0,residentBytes=0; };

class GeometryPacketCache {
public:
    PolygonGeometryPacket polygon(const pandoeditor::ObjectRef&,const pandoeditor::GeometryRef&,
        const pandoeditor::Geometry&,int lod=2,
        ProjectionPreparationPolicy policy=ProjectionPreparationPolicy::Geographic);
    StrokeGeometryPacket stroke(const pandoeditor::ObjectRef&,const pandoeditor::GeometryRef&,
        const pandoeditor::Geometry&,int lod=2,
        ProjectionPreparationPolicy policy=ProjectionPreparationPolicy::Geographic);
    PointGeometryPacket point(const pandoeditor::ObjectRef&,const pandoeditor::GeometryRef&,
        const pandoeditor::Geometry&,int lod=2,
        ProjectionPreparationPolicy policy=ProjectionPreparationPolicy::Geographic);
    GeometryPacketCacheStats stats() const noexcept {return stats_;}
    void setBudget(std::size_t bytes);
    void protect(std::set<pandoeditor::ObjectRef> objects);
    void protectReasons(std::set<pandoeditor::ObjectRef> selected,std::set<pandoeditor::ObjectRef> editing);
    std::size_t budget() const noexcept {return budget_;}
    void clear() {policy_.resetScope();packets_.clear();stats_.residentBytes=0;}
    pandoeditor::ResourceCacheSnapshot resourceCacheSnapshot() const;
    void setActiveScene(const std::shared_ptr<const RenderScene>&);
private:
    using Packet=std::variant<PolygonGeometryPacket,StrokeGeometryPacket,PointGeometryPacket>;
    struct Entry {Packet packet;std::size_t bytes=0;};
    void admit(const GeometryPacketCacheKey&,Packet,std::size_t);
    void trim();
    void discardOldVersions(const GeometryPacketCacheKey& key);
    std::map<GeometryPacketCacheKey,Entry> packets_;
    GeometryPacketCacheStats stats_;
    std::set<pandoeditor::ObjectRef> protected_,editing_;
    std::size_t budget_=192ull*1024*1024;
    std::weak_ptr<const RenderScene> activeScene_;
    std::map<GeometryPacketCacheKey,const void*> activePackets_;
    pandoeditor::ResourceCachePolicy<GeometryPacketCacheKey> policy_{192ull*1024*1024};
};

#include <pandoeditor/document.h>
#include <algorithm>
#include <cmath>
#include <random>
#include <set>
#include <stdexcept>

namespace pandoeditor {
namespace {
void require(bool ok,const std::string& code) { if(!ok) throw std::invalid_argument(code); }
bool opacity(double x) { return std::isfinite(x) && x>=0 && x<=1; }
bool named(const std::string& x) { return x.find_first_not_of(" \t\r\n")!=std::string::npos; }
void point(Point p) { require(std::isfinite(p.x)&&std::isfinite(p.y)&&std::abs(p.x)<=180&&std::abs(p.y)<=90,"INVALID_GEOMETRY: coordinate"); }
void validateGeometry(const Geometry& g) {
    if(g.type=="Point" || g.type=="MultiPoint") {
        require(!g.points.empty() && (g.type!="Point" || g.points.size()==1) && g.lines.empty() && g.polygons.empty(),"INVALID_GEOMETRY: points");
        for(auto p:g.points) point(p);
    } else if(g.type=="LineString" || g.type=="MultiLineString") {
        require(!g.lines.empty() && (g.type!="LineString" || g.lines.size()==1) && g.points.empty() && g.polygons.empty(),"INVALID_GEOMETRY: lines");
        for(const auto& r:g.lines) { require(r.size()>=2,"INVALID_GEOMETRY: line size"); for(auto p:r) point(p); }
    } else {
        require((g.type=="Polygon" || g.type=="MultiPolygon") && !g.polygons.empty() && (g.type!="Polygon" || g.polygons.size()==1) && g.points.empty() && g.lines.empty(),"INVALID_GEOMETRY: polygon type");
        for(const auto& poly:g.polygons) {
            require(!poly.empty(),"INVALID_GEOMETRY: empty polygon");
            for(const auto& ring:poly) {
                require(ring.size()>=4 && ring.front().x==ring.back().x && ring.front().y==ring.back().y,"INVALID_GEOMETRY: open ring");
                double area=0;
                for(std::size_t i=0;i<ring.size();++i) { point(ring[i]); auto a=ring[i],b=ring[(i+1)%ring.size()]; area+=a.x*b.y-b.x*a.y; }
                require(std::abs(area)>=1e-14,"INVALID_GEOMETRY: degenerate ring");
            }
        }
    }
}
}
void GeometryStore::insert(GeometryRef ref,Geometry geometry) {
    require(!ref.id.empty() && ref.version>0,"INVALID_GEOMETRY: version");
    validateGeometry(geometry);
    require(versions_.emplace(std::move(ref),std::make_shared<const Geometry>(std::move(geometry))).second,"DUPLICATE_ID: geometry version");
}
std::shared_ptr<const Geometry> GeometryStore::get(const GeometryRef& ref) const {
    auto it=versions_.find(ref); return it==versions_.end()?nullptr:it->second;
}
ProjectDocument::ProjectDocument(std::vector<Country> countries,std::vector<Layer> layers) {
    std::random_device random;
    static const char hex[]="0123456789abcdef";
    for(int i=0;i<32;++i) documentId+=hex[random()%16];
    presentation.userLayers=std::move(layers);
    for(auto& c:countries) {
        GeometryRef geometry{"legacy-geometry-"+c.id,1};
        geometries.insert(geometry,Geometry{"MultiPolygon",{},{},std::move(c.polygons)});
        const auto baseName=c.name;
        units.push_back({c.id,std::move(c.name),std::move(c.memo),UnitKind::General});
        units.back().locked=c.locked;
        addStaticTerritorialRecords(*this,c.id,geometry);
        units.back().baseName=baseName;
        presentation.membership.emplace(territorialRef(c.id),std::move(c.layerId));
        presentation.objectStyles.emplace(territorialRef(c.id),ObjectStyle{c.color,c.opacity});
    }
}
std::vector<TimelineEntityIdentity> timelineEntityCatalog(const ProjectDocument& d) {
    std::vector<TimelineEntityIdentity> result;
    for(const auto& unit:d.units)result.push_back({unit.id,unit.kind==UnitKind::General?"general":"regional"});
    return result;
}
namespace {
template<class Rows> bool staticRows(const Rows& rows,const std::string& id) {
    std::size_t count=0;
    for(const auto& row:rows)if(row.entityId==id) {
        if(row.validity.from||row.validity.to)return false;
        ++count;
    }
    return count==1;
}
template<class Rows> auto& staticRow(Rows& rows,const std::string& id) {
    if(!staticRows(rows,id))throw TimelineError("TIMELINE_ACTIVATION","Current editing requires a single unbounded record.");
    return *std::find_if(rows.begin(),rows.end(),[&](const auto& row){return row.entityId==id;});
}
}
bool isStaticTimeline(const ProjectDocument& d) {
    for(const auto& unit:d.units)if(!staticRows(d.timelineRecords.lifetimes,unit.id)
      ||!staticRows(d.timelineRecords.geometryBindings,unit.id)||!staticRows(d.timelineRecords.parentRelations,unit.id))return false;
    return true;
}
void requireStaticTimeline(const ProjectDocument& d) {
    if(!isStaticTimeline(d))throw TimelineError("TIMELINE_ACTIVATION","Timeline activation requires T3/T4.");
}
const TimelineGeometryBinding& staticGeometryBinding(const ProjectDocument& d,const std::string& id) {return staticRow(d.timelineRecords.geometryBindings,id);}
TimelineGeometryBinding& staticGeometryBinding(ProjectDocument& d,const std::string& id) {return staticRow(d.timelineRecords.geometryBindings,id);}
const TimelineParentRelation& staticParentRelation(const ProjectDocument& d,const std::string& id) {return staticRow(d.timelineRecords.parentRelations,id);}
TimelineParentRelation& staticParentRelation(ProjectDocument& d,const std::string& id) {return staticRow(d.timelineRecords.parentRelations,id);}
const TimelineLifetime& staticLifetime(const ProjectDocument& d,const std::string& id) {return staticRow(d.timelineRecords.lifetimes,id);}
TimelineLifetime& staticLifetime(ProjectDocument& d,const std::string& id) {return staticRow(d.timelineRecords.lifetimes,id);}
bool isRootGeneral(const ProjectDocument& d,const TerritorialUnit& u) {
    return u.kind==UnitKind::General&&staticParentRelation(d,u.id).parentId.empty();
}
void addStaticTerritorialRecords(ProjectDocument& d,const std::string& id,GeometryRef geometry,
                                 const std::string& parent,const std::string& coverage) {
    for(const auto& row:d.timelineRecords.lifetimes)require(row.entityId!=id,"DUPLICATE_ID: lifetime owner");
    d.timelineRecords.lifetimes.push_back({"lifetime:"+id,id,{}});
    d.timelineRecords.geometryBindings.push_back({"geometry:"+id,id,{},std::move(geometry)});
    d.timelineRecords.parentRelations.push_back({"parent:"+id,id,{},parent,coverage});
}
void removeTerritorialRecords(ProjectDocument& d,const std::vector<std::string>& ids) {
    const std::set<std::string> removed(ids.begin(),ids.end());
    const auto erase=[&](auto& rows){rows.erase(std::remove_if(rows.begin(),rows.end(),[&](const auto& row){return removed.count(row.entityId);}),rows.end());};
    erase(d.timelineRecords.lifetimes);erase(d.timelineRecords.geometryBindings);erase(d.timelineRecords.parentRelations);
}
DocumentIndex validateDocument(const ProjectDocument& d) {
    validatePresentation(d);
    require(!d.documentId.empty(),"INVALID_DOCUMENT: documentId");
    DocumentIndex idx;
    for(std::size_t i=0;i<d.presentation.userLayers.size();++i) {
        const auto& l=d.presentation.userLayers[i];
        require(!l.id.empty() && named(l.name) && opacity(l.opacity),"INVALID_LAYER");
        require(idx.layers.emplace(l.id,i).second,"DUPLICATE_ID: layer");
    }
    for(std::size_t i=0;i<d.units.size();++i) {
        const auto& u=d.units[i]; auto ref=territorialRef(u.id);
        require(!u.id.empty(),"INVALID_UNIT: id");
        require(u.kind==UnitKind::General || u.kind==UnitKind::Regional,"INVALID_UNIT: kind");
        if(u.libraryOrigin) {
            require(!u.libraryOrigin->libraryId.empty()&&!u.libraryOrigin->geometryVersionId.empty(),
                    "INVALID_LIBRARY_ORIGIN: identity");
            if(u.libraryOrigin->referenceDate)parseTemporal(*u.libraryOrigin->referenceDate);
            for(const auto& missing:u.libraryOrigin->missingLibraryRefs)
                require(!missing.empty(),"INVALID_LIBRARY_ORIGIN: missing ref");
        }
        require(idx.objects.emplace(ref,i).second,"DUPLICATE_ID: territorial unit");
        require(d.presentation.objectStyles.count(ref),"DANGLING_REF: presentation missing");
    }
    indexContent(d,idx);
    for(const auto& [ref,layer]:d.presentation.membership) {
        require(idx.objects.count(ref)&&idx.layers.count(layer),"DANGLING_REF: membership");
        idx.dependents[{"userLayer",layer}].push_back(ref);
    }
    for(const auto& [ref,s]:d.presentation.objectStyles) require(idx.objects.count(ref)&&s.color<=0xffffff&&opacity(s.opacity),"INVALID_STYLE");
    const auto records=normalizeTimelineRecords(d.timelineRecords,{timelineEntityCatalog(d),[&](const GeometryRef& ref){
        const auto geometry=d.geometries.get(ref);return geometry&&(geometry->type=="Polygon"||geometry->type=="MultiPolygon");
    }});
    for(const auto& binding:records.geometryBindings)idx.geometryUsers[binding.geometryRef].push_back(territorialRef(binding.entityId));
    for(std::size_t i=0;i<records.parentRelations.size();++i) {
        const auto& row=records.parentRelations[i];const auto owner=territorialRef(row.entityId);
        idx.parentRelationsByUnit[owner].push_back(i);
        if(!row.parentId.empty()) {const auto parent=territorialRef(row.parentId);idx.children[parent].push_back(owner);idx.dependents[parent].push_back(owner);}
    }
    std::set<std::string> extensionIds;
    for(const auto& e:d.extensions) {
        require(!e.id.empty()&&extensionIds.insert(e.id).second&&!e.payload.empty(),"INVALID_EXTENSION");
        require(e.dependencyKnowledge=="known"||e.dependencyKnowledge=="unknown","INVALID_EXTENSION: dependencies");
        require(e.status=="unsupported"||e.status=="migrationArchive","INVALID_EXTENSION: status");
        for(const auto& ref:e.dependencies) {
            require(idx.objects.count(ref)||(ref.domain=="userLayer"&&idx.layers.count(ref.id)),"DANGLING_REF: extension");
            idx.dependents[ref].push_back({"extension",e.id});
        }
    }
    return idx;
}
std::vector<CountryView> countryViews(const ProjectDocument& d) {
    static const std::uint32_t countryDefault=0xcccccc;
    std::vector<CountryView> result;
    requireStaticTimeline(d);
    for(const auto& u:d.units) if(isRootGeneral(d,u)) {
        auto ref=territorialRef(u.id); const auto& s=d.presentation.objectStyles.at(ref);
        result.push_back({u.id,(u.nameExplicit&&!u.name.empty()?u.name:u.baseName),d.geometries.get(staticGeometryBinding(d,u.id).geometryRef)->polygons,(s.explicitColor?s.color:countryDefault),u.notes,s.opacity,nativeLayerId(d,ref),u.locked});
    }
    return result;
}
std::vector<std::string> blockingExtensions(const ProjectDocument& d,const ObjectRef& ref,const std::string& effect) {
    std::vector<std::string> result;
    for(const auto& e:d.extensions) {
        if(e.envelopeExtras!="{}" && effect!="name" && effect!="notes") { result.push_back(e.id); continue; }
        if(e.status=="migrationArchive") continue;
        bool related=std::find(e.dependencies.begin(),e.dependencies.end(),ref)!=e.dependencies.end();
        if(e.dependencyKnowledge=="unknown" && effect!="name" && effect!="notes") { result.push_back(e.id); continue; }
        if(related && (e.forbiddenEffects.empty()||std::find(e.forbiddenEffects.begin(),e.forbiddenEffects.end(),effect)!=e.forbiddenEffects.end())) result.push_back(e.id);
    }
    return result;
}
const std::string& nativeLayerId(const ProjectDocument& d,const ObjectRef& ref) {
    static const std::string none;
    const auto i=d.presentation.membership.find(ref);return i==d.presentation.membership.end()?none:i->second;
}
bool effectAllowed(const ProjectDocument& d,const ObjectRef& ref,const std::string& effect) {
    return blockingExtensions(d,ref,effect).empty();
}
} // namespace pandoeditor

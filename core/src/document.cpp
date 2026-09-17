#include <pandoeditor/document.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <random>
#include <regex>
#include <set>
#include <stdexcept>

namespace pandoeditor {
namespace {
constexpr auto infinity=std::numeric_limits<std::int64_t>::max()/2;
void require(bool ok,const std::string& code) { if(!ok) throw std::invalid_argument(code); }
bool opacity(double x) { return std::isfinite(x) && x>=0 && x<=1; }
bool named(const std::string& x) { return x.find_first_not_of(" \t\r\n")!=std::string::npos; }
int monthDays(std::int64_t year,int month) {
    const auto magnitude=std::abs(year);
    return month==2 ? ((magnitude%4==0 && (magnitude%100!=0 || magnitude%400==0))?29:28)
                    : ((month==4||month==6||month==9||month==11)?30:31);
}
// Public temporal keys retain YYYYMMDD ordering, but incrementing the integer
// creates nonexistent dates at month/year boundaries. Sweep actual calendar days.
std::int64_t nextCalendarDay(std::int64_t key) {
    auto year=key/10000, remainder=key%10000;
    if(remainder<0) { --year; remainder+=10000; }
    int month=static_cast<int>(remainder/100), day=static_cast<int>(remainder%100);
    if(++day>monthDays(year,month)) {
        day=1;
        if(++month>12) { month=1; if(++year==0) year=1; }
    }
    return year*10000+month*100+day;
}
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
TemporalValue parseTemporal(const std::string& text) {
    static const std::regex pattern("^([+-]?)([0-9]{4,6})(?:-([0-9]{2})-([0-9]{2}))?$");
    std::smatch m;
    require(std::regex_match(text,m,pattern),"INVALID_DATE: format");
    require(!m[1].str().empty() || m[2].length()==4,"INVALID_DATE: extended year needs sign");
    int magnitude=std::stoi(m[2]), year=m[1]=="-"?-magnitude:magnitude;
    require(year!=0,"INVALID_DATE: year zero");
    bool date=m[3].matched;
    int month=date?std::stoi(m[3]):1, day=date?std::stoi(m[4]):1;
    require(month>=1&&month<=12,"INVALID_DATE: month");
    int days=monthDays(year,month);
    require(day>=1&&day<=days,"INVALID_DATE: day");
    auto key=static_cast<std::int64_t>(year)*10000;
    return {text,date?"date":"year",key+month*100+day,date?key+month*100+day:key+1231};
}
std::pair<std::int64_t,std::int64_t> temporalBounds(const Validity& v) {
    auto lo=v.from?parseTemporal(*v.from).start:-infinity;
    auto hi=v.to?parseTemporal(*v.to).end:infinity;
    require(lo<=hi,"INVALID_DATE: reversed interval"); return {lo,hi};
}
ProjectDocument::ProjectDocument(std::vector<Country> countries,std::vector<Layer> layers) {
    std::random_device random;
    static const char hex[]="0123456789abcdef";
    for(int i=0;i<32;++i) documentId+=hex[random()%16];
    presentation.userLayers=std::move(layers);
    for(auto& c:countries) {
        GeometryRef geometry{"legacy-geometry-"+c.id,1};
        geometries.insert(geometry,Geometry{"MultiPolygon",{},{},std::move(c.polygons)});
        units.push_back({c.id,std::move(c.name),std::move(c.memo),UnitKind::Country,geometry});
        presentation.membership.emplace(territorialRef(c.id),std::move(c.layerId));
        presentation.objectStyles.emplace(territorialRef(c.id),ObjectStyle{c.color,c.opacity});
    }
}
const TerritorialRelation* effectiveRelation(const ProjectDocument& d,const std::string& id,std::int64_t date) {
    const TerritorialRelation* base=nullptr;
    for(const auto& r:d.relations) if(r.unit==territorialRef(id)) {
        if(!r.dated) base=&r;
        else { auto b=temporalBounds(r.validity); if(date>=b.first&&date<=b.second) return &r; }
    }
    return base;
}
DocumentIndex validateDocument(const ProjectDocument& d) {
    require(!d.documentId.empty(),"INVALID_DOCUMENT: documentId");
    require(!d.presentation.userLayers.empty(),"INVALID_DOCUMENT: empty layers");
    DocumentIndex idx;
    for(std::size_t i=0;i<d.presentation.userLayers.size();++i) {
        const auto& l=d.presentation.userLayers[i];
        require(!l.id.empty() && named(l.name) && opacity(l.opacity),"INVALID_LAYER");
        require(idx.layers.emplace(l.id,i).second,"DUPLICATE_ID: layer");
    }
    std::set<std::int64_t> boundaries{-infinity};
    std::map<ObjectRef,std::pair<std::int64_t,std::int64_t>> life;
    for(std::size_t i=0;i<d.units.size();++i) {
        const auto& u=d.units[i]; auto ref=territorialRef(u.id);
        require(!u.id.empty()&&named(u.name),"INVALID_UNIT: id/name");
        require(u.kind==UnitKind::Country || u.kind==UnitKind::Subunit || u.kind==UnitKind::Region,"INVALID_UNIT: kind");
        require(u.coverageMode=="partition"||u.coverageMode=="explicit","INVALID_UNIT: coverageMode");
        require(idx.objects.emplace(ref,i).second,"DUPLICATE_ID: territorial unit");
        auto g=d.geometries.get(u.geometry);
        require(g && (g->type=="Polygon"||g->type=="MultiPolygon"),"INVALID_GEOMETRY: territorial reference");
        idx.geometryUsers[u.geometry].push_back(ref);
        auto b=temporalBounds(u.validity); life[ref]=b; boundaries.insert(b.first); if(b.second<infinity) boundaries.insert(nextCalendarDay(b.second));
        require(d.presentation.membership.count(ref)&&d.presentation.objectStyles.count(ref),"DANGLING_REF: presentation missing");
    }
    for(const auto& [ref,layer]:d.presentation.membership) {
        require(idx.objects.count(ref)&&idx.layers.count(layer),"DANGLING_REF: membership");
        idx.dependents[{"userLayer",layer}].push_back(ref);
    }
    for(const auto& [ref,s]:d.presentation.objectStyles) require(idx.objects.count(ref)&&s.color<=0xffffff&&opacity(s.opacity),"INVALID_STYLE");
    std::set<std::string> relationIds,baseUnits,extensionIds;
    std::map<ObjectRef,std::vector<const TerritorialRelation*>> dated;
    for(const auto& r:d.relations) {
        require(!r.id.empty()&&relationIds.insert(r.id).second,"DUPLICATE_ID: relation");
        require(idx.objects.count(r.unit),"DANGLING_REF: relation unit");
        idx.relationsByUnit[r.unit].push_back(static_cast<std::size_t>(&r-d.relations.data()));
        if(r.parent) {
            require(idx.objects.count(*r.parent),"DANGLING_REF: parent");
            idx.children[*r.parent].push_back(r.unit); idx.dependents[*r.parent].push_back(r.unit);
        }
        if(r.sovereign) {
            require(idx.objects.count(*r.sovereign)&&d.units[idx.objects.at(*r.sovereign)].kind==UnitKind::Country,"DANGLING_REF: sovereign country");
            idx.dependents[*r.sovereign].push_back(r.unit);
            idx.sovereignMembers[*r.sovereign].push_back(r.unit);
        }
        if(!r.dated) {
            require(!r.validity.from&&!r.validity.to&&baseUnits.insert(r.unit.id).second,"PERIOD_CONFLICT: duplicate base or dated base");
        } else {
            auto b=temporalBounds(r.validity);
            require(b.first>=life.at(r.unit).first&&b.second<=life.at(r.unit).second,"PERIOD_CONFLICT: relation outside unit lifespan");
            for(auto ref:{r.parent,r.sovereign}) if(ref) require(b.first>=life.at(*ref).first&&b.second<=life.at(*ref).second,"PERIOD_CONFLICT: target lifespan");
            dated[r.unit].push_back(&r); boundaries.insert(b.first); if(b.second<infinity) boundaries.insert(nextCalendarDay(b.second));
        }
    }
    for(auto& [ref,rows]:dated) {
        std::sort(rows.begin(),rows.end(),[](auto a,auto b){return temporalBounds(a->validity).first<temporalBounds(b->validity).first;});
        for(std::size_t i=1;i<rows.size();++i) require(temporalBounds(rows[i-1]->validity).second<temporalBounds(rows[i]->validity).first,"PERIOD_CONFLICT: overlapping relations");
    }
    // Evaluate the effective graph at every boundary, including returns to base relations.
    for(auto date:boundaries) {
        std::map<ObjectRef,const TerritorialRelation*> graph;
        auto active=[&](const ObjectRef& ref){ auto b=life.at(ref);return date>=b.first&&date<=b.second; };
        for(const auto& u:d.units) if(active(territorialRef(u.id))) {
            const auto ref=territorialRef(u.id);
            const TerritorialRelation* effective=nullptr;
            auto rows=idx.relationsByUnit.find(ref);
            if(rows!=idx.relationsByUnit.end()) for(auto i:rows->second) {
                const auto& r=d.relations[i];
                if(!r.dated) effective=&r;
                else { auto interval=temporalBounds(r.validity); if(date>=interval.first&&date<=interval.second) { effective=&r; break; } }
            }
            graph[ref]=effective;
        }
        for(const auto& [ref,r]:graph) {
            const auto& u=d.units[idx.objects.at(ref)];
            if(r) for(auto target:{r->parent,r->sovereign}) if(target) require(active(*target),"PERIOD_CONFLICT: inactive target");
            if(u.kind==UnitKind::Country) require(!r || (!r->parent && (!r->sovereign || *r->sovereign==ref)),"SOVEREIGN_MISMATCH: country relationship");
            if(u.kind==UnitKind::Subunit) {
                require(r&&r->parent&&r->sovereign,"SOVEREIGN_MISMATCH: subunit needs parent and country");
                const auto& parent=d.units[idx.objects.at(*r->parent)];
                if(parent.kind==UnitKind::Country) require(*r->parent==*r->sovereign,"SOVEREIGN_MISMATCH");
                else { auto pr=graph.at(*r->parent); require(parent.kind==UnitKind::Subunit&&pr&&pr->sovereign&&*pr->sovereign==*r->sovereign,"SOVEREIGN_MISMATCH"); }
            }
        }
        std::map<ObjectRef,int> visited;
        std::function<void(const ObjectRef&)> visit=[&](const ObjectRef& ref) {
            require(visited[ref]!=1,"RELATION_CYCLE"); if(visited[ref]==2) return;
            visited[ref]=1; auto r=graph.at(ref); if(r&&r->parent) visit(*r->parent); visited[ref]=2;
        };
        for(const auto& [ref,r]:graph) visit(ref);
    }
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
    std::vector<CountryView> result;
    for(const auto& u:d.units) if(u.kind==UnitKind::Country) {
        auto ref=territorialRef(u.id); const auto& s=d.presentation.objectStyles.at(ref);
        result.push_back({u.id,u.name,d.geometries.get(u.geometry)->polygons,s.color,u.notes,s.opacity,d.presentation.membership.at(ref),u.locked});
    }
    return result;
}
bool effectAllowed(const ProjectDocument& d,const ObjectRef& ref,const std::string& effect) {
    for(const auto& e:d.extensions) {
        if(e.envelopeExtras!="{}" && effect!="name" && effect!="notes") return false;
        if(e.status=="migrationArchive") continue;
        bool related=std::find(e.dependencies.begin(),e.dependencies.end(),ref)!=e.dependencies.end();
        if(e.dependencyKnowledge=="unknown" && effect!="name" && effect!="notes") return false;
        if(related && (e.forbiddenEffects.empty()||std::find(e.forbiddenEffects.begin(),e.forbiddenEffects.end(),effect)!=e.forbiddenEffects.end())) return false;
    }
    return true;
}
} // namespace pandoeditor

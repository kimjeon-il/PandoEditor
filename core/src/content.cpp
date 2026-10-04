#include <pandoeditor/document.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace pandoeditor {
namespace {
void require(bool ok, const char* message) {
    if (!ok) throw std::invalid_argument(message);
}
auto sourceKey(const SourceProvenance& s) {
    return std::tie(s.kind,s.dataset,s.version,s.sourceId,s.sourceFormat,s.sourceType,s.importedAt,s.details);
}
auto validityKey(const Validity& v) { return std::tie(v.from,v.to); }
template<class T,class Key> bool equalContentRows(const T& a,const T& b,Key key) {
    return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),
        [&](const auto& x,const auto& y){return key(x)==key(y);});
}
void source(const SourceProvenance& s) {
    static const std::set<std::string> kinds={"user","builtin","library","gis","legacy","plugin","unsupported"};
    require(kinds.count(s.kind),"INVALID_SOURCE: kind");
    require(!s.details.empty(),"INVALID_SOURCE: details");
}
}

void indexContent(const ProjectDocument& d,DocumentIndex& idx) {
    auto add=[&](const char* domain,const std::string& id,std::size_t i) {
        ObjectRef ref{domain,id};
        require(!id.empty() && idx.objects.emplace(ref,i).second,"DUPLICATE_ID: content");
        return ref;
    };
    auto geometry=[&](const ObjectRef& ref,const GeometryRef& g) {
        auto value=d.geometries.get(g);
        require(bool(value),"DANGLING_REF: content geometry");
        idx.geometryUsers[g].push_back(ref);
        return value;
    };
    auto territory=[&](const ObjectRef& owner,const ObjectRef& ref) {
        require(ref.domain=="territorial" && idx.objects.count(ref),"DANGLING_REF: content territory");
        idx.dependents[ref].push_back(owner);
    };
    for(const auto& [ref,details]:d.countryDetails) {
        require(ref.domain=="territorial" && idx.objects.count(ref),"INVALID_COUNTRY_DETAILS");
    }
    for(const auto& [ref,s]:d.symbols) {
        require(ref.domain=="territorial" && idx.objects.count(ref),"DANGLING_REF: symbol owner");
        require(s.policy==FlagPolicy::Default || s.policy==FlagPolicy::None || s.policy==FlagPolicy::Embedded,"INVALID_FLAG: policy");
        require(s.policy==FlagPolicy::Embedded ? s.embeddedDataUrl.rfind("data:image/",0)==0 : s.embeddedDataUrl.empty(),"INVALID_FLAG: payload");
    }
    static const std::set<std::string> labelKinds={"capital","city","town","region","mountain","water","custom"};
    for(std::size_t i=0;i<d.labels.size();++i) {
        const auto& v=d.labels[i]; auto ref=add("label",v.id,i);
        require(labelKinds.count(v.kind),"INVALID_LABEL: kind");
        require(geometry(ref,v.geometry)->type=="Point","INVALID_LABEL: geometry");
        if(v.territory) territory(ref,*v.territory);
        source(v.source);
    }
    for(std::size_t i=0;i<d.hydro.size();++i) {
        const auto& v=d.hydro[i]; auto ref=add("hydro",v.id,i);
        const auto type=geometry(ref,v.geometry)->type;
        require((v.kind=="river" && (type=="LineString" || type=="MultiLineString")) ||
            (v.kind=="lake" && (type=="Polygon" || type=="MultiPolygon")),"INVALID_HYDRO: geometry or kind");
        require(v.color<=0xffffff,"INVALID_HYDRO: color"); source(v.source);
    }
    for(std::size_t i=0;i<d.distributionLayers.size();++i) {
        const auto& v=d.distributionLayers[i]; add("distributionLayer",v.id,i);
        require(!v.valueScale.manual||(std::isfinite(v.valueScale.min)&&std::isfinite(v.valueScale.max)&&v.valueScale.min<v.valueScale.max),"INVALID_DISTRIBUTION: value scale");
        require(v.color<=0xffffff,"INVALID_DISTRIBUTION: color"); temporalBounds(v.validity);
    }
    for(const auto& v:d.distributionLayers) {
        std::set<std::string> seen{v.id}; auto parent=v.parentId;
        while(parent) {
            require(seen.insert(*parent).second,"DISTRIBUTION_CYCLE");
            auto it=idx.objects.find({"distributionLayer",*parent});
            require(it!=idx.objects.end(),"DANGLING_REF: distribution parent");
            const auto& p=d.distributionLayers.at(it->second);
            parent=p.parentId;
        }
        if(v.parentId) idx.dependents[{"distributionLayer",*v.parentId}].push_back({"distributionLayer",v.id});
    }
    for(std::size_t i=0;i<d.distributionEntries.size();++i) {
        const auto& v=d.distributionEntries[i]; auto ref=add("distributionEntry",v.id,i);
        require(idx.objects.count({"distributionLayer",v.layerId}),"DANGLING_REF: distribution layer");
        idx.dependents[{"distributionLayer",v.layerId}].push_back(ref);
        require(bool(v.territory)!=bool(v.geometry),"INVALID_DISTRIBUTION: exactly one source required");
        require(std::isfinite(v.value),"INVALID_DISTRIBUTION: value");
        temporalBounds(v.validity);
        if(v.territory) territory(ref,*v.territory);
        if(v.geometry) {
            const auto type=geometry(ref,*v.geometry)->type;
            require(type=="Polygon" || type=="MultiPolygon","INVALID_DISTRIBUTION: geometry");
        }
    }
    for(std::size_t i=0;i<d.genericFeatures.size();++i) {
        const auto& v=d.genericFeatures[i]; geometry(add("generic",v.id,i),v.geometry);
        require(v.color<=0xffffff && v.fallbackOnly,"INVALID_GENERIC: fallback or color"); source(v.source);
    }
}

bool sameContent(const ProjectDocument& a,const ProjectDocument& b) {
    return equalContentRows(a.countryDetails,b.countryDetails,[](const auto& v){return std::tie(v.first,v.second.capital);}) &&
        equalContentRows(a.symbols,b.symbols,[](const auto& v){return std::tie(v.first,v.second.policy,v.second.embeddedDataUrl);}) &&
        equalContentRows(a.labels,b.labels,[](const auto& v){return std::make_tuple(v.id,v.name,v.kind,v.notes,v.geometry,v.territory,sourceKey(v.source));}) &&
        equalContentRows(a.hydro,b.hydro,[](const auto& v){return std::make_tuple(v.id,v.name,v.kind,v.notes,v.geometry,v.color,v.locked,sourceKey(v.source),v.sourceFeatureId);}) &&
        equalContentRows(a.distributionLayers,b.distributionLayers,[](const auto& v){return std::make_tuple(v.id,v.name,v.unit,v.color,v.locked,v.parentId,v.groups,validityKey(v.validity),v.metadata,v.valueScale.manual,v.valueScale.manual?v.valueScale.min:0,v.valueScale.manual?v.valueScale.max:1);}) &&
        equalContentRows(a.distributionEntries,b.distributionEntries,[](const auto& v){return std::make_tuple(v.id,v.layerId,v.territory,v.geometry,v.value,v.certainty,v.metadata,validityKey(v.validity));}) &&
        equalContentRows(a.genericFeatures,b.genericFeatures,[](const auto& v){return std::make_tuple(v.id,v.name,v.notes,v.geometry,v.color,v.locked,v.fallbackOnly,sourceKey(v.source));}) &&
        std::tie(a.physicalData.dataset,a.physicalData.version,a.physicalData.source,a.physicalData.hiddenHydroIds)==
        std::tie(b.physicalData.dataset,b.physicalData.version,b.physicalData.source,b.physicalData.hiddenHydroIds);
}
std::optional<GeometryRef> objectGeometry(const ProjectDocument& d,const DocumentIndex& index,const ObjectRef& ref) {
    const auto found=index.objects.find(ref); if(found==index.objects.end()) return {};
    const auto i=found->second;
    if(ref.domain=="territorial") return staticGeometryBinding(d,d.units.at(i).id).geometryRef;
    if(ref.domain=="label") return d.labels.at(i).geometry;
    if(ref.domain=="hydro") return d.hydro.at(i).geometry;
    if(ref.domain=="generic") return d.genericFeatures.at(i).geometry;
    if(ref.domain=="distributionEntry") {
        const auto& entry=d.distributionEntries.at(i);
        if(entry.geometry) return entry.geometry;
        if(entry.territory) return objectGeometry(d,index,*entry.territory);
    }
    return {};
}
bool objectLocked(const ProjectDocument& d,const DocumentIndex& index,const ObjectRef& ref) {
    const auto found=index.objects.find(ref); if(found==index.objects.end()) return true;
    const auto member=d.presentation.membership.find(ref);
    if(member!=d.presentation.membership.end()) for(const auto& layer:d.presentation.userLayers)
        if(layer.id==member->second && layer.locked) return true;
    const auto i=found->second;
    if(ref.domain=="territorial") return d.units.at(i).locked;
    if(ref.domain=="hydro") return d.hydro.at(i).locked || d.hydro.at(i).source.kind=="builtin";
    if(ref.domain=="generic") return d.genericFeatures.at(i).locked;
    if(ref.domain=="distributionLayer") return d.distributionLayers.at(i).locked;
    if(ref.domain=="distributionEntry") return objectLocked(d,index,{"distributionLayer",d.distributionEntries.at(i).layerId});
    return false;
}
std::string contentGroup(const ProjectDocument& d,const ObjectRef& ref) {
    if(ref.domain=="label") return "labels";
    if(ref.domain=="generic") return "genericFeatures";
    if(ref.domain=="hydro") {
        for(const auto& v:d.hydro) if(v.id==ref.id) return v.kind=="river"?"rivers":"lakes";
    }
    if(ref.domain=="distributionEntry" || ref.domain=="distributionLayer") {
        return "distributions";
    }
    return {};
}
}

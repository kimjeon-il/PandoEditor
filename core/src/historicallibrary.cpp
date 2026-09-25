#include <pandoeditor/historicallibrary.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace pandoeditor {
namespace {
std::string trim(const std::string& s) {
    const auto first=s.find_first_not_of(" \t\r\n"),last=s.find_last_not_of(" \t\r\n");
    return first==std::string::npos?"":s.substr(first,last-first+1);
}
std::string lower(std::string s) {
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return s;
}
Validity normalized(Validity source) {
    const auto interval=normalizeTemporalInterval(source.from,source.to);
    return {interval.validFrom,interval.validTo};
}
int year(const std::optional<std::string>& date,int fallback) {
    return date?parseTemporal(*date).year:fallback;
}
std::string mode(std::string requested) {
    requested=trim(requested);
    if(requested.empty())return "independent";
    if(requested=="country-territory-priority")return "territory-replacement";
    if(requested=="independent"||requested=="territory-replacement")return requested;
    throw std::invalid_argument("INVALID_LIBRARY: instantiation mode");
}
}
UnitKind historicalUnitKind(const std::string& raw) {
    const auto type=lower(trim(raw));
    if(type=="country")return UnitKind::Country;
    if(type=="subunit"||type=="territory"||type=="admin")return UnitKind::Subunit;
    if(type=="region")return UnitKind::Region;
    throw std::invalid_argument("INVALID_LIBRARY: entity type");
}
HistoricalLibrary::HistoricalLibrary(int schemaVersion,std::vector<HistoricalEntity> entities,
                                     std::vector<WorldSnapshot> snapshots)
    :entities_(std::move(entities)),snapshots_(std::move(snapshots)) {
    if(schemaVersion!=historicalLibrarySchemaVersion)
        throw std::invalid_argument("UNSUPPORTED_LIBRARY_SCHEMA");
    for(std::size_t i=0;i<entities_.size();++i) {
        auto& entity=entities_[i];
        entity.libraryId=trim(entity.libraryId);
        if(entity.libraryId.empty()||!entityIds_.emplace(entity.libraryId,i).second)
            throw std::invalid_argument("INVALID_LIBRARY: duplicate or empty entity ID");
        if(entity.canonicalName.empty())entity.canonicalName=entity.libraryId;
        entity.validity=normalized(entity.validity);
        entity.instantiation.mode=mode(entity.instantiation.mode);
        std::set<std::string> seen;
        for(auto& version:entity.geometryVersions) {
            version.id=trim(version.id);
            if(version.id.empty()||!seen.insert(version.id).second)
                throw std::invalid_argument("INVALID_LIBRARY: duplicate or empty version ID");
            version.validity=normalized(version.validity);
            if(version.geometry.type!="Polygon"&&version.geometry.type!="MultiPolygon")
                throw std::invalid_argument("INVALID_LIBRARY: polygon version required");
            GeometryStore validation;
            validation.insert({version.id,1},version.geometry);
        }
    }
    for(std::size_t i=0;i<snapshots_.size();++i) {
        auto& snapshot=snapshots_[i];
        snapshot.id=trim(snapshot.id);
        if(snapshot.id.empty()||!snapshotIds_.emplace(snapshot.id,i).second)
            throw std::invalid_argument("INVALID_LIBRARY: duplicate or empty snapshot ID");
        if(snapshot.name.empty())snapshot.name=snapshot.id;
        snapshot.referenceDate=normalizeTemporal(snapshot.referenceDate);
        std::set<std::string> seen;
        auto& refs=snapshot.entityRefs;
        refs.erase(std::remove_if(refs.begin(),refs.end(),[&](auto& ref){
            ref=trim(ref);return ref.empty()||!seen.insert(ref).second;
        }),refs.end());
    }
}
const HistoricalEntity* HistoricalLibrary::get(const std::string& id) const {
    const auto it=entityIds_.find(trim(id));
    return it==entityIds_.end()?nullptr:&entities_[it->second];
}
const WorldSnapshot* HistoricalLibrary::getSnapshot(const std::string& id) const {
    const auto it=snapshotIds_.find(trim(id));
    return it==snapshotIds_.end()?nullptr:&snapshots_[it->second];
}
std::vector<const HistoricalEntity*> HistoricalLibrary::list() const {
    std::vector<const HistoricalEntity*> result;
    for(const auto& entity:entities_)result.push_back(&entity);
    return result;
}
std::vector<const HistoricalEntity*> HistoricalLibrary::search(const HistoricalSearch& options) const {
    std::vector<const HistoricalEntity*> result;
    const auto needle=lower(trim(options.query));
    const auto point=trim(options.referenceDate).empty()
        ?std::optional<TemporalValue>{}:std::optional<TemporalValue>{parseTemporal(options.referenceDate)};
    for(const auto& entity:entities_) {
        if(!options.type.empty()&&historicalUnitKind(options.type)!=entity.type)continue;
        if(options.status==HistoricalStatus::Current&&entity.validity.to)continue;
        if(options.status==HistoricalStatus::Past&&!entity.validity.to)continue;
        if(!options.geographicRegion.empty()&&trim(options.geographicRegion)!=entity.geographicRegion)continue;
        if(point&&!temporalContains(normalizeTemporalInterval(entity.validity.from,entity.validity.to),*point))continue;
        bool matching=needle.empty();
        const auto nameMatches=[&](const std::string& value){return lower(value).find(needle)!=std::string::npos;};
        matching=matching||nameMatches(entity.canonicalName);
        for(const auto& [language,name]:entity.displayNames)matching=matching||nameMatches(name);
        for(const auto& name:entity.alternateNames)matching=matching||nameMatches(name);
        if(matching)result.push_back(&entity);
    }
    return result;
}
std::vector<std::string> HistoricalLibrary::entityRefsWithChildren(
    const std::vector<std::string>& rootIds,const std::string& depth) const {
    if(depth!="none"&&depth!="level1"&&depth!="all")
        throw std::invalid_argument("INVALID_LIBRARY: child depth");
    std::set<std::string> selected;
    std::vector<std::string> result,frontier;
    for(const auto& id:rootIds)if(selected.insert(id).second) {
        result.push_back(id);frontier.push_back(id);
    }
    if(depth=="none")return result;
    while(!frontier.empty()) {
        const std::set<std::string> parents(frontier.begin(),frontier.end());
        std::vector<std::string> next;
        for(const auto& entity:entities_)
            if(parents.count(entity.parentLibraryId)&&selected.insert(entity.libraryId).second) {
                result.push_back(entity.libraryId);next.push_back(entity.libraryId);
            }
        if(depth=="level1")break;
        frontier=std::move(next);
    }
    return result;
}
const HistoricalGeometryVersion* HistoricalLibrary::selectGeometryVersion(
                                    const std::string& libraryId,
                                    const std::string& referenceDate) const {
    const auto entity=get(libraryId);
    if(!entity||entity->geometryVersions.empty())return nullptr;
    const auto& versions=entity->geometryVersions;
    const auto reference=trim(referenceDate).empty()
        ?std::optional<TemporalValue>{}:std::optional<TemporalValue>{parseTemporal(referenceDate)};
    const HistoricalGeometryVersion* best=nullptr;
    int bestStart=0;
    for(const auto& version:versions) {
        if(reference&&!temporalContains(
            normalizeTemporalInterval(version.validity.from,version.validity.to),*reference))continue;
        const auto start=year(version.validity.from,std::numeric_limits<int>::min());
        if(!best||start>bestStart){best=&version;bestStart=start;}
    }
    if(best)return best;
    if(!reference)return &versions.back();
    int distance=std::numeric_limits<int>::max();
    for(const auto& version:versions) {
        const auto candidateYear=year(version.validity.from,year(version.validity.to,reference->year));
        const auto delta=std::abs(candidateYear-reference->year);
        if(!best||delta<distance){best=&version;distance=delta;}
    }
    return best;
}
HistoricalSelection HistoricalLibrary::instantiate(const std::string& libraryId,
                                    const std::string& referenceDate,
                                    const std::string& geometryVersionId) const {
    const auto entity=get(libraryId);
    if(!entity)throw std::invalid_argument("INVALID_LIBRARY: missing entity");
    const HistoricalGeometryVersion* version=nullptr;
    if(!geometryVersionId.empty())for(const auto& candidate:entity->geometryVersions)
        if(candidate.id==geometryVersionId){version=&candidate;break;}
    if(geometryVersionId.empty())version=selectGeometryVersion(libraryId,referenceDate);
    if(!version)throw std::invalid_argument("INVALID_LIBRARY: missing geometry version");
    const auto translated=entity->displayNames.find("ko");
    return {entity->libraryId,version->id,
            translated!=entity->displayNames.end()&&!translated->second.empty()
                ?translated->second:entity->canonicalName,
            entity->type,version->geometry,
            {entity->validity.from?entity->validity.from:version->validity.from,
             entity->validity.to?entity->validity.to:version->validity.to},
            entity->parentLibraryId,entity->sovereignLibraryId,
            entity->instantiation,entity->metadata,entity->sourceInfo,
            version->certainty,version->datePrecision,version->sourceId,
            version->partial,version->missingSourceIds};
}
}

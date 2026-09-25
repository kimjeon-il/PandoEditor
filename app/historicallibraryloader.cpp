#include "historicallibraryloader.h"
#include "webjson.h"
#include <cmath>
#include <set>
#include <stdexcept>

namespace pandoeditor {
namespace {
using V=losslessjson::Value;
using webjson::at;
using webjson::text;
void require(bool okay,const char* message) {
    if(!okay)throw std::invalid_argument(message);
}
std::optional<std::string> optionalText(const V& value) {
    auto raw=text(value);return raw.empty()?std::nullopt:std::optional<std::string>(raw);
}
Point point(const V& input) {
    const auto& values=webjson::array(input,"coordinate");
    require(values.size()==2&&values[0].kind==V::Number&&values[1].kind==V::Number,
            "INVALID_LIBRARY_GEOMETRY: coordinate");
    const auto x=values[0].raw.toDouble(),y=values[1].raw.toDouble();
    require(std::isfinite(x)&&std::isfinite(y),"INVALID_LIBRARY_GEOMETRY: nonfinite coordinate");
    return {x,y};
}
Ring ring(const V& input) {
    Ring result;for(const auto& value:webjson::array(input,"ring"))result.push_back(point(value));
    return result;
}
Polygon polygon(const V& input) {
    Polygon result;for(const auto& value:webjson::array(input,"polygon"))result.push_back(ring(value));
    return result;
}
Geometry polygonGeometry(const V& input) {
    require(input.kind==V::Object,"INVALID_LIBRARY_GEOMETRY: missing object");
    Geometry geometry;
    geometry.type=text(at(input,"type"));
    const auto& coordinates=at(input,"coordinates");
    if(geometry.type=="Polygon")geometry.polygons.push_back(polygon(coordinates));
    else if(geometry.type=="MultiPolygon")
        for(const auto& item:webjson::array(coordinates,"multipolygon"))geometry.polygons.push_back(polygon(item));
    else throw std::invalid_argument("INVALID_LIBRARY_GEOMETRY: polygon required");
    GeometryStore validator;validator.insert({"historical-version",1},geometry);
    return geometry;
}
std::vector<std::string> uniqueStrings(const V& input) {
    std::vector<std::string> result;std::set<std::string> seen;
    for(const auto& value:webjson::optionalArray(input,"strings")) {
        auto name=text(value);if(!name.empty()&&seen.insert(name).second)result.push_back(name);
    }
    return result;
}
std::string objectJson(const V& input) {
    return input.kind==V::Object?input.encode().toStdString():"{}";
}
Geometry calculate(const GeometryOperationRequest& request,const GeometryCalculator& calculator) {
    require(static_cast<bool>(calculator),"INVALID_LIBRARY_GEOMETRY: missing M4 calculator");
    const auto result=calculator(request,{});
    if(result.status!=GeometryOperationStatus::Completed)
        throw std::invalid_argument("INVALID_LIBRARY_GEOMETRY: M4 operation failed or empty");
    return result.geometry;
}
}
HistoricalSource parseHistoricalLibrarySource(const QByteArray& bytes) {
    const auto root=losslessjson::parse(bytes);
    require(root.kind==V::Object&&at(root,"schemaVersion").kind==V::Number
        &&webjson::number(at(root,"schemaVersion"))==historicalLibrarySchemaVersion,
        "UNSUPPORTED_LIBRARY_SCHEMA");
    HistoricalSource source;std::set<std::string> ids,snapshotIds;
    for(const auto& raw:webjson::array(at(root,"entities"),"entities")) {
        require(raw.kind==V::Object,"INVALID_LIBRARY: entity");
        HistoricalSourceEntity row;auto& entity=row.entity;
        entity.libraryId=text(at(raw,"libraryId"));
        require(!entity.libraryId.empty()&&ids.insert(entity.libraryId).second,
                "INVALID_LIBRARY: duplicate or empty entity");
        entity.type=historicalUnitKind(text(at(raw,"type")));
        entity.canonicalName=text(at(raw,"canonicalName"));
        const auto& names=at(raw,"displayNames");
        if(names.kind==V::Object)for(const auto& [key,value]:names.object)
            entity.displayNames.emplace(key,text(value));
        entity.alternateNames=uniqueStrings(at(raw,"alternateNames"));
        entity.validity={optionalText(at(raw,"startDate")),optionalText(at(raw,"endDate"))};
        temporalBounds(entity.validity);
        entity.parentLibraryId=text(at(raw,"parentLibraryId"));
        entity.sovereignLibraryId=text(at(raw,"sovereignLibraryId"));
        entity.metadata=objectJson(at(raw,"metadata"));
        entity.sourceInfo=objectJson(at(raw,"sourceInfo"));
        entity.geographicRegion=text(at(at(raw,"metadata"),"geographicRegion"));
        const auto& inst=at(raw,"instantiation");
        entity.instantiation.mode=text(at(inst,"mode"));
        const auto& updates=at(inst,"countryUpdates");
        if(updates.kind==V::Object)for(const auto& [id,value]:updates.object) {
            const auto name=text(at(value,"name"));
            if(!id.empty()&&!name.empty())entity.instantiation.countryNameUpdates[id]=name;
        }
        std::set<std::string> versionIds;
        for(const auto& rawVersion:webjson::optionalArray(at(raw,"geometryVersions"),"versions")) {
            HistoricalSourceVersion definition;
            auto& version=definition.version;
            version.id=text(at(rawVersion,"id"));
            require(!version.id.empty()&&versionIds.insert(version.id).second,
                    "INVALID_LIBRARY: duplicate or empty geometry version");
            version.validity={optionalText(at(rawVersion,"validFrom")),optionalText(at(rawVersion,"validTo"))};
            temporalBounds(version.validity);
            version.datePrecision=text(at(rawVersion,"datePrecision"));
            version.certainty=text(at(rawVersion,"certainty"));
            version.sourceId=text(at(rawVersion,"sourceId"));
            version.notes=text(at(rawVersion,"notes"));
            if(at(rawVersion,"geometry").kind!=V::Null) {
                version.geometry=polygonGeometry(at(rawVersion,"geometry"));definition.direct=true;
            }
            definition.memberCountryIds=uniqueStrings(at(rawVersion,"memberCountryIds"));
            if(at(rawVersion,"includeGeometry").kind!=V::Null)
                definition.includeGeometry=polygonGeometry(at(rawVersion,"includeGeometry"));
            if(at(rawVersion,"excludeGeometry").kind!=V::Null)
                definition.excludeGeometry=polygonGeometry(at(rawVersion,"excludeGeometry"));
            row.versions.push_back(std::move(definition));
        }
        source.entities.push_back(std::move(row));
    }
    for(const auto& raw:webjson::optionalArray(at(root,"snapshots"),"snapshots")) {
        WorldSnapshot snapshot;
        snapshot.id=text(at(raw,"id"));
        require(!snapshot.id.empty()&&snapshotIds.insert(snapshot.id).second,
                "INVALID_LIBRARY: duplicate or empty snapshot");
        snapshot.name=text(at(raw,"name"));
        snapshot.referenceDate=optionalText(at(raw,"referenceDate"));
        if(snapshot.referenceDate)parseTemporal(*snapshot.referenceDate);
        snapshot.entityRefs=uniqueStrings(at(raw,"entityRefs"));
        snapshot.metadata=objectJson(at(raw,"metadata"));
        snapshot.sourceInfo=objectJson(at(raw,"sourceInfo"));
        source.snapshots.push_back(std::move(snapshot));
    }
    return source;
}
HistoricalMaterialization materializeHistoricalSource(
    const HistoricalSource& source,
    const std::function<std::optional<Geometry>(const std::string&)>& countryGeometry,
    const GeometryCalculator& calculator) {
    std::vector<HistoricalEntity> entities;
    std::vector<std::string> missingEntities,missingCountries;
    for(const auto& definition:source.entities) {
        auto entity=definition.entity;
        for(const auto& raw:definition.versions) {
            auto version=raw.version;
            std::optional<Geometry> geometry;
            if(raw.direct)geometry=version.geometry;
            else {
                std::vector<Geometry> members;
                for(const auto& id:raw.memberCountryIds) {
                    const auto item=countryGeometry(id);
                    if(item)members.push_back(*item);
                    else {
                        missingCountries.push_back(id);
                        version.partial=true;
                        version.missingSourceIds.push_back("current-country:"+id);
                    }
                }
                if(members.size()==1)geometry=std::move(members.front());
                else if(members.size()>1)
                    geometry=calculate({GeometryOperation::Union,{},{},std::move(members)},calculator);
            }
            if(!geometry)continue;
            if(raw.includeGeometry)geometry=calculate(
                {GeometryOperation::Union,*geometry,*raw.includeGeometry,{}},calculator);
            if(raw.excludeGeometry)geometry=calculate(
                {GeometryOperation::Difference,*geometry,*raw.excludeGeometry,{}},calculator);
            version.geometry=std::move(*geometry);
            entity.geometryVersions.push_back(std::move(version));
        }
        if(entity.geometryVersions.empty())missingEntities.push_back(entity.libraryId);
        else entities.push_back(std::move(entity));
    }
    return {HistoricalLibrary(historicalLibrarySchemaVersion,std::move(entities),source.snapshots),
            std::move(missingEntities),std::move(missingCountries)};
}
}

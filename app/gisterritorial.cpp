#include "gisterritorial.h"
#include "giszip.h"
#include "gisgeopackage.h"
#include "webjson.h"
#include <pandoeditor/project.h>
#include <pandoeditor/geometrypredicates.h>
#include <QString>
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <stdexcept>

namespace pandoeditor {
namespace {
using V=losslessjson::Value;
void require(bool ok,const char* message) {
    if(!ok)throw std::invalid_argument(message);
}
std::string property(const V& props,const std::string& key) {
    if(key.empty())return {};
    const auto& value=webjson::at(props,key);
    if(value.kind==V::Null)return {};
    require(value.kind==V::String||value.kind==V::Number,"INVALID_GIS_MAPPING: string field");
    return webjson::jsString(value);
}
std::optional<std::uint32_t> color(const std::string& value) {
    if(value.empty())return {};
    require(value.size()==7&&value[0]=='#'&&
        std::all_of(value.begin()+1,value.end(),[](unsigned char c){return std::isxdigit(c)!=0;}),
        "INVALID_GIS_COLOR");
    bool ok=false;
    const auto parsed=QString::fromStdString(value.substr(1)).toUInt(&ok,16);
    require(ok&&parsed<=0xffffff,"INVALID_GIS_COLOR");
    return parsed;
}
const TerritorialUnit* existing(const ProjectSnapshot& snapshot,const ObjectRef& ref) {
    const auto found=snapshot.index().objects.find(ref);
    return found==snapshot.index().objects.end()?nullptr:&snapshot.document().units.at(found->second);
}
Geometry calculate(const GeometryCalculator& calculator,GeometryOperation operation,
    const Geometry& left,const Geometry& right,const GeometryCancellation& cancelled) {
    require(!cancelled||!cancelled(),"CANCELLED_GIS_IMPORT");
    const auto output=calculator({operation,left,right},cancelled);
    require(output.status!=GeometryOperationStatus::Cancelled,"CANCELLED_GIS_IMPORT");
    require(output.succeeded(),"GIS_M4_GEOMETRY_FAILED");
    return output.geometry;
}
bool populated(const Geometry& geometry) {return !geometry.polygons.empty();}
void checkLayerTarget(const std::string& source,const GisTerritorialMapping& mapping) {
    const auto selected=mapping.target==GisExchangeTarget::Country?"country":
        mapping.target==GisExchangeTarget::Subunit?"subunit":"region";
    require(source==selected,"GIS_LAYER_TARGET_MISMATCH");
}
}
GisTerritorialImportPlan prepareGisTerritorialImport(const ProjectSnapshot& snapshot,
    const GisGeoJsonCollection& collection,std::string planId,GisSource source,
    const GisTerritorialMapping& mapping,const GeometryCalculator& calculator,
    const GeometryCancellation& cancelled) {
    require(mapping.target==GisExchangeTarget::Country||mapping.target==GisExchangeTarget::Subunit||
            mapping.target==GisExchangeTarget::Region,"INVALID_GIS_TARGET");
    require(bool(calculator),"GIS_M4_CALCULATOR_REQUIRED");
    require(mapping.coast!=GisTerritorialMapping::CoastDecision::Cancel,"CANCELLED_GIS_IMPORT");
    std::vector<GisTerritorialInput> units;
    std::set<std::string> ids;
    const auto kind=mapping.target==GisExchangeTarget::Country?UnitKind::Country:
        mapping.target==GisExchangeTarget::Subunit?UnitKind::Subunit:UnitKind::Region;
    for(const auto& feature:collection.features) {
        require(!cancelled||!cancelled(),"CANCELLED_GIS_IMPORT");
        require(feature.geometry.type=="Polygon"||feature.geometry.type=="MultiPolygon",
                "INVALID_GIS_GEOMETRY: territorial polygon required");
        const auto props=losslessjson::parse(QByteArray::fromStdString(feature.propertiesJson));
        require(props.kind==V::Object,"INVALID_GIS_PROPERTIES");
        GisTerritorialInput row;
        row.kind=kind;
        row.id=mapping.idField=="__fid__"?feature.id:property(props,mapping.idField);
        row.name=property(props,mapping.nameField);
        row.notes=property(props,"notes");
        row.geometry=feature.geometry;
        row.color=color(property(props,mapping.colorField));
        const auto from=property(props,mapping.validFromField);
        const auto to=property(props,mapping.validToField);
        if(!from.empty())row.validity.from=from;
        if(!to.empty())row.validity.to=to;
        require(!row.id.empty()&&!row.name.empty()&&ids.insert(row.id).second,
                "INVALID_GIS_MAPPING: ID or name");
        if(kind!=UnitKind::Country) {
            const auto sovereign=mapping.commonSovereign?
                mapping.commonSovereign->id:property(props,mapping.sovereignField);
            const auto parent=mapping.commonParent?
                mapping.commonParent->id:property(props,mapping.parentField);
            if(!sovereign.empty())row.sovereign=territorialRef(sovereign);
            if(!parent.empty())row.parent=territorialRef(parent);
            if(kind==UnitKind::Subunit) {
                require(row.sovereign.has_value(),"GIS_SOVEREIGN_REQUIRED");
                // A country is an explicit level-one parent, not a guessed one.
                if(!row.parent)row.parent=row.sovereign;
            }
        }
        const auto old=existing(snapshot,territorialRef(row.id));
        require(!old||kind==UnitKind::Country&&old->kind==UnitKind::Country,
                "DUPLICATE_ID: territorial import");
        row.replaceExisting=old!=nullptr;
        units.push_back(std::move(row));
    }
    require(!units.empty(),"INVALID_GIS_PLAN: empty territorial import");
    // The M4 calculator processes every donor against a detached draft. Each
    // resulting patch is applied with the imported rows by one ChangeSet.
    std::map<std::string,Geometry> countries;
    std::set<std::string> touched;
    for(const auto& row:snapshot.document().units)if(row.kind==UnitKind::Country)
        countries.emplace(row.id,*snapshot.document().geometries.get(row.geometry));
    auto transfer=[&](const std::string& destination,const Geometry& claimed) {
        for(auto& [id,shape]:countries) {
            if(id==destination)continue;
            const auto overlap=calculate(calculator,GeometryOperation::Intersection,
                shape,claimed,cancelled);
            if(!populated(overlap)||!significantArea(planarArea(overlap),planarArea(shape)))continue;
            require(mapping.coast==GisTerritorialMapping::CoastDecision::ImportedGeometry,
                    "GIS_LAND_TRANSFER_REQUIRES_APPROVAL");
            auto remainder=calculate(calculator,GeometryOperation::Difference,
                shape,claimed,cancelled);
            require(populated(remainder),"GIS_WOULD_REMOVE_COUNTRY");
            shape=std::move(remainder);touched.insert(id);
        }
    };
    for(auto& row:units) {
        require(!cancelled||!cancelled(),"CANCELLED_GIS_IMPORT");
        if(kind==UnitKind::Country) {
            transfer(row.id,row.geometry);
            countries[row.id]=row.geometry;
            continue;
        }
        if(!row.sovereign)continue; // An unowned explicit region is permitted.
        auto sovereign=countries.find(row.sovereign->id);
        require(sovereign!=countries.end(),"GIS_SOVEREIGN_MISSING");
        if(kind==UnitKind::Subunit) {
            const auto outside=calculate(calculator,GeometryOperation::Difference,
                row.geometry,sovereign->second,cancelled);
            if(populated(outside)&&significantArea(planarArea(outside),planarArea(row.geometry))) {
                if(mapping.coast==GisTerritorialMapping::CoastDecision::CountryGeometry) {
                    auto kept=calculate(calculator,GeometryOperation::Intersection,
                        row.geometry,sovereign->second,cancelled);
                    require(populated(kept),"GIS_OUTSIDE_SOVEREIGN");
                    row.geometry=std::move(kept);
                } else if(mapping.coast==GisTerritorialMapping::CoastDecision::ImportedGeometry) {
                    transfer(sovereign->first,row.geometry);
                    sovereign->second=calculate(calculator,GeometryOperation::Union,
                        sovereign->second,row.geometry,cancelled);
                    touched.insert(sovereign->first);
                } else throw std::invalid_argument("GIS_COAST_DECISION_REQUIRED");
            }
        }
        if(row.parent&&row.sovereign&&!(*row.parent==*row.sovereign)) {
            const auto parent=std::find_if(units.begin(),units.end(),[&](const auto& candidate){
                return candidate.id==row.parent->id;});
            const auto old=existing(snapshot,*row.parent);
            require(parent!=units.end()||old,"GIS_PARENT_MISSING");
            const auto& parentShape=parent!=units.end()?parent->geometry:
                *snapshot.document().geometries.get(old->geometry);
            const auto outside=calculate(calculator,GeometryOperation::Difference,
                row.geometry,parentShape,cancelled);
            require(!populated(outside)||!significantArea(planarArea(outside),planarArea(row.geometry)),
                    "GIS_OUTSIDE_PARENT");
        }
    }
    std::vector<GeometryReplacement> replacements;
    for(const auto& id:touched)replacements.push_back({territorialRef(id),countries.at(id)});
    return planTerritorialGisImport(snapshot,std::move(planId),std::move(source),
        mapping.target,std::move(units),std::move(replacements));
}
GisTerritorialImportPlan prepareGisTerritorialGeoJsonImport(const ProjectSnapshot& snapshot,
    const QByteArray& bytes,std::string planId,std::string fileName,
    const GisTerritorialMapping& mapping,const GeometryCalculator& calculator,
    const GeometryCancellation& cancelled) {
    return prepareGisTerritorialImport(snapshot,parseGisGeoJson(bytes),std::move(planId),
        {std::move(fileName),"geojson"},mapping,calculator,cancelled);
}
GisTerritorialImportPlan prepareGisTerritorialZipImport(const ProjectSnapshot& snapshot,
    const QByteArray& bytes,std::size_t index,std::string planId,std::string fileName,
    const GisTerritorialMapping& mapping,const GeometryCalculator& calculator,
    const GeometryCancellation& cancelled) {
    const auto archive=parseGisGeoJsonZip(bytes);
    require(index<archive.layers.size(),"GIS_LAYER_MISSING");
    checkLayerTarget(archive.layers[index].targetType,mapping);
    return prepareGisTerritorialImport(snapshot,archive.layers[index].collection,std::move(planId),
        {std::move(fileName),"geojson-zip"},mapping,calculator,cancelled);
}
GisTerritorialImportPlan prepareGisTerritorialGeoPackageImport(const ProjectSnapshot& snapshot,
    const QString& filePath,std::size_t index,std::string planId,
    const GisTerritorialMapping& mapping,const GeometryCalculator& calculator,
    const GeometryCancellation& cancelled) {
    require(!cancelled||!cancelled(),"CANCELLED_GIS_IMPORT");
    const auto package=readGisGeoPackage(filePath);
    require(index<package.layers.size(),"GIS_LAYER_MISSING");
    checkLayerTarget(package.layers[index].targetType,mapping);
    return prepareGisTerritorialImport(snapshot,package.layers[index].collection,std::move(planId),
        {filePath.toUtf8().toStdString(),"geopackage"},mapping,calculator,cancelled);
}
}

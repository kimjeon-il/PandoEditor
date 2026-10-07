#include "territorialcatalogadapter.h"
#include "webjson.h"
#include <QJsonDocument>
#include <QUuid>
#include <QRegularExpression>
#include <cmath>
#include <stdexcept>

namespace pandoeditor {
namespace {
std::string text(const QJsonValue& value){return value.toString().toStdString();}
std::string json(const QJsonObject& value){return QJsonDocument(value).toJson(QJsonDocument::Compact).toStdString();}
void require(bool value,const char* message){if(!value)throw std::invalid_argument(message);}
Polygon polygon(const QJsonArray& raw) {
    Polygon result;
    for(const auto& ringValue:raw) {
        Ring ring;require(ringValue.isArray(),"PL-LIB-GEOMETRY: ring required");
        for(const auto& pointValue:ringValue.toArray()) {
            const auto point=pointValue.toArray();
            require(point.size()==2&&point[0].isDouble()&&point[1].isDouble()&&
                std::isfinite(point[0].toDouble())&&std::isfinite(point[1].toDouble()),"PL-LIB-GEOMETRY: finite coordinate required");
            ring.push_back({point[0].toDouble(),point[1].toDouble()});
        }
        result.push_back(std::move(ring));
    }
    return result;
}
bool generalRoot(const ProjectSnapshot& project,const std::string& id) {
    const auto found=project.index().objects.find(territorialRef(id));
    return found!=project.index().objects.end()&&isRootGeneral(project.document(),project.document().units.at(found->second));
}
bool parentBelongsTo(const ProjectSnapshot& project,std::string parent,const std::string& country) {
    std::set<std::string> seen;
    while(!parent.empty()&&seen.insert(parent).second) {
        const auto found=project.index().objects.find(territorialRef(parent));
        if(found==project.index().objects.end()||project.document().units.at(found->second).kind!=UnitKind::General)return false;
        if(parent==country)return generalRoot(project,country);
        parent=staticParentRelation(project.document(),parent).parentId;
    }
    return false;
}
}
Geometry territorialCatalogGeometry(const QJsonObject& raw) {
    Geometry result;result.type=text(raw.value("type"));
    require(raw.value("coordinates").isArray(),"PL-LIB-GEOMETRY: coordinates required");
    const auto coordinates=raw.value("coordinates").toArray();
    if(result.type=="Polygon")result.polygons.push_back(polygon(coordinates));
    else if(result.type=="MultiPolygon")for(const auto& value:coordinates) {
        require(value.isArray(),"PL-LIB-GEOMETRY: polygon required");result.polygons.push_back(polygon(value.toArray()));
    } else throw std::invalid_argument("PL-LIB-GEOMETRY: polygon required");
    GeometryStore validator;validator.insert({"catalog-validation",1},result);return result;
}
std::vector<HistoricalAddition> territorialCatalogSelections(const TerritorialLibraryCatalog& catalog,
    const ProjectSnapshot& project,const QStringList& roots,const QString& date,const QString& depth,
    const QVariantMap& ownership,const QString& selectedId,const QString& selectedVersionId) {
    // Fixed Web library-ownership.js allocates each project ID before resolving
    // source parents. Previous imports with the same source ID never participate.
    const auto descriptors=catalog.instantiateDescriptors(roots,date,depth);
    std::map<std::string,std::string> ids;
    for(const auto& value:descriptors) {
        const auto descriptor=value.toObject();const auto source=text(descriptor.value("entityId"));
        const auto kind=descriptor.value("entityKind").toString();
        require(ids.emplace(source,("library_"+kind+"_"+QUuid::createUuid().toString(QUuid::WithoutBraces)).toStdString()).second,
            "PL-LIB-ENTITY: duplicate source selection");
    }
    std::vector<HistoricalAddition> additions;
    for(const auto& value:descriptors) {
        const auto descriptor=value.toObject();const auto id=descriptor.value("entityId").toString();
        const auto choice=ownership.value(id).toMap();const auto mode=choice.value("mode").toString();
        require(choice.isEmpty()||mode=="child"||mode=="root","PL-LIB-PARENT: choose child or root");
        HistoricalAddition addition;auto& selection=addition.selection;
        selection.libraryId=id.toStdString();selection.geometryVersionId=text(descriptor.value("geometryVersionId"));
        const auto overrideVersion=choice.value("geometryVersionId",id==selectedId?QVariant(selectedVersionId):QVariant()).toString();
        require(overrideVersion.isEmpty()||overrideVersion.toStdString()==selection.geometryVersionId,
            "PL-LIB-GEOMETRY-GAP: version is not selected at reference date");
        selection.name=text(descriptor.value("name"));selection.type=descriptor.value("entityKind")=="regional"?UnitKind::Regional:UnitKind::General;
        selection.parentLibraryId=text(descriptor.value("parentEntityId"));
        selection.geometry=territorialCatalogGeometry(descriptor.value("geometry").toObject());
        // A selected historical boundary is a static project copy. Dates remain
        // source provenance; no timeline activation or nearest-version fallback.
        selection.validity={};addition.referenceDate=date.toStdString();addition.instanceId=ids.at(selection.libraryId);
        auto metadata=descriptor.value("metadata").toObject();
        selection.sourceInfo=json(metadata.value("sourceInfo").toObject());
        selection.sourceId=text(metadata.value("sourceInfo").toObject().value("sourceId"));
        selection.certainty=text(metadata.value("geometryCertainty"));selection.datePrecision=text(metadata.value("geometryDatePrecision"));
        auto policy=descriptor.value("instantiation").toObject();selection.instantiation.mode=text(policy.value("mode"));
        if(selection.instantiation.mode.empty())selection.instantiation.mode="independent";
        const auto updates=policy.value("countryUpdates").toObject();
        // Fixed Web gis-import-transaction.js maps updates onto actual retained
        // project root IDs; an absent target does not make an import invalid.
        for(auto it=updates.begin();it!=updates.end();++it)if(generalRoot(project,it.key().toStdString()))
            selection.instantiation.countryNameUpdates[it.key().toStdString()]=text(it.value().toObject().value("name"));
        if(selection.type==UnitKind::Regional)require(selection.parentLibraryId.empty()&&choice.isEmpty(),"PL-LIB-PARENT: regional entity is independent");
        else if(mode=="root"||selection.parentLibraryId.empty()) {
            if(choice.contains("name"))selection.name=webjson::jsTrim(choice.value("name").toString()).toStdString();
            require(!selection.name.empty(),"PL-LIB-ENTITY: root name required");
        } else if(!choice.isEmpty()) {
            const auto country=choice.value("countryId").toString().toStdString();
            const auto explicitParent=choice.value("parentId").toString();
            const auto parent=(explicitParent.isEmpty()?choice.value("countryId").toString():explicitParent).toStdString();
            require(generalRoot(project,country)&&parentBelongsTo(project,parent,country),"PL-LIB-PARENT: select parent in chosen country");
            addition.parent=territorialRef(parent);
        } else {
            const auto parent=ids.find(selection.parentLibraryId);require(parent!=ids.end(),"PL-LIB-PARENT: ownership required");
            addition.parent=territorialRef(parent->second);
        }
        if(metadata.contains("capital"))addition.initialCountryDetails=CountryDetails{text(metadata.value("capital"))};
        if(metadata.contains("flagDataUrl")) {
            const auto flag=metadata.value("flagDataUrl");
            addition.initialSymbolStyle=TerritorialSymbolStyle{flag.isNull()?FlagPolicy::None:FlagPolicy::Embedded,text(flag)};
        }
        const auto color=metadata.value("defaultColor").toString();
        if(!color.isEmpty()) {
            require(QRegularExpression("^#[0-9a-fA-F]{6}$").match(color).hasMatch(),"PL-LIB-STYLE: invalid default color");
            addition.initialObjectStyle=ObjectStyle{color.mid(1).toUInt(nullptr,16),1,true};
        }
        // These fields have typed native owners, rather than duplicated JSON.
        for(const auto& key:{"capital","flagDataUrl","nameSource","libraryOrigin"})metadata.remove(key);
        selection.metadata=json(metadata);additions.push_back(std::move(addition));
    }
    return additions;
}
}

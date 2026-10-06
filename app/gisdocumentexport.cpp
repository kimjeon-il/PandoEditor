#include "gisdocumentexport.h"
#include "webjson.h"
#include <pandoeditor/giszip.h>
#include <pandoeditor/objectproperties.h>
#include <QString>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <tuple>

namespace pandoeditor {
namespace {
using V=losslessjson::Value;
V nullable(const std::optional<std::string>& value) {
    return value?V::str(*value):V{};
}
std::string color(std::uint32_t rgb) {
    return QString("#%1").arg(rgb,6,16,QChar('0')).toStdString();
}
Geometry geometry(const ProjectDocument& document,const GeometryRef& ref) {
    const auto stored=document.geometries.get(ref);
    if(!stored)throw std::invalid_argument("DANGLING_GIS_GEOMETRY");
    return *stored;
}
void append(GisGeoJsonCollection& out,std::string id,Geometry shape,V properties) {
    out.features.push_back({std::move(id),std::move(shape),properties.encode().toStdString()});
}
}
GisGeoJsonCollection exportGisDocumentLayer(const ProjectDocument& document,
                                            const std::string& layer) {
    validateDocument(document);requireStaticTimeline(document);
    GisGeoJsonCollection out;
    if(layer=="countries"||layer=="subunits"||layer=="regions") {
        const auto kind=layer=="countries"?UnitKind::General:layer=="subunits"?UnitKind::General:UnitKind::Regional;
        for(const auto& unit:document.units)if(territorialGroup(document,unit.id)==layer) {
            const auto ref=territorialRef(unit.id);
            const auto& relation=staticParentRelation(document,unit.id);
            auto properties=webjson::obj({
                {"id",V::str(unit.id)},{"name",V::str(objectDisplayName(unit))},
                {"pandolab_id",V::str(unit.id)},{"pandolab_name",V::str(objectDisplayName(unit))},
                {"entityKind",V::str(unit.kind==UnitKind::General?"general":"regional")},
                {"parent_id",V::str(relation.parentId)},
                {"valid_from",nullable(staticLifetime(document,unit.id).validity.from)},{"valid_to",nullable(staticLifetime(document,unit.id).validity.to)},
                {"color",V::str(color(effectiveObjectColor(document,ref)))},
                {"source_entity_id",unit.sourceEntityId.empty()?V{}:V::str(unit.sourceEntityId)},
                {"source_geometry_version",unit.sourceGeometryVersion.empty()?V{}:V::str(unit.sourceGeometryVersion)}
            });
            append(out,unit.id,geometry(document,staticGeometryBinding(document,unit.id).geometryRef),std::move(properties));
        }
    } else if(layer=="distributions") {
        for(const auto& entry:document.distributionEntries) {
            const auto layerIt=std::find_if(document.distributionLayers.begin(),document.distributionLayers.end(),
                [&](const auto& item){return item.id==entry.layerId;});
            if(layerIt==document.distributionLayers.end())throw std::invalid_argument("DANGLING_GIS_LAYER");
            Geometry shape;
            if(entry.territory) {
                const auto ref=std::find_if(document.units.begin(),document.units.end(),
                    [&](const auto& unit){return territorialRef(unit.id)==*entry.territory;});
                if(ref==document.units.end())throw std::invalid_argument("DANGLING_GIS_TERRITORY");
                shape=geometry(document,staticGeometryBinding(document,ref->id).geometryRef);
            } else if(entry.geometry)shape=geometry(document,*entry.geometry);
            else throw std::invalid_argument("DANGLING_GIS_GEOMETRY");
            auto properties=webjson::obj({
                {"entry_id",V::str(entry.id)},{"layer_id",V::str(entry.layerId)},
                {"name",V::str(layerIt->name)},
                {"unit",V::str(layerIt->unit)},
                {"value_scale_mode",V::str(layerIt->valueScale.manual?"manual":"auto")},
                {"value_scale_min",layerIt->valueScale.manual?V::num(layerIt->valueScale.min):V{}},
                {"value_scale_max",layerIt->valueScale.manual?V::num(layerIt->valueScale.max):V{}},
                {"parent_layer_id",layerIt->parentId?V::str(*layerIt->parentId):V{}},
                {"color",V::str(color(layerIt->color))},
                {"layer_visible",V::num(itemVisible(document.presentation.webPresentation,
                    "distributions",
                    layerIt->id)?1:0)},
                {"layer_locked",V::num(layerIt->locked?1:0)},
                {"source_mode",V::str(entry.territory?"territorial":"geometry")},
                {"territorial_unit_id",entry.territory?V::str(entry.territory->id):V{}},
                {"value",V::num(entry.value)},{"certainty",V::str(entry.certainty)},
                {"valid_from",nullable(entry.validity.from)},{"valid_to",nullable(entry.validity.to)},
                {"layer_metadata_json",V::str(layerIt->metadata)},
                {"entry_metadata_json",V::str(entry.metadata)}
            });
            append(out,entry.id,std::move(shape),std::move(properties));
        }
    } else if(layer=="genericFeatures") {
        for(const auto& item:document.genericFeatures) {
            auto properties=webjson::obj({{"id",V::str(item.id)},{"name",V::str(item.name)},
                {"notes",V::str(item.notes)},{"color",V::str(color(item.color))}});
            properties.object["source_metadata"]=losslessjson::parse(
                QByteArray::fromStdString(item.source.details));
            append(out,item.id,geometry(document,item.geometry),std::move(properties));
        }
    } else if(layer=="labels") {
        for(const auto& item:document.labels) {
            auto properties=webjson::obj({{"id",V::str(item.id)},
                {"name",V::str(item.name)},{"kind",V::str(item.kind)},
                {"pandolab_id",V::str(item.id)},
                {"country_id",item.territory?V::str(item.territory->id):V{}},
                {"notes",V::str(item.notes)}});
            append(out,item.id,geometry(document,item.geometry),std::move(properties));
        }
    } else throw std::invalid_argument("UNSUPPORTED_GIS_EXPORT_LAYER");
    return out;
}
std::vector<GisExportLayer> buildGisExportLayers(const ProjectDocument& document,
    const std::vector<std::string>& selected) {
    static const std::set<std::string> allowed={"countries","subunits","regions",
        "genericFeatures","distributions","labels"};
    std::set<std::string> chosen;
    for(const auto& category:selected) {
        if(!allowed.count(category))throw std::invalid_argument("UNSUPPORTED_GIS_EXPORT_LAYER");
        chosen.insert(category);
    }
    std::vector<GisExportLayer> layers;
    auto add=[&](const char* category,const char* file,const char* target,
                 GisGeoJsonCollection collection) {
        if(!chosen.count(category)||collection.features.empty())return;
        layers.push_back({category,file,target,std::move(collection)});
    };
    for(const auto& specification:std::vector<std::tuple<const char*,const char*,const char*>>{
        {"countries","countries.geojson","country"},{"subunits","subunits.geojson","subunit"},
        {"regions","regions.geojson","region"},{"genericFeatures","generic_features.geojson","generic"}}) {
        const auto [category,file,target]=specification;
        if(chosen.count(category))add(category,file,target,exportGisDocumentLayer(document,category));
    }
    if(chosen.count("distributions")) {
        add("distributions","distributions.geojson","distribution",exportGisDocumentLayer(document,"distributions"));
    }
    if(chosen.count("labels"))add("labels","labels.geojson","label",
        exportGisDocumentLayer(document,"labels"));
    if(layers.empty())throw std::invalid_argument("EMPTY_GIS_EXPORT");
    return layers;
}
QByteArray exportGisGeoJsonZip(const ProjectDocument& document,
    const std::vector<std::string>& selected,const std::string& createdAt) {
    const auto layers=buildGisExportLayers(document,selected);
    GisZipArchive archive;
    V descriptors=V::arr();
    for(const auto& layer:layers) {
        const auto bytes=exportGisGeoJson(layer.collection);
        archive.entries.push_back({layer.file,bytes.toStdString()});
        auto row=webjson::obj({{"name",V::str(layer.file.substr(0,layer.file.size()-8))},
            {"file",V::str(layer.file)},{"category",V::str(layer.category)},
            {"targetType",V::str(layer.targetType)},
            {"crs",V::str("EPSG:4326")},
            {"featureCount",V::num(layer.collection.features.size())}});
        if(layer.targetType=="country")row.object["fields"]=webjson::arr({
            V::str("pandolab_id"),V::str("pandolab_name"),
            V::str("valid_from"),V::str("valid_to")});
        descriptors.array.push_back(std::move(row));
    }
    auto manifest=webjson::obj({{"pandolabExport",V::boolean(true)},
        {"schemaVersion",V::num(3)},{"crs",V::str("EPSG:4326")},
        {"createdAt",V::str(createdAt)},{"layers",std::move(descriptors)}});
    archive.entries.push_back({"manifest.json",manifest.encode().toStdString()});
    return QByteArray::fromStdString(writeGisZipArchive(archive));
}
}

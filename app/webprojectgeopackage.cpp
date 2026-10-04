#include "webprojectgeopackage.h"
#include "projectcodec.h"
#include "webjson.h"
#include <pandoeditor/project.h>
#include <QImage>
#include <algorithm>
#include <set>
#include <stdexcept>

namespace pandoeditor {
namespace {
using V=losslessjson::Value;
void require(bool ok,const char* error){if(!ok)throw std::invalid_argument(error);}
const V& field(const V& object,const char* key){static const V empty;const auto found=object.object.find(key);return found==object.object.end()?empty:found->second;}
V text(const V& value){return V::str(value.kind==V::String?QString::fromStdString(value.string).trimmed().toStdString():"");}
// Validate the current v9 Worker's derived table contract against authoritative
// JSON and canonical static bindings. This is a read boundary, not a writer.
std::vector<GisGeoPackageLayer> webVectors(const ProjectDocument& d,const V& state) {
    std::vector<GisGeoPackageLayer> out;
    auto table=[&](const char* name,const char* type,std::initializer_list<const char*> columns)->GisGeoPackageLayer&{
        GisGeoPackageLayer layer;layer.tableName=name;layer.geometryType=type;layer.propertyColumns={"fid"};
        for(const auto* column:columns)layer.propertyColumns.push_back(column);out.push_back(std::move(layer));return out.back();
    };
    auto add=[](GisGeoPackageLayer& layer,const std::string& id,const Geometry& source,V props){
        auto shape=source;if(shape.type=="Polygon")shape.type="MultiPolygon";if(shape.type=="LineString")shape.type="MultiLineString";
        layer.collection.features.push_back({id,std::move(shape),props.encode().toStdString()});
    };
    auto json=[](const V& value){return V::str(value.encode().toStdString());};
    const std::initializer_list<const char*> territorial={"id","name","entity_kind","parent_id","valid_from","valid_to","color","style_key","source_library_id","source_geometry_version","metadata_json","properties_json"};
    for(const auto* name:{"entities","regions"}) {
        auto& layer=table(name,"MULTIPOLYGON",territorial);
        for(const auto& entity:field(state,"territorialEntities").array) {
            auto p=field(entity,"properties");const auto& kind=field(p,"entityKind").string;
            if((kind=="regional")!=(std::string(name)=="regions"))continue;
            const auto& id=field(entity,"id").string;const auto& parent=staticParentRelation(d,id);
            p.object["parentId"]=V::str(parent.parentId);p.object["coverageMode"]=V::str(parent.coverageMode);
            p.object["validFrom"]=V{};p.object["validTo"]=V{};
            p.object.erase("adminLevel");p.object.erase("isRemainder");p.object["metadata"].object.erase("legacyTerritorialPartition");
            const auto& style=field(p,"style");
            auto row=webjson::obj({{"id",V::str(id)},{"name",text(field(p,"name"))},{"entity_kind",V::str(kind)},
                {"parent_id",V::str(parent.parentId)},{"valid_from",V::str("")},{"valid_to",V::str("")},
                {"color",text(field(style,"color"))},{"style_key",text(field(style,"key"))},
                {"source_library_id",text(field(p,"sourceLibraryId"))},{"source_geometry_version",text(field(p,"sourceGeometryVersion"))},
                {"metadata_json",json(field(p,"metadata"))},{"properties_json",json(p)}});
            add(layer,id,*d.geometries.get(staticGeometryBinding(d,id).geometryRef),std::move(row));
        }
    }
    {
        auto& layer=table("places","POINT",{"pandolab_id","name","kind","country_id","notes"});
        for(const auto& label:d.labels) {
            // Current Worker leaves its old country_id spool field empty;
            // territorialUnitId remains in authoritative project JSON.
            auto row=webjson::obj({{"pandolab_id",V::str(label.id)},{"name",V::str(label.name)},{"kind",V::str(label.kind)},{"country_id",V::str("")},{"notes",V::str(label.notes)}});
            add(layer,label.id,*d.geometries.get(label.geometry),std::move(row));
        }
    }
    for(const auto* name:{"generic_features_point","generic_features_line","generic_features_polygon"}) {
        auto& layer=table(name,std::string(name)=="generic_features_point"?"POINT":std::string(name)=="generic_features_line"?"MULTILINESTRING":"MULTIPOLYGON",
            {"id","name","role","owner_id","parent_id","topology_group","land_binding","color","notes","locked","properties_json"});
        for(const auto& feature:field(state,"genericFeatures").array) {
            const auto& id=field(feature,"id").string;const auto& p=field(feature,"properties");
            const auto item=std::find_if(d.genericFeatures.begin(),d.genericFeatures.end(),[&](const auto& row){return row.id==id;});
            require(item!=d.genericFeatures.end(),"PROJECT_VECTOR_ROW_MISMATCH");const auto& shape=*d.geometries.get(item->geometry);
            const auto expected=shape.type=="Point"||shape.type=="MultiPoint"?"generic_features_point":shape.type=="LineString"||shape.type=="MultiLineString"?"generic_features_line":"generic_features_polygon";
            if(std::string(name)!=expected)continue;
            auto row=webjson::obj({{"id",V::str(id)},{"name",field(p,"name")},{"role",V::str("generic")},{"owner_id",V::str("")},{"parent_id",V::str("")},{"topology_group",V::str("")},{"land_binding",V::str("none")},
                {"color",field(p,"color")},{"notes",field(p,"notes")},{"locked",V::num(field(p,"locked").raw=="true"?1:0)},{"properties_json",json(p)}});
            add(layer,id,shape,std::move(row));
        }
    }
    {
        auto& layer=table("distributions","MULTIPOLYGON",{"entry_id","layer_id","name","unit","value_scale_mode","value_scale_min","value_scale_max","parent_layer_id","layer_groups_json","layer_valid_from","layer_valid_to","color","layer_visible","layer_locked","source_mode","territorial_unit_id","value","certainty","valid_from","valid_to","layer_metadata_json","entry_metadata_json"});
        for(const auto& entry:d.distributionEntries) {
            const auto& definition=*std::find_if(d.distributionLayers.begin(),d.distributionLayers.end(),[&](const auto& row){return row.id==entry.layerId;});
            const auto rawLayer=std::find_if(field(state,"distributionLayers").array.begin(),field(state,"distributionLayers").array.end(),[&](const auto& row){return field(row,"id").string==entry.layerId;});
            const auto rawEntry=std::find_if(field(state,"distributionEntries").array.begin(),field(state,"distributionEntries").array.end(),[&](const auto& row){return field(row,"id").string==entry.id;});
            require(rawLayer!=field(state,"distributionLayers").array.end()&&rawEntry!=field(state,"distributionEntries").array.end(),"PROJECT_VECTOR_ROW_MISMATCH");
            const auto ref=entry.territory?staticGeometryBinding(d,entry.territory->id).geometryRef:*entry.geometry;
            auto row=webjson::obj({{"entry_id",V::str(entry.id)},{"layer_id",V::str(entry.layerId)},{"name",text(field(*rawLayer,"name"))},{"unit",text(field(*rawLayer,"unit"))},
                {"value_scale_mode",V::str(definition.valueScale.manual?"manual":"auto")},{"value_scale_min",definition.valueScale.manual?V::num(definition.valueScale.min):V{}},{"value_scale_max",definition.valueScale.manual?V::num(definition.valueScale.max):V{}},
                {"parent_layer_id",text(field(*rawLayer,"parentId"))},{"layer_groups_json",json(field(*rawLayer,"groups"))},{"layer_valid_from",text(field(*rawLayer,"validFrom"))},{"layer_valid_to",text(field(*rawLayer,"validTo"))},
                {"color",text(field(*rawLayer,"color"))},{"layer_visible",V::num(itemVisible(d.presentation.webPresentation,"distributions",entry.layerId)?1:0)},{"layer_locked",V::num(definition.locked?1:0)},
                {"source_mode",V::str(entry.territory?"territorial":"geometry")},{"territorial_unit_id",V::str(entry.territory?entry.territory->id:"")},{"value",V::num(entry.value)},{"certainty",V::str(entry.certainty)},
                {"valid_from",text(field(*rawEntry,"validFrom"))},{"valid_to",text(field(*rawEntry,"validTo"))},{"layer_metadata_json",json(field(*rawLayer,"metadata"))},{"entry_metadata_json",json(field(*rawEntry,"metadata"))}});
            add(layer,entry.id,*d.geometries.get(ref),std::move(row));
        }
    }
    return out;
}
}
QByteArray convertWebProjectGeoPackage(const GisGeoPackage& vectors,V state,
    const std::map<std::string,WebProjectAsset>& assets) {
    require(vectors.projectPackage&&state.kind==V::Object,"UNSUPPORTED_PROJECT_GPKG_STATE");
    auto& entities=state.object["territorialEntities"];
    require(entities.kind==V::Array,"UNSUPPORTED_PROJECT_GPKG_STATE");
    std::set<std::string> used;
    for(auto& entity:entities.array) {
        const auto& id=webjson::at(entity,"id");require(id.kind==V::String,"INVALID_WEB_GPKG_ID");
        auto& metadata=entity.object["properties"].object["metadata"];
        require(metadata.kind==V::Object,"INVALID_WEB_GPKG_METADATA");
        if(const auto asset=assets.find(id.string);asset!=assets.end()) {
            require(asset->second.mime.startsWith("image/")&&!QImage::fromData(asset->second.bytes).isNull(),"INVALID_WEB_GPKG_ASSET_IMAGE");
            const auto flag=metadata.object.find("flagDataUrl");
            const auto url="data:"+asset->second.mime.toStdString()+";base64,"+asset->second.bytes.toBase64().toStdString();
            require(flag==metadata.object.end()||(flag->second.kind==V::String&&flag->second.string==url),"WEB_GPKG_ASSET_STATE_MISMATCH");
            metadata.object["flagDataUrl"]=V::str(url);used.insert(id.string);
        }
    }
    require(used.size()==assets.size(),"ORPHAN_PROJECT_GPKG_ASSET");
    // Complete v9 JSON is authoritative. Spatial tables are derived views and
    // never supply or replace identity, timeline records or archived geometry.
    auto document=projectcodec::decodeWeb(state.encode());
    if(!isStaticTimeline(document))require(vectors.layers.empty(),"WEB_GPKG_TIMELINE_VECTORS");
    else {
        validateProjectGeoPackageVectors(vectors,webVectors(document,state));
    }
    Project candidate;candidate.replace(std::move(document));return projectcodec::encode(candidate);
}
}

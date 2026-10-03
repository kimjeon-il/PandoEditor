#include "webprojectgeopackage.h"
#include "projectcodec.h"
#include "webimport.h"
#include "webjson.h"
#include <pandoeditor/project.h>
#include <QImage>
#include <initializer_list>
#include <map>
#include <set>
#include <stdexcept>

namespace pandoeditor {
namespace {
using V=losslessjson::Value;
void require(bool ok,const char* error) {if(!ok)throw std::invalid_argument(error);}
const V& field(const V& object,const char* name) {return webjson::at(object,name);}
std::string string(const V& value) {
    require(value.kind==V::String,"INVALID_WEB_GPKG_STATE");
    return value.string;
}
V point(Point p) {return webjson::arr({V::num(p.x),V::num(p.y)});}
V line(const Ring& ring) {
    auto value=V::arr();for(const auto p:ring)value.array.push_back(point(p));return value;
}
V polygon(const Polygon& shape) {
    auto value=V::arr();for(const auto& ring:shape)value.array.push_back(line(ring));return value;
}
V geometry(const Geometry& shape) {
    V coordinates=V::arr();
    if(shape.type=="Point") {
        require(shape.points.size()==1,"INVALID_WEB_GPKG_GEOMETRY");coordinates=point(shape.points.front());
    } else if(shape.type=="MultiPoint") {
        for(const auto p:shape.points)coordinates.array.push_back(point(p));
    } else if(shape.type=="LineString") {
        require(shape.lines.size()==1,"INVALID_WEB_GPKG_GEOMETRY");coordinates=line(shape.lines.front());
    } else if(shape.type=="MultiLineString") {
        for(const auto& row:shape.lines)coordinates.array.push_back(line(row));
    } else if(shape.type=="Polygon") {
        require(shape.polygons.size()==1,"INVALID_WEB_GPKG_GEOMETRY");coordinates=polygon(shape.polygons.front());
    } else if(shape.type=="MultiPolygon") {
        for(const auto& row:shape.polygons)coordinates.array.push_back(polygon(row));
    } else throw std::invalid_argument("INVALID_WEB_GPKG_GEOMETRY");
    return webjson::obj({{"type",V::str(shape.type)},{"coordinates",std::move(coordinates)}});
}
using FeatureMap=std::map<std::string,const GisGeoJsonFeature*>;
FeatureMap features(const GisGeoPackage& archive,std::initializer_list<const char*> tables) {
    const std::set<std::string> selected(tables.begin(),tables.end());
    FeatureMap result;
    for(const auto& layer:archive.layers)if(selected.count(layer.tableName))
        for(const auto& feature:layer.collection.features)
            require(!feature.id.empty()&&result.emplace(feature.id,&feature).second,
                    "DUPLICATE_WEB_GPKG_ID");
    return result;
}
void align(V& state,const char* key,const FeatureMap& vectors,bool pointCoordinates=false) {
    auto& rows=state.object[key];
    if(rows.kind==V::Null)rows=V::arr();
    require(rows.kind==V::Array,"INVALID_WEB_GPKG_STATE");
    std::set<std::string> seen;
    for(auto& row:rows.array) {
        require(row.kind==V::Object,"INVALID_WEB_GPKG_STATE");
        const auto id=string(field(row,"id"));
        const auto it=vectors.find(id);
        require(it!=vectors.end()&&seen.insert(id).second,"WEB_GPKG_VECTOR_STATE_MISMATCH");
        if(pointCoordinates) {
            require(it->second->geometry.type=="Point"&&it->second->geometry.points.size()==1,
                    "WEB_GPKG_VECTOR_STATE_MISMATCH");
            row.object["coordinates"]=point(it->second->geometry.points.front());
        } else row.object["geometry"]=geometry(it->second->geometry);
    }
    require(seen.size()==vectors.size(),"WEB_GPKG_VECTOR_STATE_MISMATCH");
}
void alignDistribution(V& state,const FeatureMap& vectors,bool fixtureProjection) {
    auto& rows=state.object["distributionEntries"];
    if(rows.kind==V::Null)rows=V::arr();
    require(rows.kind==V::Array,"INVALID_WEB_GPKG_STATE");
    std::set<std::string> seen;
    for(auto& row:rows.array) {
        require(row.kind==V::Object,"INVALID_WEB_GPKG_STATE");
        const auto id=string(field(row,"id"));
        const auto it=vectors.find(id);
        require(it!=vectors.end()&&seen.insert(id).second,"WEB_GPKG_VECTOR_STATE_MISMATCH");
        const auto mode=string(field(row,"mode"));
        require(mode=="geometry"||mode=="territorial","INVALID_WEB_GPKG_STATE");
        if(mode=="geometry")row.object["geometry"]=geometry(it->second->geometry);
        if(fixtureProjection)row.object["schemaVersion"]=V::num(2);
    }
    require(seen.size()==vectors.size(),"WEB_GPKG_VECTOR_STATE_MISMATCH");
}
}

QByteArray convertWebProjectGeoPackage(const GisGeoPackage& vectors,V state,
    const std::map<std::string,WebProjectAsset>& assets) {
    require(vectors.projectPackage&&state.kind==V::Object&&field(state,"countriesData").kind==V::Null,
            "UNSUPPORTED_PROJECT_GPKG_STATE");
    const auto& format=field(state,"format");
    const bool fixtureProjection=format.kind==V::Null;
    require(fixtureProjection||(format.kind==V::String&&
            (format.string=="pandolab-project-state"||format.string=="pandolab-autosave-full")),
            "UNSUPPORTED_PROJECT_GPKG_STATE");
    for(const auto& layer:vectors.layers)
        require(!layer.targetType.empty(),"UNSUPPORTED_WEB_GPKG_LAYER");
    const auto countries=features(vectors,{"countries"});
    require(!countries.empty(),"WEB_GPKG_COUNTRIES_MISSING");
    V countryFeatures=V::arr();
    for(const auto& [id,feature]:countries) {
        const auto props=losslessjson::parse(QByteArray::fromStdString(feature->propertiesJson));
        require(props.kind==V::Object,"INVALID_WEB_GPKG_COUNTRY");
        const auto name=string(field(props,"pandolab_name"));
        require(!name.empty(),"INVALID_WEB_GPKG_COUNTRY");
        V countryProps=webjson::obj({{"name",V::str(name)}});
        for(const auto& [column,webKey]:std::initializer_list<std::pair<const char*,const char*>>{
                {"valid_from","validFrom"},{"valid_to","validTo"}}) {
            const auto& value=webjson::at(props,column);
            if(value.kind!=V::Null)countryProps.object[webKey]=V::str(string(value));
        }
        countryFeatures.array.push_back(webjson::obj({
            {"type",V::str("Feature")},{"id",V::str(id)},
            {"properties",std::move(countryProps)},{"geometry",geometry(feature->geometry)}}));
    }
    state.object["countriesData"]=webjson::obj({
        {"type",V::str("FeatureCollection")},{"features",std::move(countryFeatures)}});

    auto& overrides=state.object["countryOverrides"];
    if(overrides.kind==V::Null)overrides=V::obj();
    require(overrides.kind==V::Object,"INVALID_WEB_GPKG_STATE");
    for(const auto& [id,asset]:assets) {
        require(countries.count(id),"ORPHAN_PROJECT_GPKG_ASSET");
        require(asset.mime.startsWith("image/")&&!asset.bytes.isEmpty()&&
                !QImage::fromData(asset.bytes).isNull(),"INVALID_WEB_GPKG_ASSET_IMAGE");
        auto& override=overrides.object[id];
        if(override.kind==V::Null)override=V::obj();
        require(override.kind==V::Object&&!override.object.count("flagDataUrl"),
                "INVALID_WEB_GPKG_FLAG_POLICY");
        override.object["flagDataUrl"]=V::str("data:"+asset.mime.toStdString()+
            ";base64,"+asset.bytes.toBase64().toStdString());
    }
    for(const auto& [id,override]:overrides.object) {
        require(countries.count(id)&&override.kind==V::Object,"INVALID_WEB_GPKG_OVERRIDE");
        const auto& flag=field(override,"flagDataUrl");
        if(assets.count(id))require(flag.kind==V::String,"INVALID_WEB_GPKG_FLAG_POLICY");
        else require(flag.kind==V::Null,"MISSING_PROJECT_GPKG_ASSET");
    }

    align(state,"territorialUnits",features(vectors,{"subunits","regions"}));
    for(auto& row:state.object["territorialUnits"].array) {
        require(field(row,"type").kind==V::Null||string(field(row,"type"))=="Feature",
                "INVALID_WEB_GPKG_STATE");
        row.object["type"]=V::str("Feature");
        auto& props=row.object["properties"];
        require(props.kind==V::Object,"INVALID_WEB_GPKG_STATE");
        if(fixtureProjection&&!props.object.count("coverageMode"))
            props.object["coverageMode"]=V::str("explicit");
    }
    align(state,"genericFeatures",features(vectors,{
        "generic_features_point","generic_features_line","generic_features_polygon"}));
    align(state,"labels",features(vectors,{"places"}),true);
    alignDistribution(state,features(vectors,{"distributions","language_distribution",
        "ethnicity_distribution","religion_distribution"}),fixtureProjection);
    auto& layers=state.object["distributionLayers"];
    if(layers.kind==V::Null)layers=V::arr();
    require(layers.kind==V::Array,"INVALID_WEB_GPKG_STATE");
    for(auto& layer:layers.array) {
        require(layer.kind==V::Object,"INVALID_WEB_GPKG_STATE");
        if(fixtureProjection)layer.object["schemaVersion"]=V::num(2);
    }
    if(fixtureProjection) {
        // The small worker fixture has no full-project envelope. Run its old
        // content through the existing v3→v5 migrator after restoring vectors.
        state.object["format"]=V::str("pandolab-project-state");
        state.object["schemaVersion"]=V::num(3);
        state.object["version"]=V::str("0.33.0");
    }
    if(fixtureProjection&&field(state,"distributionModel").kind==V::Null)
        state.object["distributionModel"]=webjson::obj({{"schemaVersion",V::num(2)}});
    const auto input=state.encode();
    // GeoPackage rows can contain stable IDs from older web revisions that
    // predate the standalone JSON importer's UUID-only check.
    auto imported=webimport::prepare(input,{},true);
    const auto& document=imported.document;
    require(document.units.size()==countries.size()+state.object.at("territorialUnits").array.size()&&
            document.genericFeatures.size()==state.object.at("genericFeatures").array.size()&&
            document.labels.size()==state.object.at("labels").array.size()&&
            document.distributionLayers.size()==state.object.at("distributionLayers").array.size()&&
            document.distributionEntries.size()==state.object.at("distributionEntries").array.size(),
            "INCOMPLETE_WEB_GPKG_RESTORE");
    for(const auto& [id,asset]:assets) {
        const auto found=document.symbols.find(territorialRef(id));
        require(found!=document.symbols.end()&&found->second.policy==FlagPolicy::Embedded&&
                found->second.embeddedDataUrl=="data:"+asset.mime.toStdString()+
                    ";base64,"+asset.bytes.toBase64().toStdString(),
                "INCOMPLETE_WEB_GPKG_RESTORE");
    }
    Project candidate;candidate.replace(std::move(imported.document));
    return projectcodec::encode(candidate);
}
}

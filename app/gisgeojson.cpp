#include "gisgeojson.h"
#include "webjson.h"
#include <QString>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <stdexcept>

namespace pandoeditor {
namespace {
using V=losslessjson::Value;
void require(bool ok,const char* message) {
    if(!ok)throw std::invalid_argument(message);
}
Point point(const V& value) {
    const auto& coords=webjson::array(value,"coordinate");
    require(coords.size()==2&&coords[0].kind==V::Number&&coords[1].kind==V::Number,
            "INVALID_GEOJSON: expected two numeric coordinates");
    bool okX=false,okY=false;
    const auto x=coords[0].raw.toDouble(&okX),y=coords[1].raw.toDouble(&okY);
    require(okX&&okY&&std::isfinite(x)&&std::isfinite(y),"INVALID_GEOJSON: nonfinite coordinate");
    return {x,y};
}
Ring ring(const V& input) {
    Ring result;
    for(const auto& value:webjson::array(input,"line or ring"))result.push_back(point(value));
    return result;
}
Polygon polygon(const V& input) {
    Polygon result;
    for(const auto& value:webjson::array(input,"polygon"))result.push_back(ring(value));
    return result;
}
Geometry geometry(const V& input) {
    require(input.kind==V::Object,"INVALID_GEOJSON: missing geometry");
    Geometry result;result.type=webjson::text(webjson::at(input,"type"));
    const auto& coordinates=webjson::at(input,"coordinates");
    if(result.type=="Point")result.points.push_back(point(coordinates));
    else if(result.type=="MultiPoint")
        for(const auto& value:webjson::array(coordinates,"multipoint"))result.points.push_back(point(value));
    else if(result.type=="LineString")result.lines.push_back(ring(coordinates));
    else if(result.type=="MultiLineString")
        for(const auto& value:webjson::array(coordinates,"multiline"))result.lines.push_back(ring(value));
    else if(result.type=="Polygon")result.polygons.push_back(polygon(coordinates));
    else if(result.type=="MultiPolygon")
        for(const auto& value:webjson::array(coordinates,"multipolygon"))result.polygons.push_back(polygon(value));
    else throw std::invalid_argument("UNSUPPORTED_GEOJSON_GEOMETRY");
    GeometryStore validation;validation.insert({"geojson-preflight",1},result);
    return result;
}
V pointValue(Point p) {return webjson::arr({V::num(p.x),V::num(p.y)});}
V ringValue(const Ring& ring) {
    auto value=V::arr();for(const auto& p:ring)value.array.push_back(pointValue(p));return value;
}
V polygonValue(const Polygon& polygon) {
    auto value=V::arr();for(const auto& ring:polygon)value.array.push_back(ringValue(ring));return value;
}
V geometryValue(const Geometry& geometry) {
    auto coordinates=V::arr();
    if(geometry.type=="Point")coordinates=pointValue(geometry.points.front());
    else if(geometry.type=="MultiPoint")for(const auto& point:geometry.points)coordinates.array.push_back(pointValue(point));
    else if(geometry.type=="LineString")coordinates=ringValue(geometry.lines.front());
    else if(geometry.type=="MultiLineString")for(const auto& line:geometry.lines)coordinates.array.push_back(ringValue(line));
    else if(geometry.type=="Polygon")coordinates=polygonValue(geometry.polygons.front());
    else if(geometry.type=="MultiPolygon")for(const auto& polygon:geometry.polygons)coordinates.array.push_back(polygonValue(polygon));
    else throw std::invalid_argument("UNSUPPORTED_GEOJSON_GEOMETRY");
    return webjson::obj({{"type",V::str(geometry.type)},{"coordinates",std::move(coordinates)}});
}
}
GisGeoJsonCollection parseGisGeoJson(const QByteArray& bytes) {
    const auto root=losslessjson::parse(bytes);
    require(root.kind==V::Object&&webjson::text(webjson::at(root,"type"))=="FeatureCollection",
            "INVALID_GEOJSON: FeatureCollection required");
    const auto& crs=webjson::at(root,"crs");
    if(crs.kind!=V::Null) {
        require(crs.kind==V::Object&&webjson::text(webjson::at(crs,"type"))=="name"&&
                webjson::text(webjson::at(webjson::at(crs,"properties"),"name"))=="EPSG:4326",
                "UNSUPPORTED_GEOJSON_CRS");
    }
    GisGeoJsonCollection collection;
    std::set<std::string> ids;
    for(const auto& item:webjson::array(webjson::at(root,"features"),"features")) {
        require(item.kind==V::Object&&webjson::text(webjson::at(item,"type"))=="Feature",
                "INVALID_GEOJSON: feature required");
        GisGeoJsonFeature feature;
        const auto& id=webjson::at(item,"id");
        require(id.kind==V::Null||id.kind==V::String||id.kind==V::Number,
                "INVALID_GEOJSON: feature id");
        if(id.kind!=V::Null) {
            feature.id=webjson::jsString(id);
            require(!feature.id.empty()&&ids.insert(feature.id).second,"DUPLICATE_GEOJSON_ID");
        }
        feature.geometry=geometry(webjson::at(item,"geometry"));
        const auto& properties=webjson::at(item,"properties");
        require(properties.kind==V::Null||properties.kind==V::Object,
                "INVALID_GEOJSON: properties object");
        if(properties.kind==V::Object)feature.propertiesJson=properties.encode().toStdString();
        collection.features.push_back(std::move(feature));
    }
    return collection;
}
QByteArray exportGisGeoJson(const GisGeoJsonCollection& collection) {
    V rows=V::arr();std::set<std::string> ids;
    for(const auto& feature:collection.features) {
        GeometryStore validation;validation.insert({"geojson-export",1},feature.geometry);
        const auto properties=losslessjson::parse(QByteArray::fromStdString(feature.propertiesJson));
        require(properties.kind==V::Object,"INVALID_GEOJSON: properties object");
        V row=webjson::obj({{"type",V::str("Feature")},{"properties",properties},
                             {"geometry",geometryValue(feature.geometry)}});
        if(!feature.id.empty()) {
            require(ids.insert(feature.id).second,"DUPLICATE_GEOJSON_ID");
            row.object["id"]=V::str(feature.id);
        }
        rows.array.push_back(std::move(row));
    }
    return webjson::obj({{"type",V::str("FeatureCollection")},{"features",std::move(rows)}}).encode()+"\n";
}
GisGenericImportPlan planGenericGeoJsonImport(const ProjectSnapshot& project,const QByteArray& bytes,
    std::string planId,std::string fileName) {
    auto collection=parseGisGeoJson(bytes);
    std::vector<GisGenericInput> rows;
    rows.reserve(collection.features.size());
    for(auto& feature:collection.features) {
        const auto properties=losslessjson::parse(QByteArray::fromStdString(feature.propertiesJson));
        auto id=feature.id.empty()?webjson::text(webjson::at(properties,"id")):feature.id;
        require(!id.empty(),"INVALID_GIS_PLAN: generic ID required");
        std::uint32_t color=0x888888;
        const auto& rawColor=webjson::at(properties,"color");
        if(rawColor.kind!=V::Null) {
            const auto value=webjson::text(rawColor);
            bool ok=false;
            require(value.size()==7&&value[0]=='#'&&
                    std::all_of(value.begin()+1,value.end(),[](unsigned char c){return std::isxdigit(c)!=0;}),
                    "INVALID_GIS_COLOR");
            color=QString::fromStdString(value.substr(1)).toUInt(&ok,16);
            require(ok&&color<=0xffffff,"INVALID_GIS_COLOR");
        }
        const auto& rawNotes=webjson::at(properties,"notes");
        require(rawNotes.kind==V::Null||rawNotes.kind==V::String,"INVALID_GIS_NOTES");
        rows.push_back({std::move(id),webjson::text(webjson::at(properties,"name")),
                        std::move(feature.geometry),std::move(feature.propertiesJson),
                        rawNotes.kind==V::String?rawNotes.string:std::string{},color});
    }
    return planGenericGisImport(project,std::move(planId),{std::move(fileName),"geojson"},
                                std::move(rows));
}
}

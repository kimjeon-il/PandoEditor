#include <gisgeojson.h>
#include <QCoreApplication>
#include <cassert>
#include <functional>
#include <stdexcept>

using namespace pandoeditor;
namespace {
bool rejected(const std::function<void()>& fn) {
    try {fn();}catch(const std::invalid_argument&){return true;}
    return false;
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    const QByteArray valid=R"({"type":"FeatureCollection","features":[
      {"type":"Feature","id":"lake","properties":{"name":"Lake","ordered":[3,1,2],"large":900719925474099312345},
       "geometry":{"type":"Polygon","coordinates":[[[0,0],[4,0],[4,4],[0,4],[0,0]],[[1,1],[2,1],[2,2],[1,2],[1,1]]]}},
      {"type":"Feature","id":"river","properties":{"name":"River"},
       "geometry":{"type":"LineString","coordinates":[[0,0],[1,1]]}}
    ]})";
    const auto parsed=parseGisGeoJson(valid);
    assert(parsed.features.size()==2);
    assert(parsed.features[0].id=="lake"&&parsed.features[0].geometry.polygons.front().size()==2);
    assert(parsed.features[0].propertiesJson.find("900719925474099312345")!=std::string::npos);
    assert(parsed.features[1].geometry.type=="LineString");
    const auto encoded=exportGisGeoJson(parsed);
    assert(encoded.contains("900719925474099312345"));
    assert(!encoded.contains("pandolab_project_settings"));
    const auto roundTrip=parseGisGeoJson(encoded);
    assert(roundTrip.features[0].geometry.polygons.front().size()==2);
    assert(rejected([]{parseGisGeoJson("{");}));
    assert(rejected([]{parseGisGeoJson(R"({"type":"FeatureCollection","features":[{"type":"Feature","id":"A","properties":{},"geometry":{"type":"Polygon","coordinates":[[[0,0],[1,0],[1,1],[0,1]]]}}]})");}));
    assert(rejected([]{parseGisGeoJson(R"({"type":"FeatureCollection","crs":{"type":"name","properties":{"name":"EPSG:3857"}},"features":[]})");}));
    assert(rejected([]{parseGisGeoJson(R"({"type":"FeatureCollection","features":[{"type":"Feature","id":"A","properties":{},"geometry":{"type":"Point","coordinates":[181,0]}}]})");}));
    assert(rejected([]{parseGisGeoJson(R"({"type":"FeatureCollection","features":[{"type":"Feature","id":"A","properties":{},"geometry":{"type":"Point","coordinates":[0,0]}},{"type":"Feature","id":"A","properties":{},"geometry":{"type":"Point","coordinates":[1,1]}}]})");}));
}

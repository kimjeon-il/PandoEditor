#include <gisgeojson.h>
#include <pandoeditor/project.h>
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
    Project project;project.replace(ProjectDocument({{"A","Alpha",{{{{0,0},{4,0},{4,4},{0,4},{0,0}}}},0x123456}},{{"countries","Countries"}}));
    const auto import=planGenericGeoJsonImport(project.snapshot(),valid,"plan:geojson","input.geojson");
    assert(import.features.size()==2 && import.features.front().id=="lake");
    assert(import.features.front().propertiesJson.find("900719925474099312345")!=std::string::npos);
    assert(project.document().genericFeatures.empty());
    CommandArguments args;args.action=import;
    auto preview=CommandProcessor::prepare(project,CommandProcessor::makeRequest(project,"gis.import.generic",args));
    assert(preview.ok()&&preview.preview);
    CommandProcessor::cancel(*preview.preview);
    assert(project.document().genericFeatures.empty());
    assert(rejected([&]{planGenericGeoJsonImport(project.snapshot(),
        R"({"type":"FeatureCollection","features":[{"type":"Feature","properties":{"name":"Nameless ID"},"geometry":{"type":"Point","coordinates":[0,0]}}]})",
        "plan:no-id","input.geojson");}));
}

#include <giszip.h>
#include <QCoreApplication>
#include <QFile>
#include <cassert>
#include <functional>
#include <stdexcept>

using namespace pandoeditor;

namespace {
QByteArray fixture(const char* name) {
    QFile file(QString::fromUtf8(WEB_GIS_ZIP_FIXTURE)+"/"+QString::fromUtf8(name));
    assert(file.open(QIODevice::ReadOnly));
    return file.readAll();
}
bool rejected(const std::function<void()>& f) {
    try { f(); } catch(const std::invalid_argument&) { return true; }
    return false;
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    const auto bundle=parseGisGeoJsonZip(fixture("web-gis-geojson.zip"));
    assert(bundle.webManifest);
    assert(bundle.layers.size()==5);
    assert(bundle.layers[0].path=="countries.geojson");
    assert(bundle.layers[0].targetType=="country");
    assert(bundle.layers[0].collection.features.size()==1);
    assert(bundle.layers[0].collection.features[0].propertiesJson.find("pandolab_id")!=std::string::npos);
    assert(bundle.layers[1].targetType=="subunit");
    assert(bundle.layers[1].collection.features[0].geometry.polygons.size()==1);
    assert(bundle.layers[3].targetType=="distribution");
    assert(bundle.layers[3].distributionType=="language");
    assert(bundle.layers[3].collection.features[0].propertiesJson.find("territorial_unit_id")!=std::string::npos);
    assert(bundle.layers[4].targetType=="label");
    const auto plain=parseGisGeoJsonZip(fixture("plain-geojson.zip"));
    assert(!plain.webManifest && plain.layers.size()==1 && plain.layers[0].targetType.empty());
    for(const auto* name:{"bad-schema.zip","bad-crs.zip","missing-layer.zip","wrong-count.zip"}) {
        assert(rejected([&]{ parseGisGeoJsonZip(fixture(name)); }));
    }
    assert(rejected([]{parseGisGeoJsonZip("bad");}));
}

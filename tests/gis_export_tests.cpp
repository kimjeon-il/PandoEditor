#include <gisdocumentexport.h>
#include <gisgeopackage.h>
#include <giszip.h>
#include <pandoeditor/giszip.h>
#include <pandoeditor/project.h>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QString>
#include <cassert>
#include <functional>

using namespace pandoeditor;
namespace {
ProjectDocument document() {
    ProjectDocument doc({{"A","Alpha",{{{{0,0},{4,0},{4,4},{0,4},{0,0}},
        {{1,1},{1,2},{2,2},{2,1},{1,1}}}},0x123456}},
        {{"countries","Countries"}});
    doc.units.front().sourceEntityId="state:A";
    doc.units.front().sourceGeometryVersion="v1";
    assert(!doc.units.front().libraryOrigin);
    DistributionLayer language;language.id="lang:1";language.unit="%";language.name="Language";
    doc.distributionLayers.push_back(language);
    DistributionEntry entry;entry.id="entry:1";entry.layerId=language.id;
    entry.territory=territorialRef("A");entry.value=73;doc.distributionEntries.push_back(entry);
    Geometry point;point.type="Point";point.points={{2,2}};
    doc.geometries.insert({"label:1",1},point);
    PlaceLabel label;label.id="label:1";label.name="City";label.geometry={"label:1",1};
    doc.labels.push_back(label);
    doc.geometries.insert({"generic:1",1},point);
    GenericFeature feature;feature.id="generic:1";feature.name="Point";feature.geometry={"generic:1",1};
    doc.genericFeatures.push_back(feature);
    validateDocument(doc);return doc;
}
bool rejected(const std::function<void()>& action) {
    try {action();}catch(const std::invalid_argument&){return true;}return false;
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    const auto doc=document();
    if(argc==3&&QString::fromLocal8Bit(argv[1])=="--emit") {
        const auto base=QString::fromLocal8Bit(argv[2]);
        for(const auto& [name,bytes]:std::vector<std::pair<QString,QByteArray>>{
            {"selected.zip",exportGisGeoJsonZip(doc,{"countries","distributions","genericFeatures","labels"},
                "2026-09-25T00:00:00.000Z")},
            {"selected.gpkg",exportGisGeoPackage(doc,{"countries","distributions","genericFeatures","labels"})},
            {"countries.gpkg",exportGisGeoPackage(doc,{"countries"})}}) {
            QFile file(base+"/"+name);
            if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size())return 2;
        }
        return 0;
    }
    const auto snapshot=doc.units.size();
    const auto layers=buildGisExportLayers(doc,{"countries","distributions","genericFeatures","labels"});
    assert(layers.size()==4);
    assert(layers[0].file=="countries.geojson"&&layers[0].targetType=="country");
    assert(layers[1].file=="generic_features.geojson");
    assert(layers[2].file=="distributions.geojson");
    assert(layers[2].collection.features.front().propertiesJson.find("territorial_unit_id")!=std::string::npos);
    assert(layers[3].file=="labels.geojson");
    const auto zip=exportGisGeoJsonZip(doc,{"countries","distributions","genericFeatures","labels"},
        "2026-09-25T00:00:00.000Z");
    const auto archive=parseGisGeoJsonZip(zip);
    assert(archive.webManifest&&archive.layers.size()==4);
    assert(archive.layers.front().collection.features.front().propertiesJson.find("state:A")!=std::string::npos);
    assert(archive.layers[2].distributionType.empty());
    const auto raw=readGisZipArchive(std::string_view(zip.constData(),std::size_t(zip.size())));
    assert(raw.entries.back().path=="manifest.json");
    assert(raw.entries.back().bytes.find("\"schemaVersion\":3")!=std::string::npos);
    assert(rejected([&]{exportGisGeoJsonZip(doc,{},"now");}));
    assert(rejected([&]{exportGisGeoJsonZip(doc,{"unknown"},"now");}));

    const auto gpkg=exportGisGeoPackage(doc,{"countries"});
    QTemporaryDir temporary;assert(temporary.isValid());
    const auto path=temporary.filePath("gis.gpkg");
    QFile file(path);assert(file.open(QIODevice::WriteOnly));
    assert(file.write(gpkg)==gpkg.size());file.close();
    const auto parsed=readGisGeoPackage(path);
    assert(!parsed.projectPackage&&parsed.layers.size()==1);
    assert(parsed.layers.front().tableName=="countries");
    assert(parsed.layers.front().collection.features.size()==1);
    assert(parsed.layers.front().collection.features.front().id=="A");
    assert(parsed.layers.front().collection.features.front().propertiesJson.find("state:A")!=std::string::npos);
    assert(parsed.layers.front().collection.features.front().propertiesJson.find("v1")!=std::string::npos);
    assert(doc.units.size()==snapshot&&doc.distributionEntries.front().territory==territorialRef("A"));
    assert(rejected([&]{exportGisGeoPackage(doc,{"regions"});}));
}

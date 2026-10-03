#include <gisdocumentexport.h>
#include <gisgeojson.h>
#include <QCoreApplication>
#include <cassert>

using namespace pandoeditor;
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    ProjectDocument doc({{"A","Alpha",{{{{0,0},{4,0},{4,4},{0,4},{0,0}}}},0x123456}},{{"countries","Countries"}});
    doc.units.front().libraryOrigin=LibraryOrigin{"historical-country:A","v-1945","1945","archive","2","medium","year",false,{}};
    DistributionLayer layer;layer.id="language:1";layer.unit="language";layer.name="L";
    doc.distributionLayers.push_back(layer);
    DistributionEntry entry;entry.id="entry:1";entry.layerId=layer.id;
    entry.territory=territorialRef("A");entry.value=73;entry.certainty="high";
    doc.distributionEntries.push_back(entry);
    Geometry point;point.type="Point";point.points={{2,2}};
    doc.geometries.insert({"label:1",1},point);
    PlaceLabel label;label.id="label:1";label.name="City";label.geometry={"label:1",1};
    doc.labels.push_back(label);
    validateDocument(doc);
    const auto countries=exportGisDocumentLayer(doc,"countries");
    assert(countries.features.size()==1);
    const auto json=exportGisGeoJson(countries);
    assert(json.contains("historical-country:A")&&json.contains("v-1945"));
    assert(!json.contains("pandolab_project_settings")&&!json.contains("pandolab_country_assets"));
    const auto distribution=exportGisDocumentLayer(doc,"distributions");
    assert(distribution.features.size()==1);
    assert(distribution.features.front().geometry.type=="MultiPolygon");
    assert(distribution.features.front().propertiesJson.find("territorial_unit_id")!=std::string::npos);
    assert(distribution.features.front().propertiesJson.find("entry:1")!=std::string::npos);
    assert(exportGisDocumentLayer(doc,"labels").features.front().geometry.type=="Point");
    assert(exportGisDocumentLayer(doc,"subunits").features.empty());
}

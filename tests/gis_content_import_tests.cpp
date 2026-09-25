#include <giscontentimport.h>
#include <pandoeditor/project.h>
#include <QCoreApplication>
#include <QFile>
#include <cassert>
#include <functional>
#include <stdexcept>

using namespace pandoeditor;
namespace {
Project project() {
    Project p;
    p.replace(ProjectDocument({{"AAA","Alpha",{{{{0,0},{2,0},{2,2},{0,2},{0,0}}}},0x123456}},
        {{"countries","Countries"}}));
    return p;
}
bool rejected(const std::function<void()>& run) {
    try {run();}catch(const std::invalid_argument&){return true;}return false;
}
QByteArray fixture(const char* name) {
    QFile file(QString::fromUtf8(WEB_GIS_FIXTURE)+"/"+name);
    assert(file.open(QIODevice::ReadOnly));return file.readAll();
}
template<typename Plan> void confirm(Project& project,const Plan& plan,const char* command) {
    CommandArguments args;args.action=plan;
    const auto request=CommandProcessor::makeRequest(project,command,args);
    auto prepared=CommandProcessor::prepare(project,request);
    assert(prepared.ok()&&prepared.preview);
    assert(CommandProcessor::confirm(project,*prepared.preview).changed());
    assert(CommandProcessor::prepare(project,request).error==CommandError::StaleRevision);
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    const auto zip=parseGisGeoJsonZip(fixture("web-gis-geojson.zip"));
    assert(zip.layers.size()==5);
    auto p=project();
    GisContentMapping distribution;distribution.target=GisExchangeTarget::Distribution;
    distribution.distributionType="language";
    auto imported=planGisContentZipImport(p.snapshot(),zip,3,"web-distribution",
        {"web.zip","geojson-zip"},distribution);
    const auto& plan=std::get<GisDistributionImportPlan>(imported);
    assert(plan.layers.size()==1&&plan.layers.front().id=="lang:1");
    assert(plan.entries.size()==1&&plan.entries.front().entry.id=="entry:1");
    assert(plan.entries.front().entry.territory==territorialRef("AAA"));
    assert(!plan.entries.front().geometry);
    assert(p.document().distributionEntries.empty());
    confirm(p,plan,"gis.import.distribution");
    assert(p.document().distributionEntries.size()==1);
    assert(!p.document().distributionEntries.front().geometry);
    assert(p.undo()&&p.document().distributionEntries.empty());
    assert(p.redo()&&p.document().distributionEntries.size()==1);
    assert(rejected([&]{planGisContentZipImport(p.snapshot(),zip,3,"duplicate",
        {"web.zip","geojson-zip"},distribution);}));

    GisContentMapping generic;generic.target=GisExchangeTarget::Generic;
    auto genericPlan=std::get<GisGenericImportPlan>(planGisContentZipImport(p.snapshot(),zip,2,
        "web-generic",{"web.zip","geojson-zip"},generic));
    assert(genericPlan.features.size()==1&&genericPlan.features.front().id=="generic:1");
    confirm(p,genericPlan,"gis.import.generic");
    assert(p.document().genericFeatures.size()==1);
    assert(p.document().genericFeatures.front().source.details.find("Generic")!=std::string::npos);
    assert(p.undo()&&p.document().genericFeatures.empty());
    assert(rejected([&]{planGisContentZipImport(p.snapshot(),zip,0,"wrong",
        {"web.zip","geojson-zip"},generic);}));

    const auto gpkg=readGisGeoPackage(QString::fromUtf8(WEB_GIS_FIXTURE)+"/web-gis.gpkg");
    std::size_t distributionIndex=gpkg.layers.size(),genericIndex=gpkg.layers.size();
    for(std::size_t i=0;i<gpkg.layers.size();++i) {
        if(gpkg.layers[i].tableName=="language_distribution")distributionIndex=i;
        if(gpkg.layers[i].tableName=="generic_features_point")genericIndex=i;
    }
    assert(distributionIndex<gpkg.layers.size()&&genericIndex<gpkg.layers.size());
    auto other=project();
    auto gpkgDistribution=std::get<GisDistributionImportPlan>(planGisContentGeoPackageImport(
        other.snapshot(),gpkg,distributionIndex,"gpkg-distribution",
        {"web.gpkg","geopackage"},distribution));
    assert(gpkgDistribution.entries.front().entry.territory==territorialRef("AAA"));
    confirm(other,gpkgDistribution,"gis.import.distribution");
    auto gpkgGeneric=std::get<GisGenericImportPlan>(planGisContentGeoPackageImport(
        other.snapshot(),gpkg,genericIndex,"gpkg-generic",{"web.gpkg","geopackage"},generic));
    assert(!gpkgGeneric.features.empty());
    confirm(other,gpkgGeneric,"gis.import.generic");

    auto fresh=project();
    auto broken=zip.layers[3].collection;
    broken.features.front().propertiesJson.replace(
        broken.features.front().propertiesJson.find("\"AAA\""),5,"\"missing\"");
    assert(rejected([&]{planGisContentImport(fresh.snapshot(),broken,"missing",
        {"x","geojson"},distribution);}));
    broken=zip.layers[3].collection;
    broken.features.front().propertiesJson.replace(
        broken.features.front().propertiesJson.find("\"share\":60"),10,"\"share\":101");
    assert(rejected([&]{planGisContentImport(fresh.snapshot(),broken,"share",
        {"x","geojson"},distribution);}));
    broken=zip.layers[3].collection;
    broken.features.push_back(broken.features.front());
    assert(rejected([&]{planGisContentImport(fresh.snapshot(),broken,"duplicate",
        {"x","geojson"},distribution);}));
    broken=zip.layers[3].collection;
    auto& properties=broken.features.front().propertiesJson;
    auto start=properties.find("\"source_mode\":\"territorial\"");
    assert(start!=std::string::npos);
    properties.replace(start,std::string("\"source_mode\":\"territorial\"").size(),
        "\"source_mode\":\"geometry\"");
    auto freePlan=std::get<GisDistributionImportPlan>(planGisContentImport(fresh.snapshot(),
        broken,"free",{"x","geojson"},distribution));
    assert(freePlan.entries.front().geometry&&!freePlan.entries.front().entry.territory);
    confirm(fresh,freePlan,"gis.import.distribution");
    assert(fresh.document().distributionEntries.front().geometry);
    assert(fresh.undo()&&fresh.document().distributionEntries.empty());
}

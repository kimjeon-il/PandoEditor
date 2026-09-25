#include <projectgeopackage.h>
#include <gisgeopackage.h>
#include <projectcodec.h>
#include <pandoeditor/project.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <cassert>
#include <functional>
#include <stdexcept>

using namespace pandoeditor;
namespace {
bool rejected(const std::function<void()>& action) {
    try {action();}catch(const std::invalid_argument&){return true;}
    return false;
}
void corruptCopy(const QString& original,const QString& path,const QString& sql) {
    assert(QFile::copy(original,path));
    {
        auto db=QSqlDatabase::addDatabase("QSQLITE","project-corruption-test");
        db.setDatabaseName(path);assert(db.open());
        {QSqlQuery query(db);for(const auto& statement:sql.split(';',Qt::SkipEmptyParts))
            assert(query.exec(statement));}
        db.close();db={};
    }
    QSqlDatabase::removeDatabase("project-corruption-test");
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    const auto web=QString::fromUtf8(WEB_GPKG_FIXTURE)+"/web-project.gpkg";
    const auto imported=readProjectGeoPackage(web);
    Project webProject;webProject.replace(projectcodec::decode(imported));
    const auto& webDocument=webProject.document();
    assert(webProject.country("AAA")&&webProject.country("AAA")->name=="Alpha");
    assert(webDocument.units.size()==2);
    assert(webDocument.genericFeatures.size()==1);
    assert(webDocument.distributionLayers.size()==1);
    assert(webDocument.distributionEntries.size()==1);
    assert(webDocument.labels.size()==1);
    assert(webDocument.symbols.at(territorialRef("AAA")).policy==FlagPolicy::Embedded);
    assert(webDocument.symbols.at(territorialRef("AAA")).embeddedDataUrl.find("data:image/svg+xml;base64,")==0);
    bool retainedSource=false;
    for(const auto& extension:webDocument.extensions)
        if(extension.jsonPointer=="/sourceInfo"&&extension.payload.find("web-worker")!=std::string::npos)
            retainedSource=true;
    assert(retainedSource);
    Project source;
    source.replace(ProjectDocument({{"A","Alpha",{{{{0,0},{2,0},{2,2},{0,2},{0,0}}}},0x123456},
                                    {"B","Beta",{{{{3,0},{5,0},{5,2},{3,2},{3,0}}}},0x654321},
                                    {"C","Gamma",{{{{6,0},{8,0},{8,2},{6,2},{6,0}}}},0xabcdef}},
                                   {{"countries","Countries"}}));
    auto doc=source.document();
    doc.units.front().libraryOrigin=LibraryOrigin{"history:A","v1","1945","archive","2","high","year",false,{}};
    doc.symbols[territorialRef("B")].policy=FlagPolicy::None;
    auto& embedded=doc.symbols[territorialRef("C")];
    embedded.policy=FlagPolicy::Embedded;
    embedded.embeddedDataUrl="data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciLz4=";
    source.replace(doc);
    const auto original=projectcodec::encode(source);
    QTemporaryDir directory;assert(directory.isValid());
    const auto bytes=exportProjectGeoPackage(source);
    const auto path=argc==3&&QString::fromLocal8Bit(argv[1])=="--emit"
        ?QDir(QString::fromLocal8Bit(argv[2])).filePath("project.gpkg")
        :directory.filePath("project.gpkg");
    QFile output(path);assert(output.open(QIODevice::WriteOnly));
    assert(output.write(bytes)==bytes.size());output.close();
    if(argc==3&&QString::fromLocal8Bit(argv[1])=="--emit")return 0;
    const auto restored=readProjectGeoPackage(path);
    Project decoded;decoded.replace(projectcodec::decode(restored));
    assert(projectcodec::encode(decoded)==original);
    const auto missing=directory.filePath("missing-asset.gpkg");
    corruptCopy(path,missing,"DELETE FROM pandolab_country_assets WHERE country_id='C'");
    assert(rejected([&]{readProjectGeoPackage(missing);}));
    const auto mismatch=directory.filePath("mismatched-country.gpkg");
    corruptCopy(path,mismatch,"UPDATE countries SET pandolab_id='other' WHERE pandolab_id='A'");
    assert(rejected([&]{readProjectGeoPackage(mismatch);}));
    const auto gisOnly=directory.filePath("gis-only.gpkg");
    const auto gis=exportGisGeoPackage(source.document(),{"countries"});
    QFile gisOutput(gisOnly);assert(gisOutput.open(QIODevice::WriteOnly));
    assert(gisOutput.write(gis)==gis.size());gisOutput.close();
    assert(rejected([&]{readProjectGeoPackage(gisOnly);}));
    const auto orphan=directory.filePath("orphan-web-asset.gpkg");
    corruptCopy(web,orphan,"UPDATE pandolab_country_assets SET country_id='ghost'");
    assert(rejected([&]{readProjectGeoPackage(orphan);}));
    const auto wrongWebCountry=directory.filePath("wrong-web-country.gpkg");
    corruptCopy(web,wrongWebCountry,"UPDATE countries SET pandolab_id='other' WHERE pandolab_id='AAA'");
    assert(rejected([&]{readProjectGeoPackage(wrongWebCountry);}));
    const auto wrongSource=directory.filePath("wrong-web-source.gpkg");
    corruptCopy(web,wrongSource,"UPDATE pandolab_source_info SET json_value='{}' WHERE info_key='source'");
    assert(rejected([&]{readProjectGeoPackage(wrongSource);}));
    const auto none=directory.filePath("none-web-flag.gpkg");
    corruptCopy(web,none,"DELETE FROM pandolab_country_assets; UPDATE pandolab_project_settings "
                         "SET json_value=replace(json_value,'\"AAA\":{}','\"AAA\":{\"flagDataUrl\":null}') "
                         "WHERE setting_key='project_state'");
    Project noneProject;noneProject.replace(projectcodec::decode(readProjectGeoPackage(none)));
    assert(noneProject.document().symbols.at(territorialRef("AAA")).policy==FlagPolicy::None);
    const auto defaultFlag=directory.filePath("default-web-flag.gpkg");
    corruptCopy(web,defaultFlag,"DELETE FROM pandolab_country_assets");
    Project defaultProject;defaultProject.replace(projectcodec::decode(readProjectGeoPackage(defaultFlag)));
    assert(!defaultProject.document().symbols.count(territorialRef("AAA"))||
           defaultProject.document().symbols.at(territorialRef("AAA")).policy==FlagPolicy::Default);
}

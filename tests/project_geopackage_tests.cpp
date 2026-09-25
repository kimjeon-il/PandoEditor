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
        {QSqlQuery query(db);assert(query.exec(sql));}
        db.close();db={};
    }
    QSqlDatabase::removeDatabase("project-corruption-test");
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
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
}

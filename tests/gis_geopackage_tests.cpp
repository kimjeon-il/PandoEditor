#include <gisgeopackage.h>
#include <QCoreApplication>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <cassert>
#include <functional>
#include <stdexcept>
#include <string>

using namespace pandoeditor;
namespace {
QString fixture(const char* name) {
    return QString::fromUtf8(WEB_GPKG_FIXTURE)+"/"+QString::fromUtf8(name);
}
bool rejected(const std::function<void()>& fn) {
    try {fn();}catch(const std::invalid_argument&){return true;}
    return false;
}
QString mutate(QTemporaryDir& dir,const QString& source,const QString& sql,int index) {
    const auto path=dir.filePath(QString::number(index)+".gpkg");
    assert(QFile::copy(source,path));
    const auto id=QUuid::createUuid().toString();
    {
        auto db=QSqlDatabase::addDatabase("QSQLITE",id);
        db.setDatabaseName(path);assert(db.open());
        QSqlQuery query(db);assert(query.exec(sql));
        db.close();
    }
    QSqlDatabase::removeDatabase(id);
    return path;
}
const GisGeoPackageLayer& layer(const GisGeoPackage& pkg,const std::string& table) {
    for(const auto& row:pkg.layers)if(row.tableName==table)return row;
    throw std::runtime_error("missing test layer");
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    const auto gis=readGisGeoPackage(fixture("web-gis.gpkg"));
    assert(!gis.projectPackage);
    assert(gis.layers.size()==10);
    const auto& country=layer(gis,"countries");
    assert(country.targetType=="country");
    assert(country.collection.features.size()==1);
    assert(country.collection.features[0].id=="AAA");
    assert(country.collection.features[0].geometry.type=="MultiPolygon");
    assert(country.collection.features[0].geometry.polygons.front().front().size()==5);
    const auto& subunit=layer(gis,"subunits");
    assert(subunit.targetType=="subunit");
    assert(subunit.collection.features[0].propertiesJson.find("history:1")!=std::string::npos);
    const auto& distribution=layer(gis,"language_distribution");
    assert(distribution.targetType=="distribution");
    assert(distribution.collection.features[0].id=="entry:1");
    assert(distribution.collection.features[0].propertiesJson.find("territorial_unit_id")!=std::string::npos);
    assert(layer(gis,"places").targetType=="label");
    const auto project=readGisGeoPackage(fixture("web-project.gpkg"));
    assert(project.projectPackage && project.layers.size()==gis.layers.size());
    QTemporaryDir dir;assert(dir.isValid());
    const auto broken=dir.filePath("broken.gpkg");
    {QFile f(broken);assert(f.open(QIODevice::WriteOnly));f.write("not SQLite");}
    assert(rejected([&]{readGisGeoPackage(broken);}));
    assert(rejected([&]{readGisGeoPackage(dir.filePath("missing.gpkg"));}));
    int n=0;
    const auto source=fixture("web-gis.gpkg");
    for(const auto& sql:{
        "DROP TABLE gpkg_spatial_ref_sys",
        "DROP TABLE gpkg_contents",
        "DROP TABLE gpkg_geometry_columns",
        "DELETE FROM gpkg_spatial_ref_sys WHERE srs_id=4326",
        "UPDATE gpkg_geometry_columns SET srs_id=3857 WHERE table_name='countries'",
        "UPDATE gpkg_contents SET srs_id=3857 WHERE table_name='countries'",
        "DELETE FROM gpkg_geometry_columns WHERE table_name='countries'",
        "DELETE FROM gpkg_contents WHERE table_name='countries'",
        "UPDATE countries SET geom=x'0001' WHERE pandolab_id='AAA'",
        "UPDATE countries SET geom=x'47500001E6100000010600000000000000' WHERE pandolab_id='AAA'",
        "UPDATE countries SET geom=substr(geom,1,4)||x'110F0000'||substr(geom,9) WHERE pandolab_id='AAA'",
        "UPDATE countries SET geom=x'' WHERE pandolab_id='AAA'",
        "UPDATE countries SET pandolab_id='' WHERE pandolab_id='AAA'",
        "INSERT INTO countries(geom,pandolab_id,pandolab_name) SELECT geom,pandolab_id,pandolab_name FROM countries LIMIT 1",
        "UPDATE gpkg_geometry_columns SET geometry_type_name='POINT' WHERE table_name='countries'",
    }) {
        const auto path=mutate(dir,source,sql,++n);
        assert(rejected([&]{readGisGeoPackage(path);}));
    }
    const auto projectSource=fixture("web-project.gpkg");
    assert(rejected([&]{readGisGeoPackage(mutate(dir,projectSource,
        "DROP TABLE pandolab_country_assets",++n));}));
}

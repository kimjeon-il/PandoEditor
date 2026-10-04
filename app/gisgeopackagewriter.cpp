#include "gisgeopackage.h"
#include "gisdocumentexport.h"
#include "webjson.h"
#include <pandoeditor/geopackagegeometry.h>
#include <QDateTime>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <string_view>

namespace pandoeditor {
namespace {
using V=losslessjson::Value;
struct Column {const char* name;const char* type="TEXT";};
struct Table {
    std::string name,category,geometryType;
    std::vector<Column> columns;
};
void require(bool okay,const char* code) {
    if(!okay)throw std::invalid_argument(code);
}
class Database {
public:
    QString name=QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlDatabase db;
    explicit Database(const QString& path) {
        require(QSqlDatabase::isDriverAvailable("QSQLITE"),"GPKG_SQLITE_DRIVER_UNAVAILABLE");
        db=QSqlDatabase::addDatabase("QSQLITE",name);
        db.setDatabaseName(path);
        if(!db.open()) {
            db={};QSqlDatabase::removeDatabase(name);
            throw std::invalid_argument("GPKG_WRITE_FAILED");
        }
    }
    ~Database(){db.close();db={};QSqlDatabase::removeDatabase(name);}
};
void exec(QSqlDatabase& db,const QString& sql) {
    QSqlQuery query(db);require(query.exec(sql),"GPKG_WRITE_FAILED");
}
QString quote(const std::string& raw) {
    QString name=QString::fromStdString(raw);name.replace('"',"\"\"");
    return "\""+name+"\"";
}
std::vector<Table> tables() {
    const std::vector<Column> territorial={{"id"},{"name"},{"type"},{"parent_id"},
        {"sovereign_id"},{"valid_from"},{"valid_to"},{"color"},{"style_key"},
        {"source_library_id"},{"source_geometry_version"},{"metadata_json"},{"properties_json"}};
    const std::vector<Column> generic={{"id"},{"name"},{"role"},{"owner_id"},
        {"parent_id"},{"topology_group"},{"land_binding"},{"color"},{"notes"},
        {"locked","INTEGER"},{"properties_json"}};
    const std::vector<Column> distribution={{"entry_id"},{"layer_id"},{"name"},
        {"unit"},{"value_scale_mode"},{"value_scale_min","REAL"},{"value_scale_max","REAL"},
        {"parent_layer_id"},{"color"},{"layer_visible","INTEGER"},
        {"layer_locked","INTEGER"},{"source_mode"},{"territorial_unit_id"},
        {"value","REAL"},{"certainty"},{"valid_from"},{"valid_to"},
        {"layer_metadata_json"},{"entry_metadata_json"}};
    return {
        {"countries","countries","MULTIPOLYGON",{{"pandolab_id"},{"pandolab_name"},
            {"valid_from"},{"valid_to"},{"source_library_id"},{"source_geometry_version"}}},
        {"subunits","subunits","MULTIPOLYGON",territorial},
        {"regions","regions","MULTIPOLYGON",territorial},
        {"generic_features_point","genericFeatures","POINT",generic},
        {"generic_features_line","genericFeatures","MULTILINESTRING",generic},
        {"generic_features_polygon","genericFeatures","MULTIPOLYGON",generic},
        {"distributions","distributions","MULTIPOLYGON",distribution},
        {"places","labels","POINT",{{"pandolab_id"},{"name"},{"kind"},
            {"country_id"},{"notes"}}}
    };
}
Geometry promote(const Geometry& source,const std::string& target) {
    Geometry shape=source;
    if(target=="MULTIPOLYGON"&&shape.type=="Polygon")shape.type="MultiPolygon";
    if(target=="MULTILINESTRING"&&shape.type=="LineString")shape.type="MultiLineString";
    const auto accepted=target=="POINT"?"Point":
        target=="MULTIPOLYGON"?"MultiPolygon":"MultiLineString";
    require(shape.type==accepted,"UNSUPPORTED_GPKG_EXPORT_GEOMETRY");
    return shape;
}
std::string tableFor(const GisExportLayer& layer,const Geometry& geometry) {
    if(layer.category=="countries")return "countries";
    if(layer.category=="subunits")return "subunits";
    if(layer.category=="regions")return "regions";
    if(layer.category=="labels")return "places";
    if(layer.category=="distributions")return "distributions";
    if(layer.category=="genericFeatures") {
        if(geometry.type=="Point")return "generic_features_point";
        if(geometry.type=="LineString"||geometry.type=="MultiLineString")return "generic_features_line";
        if(geometry.type=="Polygon"||geometry.type=="MultiPolygon")return "generic_features_polygon";
    }
    throw std::invalid_argument("UNSUPPORTED_GPKG_EXPORT_GEOMETRY");
}
V properties(const GisGeoJsonFeature& feature,const Table& table) {
    auto props=losslessjson::parse(QByteArray::fromStdString(feature.propertiesJson));
    require(props.kind==V::Object,"INVALID_GIS_EXPORT_PROPERTIES");
    if(table.category=="genericFeatures") {
        auto& metadata=props.object["source_metadata"];
        props.object["properties_json"]=V::str(metadata.kind==V::Object?
            metadata.encode().toStdString():std::string("{}"));
        props.object["role"]=V::str("generic");
        props.object["land_binding"]=V::str("none");
        props.object["locked"]=V::num(0);
    } else if(table.category=="subunits"||table.category=="regions") {
        props.object["metadata_json"]=V::str("{}");
        props.object["properties_json"]=V::str(props.encode().toStdString());
    }
    return props;
}
QVariant scalar(const V& value,const char* type) {
    if(value.kind==V::Null)return {};
    if(value.kind==V::Number) {
        if(std::string_view(type)=="INTEGER")return value.raw.toLongLong();
        if(std::string_view(type)=="REAL")return value.raw.toDouble();
        return QString::fromUtf8(value.raw);
    }
    if(value.kind==V::Bool)return value.raw=="true"?1:0;
    if(value.kind==V::String)return QString::fromStdString(value.string);
    return QString::fromUtf8(value.encode());
}
struct Bounds {
    double minX=std::numeric_limits<double>::infinity(),minY=minX;
    double maxX=-minX,maxY=-minX;
    void add(Point p) {
        minX=std::min(minX,p.x);minY=std::min(minY,p.y);
        maxX=std::max(maxX,p.x);maxY=std::max(maxY,p.y);
    }
    void add(const Geometry& shape) {
        for(const auto& p:shape.points)add(p);
        for(const auto& line:shape.lines)for(const auto& p:line)add(p);
        for(const auto& polygon:shape.polygons)
            for(const auto& ring:polygon)for(const auto& p:ring)add(p);
    }
    bool valid() const{return std::isfinite(minX)&&std::isfinite(minY);}
};
void createTables(QSqlDatabase& db) {
    exec(db,"PRAGMA application_id=1196444487");
    exec(db,"PRAGMA user_version=10300");
    exec(db,"CREATE TABLE gpkg_spatial_ref_sys (srs_name TEXT NOT NULL, srs_id INTEGER NOT NULL PRIMARY KEY, organization TEXT NOT NULL, organization_coordsys_id INTEGER NOT NULL, definition TEXT NOT NULL, description TEXT)");
    exec(db,"CREATE TABLE gpkg_contents (table_name TEXT NOT NULL PRIMARY KEY, data_type TEXT NOT NULL, identifier TEXT UNIQUE, description TEXT DEFAULT '', last_change DATETIME NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')), min_x DOUBLE, min_y DOUBLE, max_x DOUBLE, max_y DOUBLE, srs_id INTEGER)");
    exec(db,"CREATE TABLE gpkg_geometry_columns (table_name TEXT NOT NULL, column_name TEXT NOT NULL, geometry_type_name TEXT NOT NULL, srs_id INTEGER NOT NULL, z TINYINT NOT NULL, m TINYINT NOT NULL, PRIMARY KEY(table_name,column_name))");
    QSqlQuery srs(db);
    require(srs.prepare("INSERT INTO gpkg_spatial_ref_sys VALUES (?,?,?,?,?,?)"),"GPKG_WRITE_FAILED");
    for(const auto& row:std::array<std::array<QVariant,6>,3>{{
        {QString("Undefined Cartesian"),-1,QString("NONE"),-1,QString("undefined"),QString()},
        {QString("Undefined geographic"),0,QString("NONE"),0,QString("undefined"),QString()},
        {QString("WGS 84"),4326,QString("EPSG"),4326,QString("GEOGCS[\"WGS 84\"]"),QString()}
    }}) {
        for(int i=0;i<6;++i)srs.bindValue(i,row[i]);
        require(srs.exec(),"GPKG_WRITE_FAILED");
    }
}
void writeTable(QSqlDatabase& db,const Table& table,
    const std::vector<GisGeoJsonFeature>& features) {
    QString create="CREATE TABLE "+quote(table.name)+
        " (fid INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL, geom BLOB NOT NULL";
    for(const auto& field:table.columns)
        create+=", "+quote(field.name)+" "+field.type;
    exec(db,create+")");
    const auto now=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QString insert="INSERT INTO "+quote(table.name)+" (geom";
    QString placeholders="?";
    for(const auto& field:table.columns) {
        insert+=", "+quote(field.name);
        placeholders+=",?";
    }
    insert+=") VALUES ("+placeholders+")";
    QSqlQuery row(db);require(row.prepare(insert),"GPKG_WRITE_FAILED");
    Bounds bounds;
    for(const auto& feature:features) {
        const auto shape=promote(feature.geometry,table.geometryType);
        bounds.add(shape);
        const auto geometry=encodeGeoPackageGeometry(shape);
        row.bindValue(0,QByteArray(reinterpret_cast<const char*>(geometry.data()),
                                   qsizetype(geometry.size())));
        const auto props=properties(feature,table);
        for(std::size_t i=0;i<table.columns.size();++i)
            row.bindValue(int(i+1),scalar(webjson::at(props,table.columns[i].name),
                                          table.columns[i].type));
        require(row.exec(),"GPKG_WRITE_FAILED");
    }
    QSqlQuery contents(db);
    require(contents.prepare("INSERT INTO gpkg_contents (table_name,data_type,identifier,description,last_change,min_x,min_y,max_x,max_y,srs_id) VALUES (?,'features',?,?,?,?,?,?,?,4326)"),
        "GPKG_WRITE_FAILED");
    const QString tableName=QString::fromStdString(table.name);
    for(int i=0;i<4;++i)contents.bindValue(i,i==0?tableName:i==1?tableName:
        i==2?QStringLiteral("PandoEditor GIS ")+tableName:now);
    for(int i=0;i<4;++i)contents.bindValue(i+4,bounds.valid()?QVariant(
        i==0?bounds.minX:i==1?bounds.minY:i==2?bounds.maxX:bounds.maxY):QVariant());
    require(contents.exec(),"GPKG_WRITE_FAILED");
    QSqlQuery columns(db);
    require(columns.prepare("INSERT INTO gpkg_geometry_columns VALUES (?,'geom',?,4326,0,0)"),
        "GPKG_WRITE_FAILED");
    columns.bindValue(0,tableName);
    columns.bindValue(1,QString::fromStdString(table.geometryType));
    require(columns.exec(),"GPKG_WRITE_FAILED");
}
}
QByteArray exportGisGeoPackage(const ProjectDocument& document,
    const std::vector<std::string>& selected,bool metadataOnly) {
    require(!metadataOnly||selected.empty(),"INVALID_GPKG_VECTOR_SELECTION");
    const auto layers=metadataOnly?std::vector<GisExportLayer>{}:buildGisExportLayers(document,selected);
    std::map<std::string,std::vector<GisGeoJsonFeature>> rows;
    for(const auto& layer:layers)for(const auto& feature:layer.collection.features)
        rows[tableFor(layer,feature.geometry)].push_back(feature);
    const std::set<std::string> categories(selected.begin(),selected.end());
    QTemporaryDir temporary;
    require(temporary.isValid(),"GPKG_TEMP_FAILED");
    const auto fileName=temporary.filePath("gis-export.gpkg");
    {
        Database connection(fileName);
        createTables(connection.db);
        require(connection.db.transaction(),"GPKG_WRITE_FAILED");
        for(const auto& table:tables())if(categories.count(table.category))
            writeTable(connection.db,table,rows[table.name]);
        require(connection.db.commit(),"GPKG_WRITE_FAILED");
    }
    QFile file(fileName);
    require(file.open(QIODevice::ReadOnly),"GPKG_READBACK_FAILED");
    return file.readAll();
}
}

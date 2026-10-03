#include "gisgeopackage.h"
#include "webjson.h"
#include <pandoeditor/geopackagegeometry.h>
#include <QFile>
#include <QFileInfo>
#include <QMetaType>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <stdexcept>

namespace pandoeditor {
namespace {
using V=losslessjson::Value;
constexpr qint64 maxFile=512ll*1024*1024;
void require(bool valid,const char* message) {
    if(!valid)throw std::invalid_argument(message);
}
class ReadOnlyDatabase {
public:
    QString name=QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlDatabase db;
    explicit ReadOnlyDatabase(const QString& path) {
        require(QSqlDatabase::isDriverAvailable("QSQLITE"),"GPKG_SQLITE_DRIVER_UNAVAILABLE");
        db=QSqlDatabase::addDatabase("QSQLITE",name);
        db.setDatabaseName(path);
        db.setConnectOptions("QSQLITE_OPEN_READONLY");
        if(!db.open()) {
            db={};
            QSqlDatabase::removeDatabase(name);
            throw std::invalid_argument("INVALID_GPKG_SQLITE");
        }
    }
    ~ReadOnlyDatabase() {
        db.close();db={};
        QSqlDatabase::removeDatabase(name);
    }
    ReadOnlyDatabase(const ReadOnlyDatabase&)=delete;
    ReadOnlyDatabase& operator=(const ReadOnlyDatabase&)=delete;
};
QSqlQuery query(QSqlDatabase& db,const QString& sql) {
    QSqlQuery result(db);
    require(result.exec(sql),"INVALID_GPKG_SQLITE");
    return result;
}
QString identifier(const QString& value) {
    QString escaped=value;
    escaped.replace('"',"\"\"");
    return QStringLiteral("\"")+escaped+QStringLiteral("\"");
}
std::string ascii(const QString& value) {
    return value.toUtf8().toStdString();
}
bool hasTable(QSqlDatabase& db,const QString& name) {
    QSqlQuery row(db);
    row.prepare("SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1");
    row.addBindValue(name);
    require(row.exec(),"INVALID_GPKG_SQLITE");
    return row.next();
}
std::string target(const std::string& name) {
    if(name=="countries")return "country";
    if(name=="subunits"||name=="territories"||name=="administrative")return "subunit";
    if(name=="regions")return "region";
    if(name=="places")return "label";
    if(name=="distributions"||name=="language_distribution"||name=="ethnicity_distribution"||
       name=="religion_distribution")return "distribution";
    if(name=="generic_features_point"||name=="generic_features_line"||
       name=="generic_features_polygon")return "generic";
    return {};
}
std::string identityColumn(const std::string& name) {
    if(name=="countries"||name=="places")return "pandolab_id";
    if(name=="subunits"||name=="territories"||name=="administrative"||name=="regions"||
       name=="generic_features_point"||name=="generic_features_line"||
       name=="generic_features_polygon")return "id";
    if(name=="distributions"||name=="language_distribution"||name=="ethnicity_distribution"||
       name=="religion_distribution")return "entry_id";
    return {};
}
V scalar(const QVariant& value) {
    if(!value.isValid()||value.isNull())return {};
    const auto type=value.metaType().id();
    if(type==QMetaType::QByteArray)throw std::invalid_argument("UNSUPPORTED_GPKG_PROPERTY_BLOB");
    if(type==QMetaType::Bool)return V::boolean(value.toBool());
    if(type==QMetaType::Int||type==QMetaType::UInt||type==QMetaType::LongLong||
       type==QMetaType::ULongLong) {
        V result;result.kind=V::Number;result.raw=value.toString().toUtf8();
        return result;
    }
    if(type==QMetaType::Double||type==QMetaType::Float)return V::num(value.toDouble());
    return V::str(ascii(value.toString()));
}
bool compatible(const std::string& declaration,const std::string& geometry) {
    if(declaration=="GEOMETRY")return true;
    std::string actual=geometry;
    std::transform(actual.begin(),actual.end(),actual.begin(),[](unsigned char c){return char(std::toupper(c));});
    return declaration==actual;
}
struct Contents {std::string dataType;int srsId=0;};
}

GisGeoPackage readGisGeoPackage(const QString& filePath) {
    const QFileInfo info(filePath);
    require(info.isFile()&&info.size()>100&&info.size()<=maxFile,"INVALID_GPKG_FILE");
    QFile input(filePath);
    require(input.open(QIODevice::ReadOnly)&&input.read(16)==QByteArray("SQLite format 3\0",16),
            "INVALID_GPKG_SQLITE_HEADER");
    ReadOnlyDatabase connection(filePath);
    auto& db=connection.db;
    {
        auto check=query(db,"PRAGMA integrity_check(1)");
        require(check.next()&&check.value(0).toString()=="ok","INVALID_GPKG_SQLITE");
    }
    {
        auto id=query(db,"PRAGMA application_id");
        require(id.next()&&id.value(0).toLongLong()==1196444487,"INVALID_GPKG_APPLICATION_ID");
    }
    require(hasTable(db,"gpkg_spatial_ref_sys")&&hasTable(db,"gpkg_contents")&&
            hasTable(db,"gpkg_geometry_columns"),"INVALID_GPKG_SCHEMA");
    {
        auto srs=query(db,"SELECT organization,organization_coordsys_id FROM gpkg_spatial_ref_sys WHERE srs_id=4326");
        require(srs.next()&&srs.value(0).toString()=="EPSG"&&
                srs.value(1).toInt()==4326,"UNSUPPORTED_GPKG_CRS");
    }
    const bool hasSettings=hasTable(db,"pandolab_project_settings");
    const bool hasAssets=hasTable(db,"pandolab_country_assets");
    require(hasSettings==hasAssets,"INVALID_GPKG_PROJECT_TABLES");
    GisGeoPackage result;result.projectPackage=hasSettings;
    std::map<QString,Contents> content;
    {
        auto rows=query(db,"SELECT table_name,data_type,srs_id FROM gpkg_contents");
        while(rows.next()) {
            const auto name=rows.value(0).toString();
            require(!name.isEmpty()&&content.emplace(name,Contents{
                ascii(rows.value(1).toString()),rows.value(2).isNull()?0:rows.value(2).toInt()}).second,
                "INVALID_GPKG_CONTENTS");
        }
        require(!rows.lastError().isValid(),"INVALID_GPKG_CONTENTS");
    }
    std::set<QString> geometryTables;
    {
        auto rows=query(db,"SELECT table_name,column_name,geometry_type_name,srs_id,z,m FROM gpkg_geometry_columns");
        while(rows.next()) {
            const auto table=rows.value(0).toString();
            const auto column=rows.value(1).toString();
            const auto kind=ascii(rows.value(2).toString());
            require(!table.isEmpty()&&!column.isEmpty()&&!kind.empty()&&
                    geometryTables.insert(table).second&&hasTable(db,table),
                    "INVALID_GPKG_GEOMETRY_COLUMNS");
            const auto it=content.find(table);
            require(it!=content.end()&&it->second.dataType=="features"&&
                    it->second.srsId==4326&&rows.value(3).toInt()==4326&&
                    rows.value(4).toInt()==0&&rows.value(5).toInt()==0,
                    "UNSUPPORTED_GPKG_CRS");
            GisGeoPackageLayer layer;
            layer.tableName=ascii(table);
            layer.targetType=target(layer.tableName);
            layer.geometryType=kind;
            auto data=query(db,"SELECT * FROM "+identifier(table));
            const auto record=data.record();
            const int geomIndex=record.indexOf(column);
            const auto idKey=identityColumn(layer.tableName);
            const int idIndex=idKey.empty()?-1:record.indexOf(QString::fromStdString(idKey));
            require(geomIndex>=0&&(idKey.empty()||idIndex>=0),
                    "INVALID_GPKG_GEOMETRY_COLUMN");
            std::set<std::string> ids;
            while(data.next()) {
                require(layer.collection.features.size()<1000000,"GPKG_FEATURE_LIMIT");
                const auto value=data.value(geomIndex);
                require(!value.isNull()&&value.metaType().id()==QMetaType::QByteArray,
                        "INVALID_GPKG_GEOMETRY_BLOB");
                const auto bytes=value.toByteArray();
                auto geometry=decodeGeoPackageGeometry(std::vector<std::uint8_t>(
                    bytes.cbegin(),bytes.cend()));
                require(compatible(kind,geometry.type),"GPKG_GEOMETRY_TYPE_MISMATCH");
                GisGeoJsonFeature feature;
                feature.geometry=std::move(geometry);
                if(idIndex>=0) {
                    feature.id=ascii(data.value(idIndex).toString());
                    require(!feature.id.empty()&&ids.insert(feature.id).second,
                            "DUPLICATE_GPKG_ID");
                }
                auto props=V::obj();
                for(int i=0;i<record.count();++i)if(i!=geomIndex)
                    props.object.emplace(ascii(record.fieldName(i)),scalar(data.value(i)));
                feature.propertiesJson=props.encode().toStdString();
                layer.collection.features.push_back(std::move(feature));
            }
            require(!data.lastError().isValid(),"INVALID_GPKG_FEATURE_TABLE");
            result.layers.push_back(std::move(layer));
        }
        require(!rows.lastError().isValid(),"INVALID_GPKG_GEOMETRY_COLUMNS");
    }
    for(const auto& [table,row]:content)
        if(row.dataType=="features")require(geometryTables.count(table)>0,
                                           "MISSING_GPKG_GEOMETRY_COLUMNS");
    require(!result.layers.empty(),"GPKG_NO_VECTOR_LAYERS");
    return result;
}
}

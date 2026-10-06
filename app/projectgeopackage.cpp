#include "projectgeopackage.h"
#include "gisgeopackage.h"
#include "projectcodec.h"
#include "webprojectgeopackage.h"
#include "losslessjson.h"
#include <pandoeditor/document.h>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <map>
#include <set>
#include <stdexcept>

namespace pandoeditor {
namespace {
using V=losslessjson::Value;
constexpr qint64 limit=64ll*1024*1024;
void require(bool value,const char* code) {
    if(!value)throw std::invalid_argument(code);
}
class Database {
public:
    const QString name=QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlDatabase db;
    Database(const QString& path,bool readOnly) {
        require(QSqlDatabase::isDriverAvailable("QSQLITE"),"GPKG_SQLITE_DRIVER_UNAVAILABLE");
        db=QSqlDatabase::addDatabase("QSQLITE",name);
        db.setDatabaseName(path);
        if(readOnly)db.setConnectOptions("QSQLITE_OPEN_READONLY");
        if(!db.open()) {
            db={};QSqlDatabase::removeDatabase(name);
            throw std::invalid_argument("INVALID_PROJECT_GPKG_SQLITE");
        }
    }
    ~Database(){db.close();db={};QSqlDatabase::removeDatabase(name);}
    Database(const Database&)=delete;
    Database& operator=(const Database&)=delete;
};
void run(QSqlDatabase& db,const QString& sql) {
    QSqlQuery query(db);require(query.exec(sql),"PROJECT_GPKG_WRITE_FAILED");
}
V& member(V& object,const char* key) {
    require(object.kind==V::Object,"INVALID_PROJECT_GPKG_STATE");
    auto it=object.object.find(key);
    require(it!=object.object.end(),"INVALID_PROJECT_GPKG_STATE");
    return it->second;
}
const V& member(const V& object,const char* key) {
    require(object.kind==V::Object,"INVALID_PROJECT_GPKG_STATE");
    const auto it=object.object.find(key);
    require(it!=object.object.end(),"INVALID_PROJECT_GPKG_STATE");
    return it->second;
}
std::string string(const V& value) {
    require(value.kind==V::String,"INVALID_PROJECT_GPKG_STATE");
    return value.string;
}
using Asset=WebProjectAsset;
std::map<std::string,Asset> stripAssets(V& root) {
    auto& symbols=member(member(root,"content"),"symbols");
    require(symbols.kind==V::Array,"INVALID_PROJECT_GPKG_STATE");
    std::map<std::string,Asset> assets;
    for(auto& symbol:symbols.array) {
        const auto policy=string(member(symbol,"policy"));
        if(policy!="embedded")continue;
        const auto& ref=member(symbol,"ref");
        if(string(member(ref,"domain"))!="territorial")continue;
        const auto id=string(member(ref,"id"));
        const QString url=QString::fromStdString(string(member(symbol,"embeddedDataUrl")));
        const auto marker=url.indexOf(";base64,");
        require(url.startsWith("data:image/")&&marker>10,"INVALID_PROJECT_GPKG_ASSET");
        const auto mime=url.mid(5,marker-5);
        const auto encoded=url.mid(marker+8).toLatin1();
        const auto decoded=QByteArray::fromBase64(encoded);
        require(!id.empty()&&!decoded.isEmpty()&&decoded.toBase64()==encoded&&
                decoded.size()<=limit,"INVALID_PROJECT_GPKG_ASSET");
        require(assets.emplace(id,Asset{mime,decoded}).second,"DUPLICATE_PROJECT_GPKG_ASSET");
        symbol.object["embeddedDataUrl"]=V::str("");
    }
    return assets;
}
void restoreAssets(V& root,const std::map<std::string,Asset>& assets) {
    auto& symbols=member(member(root,"content"),"symbols");
    require(symbols.kind==V::Array,"INVALID_PROJECT_GPKG_STATE");
    std::set<std::string> used;
    for(auto& symbol:symbols.array) {
        const auto policy=string(member(symbol,"policy"));
        if(policy!="embedded")continue;
        const auto& ref=member(symbol,"ref");
        if(string(member(ref,"domain"))!="territorial")continue;
        const auto id=string(member(ref,"id"));
        const auto it=assets.find(id);
        require(it!=assets.end()&&string(member(symbol,"embeddedDataUrl")).empty()&&
                used.insert(id).second,"MISSING_PROJECT_GPKG_ASSET");
        symbol.object["embeddedDataUrl"]=V::str("data:"+it->second.mime.toStdString()+
            ";base64,"+it->second.bytes.toBase64().toStdString());
    }
    require(used.size()==assets.size(),"ORPHAN_PROJECT_GPKG_ASSET");
}
void attributeTable(QSqlDatabase& db,const char* name,const char* columns) {
    run(db,QString("CREATE TABLE \"")+name+"\" ("+columns+")");
    QSqlQuery row(db);
    require(row.prepare("INSERT INTO gpkg_contents (table_name,data_type,identifier,description,srs_id) VALUES (?,'attributes',?,?,NULL)"),
            "PROJECT_GPKG_WRITE_FAILED");
    row.addBindValue(QString::fromLatin1(name));row.addBindValue(QString::fromLatin1(name));
    row.addBindValue(QStringLiteral("PandoEditor project data"));
    require(row.exec(),"PROJECT_GPKG_WRITE_FAILED");
}
}

QByteArray exportProjectGeoPackage(const Project& project) {
    auto root=losslessjson::parse(projectcodec::encode(project));
    Project persisted;persisted.replace(projectcodec::decode(root.encode()));
    auto assets=stripAssets(root);
    const auto state=root.encode();
    const auto& document=persisted.document();
    const bool metadataOnly=!isStaticTimeline(document)||(document.units.empty()&&document.labels.empty()&&document.genericFeatures.empty()&&document.distributionEntries.empty());
    const auto vectors=exportGisGeoPackage(document,metadataOnly?std::vector<std::string>{}:
        std::vector<std::string>{"countries","subunits","regions","genericFeatures","distributions","labels"},metadataOnly);
    QTemporaryDir directory;require(directory.isValid(),"PROJECT_GPKG_TEMP_FAILED");
    const auto path=directory.filePath("project.gpkg");
    {
        QFile file(path);require(file.open(QIODevice::WriteOnly)&&file.write(vectors)==vectors.size(),
                            "PROJECT_GPKG_TEMP_FAILED");
    }
    {
        Database connection(path,false);auto& db=connection.db;
        require(db.transaction(),"PROJECT_GPKG_WRITE_FAILED");
        attributeTable(db,"pandolab_project_settings",
                       "setting_key TEXT PRIMARY KEY NOT NULL, json_value TEXT NOT NULL");
        attributeTable(db,"pandolab_country_assets",
                       "country_id TEXT PRIMARY KEY NOT NULL, mime_type TEXT NOT NULL, image_data BLOB NOT NULL");
        attributeTable(db,"pandolab_source_info",
                       "info_key TEXT PRIMARY KEY NOT NULL, json_value TEXT NOT NULL");
        QSqlQuery settings(db);
        require(settings.prepare("INSERT INTO pandolab_project_settings VALUES ('project_state',?)"),
                "PROJECT_GPKG_WRITE_FAILED");
        settings.addBindValue(QString::fromUtf8(state));
        require(settings.exec(),"PROJECT_GPKG_WRITE_FAILED");
        QSqlQuery source(db);
        require(source.prepare("INSERT INTO pandolab_source_info VALUES ('source',?)"),
                "PROJECT_GPKG_WRITE_FAILED");
        source.addBindValue(QStringLiteral("{\"format\":\"pandoeditor-project\",\"version\":10}"));
        require(source.exec(),"PROJECT_GPKG_WRITE_FAILED");
        QSqlQuery asset(db);
        require(asset.prepare("INSERT INTO pandolab_country_assets VALUES (?,?,?)"),
                "PROJECT_GPKG_WRITE_FAILED");
        for(const auto& [id,value]:assets) {
            asset.bindValue(0,QString::fromStdString(id));asset.bindValue(1,value.mime);
            asset.bindValue(2,value.bytes);
            require(asset.exec(),"PROJECT_GPKG_WRITE_FAILED");
        }
        require(db.commit(),"PROJECT_GPKG_WRITE_FAILED");
    }
    QFile file(path);require(file.open(QIODevice::ReadOnly),"PROJECT_GPKG_READBACK_FAILED");
    const auto result=file.readAll();
    require(result.size()<=limit,"PROJECT_GPKG_SIZE_LIMIT");
    return result;
}

QByteArray readProjectGeoPackage(const QString& filePath) {
    require(QFileInfo(filePath).size()<=limit,"PROJECT_GPKG_SIZE_LIMIT");
    const auto vectors=readGisGeoPackage(filePath);
    require(vectors.projectPackage,"GIS_ONLY_GPKG_NOT_PROJECT");
    Database connection(filePath,true);auto& db=connection.db;
    {
        QSqlQuery contents(db);require(contents.exec("SELECT table_name,data_type FROM gpkg_contents"),"INVALID_PROJECT_GPKG_SCHEMA");
        while(contents.next()) {
            const auto type=contents.value(1).toString(),name=contents.value(0).toString();
            require(type=="features"||(type=="attributes"&&(name=="pandolab_project_settings"||name=="pandolab_country_assets"||name=="pandolab_source_info")),"UNSUPPORTED_PROJECT_GPKG_TABLE");
        }
        for(const auto& contract:std::map<QString,std::set<QString>>{
            {"pandolab_project_settings",{"setting_key","json_value"}},
            {"pandolab_country_assets",{"country_id","mime_type","image_data"}},
            {"pandolab_source_info",{"info_key","json_value"}}}) {
            QSqlQuery columns(db);require(columns.exec("PRAGMA table_info("+contract.first+")"),"INVALID_PROJECT_GPKG_SCHEMA");std::set<QString> actual;
            while(columns.next())actual.insert(columns.value(1).toString());require(actual==contract.second,"UNSUPPORTED_PROJECT_GPKG_COLUMN");
        }
        QSqlQuery rows(db);require(rows.exec("SELECT COUNT(*) FROM pandolab_project_settings")&&rows.next()&&rows.value(0).toInt()==1,"UNSUPPORTED_PROJECT_GPKG_SETTINGS");
        require(rows.exec("SELECT COUNT(*) FROM pandolab_source_info WHERE info_key='source'")&&rows.next()&&rows.value(0).toInt()==1,"UNSUPPORTED_PROJECT_GPKG_SOURCE");
        require(rows.exec("SELECT COUNT(*) FROM pandolab_source_info")&&rows.next()&&rows.value(0).toInt()==1,"UNSUPPORTED_PROJECT_GPKG_SOURCE");
    }
    QSqlQuery state(db);
    require(state.exec("SELECT json_value FROM pandolab_project_settings WHERE setting_key='project_state' LIMIT 1")&&
            state.next()&&!state.value(0).isNull(),"MISSING_PROJECT_GPKG_STATE");
    auto root=losslessjson::parse(state.value(0).toString().toUtf8());
    require(root.kind==V::Object,"UNSUPPORTED_PROJECT_GPKG_STATE");
    const auto native=root.object.count("format")&&
        root.object.at("format").kind==V::String&&root.object.at("format").string=="pandoeditor-project";
    if(native)require(member(root,"version").kind==V::Number&&member(root,"version").raw==QByteArray::number(projectcodec::ProjectVersion),
                      "UNSUPPORTED_PROJECT_GPKG_STATE");
    std::map<std::string,Asset> assets;
    QSqlQuery rows(db);
    require(rows.exec("SELECT country_id,mime_type,image_data FROM pandolab_country_assets"),
            "INVALID_PROJECT_GPKG_ASSETS");
    while(rows.next()) {
        const auto id=rows.value(0).toString().toStdString();
        const auto mime=rows.value(1).toString();
        const auto bytes=rows.value(2).toByteArray();
        require(!id.empty()&&mime.startsWith("image/")&&!bytes.isEmpty()&&bytes.size()<=limit&&
                assets.emplace(id,Asset{mime,bytes}).second,"INVALID_PROJECT_GPKG_ASSETS");
    }
    require(!rows.lastError().isValid(),"INVALID_PROJECT_GPKG_ASSETS");
    if(!native) {
        QSqlQuery source(db);
        require(source.exec("SELECT json_value FROM pandolab_source_info WHERE info_key='source' LIMIT 1")&&
                source.next()&&!source.value(0).isNull(),"MISSING_WEB_GPKG_SOURCE");
        const auto recorded=losslessjson::parse(source.value(0).toString().toUtf8());
        const auto found=root.object.find("sourceInfo");
        require(found!=root.object.end()&&recorded.encode()==(found->second.kind==V::Null?QByteArray("{}"):found->second.encode()),
                "WEB_GPKG_SOURCE_MISMATCH");
        return convertWebProjectGeoPackage(vectors,std::move(root),assets);
    }
    restoreAssets(root,assets);
    {
        QSqlQuery source(db);require(source.exec("SELECT json_value FROM pandolab_source_info WHERE info_key='source'")&&source.next(),"MISSING_PROJECT_GPKG_SOURCE");
        const auto marker=losslessjson::parse(source.value(0).toString().toUtf8());
        require(marker.encode()==QByteArray("{\"format\":\"pandoeditor-project\",\"version\":10}"),"PROJECT_GPKG_SOURCE_MISMATCH");
    }
    Project candidate;candidate.replace(projectcodec::decode(root.encode()));
    if(!isStaticTimeline(candidate.document()))require(vectors.layers.empty(),"PROJECT_GPKG_TIMELINE_VECTORS");
    else {
        const auto& document=candidate.document();
        if(document.units.empty()&&document.labels.empty()&&document.genericFeatures.empty()&&document.distributionEntries.empty())
            require(vectors.layers.empty(),"PROJECT_VECTOR_ROW_MISMATCH");
        else {
            // Derive through the existing production GIS writer, including its
            // SQL scalar/geometry normalization; no alternate project writer.
            const auto bytes=exportGisGeoPackage(document,{"countries","subunits","regions","genericFeatures","distributions","labels"});
            QTemporaryDir directory;require(directory.isValid(),"PROJECT_GPKG_TEMP_FAILED");
            const auto path=directory.filePath("expected.gpkg");QFile file(path);
            require(file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size(),"PROJECT_GPKG_TEMP_FAILED");file.close();
            validateProjectGeoPackageVectors(vectors,readGisGeoPackage(path).layers);
        }
    }
    return projectcodec::encode(candidate);
}
}

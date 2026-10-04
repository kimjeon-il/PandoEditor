#include <projectgeopackage.h>
#include <gisgeopackage.h>
#include <projectcodec.h>
#include <losslessjson.h>
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
#include <iostream>
using namespace pandoeditor;
namespace {
using V=losslessjson::Value;
bool rejected(const std::function<void()>& action){try{action();}catch(const std::invalid_argument&){return true;}return false;}
void update(const QString& path,const std::function<void(QSqlDatabase&)>& action){
    {auto db=QSqlDatabase::addDatabase("QSQLITE","project-corruption-test");db.setDatabaseName(path);assert(db.open());action(db);db.close();db={};}
    QSqlDatabase::removeDatabase("project-corruption-test");
}
void corruptCopy(const QString& original,const QString& path,const QString& sql){
    assert(QFile::copy(original,path));update(path,[&](QSqlDatabase& db){QSqlQuery query(db);for(const auto& statement:sql.split(';',Qt::SkipEmptyParts))assert(query.exec(statement));});
}
void stateCopy(const QString& original,const QString& path,const std::function<void(V&)>& change,bool removeAssets=false){
    assert(QFile::copy(original,path));update(path,[&](QSqlDatabase& db){
        QSqlQuery read(db);assert(read.exec("SELECT json_value FROM pandolab_project_settings WHERE setting_key='project_state'"));assert(read.next());
        auto root=losslessjson::parse(read.value(0).toString().toUtf8());change(root);
        QSqlQuery write(db);write.prepare("UPDATE pandolab_project_settings SET json_value=? WHERE setting_key='project_state'");write.addBindValue(QString::fromUtf8(root.encode()));assert(write.exec());
        if(removeAssets){
            QSqlQuery clear(db);assert(clear.exec("DELETE FROM pandolab_country_assets"));
            for(const auto& entity:root.object.at("territorialEntities").array) {
                const auto& id=entity.object.at("id").string;const auto& metadata=entity.object.at("properties").object.at("metadata");
                QSqlQuery readVector(db);readVector.prepare("SELECT properties_json FROM entities WHERE id=?");readVector.addBindValue(QString::fromStdString(id));assert(readVector.exec());
                if(!readVector.next())continue;auto properties=losslessjson::parse(readVector.value(0).toString().toUtf8());properties.object["metadata"]=metadata;
                QSqlQuery writeVector(db);writeVector.prepare("UPDATE entities SET metadata_json=?,properties_json=? WHERE id=?");writeVector.addBindValue(QString::fromUtf8(metadata.encode()));writeVector.addBindValue(QString::fromUtf8(properties.encode()));writeVector.addBindValue(QString::fromStdString(id));assert(writeVector.exec());
            }
        }
    });
}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    const auto web=QString::fromUtf8(WEB_GPKG_FIXTURE)+"/../timeline-exchange/content.gpkg";
    Project webProject;webProject.replace(projectcodec::decode(readProjectGeoPackage(web)));
    const auto& webDocument=webProject.document();
    assert(webProject.country("A")&&webProject.country("A")->name=="A 영토");
    assert(webDocument.units.size()==4&&webDocument.genericFeatures.size()==1&&webDocument.distributionLayers.size()==1&&webDocument.distributionEntries.size()==1&&webDocument.labels.size()==1);
    assert(webDocument.symbols.at(territorialRef("A")).policy==FlagPolicy::Embedded);
    assert(webDocument.symbols.at(territorialRef("A")).embeddedDataUrl.find("data:image/svg+xml;base64,")==0);
    assert(webDocument.exchangeMetadata.find("web-worker")!=std::string::npos);
    QTemporaryDir directory;assert(directory.isValid());
    // The actual Worker stores the embedded flag in project_state. Add a valid
    // optional spool row before corrupting it, so every UPDATE affects a row.
    const auto webWithAsset=directory.filePath("web-with-asset.gpkg");assert(QFile::copy(web,webWithAsset));
    update(webWithAsset,[&](QSqlDatabase& db){
        const auto data=QByteArray::fromStdString(webDocument.symbols.at(territorialRef("A")).embeddedDataUrl);
        QSqlQuery insert(db);insert.prepare("INSERT INTO pandolab_country_assets(country_id,mime_type,image_data) VALUES(?,?,?)");
        insert.addBindValue("A");insert.addBindValue(QString::fromUtf8(data.mid(5,data.indexOf(';')-5)));insert.addBindValue(QByteArray::fromBase64(data.mid(data.indexOf(',')+1)));assert(insert.exec());
    });
    Project assetProject;assetProject.replace(projectcodec::decode(readProjectGeoPackage(webWithAsset)));assert(semanticallyEqual(webDocument,assetProject.document()));
    const auto nativeCopy=directory.filePath("restored-native.gpkg");QFile nativeFile(nativeCopy);assert(nativeFile.open(QIODevice::WriteOnly));
    const auto nativeBytes=exportProjectGeoPackage(webProject);assert(nativeFile.write(nativeBytes)==nativeBytes.size());nativeFile.close();
    Project reopened;reopened.replace(projectcodec::decode(readProjectGeoPackage(nativeCopy)));assert(semanticallyEqual(webDocument,reopened.document()));
    int vectorFailures=0;
    for(bool isWeb:{false,true}) {
        const auto originalFile=isWeb?web:nativeCopy;const auto table=isWeb?QString("entities"):QString("countries");
        const auto id=isWeb?QString("id"):QString("pandolab_id");
        const std::vector<QString> corruptions={"DELETE FROM places","DELETE FROM generic_features_point","DELETE FROM distributions",
            QString(),
            "ALTER TABLE "+table+" ADD COLUMN review_extra TEXT"};
        for(std::size_t i=0;i<corruptions.size();++i) {
            const auto bad=directory.filePath(QString("vector-%1-%2.gpkg").arg(isWeb).arg(i));
            if(i==3) {
                assert(QFile::copy(originalFile,bad));update(bad,[&](QSqlDatabase& db){
                    QSqlQuery read(db);assert(read.exec("SELECT geom FROM "+table+" WHERE "+id+"='A'"));assert(read.next());
                    auto shape=read.value(0).toByteArray();const auto before=shape;
                    // All territorial fixture rows share the same shape. Change 10 to
                    // 11 in both envelope and WKB coordinates, preserving a valid ring.
                    shape.replace(QByteArray::fromHex("0000000000002440"),QByteArray::fromHex("0000000000002640"));assert(shape!=before);
                    QSqlQuery write(db);write.prepare("UPDATE "+table+" SET geom=? WHERE "+id+"='A'");write.addBindValue(shape);assert(write.exec());
                });
            } else corruptCopy(originalFile,bad,corruptions[i]);
            if(!rejected([&]{readProjectGeoPackage(bad);})){++vectorFailures;std::cerr<<"FAIL vector/state mismatch web="<<isWeb<<" case="<<i<<'\n';}
        }
    }
    assert(vectorFailures==0);
    const auto old=directory.filePath("old-web.gpkg");
    stateCopy(web,old,[](V& root){root.object["schemaVersion"]=V::num(5);});assert(rejected([&]{readProjectGeoPackage(old);}));
    const auto unknown=directory.filePath("unknown-web.gpkg");
    stateCopy(web,unknown,[](V& root){root.object["future"]=V::boolean(true);});assert(rejected([&]{readProjectGeoPackage(unknown);}));
    Project source;source.replace(ProjectDocument({{"A","Alpha",{{{{0,0},{2,0},{2,2},{0,2},{0,0}}}},0x123456},{"B","Beta",{{{{3,0},{5,0},{5,2},{3,2},{3,0}}}},0x654321},{"C","Gamma",{{{{6,0},{8,0},{8,2},{6,2},{6,0}}}},0xabcdef}},{{"countries","Countries"}}));
    auto doc=source.document();doc.units.front().libraryOrigin=LibraryOrigin{"history:A","v1","1945","archive","2","high","year",false,{}};
    doc.symbols[territorialRef("B")].policy=FlagPolicy::None;doc.symbols[territorialRef("C")]=webDocument.symbols.at(territorialRef("A"));source.replace(doc);
    const auto original=projectcodec::encode(source),bytes=exportProjectGeoPackage(source);
    const auto path=argc==3&&QString::fromLocal8Bit(argv[1])=="--emit"?QDir(QString::fromLocal8Bit(argv[2])).filePath("project.gpkg"):directory.filePath("project.gpkg");
    QFile output(path);assert(output.open(QIODevice::WriteOnly));assert(output.write(bytes)==bytes.size());output.close();
    if(argc==3&&QString::fromLocal8Bit(argv[1])=="--emit")return 0;
    Project decoded;decoded.replace(projectcodec::decode(readProjectGeoPackage(path)));assert(projectcodec::encode(decoded)==original);
    const auto missing=directory.filePath("missing-asset.gpkg");corruptCopy(path,missing,"DELETE FROM pandolab_country_assets WHERE country_id='C'");assert(rejected([&]{readProjectGeoPackage(missing);}));
    const auto mismatch=directory.filePath("mismatched-country.gpkg");corruptCopy(path,mismatch,"UPDATE countries SET pandolab_id='other' WHERE pandolab_id='A'");assert(rejected([&]{readProjectGeoPackage(mismatch);}));
    const auto gisOnly=directory.filePath("gis-only.gpkg");QFile gisOutput(gisOnly);assert(gisOutput.open(QIODevice::WriteOnly));const auto gis=exportGisGeoPackage(source.document(),{"countries"});assert(gisOutput.write(gis)==gis.size());gisOutput.close();assert(rejected([&]{readProjectGeoPackage(gisOnly);}));
    const auto orphan=directory.filePath("orphan-web-asset.gpkg");corruptCopy(webWithAsset,orphan,"UPDATE pandolab_country_assets SET country_id='ghost'");assert(rejected([&]{readProjectGeoPackage(orphan);}));
    const auto brokenImage=directory.filePath("broken-web-image.gpkg");corruptCopy(webWithAsset,brokenImage,"UPDATE pandolab_country_assets SET image_data=x'00' WHERE country_id='A'");assert(rejected([&]{readProjectGeoPackage(brokenImage);}));
    const auto conflicting=directory.filePath("conflicting-web-flag.gpkg");stateCopy(webWithAsset,conflicting,[](V& root){root.object["territorialEntities"].array[0].object["properties"].object["metadata"].object["flagDataUrl"]=V{};});assert(rejected([&]{readProjectGeoPackage(conflicting);}));
    const auto wrongCountry=directory.filePath("wrong-web-country.gpkg");corruptCopy(web,wrongCountry,"UPDATE entities SET id='other' WHERE id='A'");assert(rejected([&]{readProjectGeoPackage(wrongCountry);}));
    const auto wrongSource=directory.filePath("wrong-web-source.gpkg");corruptCopy(web,wrongSource,"UPDATE pandolab_source_info SET json_value='{}' WHERE info_key='source'");assert(rejected([&]{readProjectGeoPackage(wrongSource);}));
    const auto none=directory.filePath("none-web-flag.gpkg");stateCopy(webWithAsset,none,[](V& root){root.object["territorialEntities"].array[0].object["properties"].object["metadata"].object["flagDataUrl"]=V{};},true);
    Project noneProject;noneProject.replace(projectcodec::decode(readProjectGeoPackage(none)));assert(noneProject.document().symbols.at(territorialRef("A")).policy==FlagPolicy::None);
    const auto defaultFlag=directory.filePath("default-web-flag.gpkg");stateCopy(webWithAsset,defaultFlag,[](V& root){root.object["territorialEntities"].array[0].object["properties"].object["metadata"].object.erase("flagDataUrl");},true);
    Project defaultProject;defaultProject.replace(projectcodec::decode(readProjectGeoPackage(defaultFlag)));assert(!defaultProject.document().symbols.count(territorialRef("A"))||defaultProject.document().symbols.at(territorialRef("A")).policy==FlagPolicy::Default);
}

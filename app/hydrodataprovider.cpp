#include "hydrodataprovider.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

HydroDataInspection inspectHydroData(const QString& path) {
    HydroDataInspection result;QFileInfo input(path);const auto manifestPath=input.isDir()?QDir(path).filePath("manifest.json"):path;
    QFile manifest(manifestPath);result.root=QFileInfo(manifestPath).absolutePath();
    if(!manifest.open(QIODevice::ReadOnly)){result.error=QStringLiteral("manifest.json을 열 수 없습니다.");return result;}
    QJsonParseError error;const auto document=QJsonDocument::fromJson(manifest.readAll(),&error);
    if(error.error!=QJsonParseError::NoError||!document.isObject()){result.error=QStringLiteral("수계 manifest가 올바른 JSON이 아닙니다.");return result;}
    const auto object=document.object();result.version=object.value("version").toString();result.dataset=object.value("dataset").toString();
    if(result.version!="0.13.1"||object.value("schema").toString()!="pandolab-water-shards-v5"){result.error=QStringLiteral("웹 기준 수계 0.13.1 자료가 아닙니다.");return result;}
    if(!QFileInfo::exists(QDir(result.root).filePath("index.bin.gz"))){result.error=QStringLiteral("수계 index.bin.gz가 없습니다.");return result;}
    result.ready=true;return result;
}

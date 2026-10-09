#include "hydromanifest.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <cmath>
#include <limits>
#include <algorithm>
#include <QPair>

namespace {
bool contained(const QString& root,const QString& path) {
    const auto normalizedRoot=QDir::cleanPath(QDir::fromNativeSeparators(root));
    const auto normalizedPath=QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    return normalizedPath.startsWith(normalizedRoot+'/',Qt::CaseInsensitive);
#else
    return normalizedPath.startsWith(normalizedRoot+'/',Qt::CaseSensitive);
#endif
}
bool integer(const QJsonValue& value,int& result) {
    if(!value.isDouble())return false;
    const auto number=value.toDouble();
    if(!std::isfinite(number)||number<0||number>std::numeric_limits<int>::max()||std::floor(number)!=number)return false;
    result=static_cast<int>(number);return true;
}
bool asset(const QJsonValue& value,const HydroManifest& manifest,HydroAssetSpec& result,QString& error,const QString& label) {
    if(!value.isObject()){error=label+QStringLiteral(" 항목이 없습니다.");return false;}
    const auto object=value.toObject();result.url=object.value("url").toString();
    const auto size=object.value("bytes");
    const auto count=size.toDouble(-1);
    result.sha256=object.value("sha256").toString();
    static const QRegularExpression digest("^[0-9a-f]{64}$");
    if(result.url.isEmpty()||QDir::isAbsolutePath(result.url)||result.url.startsWith("//")||
       !QUrl(result.url).scheme().isEmpty()||result.url.contains('\\')||
       !size.isDouble()||!std::isfinite(count)||count<=0||count>std::numeric_limits<qint64>::max()||
       std::floor(count)!=count||!digest.match(result.sha256).hasMatch()) {
        error=label+QStringLiteral(" 경로 또는 검증 정보가 올바르지 않습니다.");return false;
    }
    result.bytes=static_cast<qint64>(count);
    result.fileBytes=result.bytes;
    if(manifest.version=="0.13.2" && label!=QStringLiteral("container")){
        const auto offsetValue=object.value("offset");
        const double start=offsetValue.toDouble(-1);
        if(!offsetValue.isDouble()||!std::isfinite(start)||start<0||
           start>std::numeric_limits<qint64>::max()||std::floor(start)!=start||
           result.url!=manifest.container.url||manifest.container.bytes<=0){
            error=label+QStringLiteral(" 통합 파일 위치가 올바르지 않습니다.");return false;
        }
        result.offset=static_cast<qint64>(start);
        result.fileBytes=manifest.container.bytes;
        if(result.offset>result.fileBytes||result.bytes>result.fileBytes-result.offset){
            error=label+QStringLiteral(" 통합 파일 범위가 올바르지 않습니다.");return false;
        }
    }
    result.assetRoot=manifest.assetRoot;
    result.path=QDir::cleanPath(QDir::fromNativeSeparators(
        QDir(manifest.root).absoluteFilePath(result.url)));
    if(!contained(manifest.assetRoot,result.path)){
        error=label+QStringLiteral(" 경로가 수계 자료 폴더를 벗어났습니다.");return false;
    }
    const auto canonical=QFileInfo(result.path).canonicalFilePath();
    if(!canonical.isEmpty()&&!contained(manifest.assetRoot,canonical)){
        error=label+QStringLiteral(" 링크가 수계 자료 폴더를 벗어났습니다.");return false;
    }
    return true;
}
}

HydroManifest readHydroManifest(const QString& path) {
    HydroManifest result;
    const QFileInfo input(path);
    const auto selected=input.isDir()?QDir(path).filePath("manifest.json"):path;
    result.manifestPath=QFileInfo(selected).canonicalFilePath();
    if(result.manifestPath.isEmpty()){result.error=QStringLiteral("manifest.json을 열 수 없습니다.");return result;}
    result.root=QDir::cleanPath(QDir::fromNativeSeparators(
        QFileInfo(result.manifestPath).absolutePath()));
    result.assetRoot=QDir::cleanPath(QDir::fromNativeSeparators(
        QDir(QDir(result.root).absoluteFilePath("..")).canonicalPath()));
    QFile file(result.manifestPath);
    if(!file.open(QIODevice::ReadOnly)){result.error=QStringLiteral("manifest.json을 열 수 없습니다.");return result;}
    QJsonParseError parseError;
    const auto document=QJsonDocument::fromJson(file.readAll(),&parseError);
    if(parseError.error!=QJsonParseError::NoError||!document.isObject()){
        result.error=QStringLiteral("수계 manifest가 올바른 JSON이 아닙니다.");return result;
    }
    const auto object=document.object();
    result.version=object.value("version").toString();
    result.dataset=object.value("dataset").toString();
    result.schema=object.value("schema").toString();
    result.crs=object.value("crs").toString();
    if((result.version!="0.13.1"&&result.version!="0.13.2")||result.schema!="pandolab-water-shards-v5"||result.crs!="EPSG:4326"){
        result.error=QStringLiteral("웹 기준 수계 0.13.1 자료가 아닙니다.");return result;
    }
    if(result.version=="0.13.2"){
        const auto package=object.value("container").toObject();
        if(package.value("format").toString()!="byte-concatenated-subresources-v1"||
           !asset(object.value("container"),result,result.container,result.error,"container")||
           result.container.url!="hydro.bin"||result.container.offset!=0){
            if(result.error.isEmpty())result.error=QStringLiteral("통합 수계 컨테이너 정보가 올바르지 않습니다.");
            return result;
        }
    }
    const auto stages=object.value("stages").toArray();
    if(stages.size()!=4){result.error=QStringLiteral("수계 단계 정보가 올바르지 않습니다.");return result;}
    for(int i=0;i<stages.size();++i) {
        const auto row=stages[i].toObject();HydroStageSpec stage;
        if(!integer(row.value("id"),stage.id)||stage.id!=i||
           !row.value("minZoom").isDouble()||!std::isfinite(row.value("minZoom").toDouble())||
           !integer(row.value("columns"),stage.columns)||stage.columns<1||
           !integer(row.value("rows"),stage.rows)||stage.rows<1){
            result.error=QStringLiteral("수계 단계 정보가 올바르지 않습니다.");return result;
        }
        stage.minZoom=row.value("minZoom").toDouble();result.stages.append(stage);
    }
    if(!asset(object.value("index"),result,result.index,result.error,"index"))return result;
    const auto metadata=object.value("metadata").toObject();
    int metadataVersion=0;
    if(!integer(metadata.value("version"),metadataVersion)||metadataVersion!=5||
       !integer(metadata.value("featureCount"),result.metadataFeatureCount)||
       !asset(metadata.value("core"),result,result.metadataCore,result.error,"metadata.core")||
       !asset(metadata.value("detail"),result,result.metadataDetail,result.error,"metadata.detail")){
        if(result.error.isEmpty())result.error=QStringLiteral("수계 메타데이터 정보가 올바르지 않습니다.");
        return result;
    }
    if(!metadata.value("detail").toObject().value("lazy").toBool()){
        result.error=QStringLiteral("상세 메타데이터 지연 로딩 정보가 없습니다.");return result;
    }
    if(!integer(object.value("index").toObject().value("tileCount"),result.indexTileCount)||
       !integer(object.value("index").toObject().value("logicalFeatureCount"),result.logicalFeatureCount)){
        result.error=QStringLiteral("수계 index 개수 정보가 올바르지 않습니다.");return result;
    }
    const auto shards=object.value("shards").toArray();
    if(shards.isEmpty()){result.error=QStringLiteral("수계 shard 목록이 없습니다.");return result;}
    for(const auto& value:shards){
        const auto row=value.toObject();HydroShardSpec shard;
        if(!integer(row.value("id"),shard.id)||shard.id!=result.shards.size()||
           !integer(row.value("packs"),shard.packs)||shard.packs<1||
           !asset(value,result,shard.asset,result.error,"shard")){
            if(result.error.isEmpty())result.error=QStringLiteral("수계 shard 정보가 올바르지 않습니다.");
            return result;
        }
        result.shards.append(shard);
    }
    for(const auto& value:object.value("layers").toArray()){
        const auto id=value.toObject().value("id").toString();
        if(id.isEmpty()){result.error=QStringLiteral("수계 layer 정보가 올바르지 않습니다.");return result;}
        result.layers.append(id);
    }
    if(result.layers.isEmpty())result.error=QStringLiteral("수계 layer 목록이 없습니다.");
    if(result.error.isEmpty() && result.version=="0.13.2"){
        QVector<QPair<qint64,qint64>> parts;
        const auto append=[&](const HydroAssetSpec& part){
            parts.append(qMakePair(part.offset,part.bytes));
        };
        append(result.index);append(result.metadataCore);append(result.metadataDetail);
        for(const auto& shard:result.shards)append(shard.asset);
        std::sort(parts.begin(),parts.end(),[](const auto& left,const auto& right){return left.first<right.first;});
        qint64 cursor=0;
        for(const auto& part:parts){
            if(part.first!=cursor){result.error=QStringLiteral("통합 수계 영역이 중복되거나 누락됐습니다.");return result;}
            cursor+=part.second;
        }
        if(cursor!=result.container.bytes)
            result.error=QStringLiteral("통합 수계 전체 길이가 일치하지 않습니다.");
    }
    return result;
}

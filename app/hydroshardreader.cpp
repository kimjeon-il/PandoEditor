#include "hydroshardreader.h"
#include "hydroassetreader.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>

QByteArray HydroShardReader::readPack(quint32 offset,quint32 length,QString& error) {
    QMutexLocker lock(&mutex_);
    error.clear();
    const QFileInfo info(asset_.path);
    const auto canonical=info.canonicalFilePath();
    if(canonical.isEmpty()||!canonical.startsWith(asset_.assetRoot+QDir::separator())||
       info.size()!=asset_.bytes||!length||quint64(offset)+length>quint64(asset_.bytes)){
        error=QStringLiteral("수계 shard 경로, 길이 또는 pack 범위가 올바르지 않습니다.");return {};
    }
    if(!verified_){
        if(!verifyHydroAsset(asset_,error))return {};
        verifiedCanonical_=canonical;verifiedModified_=QFileInfo(asset_.path).lastModified();
        verified_=true;
    }
    if(canonical!=verifiedCanonical_||info.lastModified()!=verifiedModified_){
        verified_=false;
        error=QStringLiteral("수계 shard 파일이 검증 이후 변경되었습니다.");return {};
    }
    QFile file(canonical);
    if(!file.open(QIODevice::ReadOnly)||file.size()!=asset_.bytes||!file.seek(offset)){
        error=QStringLiteral("수계 pack seek에 실패했습니다.");return {};
    }
    const auto compressed=file.read(length);
    const QFileInfo after(asset_.path);
    if(compressed.size()!=length||file.error()!=QFileDevice::NoError||
       after.canonicalFilePath()!=canonical||after.size()!=asset_.bytes||
       after.lastModified()!=verifiedModified_){
        verified_=false;error=QStringLiteral("수계 shard가 pack 읽기 중 변경되었습니다.");return {};
    }
    return inflateHydroGzip(compressed,error);
}

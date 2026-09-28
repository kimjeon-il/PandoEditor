#include "hydroassetreader.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <zlib.h>
#include <limits>

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
bool openChecked(const HydroAssetSpec& asset,QFile& file,QString& error) {
    const auto actual=QDir::cleanPath(QDir::fromNativeSeparators(
        QFileInfo(asset.path).canonicalFilePath()));
    if(actual.isEmpty()||!contained(asset.assetRoot,actual)){
        error=QStringLiteral("수계 자산 경로를 열 수 없거나 범위를 벗어났습니다: ")+asset.url;return false;
    }
    file.setFileName(actual);
    if(!file.open(QIODevice::ReadOnly)||file.size()!=asset.bytes){
        error=QStringLiteral("수계 자산 길이가 다르거나 읽을 수 없습니다: ")+asset.url;return false;
    }
    return true;
}
bool digest(QFile& file,const HydroAssetSpec& asset,QString& error) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while(!file.atEnd()) {
        const auto chunk=file.read(1024*1024);
        if(chunk.isEmpty()){error=QStringLiteral("수계 자산 읽기에 실패했습니다: ")+asset.url;return false;}
        hash.addData(chunk);
    }
    if(hash.result().toHex()!=asset.sha256.toLatin1()){
        error=QStringLiteral("수계 자산 SHA-256이 다릅니다: ")+asset.url;return false;
    }
    return true;
}
}

bool verifyHydroAsset(const HydroAssetSpec& asset,QString& error) {
    error.clear();
    QFile file;
    return openChecked(asset,file,error)&&digest(file,asset,error);
}

QByteArray readHydroAsset(const HydroAssetSpec& asset,bool gzip,QString& error,qsizetype maxDecoded) {
    error.clear();
    QFile file;
    if(!openChecked(asset,file,error)||!digest(file,asset,error))return {};
    if(!file.seek(0)){error=QStringLiteral("수계 자산 seek에 실패했습니다: ")+asset.url;return {};}
    if(file.size()>maxDecoded && !gzip){error=QStringLiteral("수계 자산 크기가 한도를 초과했습니다.");return {};}
    const QByteArray compressed=file.readAll();
    if(compressed.size()!=asset.bytes||file.error()!=QFileDevice::NoError){
        error=QStringLiteral("수계 자산 읽기에 실패했습니다: ")+asset.url;return {};
    }
    if(!gzip)return compressed;
    return inflateHydroGzip(compressed,error,maxDecoded);
}

QByteArray inflateHydroGzip(const QByteArray& compressed,QString& error,qsizetype maxDecoded) {
    error.clear();
    if(compressed.size()>std::numeric_limits<uInt>::max()||maxDecoded<1){
        error=QStringLiteral("수계 gzip 크기가 한도를 초과했습니다.");return {};
    }
    z_stream stream{};
    stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(compressed.constData()));
    stream.avail_in=static_cast<uInt>(compressed.size());
    if(inflateInit2(&stream,MAX_WBITS+16)!=Z_OK){error=QStringLiteral("수계 gzip 초기화에 실패했습니다.");return {};}
    QByteArray output;
    int status=Z_OK;
    do {
        char buffer[32768];
        stream.next_out=reinterpret_cast<Bytef*>(buffer);
        stream.avail_out=sizeof(buffer);
        status=inflate(&stream,Z_NO_FLUSH);
        const auto count=sizeof(buffer)-stream.avail_out;
        if(count>maxDecoded-output.size()){
            error=QStringLiteral("수계 gzip 해제 크기가 한도를 초과했습니다.");break;
        }
        output.append(buffer,static_cast<qsizetype>(count));
    } while(status==Z_OK);
    const bool valid=status==Z_STREAM_END && stream.avail_in==0 && error.isEmpty();
    inflateEnd(&stream);
    if(!valid){if(error.isEmpty())error=QStringLiteral("수계 gzip 자료가 손상되었습니다.");return {};}
    return output;
}

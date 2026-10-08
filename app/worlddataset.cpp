#include "worlddataset.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStringList>
#include <algorithm>
#include <array>
#include <set>
#include <limits>
#include <stdexcept>
#include <zlib.h>

namespace {
void require(bool condition,const char* reason) {if(!condition)throw std::runtime_error(reason);}
QByteArray fileBytes(const QString& name) {
    QFile file(name);
    require(file.open(QIODevice::ReadOnly),"World asset is unavailable");
    return file.readAll();
}
QString hash(QCryptographicHash::Algorithm algorithm,const QByteArray& bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes,algorithm).toHex());
}
constexpr std::array<const char*,7> worldRoles{{
    "countryPreview","previewMesh","countryCanonical","canonicalMesh",
    "terrain","hydro","labelAnchors"
}};
bool validHex(const QString& value,int size) {
    if(value.size()!=size)return false;
    return std::all_of(value.begin(),value.end(),[](QChar c){
        const auto ch=c.unicode();
        return (ch>='0'&&ch<='9')||(ch>='a'&&ch<='f');
    });
}
bool safeRelativePath(const QString& value) {
    if(value.isEmpty()||value.size()>512||value.startsWith('/')||
       value.contains('\\')||value.contains('?')||value.contains('#')||
       value.contains('%')||value.contains(':')||value.contains(QChar(0)))return false;
    const auto parts=value.split(QLatin1Char('/'),Qt::KeepEmptyParts);
    return std::all_of(parts.begin(),parts.end(),[](const QString& segment){
        return !segment.isEmpty()&&segment!=QStringLiteral(".")&&segment!=QStringLiteral("..");
    });
}
bool requiredRole(const QString& key) {
    return key!=QStringLiteral("countryPreview")&&
           key!=QStringLiteral("terrain")&&key!=QStringLiteral("hydro");
}

}

WorldDataset::WorldDataset(QString root):root_(std::move(root)) {
    QJsonParseError error;
    const auto parsed=QJsonDocument::fromJson(fileBytes(root_+"/manifest.json"),&error);
    require(error.error==QJsonParseError::NoError&&parsed.isObject(),
            "Invalid world dataset manifest");
    manifest_=parsed.object();
    require(manifest_.value("schema").toString()==QStringLiteral("pandoeditor-world-dataset")&&
            manifest_.value("version").toInt()==2&&
            manifest_.value("countryCount").toInt()==258&&
            manifest_.value("canonicalPositionCount").toInt()==548454,
            "Unexpected world dataset schema or country envelope");
    const auto origin=manifest_.value("origin").toObject();
    const auto mode=origin.value("mode").toString();
    require(origin.value("repository").toString()==QStringLiteral("kimjeon-il/Pando")&&
            (mode==QStringLiteral("mixed-pinned")||mode==QStringLiteral("web-bundle")),
            "Unexpected world dataset source");
    if(mode==QStringLiteral("web-bundle")) {
        require(validHex(origin.value("webCommit").toString(),40)&&
                validHex(origin.value("worldBundleSha256").toString(),64),
                "Unapproved world bundle identity");
    } else {
        require(origin.value("webCommit").isNull()&&
                origin.value("worldBundleSha256").isNull(),
                "Legacy mixed bundle must not declare approval");
    }
    std::set<QString> paths;
    for(const auto* name:worldRoles) {
        const auto key=QString::fromLatin1(name);
        const auto item=asset(key);
        require(paths.insert(item.path).second,"Duplicate world asset path");
    }
}
WorldAsset WorldDataset::asset(const QString& key) const {
    require(std::any_of(worldRoles.begin(),worldRoles.end(),
                        [&key](const char* name){return key==QLatin1String(name);}),
            "Unknown world dataset asset");
    const auto entry=manifest_.value(key).toObject();
    const auto provenance=entry.value("source").toObject();
    const auto origin=manifest_.value("origin").toObject();
    WorldAsset asset{
        entry.value("path").toString(),entry.value("version").toString(),
        entry.value("gitBlobSha").toString(),entry.value("sha256").toString(),
        entry.value("required").toBool(),entry.value("bytes").toInteger()
    };
    require(safeRelativePath(asset.path)&&
            validHex(asset.gitBlobSha,40)&&validHex(asset.sha256,64)&&
            !asset.version.isEmpty()&&asset.bytes>0&&asset.bytes<100000000&&
            entry.value("required").isBool()&&asset.required==requiredRole(key)&&
            validHex(provenance.value("ref").toString(),40)&&
            safeRelativePath(provenance.value("path").toString())&&
            provenance.value("path").toString().startsWith(QStringLiteral("assets/data/")),
            "Invalid world asset identity or provenance");
    if(origin.value("mode").toString()==QStringLiteral("web-bundle")&&
       key!=QStringLiteral("terrain")&&key!=QStringLiteral("hydro"))
        require(provenance.value("ref").toString()==origin.value("webCommit").toString(),
                "World bundle contains an unapproved commit");
    return asset;
}
QByteArray WorldDataset::read(const QString& key) const {
    const auto entry=asset(key);
    const auto bytes=fileBytes(root_+"/"+entry.path);
    require(qint64(bytes.size())==entry.bytes,"World asset size mismatch");
    QByteArray header("blob ");
    header+=QByteArray::number(bytes.size());
    header.append('\0');
    QCryptographicHash blob(QCryptographicHash::Sha1);
    blob.addData(header);blob.addData(bytes);
    require(QString::fromLatin1(blob.result().toHex())==entry.gitBlobSha&&
        hash(QCryptographicHash::Sha256,bytes)==entry.sha256,
        "World asset content hash mismatch");
    return bytes;
}
QByteArray WorldDataset::decompress(const QString& key,int maximumBytes) const {
    require(maximumBytes>0,"Invalid decompression limit");
    const auto bytes=read(key);
    require(bytes.size()<=std::numeric_limits<uInt>::max(),"World asset is too large");
    z_stream stream{};
    require(inflateInit2(&stream,MAX_WBITS+16)==Z_OK,"Cannot initialize gzip decoder");
    stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(bytes.constData()));
    stream.avail_in=uInt(bytes.size());
    QByteArray result;
    std::array<char,65536> buffer{};
    int status=Z_OK;
    while(status==Z_OK) {
        stream.next_out=reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out=uInt(buffer.size());
        status=inflate(&stream,Z_NO_FLUSH);
        const auto produced=int(buffer.size()-stream.avail_out);
        if(result.size()>maximumBytes-produced) {inflateEnd(&stream);throw std::runtime_error("World asset exceeds decompression limit");}
        result.append(buffer.data(),produced);
    }
    const bool complete=status==Z_STREAM_END&&stream.avail_in==0;
    inflateEnd(&stream);
    require(complete,"Truncated or trailing world asset gzip data");
    return result;
}
QString WorldDataset::optionalDataRoot() const {
    const auto path=qEnvironmentVariable("PANDOEDITOR_WORLD_DATA_ROOT");
    return path.isEmpty()?QString():QDir(path).absolutePath();
}

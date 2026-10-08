#include "worlddataset.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <algorithm>
#include <array>
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
}

WorldDataset::WorldDataset(QString root):root_(std::move(root)) {
    QJsonParseError error;
    auto parsed=QJsonDocument::fromJson(fileBytes(root_+"/manifest.json"),&error);
    require(error.error==QJsonParseError::NoError&&parsed.isObject(),"Invalid world dataset manifest");
    manifest_=parsed.object();
    require(manifest_.value("schema").toString()==QStringLiteral("pandoeditor-world-dataset")&&
        manifest_.value("version").toInt()==1&&
        manifest_.value("worldMapCommit").toString()==QStringLiteral("c0bd31d13dc8495593d78cf51f7cc195de7c9469")&&
        manifest_.value("countryCount").toInt()==258&&
        manifest_.value("canonicalPositionCount").toInt()==548454,
        "Unexpected world dataset identity");
}
WorldAsset WorldDataset::asset(const QString& key) const {
    struct Identity {const char* key;const char* path;const char* version;const char* blob;};
    static const std::array<Identity,6> identities{{
        {"countryPreview","countries-preview-v0.33.0.geojson.gz","0.33.0","ce5eecd8f4e1a611131f2117780c34ca3cf555b2"},
        {"previewMesh","world-mesh-preview-v0.33.0.bin.gz","0.33.0","caff1ab319706f6bf90635ed5a992ffbf4f2a84a"},
        {"countryCanonical","countries-canonical-v0.33.0.pcg.gz","0.33.0","54146d9eeb28e4af08e094f5061bf64689e6bdf3"},
        {"canonicalMesh","world-mesh-v0.12.6.bin.gz","0.12.6","8c73420b92e89ab64cbe75dcc5016efe2c0a22b6"},
        {"terrain","terrain/v0.12.6/manifest.json","0.12.6","6821c49315ffd381758f81dfe4d83b6b574f0ad4"},
        {"hydro","hydro/v0.13.1/manifest.json","0.13.1","9a9cdb719351e51d2ff509c410cae8741ac36a0a"}}};
    auto found=std::find_if(identities.begin(),identities.end(),[&](const auto& item){return key==item.key;});
    require(found!=identities.end(),"Unknown world dataset asset");
    const auto object=manifest_.value(key).toObject();
    WorldAsset result{object.value("path").toString(),object.value("version").toString(),
        object.value("gitBlobSha").toString(),object.value("sha256").toString(),
        object.value("required").toBool()};
    require(result.path==found->path&&result.version==found->version&&
        result.gitBlobSha==found->blob&&
        result.sha256.size()==64&&result.required==
            (key=="previewMesh"||key=="countryCanonical"||key=="canonicalMesh"),
        "Wrong world dataset asset identity");
    return result;
}
QByteArray WorldDataset::read(const QString& key) const {
    const auto entry=asset(key);
    const auto bytes=fileBytes(root_+"/"+entry.path);
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

#pragma once

#include "hydroassetreader.h"
#include <pandoeditor/hydroformat.h>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <zlib.h>

namespace river_controller_fixture {
inline QByteArray read(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("fixture read");
    return file.readAll();
}
inline void write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        throw std::runtime_error("fixture write");
}
inline QByteArray gzip(const QByteArray& bytes) {
    z_stream stream{};
    if (deflateInit2(&stream, Z_BEST_SPEED, Z_DEFLATED, MAX_WBITS + 16, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) throw std::runtime_error("fixture gzip init");
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data()));
    stream.avail_in = bytes.size();
    QByteArray result;
    int status;
    do {
        char block[32768];
        stream.next_out = reinterpret_cast<Bytef*>(block);
        stream.avail_out = sizeof block;
        status = deflate(&stream, Z_FINISH);
        result.append(block, sizeof(block) - stream.avail_out);
    } while (status == Z_OK);
    deflateEnd(&stream);
    if (status != Z_STREAM_END) throw std::runtime_error("fixture gzip");
    return result;
}
inline void number(QByteArray& bytes, std::uint64_t value, int count) {
    for (int i = 0; i < count; ++i) bytes.append(char((value >> (i * 8)) & 255));
}
inline QByteArray encodeIndex(const pandoeditor::HydroIndex& index) {
    QByteArray bytes("AWI4");
    number(bytes, 4, 2); number(bytes, 0, 2);
    number(bytes, index.tilePacks.size(), 4); number(bytes, index.logicalPacks.size(), 4);
    number(bytes, index.packSpecs.size(), 4);
    for (const auto& [tile, ids] : index.tilePacks) {
        number(bytes, tile.stage, 1); number(bytes, tile.x, 2); number(bytes, tile.y, 2);
        number(bytes, ids.size(), 2); for (const auto id : ids) number(bytes, id, 4);
    }
    for (const auto& [logical, ids] : index.logicalPacks) {
        number(bytes, logical, 4); number(bytes, ids.size(), 2);
        for (const auto id : ids) number(bytes, id, 4);
    }
    for (const auto& [id, spec] : index.packSpecs) {
        number(bytes, id, 4); number(bytes, spec.shard, 2); number(bytes, spec.offset, 4);
        number(bytes, spec.length, 4); number(bytes, spec.stage, 1);
    }
    return gzip(bytes);
}

// Copies and rewrites the genuine mini AWI4/AWHF dataset. The split-shard
// transformation is the same one exercised by hydro_river_source_tests.
struct FixtureCopy {
    QTemporaryDir dir;
    QString manifestPath;
    QJsonObject manifest;
    pandoeditor::HydroIndex index;
    FixtureCopy() {
        if (!dir.isValid()) throw std::runtime_error("fixture temp dir");
        QDirIterator files(QStringLiteral(WEB_HYDRO_FIXTURE), QDir::Files,
                           QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const auto source = files.next();
            const auto destination = path(QDir(QStringLiteral(WEB_HYDRO_FIXTURE)).relativeFilePath(source));
            QDir().mkpath(QFileInfo(destination).path());
            if (!QFile::copy(source, destination)) throw std::runtime_error("fixture copy");
        }
        manifestPath = path("v0.13.1/manifest.json");
        manifest = QJsonDocument::fromJson(read(manifestPath)).object();
        const auto decodedManifest = readHydroManifest(manifestPath);
        QString error;
        const auto bytes = readHydroAsset(decodedManifest.index, true, error);
        if (!error.isEmpty()) throw std::runtime_error(error.toStdString());
        index = pandoeditor::decodeHydroIndex({reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                 std::size_t(bytes.size())},
                                {std::uint64_t(decodedManifest.shards[0].asset.bytes)});
    }
    QString path(const QString& relative) const { return dir.path() + "/" + relative; }
    void save() { write(manifestPath, QJsonDocument(manifest).toJson()); }
    QJsonObject replaceAsset(QJsonObject descriptor, const QString& relative,
                             const QByteArray& bytes) {
        write(path(relative), bytes);
        descriptor["bytes"] = double(bytes.size());
        descriptor["sha256"] = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        return descriptor;
    }
    void splitLastFragment() {
        auto& spec = index.packSpecs.at(5);
        const auto bytes = read(path("v0.13.0/shards/s0.bin")).mid(spec.offset, spec.length);
        auto shards = manifest["shards"].toArray();
        auto first = shards[0].toObject(); first["packs"] = 5; shards[0] = first;
        shards.append(replaceAsset({{"id", 1}, {"packs", 1}, {"url", "../v0.13.0/shards/s1.bin"}},
                                  "v0.13.0/shards/s1.bin", bytes));
        manifest["shards"] = shards; spec.shard = 1; spec.offset = 0;
        manifest["index"] = replaceAsset(manifest["index"].toObject(), "v0.13.0/index.bin.gz", encodeIndex(index));
        save();
    }
    void enlargeValidDetail() {
        QString error;
        auto decoded = readHydroAsset(readHydroManifest(manifestPath).metadataDetail, true, error);
        if (!error.isEmpty()) throw std::runtime_error(error.toStdString());
        decoded += QByteArray(32 * 1024 * 1024, ' ');
        auto metadata = manifest["metadata"].toObject();
        metadata["detail"] = replaceAsset(metadata["detail"].toObject(),
                                           "v0.13.0/metadata-detail.json.gz", gzip(decoded));
        manifest["metadata"] = metadata;
        save();
    }
};
struct ScopedEnvironment {
    QByteArray name, old;
    bool existed;
    ScopedEnvironment(const char* variable, const QString& value)
        : name(variable), old(qgetenv(variable)), existed(qEnvironmentVariableIsSet(variable)) {
        qputenv(variable, value.toUtf8());
    }
    ~ScopedEnvironment() { if (existed) qputenv(name.constData(), old); else qunsetenv(name.constData()); }
};
}

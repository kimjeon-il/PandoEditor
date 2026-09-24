#include "hydrodataprovider.h"
#include "hydrometadata.h"
#include "hydromanifest.h"
#include "hydroassetreader.h"
#include "hydroshardreader.h"
#include <pandoeditor/hydroformat.h>
#include <QtTest>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <stdexcept>

#ifndef WEB_HYDRO_FIXTURE
#error WEB_HYDRO_FIXTURE must identify the pinned miniature dataset
#endif

class HydroManifestTests : public QObject {
    Q_OBJECT
    static QString copyFixture(QTemporaryDir& directory) {
        const QString source=QStringLiteral(WEB_HYDRO_FIXTURE);
        for(const QString& name:{
            QStringLiteral("v0.13.0/index.bin.gz"),
            QStringLiteral("v0.13.0/metadata-detail.json.gz"),
            QStringLiteral("v0.13.0/shards/s0.bin"),
            QStringLiteral("v0.13.1/metadata-core.json.gz"),
            QStringLiteral("v0.13.1/manifest.json")}) {
            const QString destination=directory.filePath(name);
            QDir().mkpath(QFileInfo(destination).absolutePath());
            if(!QFile::copy(source+"/"+name,destination))return {};
        }
        return directory.filePath("v0.13.1/manifest.json");
    }
    static QByteArray read(const QString& path) {
        QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
        return file.readAll();
    }
    static bool replace(const QString& path,const QByteArray& before,const QByteArray& after) {
        auto content=read(path);
        if(!content.contains(before))return false;
        content.replace(before,after);
        QFile file(path);return file.open(QIODevice::WriteOnly) && file.write(content)==content.size();
    }
private slots:
    void resolvesIndexFromSiblingVersion() {
        const QString manifest=QStringLiteral(WEB_HYDRO_FIXTURE)+"/v0.13.1/manifest.json";
        QVERIFY(QFileInfo::exists(manifest));
        const auto inspected=inspectHydroData(manifest);
        QVERIFY2(inspected.ready,qPrintable(inspected.error));
        QCOMPARE(inspected.version,QStringLiteral("0.13.1"));
        QCOMPARE(QDir(inspected.root).canonicalPath(),
                 QDir(QStringLiteral(WEB_HYDRO_FIXTURE)+"/v0.13.1").canonicalPath());
    }
    void rejectsWrongSchemaAndUnsafeAssetUrl() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const QString manifest=copyFixture(dir);QVERIFY(!manifest.isEmpty());
        QVERIFY(replace(manifest,"pandolab-water-shards-v5","pandolab-water-shards-v4"));
        QVERIFY(!inspectHydroData(manifest).ready);
        QVERIFY(replace(manifest,"pandolab-water-shards-v4","pandolab-water-shards-v5"));
        QVERIFY(replace(manifest,"../v0.13.0/index.bin.gz","https://example.invalid/index.bin.gz"));
        QVERIFY(!inspectHydroData(manifest).ready);
        QVERIFY(replace(manifest,"https://example.invalid/index.bin.gz","../../outside.bin"));
        QVERIFY(!inspectHydroData(manifest).ready);
    }
    void rejectsWrongVersionCrsAndMissingRelativeAsset() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto manifest=copyFixture(dir);QVERIFY(!manifest.isEmpty());
        QVERIFY(replace(manifest,"\"version\": \"0.13.1\"","\"version\": \"0.13.2\""));
        QVERIFY(!inspectHydroData(manifest).ready);
        QVERIFY(replace(manifest,"\"version\": \"0.13.2\"","\"version\": \"0.13.1\""));
        QVERIFY(replace(manifest,"EPSG:4326","EPSG:3857"));
        QVERIFY(!inspectHydroData(manifest).ready);
        QVERIFY(replace(manifest,"EPSG:3857","EPSG:4326"));
        QVERIFY(QFile::remove(dir.filePath("v0.13.0/index.bin.gz")));
        QVERIFY(!inspectHydroData(manifest).ready);
    }
    void rejectsSymlinkEscapeAndChangedAsset() {
        QTemporaryDir dir,outside;QVERIFY(dir.isValid());QVERIFY(outside.isValid());
        const auto manifest=copyFixture(dir);QVERIFY(!manifest.isEmpty());
        const auto index=dir.filePath("v0.13.0/index.bin.gz");
        const auto external=outside.filePath("index.bin.gz");
        QVERIFY(QFile::copy(index,external));
        QVERIFY(QFile::remove(index));
        if(!QFile::link(external,index))QSKIP("symbolic link unavailable on this platform");
        QVERIFY(!inspectHydroData(manifest).ready);
        QVERIFY(QFile::remove(index));
        QVERIFY(QFile::copy(external,index));
        auto spec=readHydroManifest(manifest).index;
        QString error;
        QVERIFY(verifyHydroAsset(spec,error));
        QFile asset(index);QVERIFY(asset.open(QIODevice::Append));QCOMPARE(asset.write("X"),qint64(1));asset.close();
        QVERIFY(!verifyHydroAsset(spec,error));
    }
    void rejectsLengthHashAndInvalidGzip() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const QString manifest=copyFixture(dir);QVERIFY(!manifest.isEmpty());
        auto index=dir.filePath("v0.13.0/index.bin.gz");
        QFile file(index);QVERIFY(file.open(QIODevice::Append));file.write("X");file.close();
        QVERIFY(!inspectHydroData(manifest).ready);
        QVERIFY(QFile::remove(index));
        QVERIFY(QFile::copy(QStringLiteral(WEB_HYDRO_FIXTURE)+"/v0.13.0/index.bin.gz",index));
        auto bytes=read(index);bytes[10]=char(bytes[10]^1);
        QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(bytes),bytes.size());file.close();
        QVERIFY(!inspectHydroData(manifest).ready);
        const auto originalHash=QCryptographicHash::hash(
            read(QStringLiteral(WEB_HYDRO_FIXTURE)+"/v0.13.0/index.bin.gz"),
            QCryptographicHash::Sha256).toHex();
        const auto alteredHash=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();
        QVERIFY(replace(manifest,originalHash,alteredHash));
        QVERIFY(!inspectHydroData(manifest).ready);
    }
    void detailMetadataIsLazyAtOpen() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const QString manifest=copyFixture(dir);QVERIFY(!manifest.isEmpty());
        QVERIFY(QFile::remove(dir.filePath("v0.13.0/metadata-detail.json.gz")));
        QVERIFY2(inspectHydroData(manifest).ready,qPrintable(inspectHydroData(manifest).error));
    }
    void parsesCoreAndMergesDetailLazily() {
        const auto manifest=readHydroManifest(QStringLiteral(WEB_HYDRO_FIXTURE)+"/v0.13.1/manifest.json");
        QVERIFY2(manifest.valid(),qPrintable(manifest.error));
        QString error;
        auto coreBytes=readHydroAsset(manifest.metadataCore,true,error);
        QVERIFY2(error.isEmpty(),qPrintable(error));
        HydroMetadata records;
        QVERIFY2(parseHydroCoreMetadata(coreBytes,6,records,error),qPrintable(error));
        QCOMPARE(records.size(),6);
        QCOMPARE(records.value(5).logicalFid,quint32(5));
        QCOMPARE(records.value(5).bounds[0],40.);
        QCOMPARE(records.value(5).bounds[2],41.);
        QVERIFY(records.value(5).sourceId.isEmpty());
        auto zeroBased=coreBytes;
        QVERIFY(zeroBased.contains("\"fid\":1"));
        zeroBased.replace("\"fid\":1","\"fid\":0");
        zeroBased.replace("\"logicalFid\":1","\"logicalFid\":0");
        HydroMetadata zeroRecords;
        QVERIFY2(parseHydroCoreMetadata(zeroBased,6,zeroRecords,error),qPrintable(error));
        QCOMPARE(zeroRecords.value(0).logicalFid,quint32(0));
        auto detailBytes=readHydroAsset(manifest.metadataDetail,true,error);
        QVERIFY2(mergeHydroDetailMetadata(detailBytes,records,error),qPrintable(error));
        QCOMPARE(records.value(5).sourceId,QStringLiteral("500"));
        auto bad=coreBytes;bad.replace("\"logicalFid\":1","\"logicalFid\":-1");
        QVERIFY(!parseHydroCoreMetadata(bad,6,records,error));
        QCOMPARE(records.value(5).sourceId,QStringLiteral("500"));
    }
    void readsOnlyPackRangeAndRejectsChangedShard() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto path=copyFixture(dir);QVERIFY(!path.isEmpty());
        const auto manifest=readHydroManifest(path);
        QVERIFY2(manifest.valid(),qPrintable(manifest.error));
        HydroShardReader reader(manifest.shards[0].asset);
        QString error;
        const auto bytes=reader.readPack(0,68,error);
        QVERIFY2(error.isEmpty(),qPrintable(error));
        QVERIFY(bytes.startsWith("AWHF"));
        QFile shard(manifest.shards[0].asset.path);
        QVERIFY(shard.open(QIODevice::Append));QCOMPARE(shard.write("X"),qint64(1));shard.close();
        QVERIFY(reader.readPack(68,90,error).isEmpty());
        QVERIFY(!error.isEmpty());
    }
    void rejectsCorruptIndexPackGeometryAndMetadataWithoutCrash(){
        const auto manifest=readHydroManifest(QStringLiteral(WEB_HYDRO_FIXTURE)+"/v0.13.1/manifest.json");
        QString error;
        const auto originalIndex=readHydroAsset(manifest.index,true,error);
        QVERIFY2(error.isEmpty(),qPrintable(error));
        const auto decodeIndex=[&](const QByteArray& bytes){return pandoeditor::decodeHydroIndex(
            {reinterpret_cast<const std::uint8_t*>(bytes.constData()),static_cast<std::size_t>(bytes.size())},
            {static_cast<std::uint64_t>(manifest.shards.front().asset.bytes)});};
        auto changed=originalIndex;changed[0]=char(changed[0]^1);
        QVERIFY_EXCEPTION_THROWN(decodeIndex(changed),std::runtime_error);
        changed=originalIndex;changed[4]=char(changed[4]^1);
        QVERIFY_EXCEPTION_THROWN(decodeIndex(changed),std::runtime_error);
        changed=originalIndex;changed.append('X');
        QVERIFY_EXCEPTION_THROWN(decodeIndex(changed),std::runtime_error);
        const auto index=decodeIndex(originalIndex);
        HydroShardReader reader(manifest.shards.front().asset);
        const auto spec=index.packSpecs.at(0);
        const auto originalPack=reader.readPack(spec.offset,spec.length,error);
        QVERIFY2(error.isEmpty(),qPrintable(error));
        const auto coreBytes=readHydroAsset(manifest.metadataCore,true,error);
        HydroMetadata metadata;QVERIFY(parseHydroCoreMetadata(coreBytes,6,metadata,error));
        std::map<std::uint32_t,std::uint32_t> logical;
        for(auto it=metadata.cbegin();it!=metadata.cend();++it)logical.emplace(it.key(),it.value().logicalFid);
        const auto decodePack=[&](const QByteArray& bytes){return pandoeditor::decodeHydroPack(
            {reinterpret_cast<const std::uint8_t*>(bytes.constData()),static_cast<std::size_t>(bytes.size())},0,logical);};
        QVERIFY(!decodePack(originalPack).features.empty());
        changed=originalPack;changed[0]=char(changed[0]^1);
        QVERIFY_EXCEPTION_THROWN(decodePack(changed),std::runtime_error);
        changed=originalPack;changed[4]=char(changed[4]^1);
        QVERIFY_EXCEPTION_THROWN(decodePack(changed),std::runtime_error);
        changed=originalPack;for(int i=48;i<52;i++)changed[i]=char(0xff);
        QVERIFY_EXCEPTION_THROWN(decodePack(changed),std::runtime_error);
        changed=originalPack;changed.resize(57);changed[48]=1;
        for(int i=49;i<56;i++)changed[i]=0;
        changed[56]=char(0x80);
        QVERIFY_EXCEPTION_THROWN(decodePack(changed),std::runtime_error);
        logical[1]=99;
        QVERIFY_EXCEPTION_THROWN(decodePack(originalPack),std::runtime_error);
    }
};

QTEST_GUILESS_MAIN(HydroManifestTests)
#include "hydro_manifest_tests.moc"

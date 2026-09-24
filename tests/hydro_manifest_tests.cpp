#include "hydrodataprovider.h"
#include "hydrometadata.h"
#include "hydromanifest.h"
#include "hydroassetreader.h"
#include <QtTest>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTemporaryDir>
#include <QCryptographicHash>

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
        QVERIFY(records.value(5).sourceId.isEmpty());
        auto detailBytes=readHydroAsset(manifest.metadataDetail,true,error);
        QVERIFY2(mergeHydroDetailMetadata(detailBytes,records,error),qPrintable(error));
        QCOMPARE(records.value(5).sourceId,QStringLiteral("500"));
        auto bad=coreBytes;bad.replace("\"logicalFid\":1","\"logicalFid\":0");
        QVERIFY(!parseHydroCoreMetadata(bad,6,records,error));
        QCOMPARE(records.value(5).sourceId,QStringLiteral("500"));
    }
};

QTEST_GUILESS_MAIN(HydroManifestTests)
#include "hydro_manifest_tests.moc"

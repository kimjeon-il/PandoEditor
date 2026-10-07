#include "placeruntimestore.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <algorithm>
#include <set>
#include <cstring>
#include <QtEndian>

namespace {
QString fixture(const QString& name) {
    return QFINDTESTDATA("fixtures/web-place-runtime-source/synthetic/")+name+"/manifest.json";
}
QByteArray bytes(const QString& path) {QFile f(path);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("fixture missing");return f.readAll();}
PlaceViewport viewport(double zoom=3) {
    PlaceViewport v;v.zoom=zoom;v.view.viewportWidth=400;v.view.viewportHeight=300;
    v.view.scale=100;v.view.translateX=200;v.view.translateY=150;return v;
}
std::shared_ptr<PlaceRuntimeStore> store(const QString& name,std::size_t budget=PlaceRuntimeLimits::CacheBytes) {
    QString error;auto result=PlaceRuntimeStore::open(fixture(name),error,budget);
    if(!result)throw std::runtime_error(error.toStdString());return result;
}
}
class PlaceRuntimeStoreTests:public QObject {
    Q_OBJECT
private slots:
    void whitespaceNormalizationMatchesEcmaSourceText() {
        QCOMPARE(PlaceRuntimeStore::normalizeQuery(QString::fromUtf8("\ufeff\t 서울\u00a0 도시 \ufeff")),QString::fromUtf8("서울 도시"));
        QCOMPARE(PlaceRuntimeStore::normalizeQuery(QString::fromUtf8("\u0085서울\u0085")),QString::fromUtf8("\u0085서울\u0085"));
        QByteArray payload(88,char(0));const QString fields[]={"id",QString::fromUtf8("\ufeff서울\ufeff"),"",QString::fromUtf8("\ufeffsynthetic\ufeff"),""};
        for(int i=0;i<5;++i){const auto text=fields[i].toUtf8();const auto offset=quint32(payload.size()-88);const auto start=payload.size();payload.resize(start+4+text.size());qToLittleEndian(quint32(text.size()),reinterpret_cast<uchar*>(payload.data()+start));std::memcpy(payload.data()+start+4,text.constData(),std::size_t(text.size()));qToLittleEndian(offset,reinterpret_cast<uchar*>(payload.data()+68+i*4));}
        qToLittleEndian(quint32(0x43414c50),reinterpret_cast<uchar*>(payload.data()));qToLittleEndian(quint16(1),reinterpret_cast<uchar*>(payload.data()+4));qToLittleEndian(quint16(56),reinterpret_cast<uchar*>(payload.data()+6));qToLittleEndian(quint32(1),reinterpret_cast<uchar*>(payload.data()+8));qToLittleEndian(quint32(payload.size()-88),reinterpret_cast<uchar*>(payload.data()+12));qToLittleEndian(quint32(payload.size()),reinterpret_cast<uchar*>(payload.data()+16));
        const auto records=PlaceRuntimeStore::decodeTile(payload);QCOMPARE(records.size(),std::size_t(1));QCOMPARE(records[0].id,QString("builtin:place:synthetic:id"));QCOMPARE(records[0].name,QString::fromUtf8("서울"));
    }
    void containedLocalShardsOpenAndTraversalIsRejected() {
        QString error;const auto valid=PlaceRuntimeStore::open(QDir::toNativeSeparators(fixture("basic")),error);
        QVERIFY2(valid,qPrintable(error));QCOMPARE(valid->queryViewport(viewport()).records.size(),std::size_t(2));
        QTemporaryDir temporary;QVERIFY(temporary.isValid());
        for(const auto& relative:{QString("../0.bin"),QString("%2e%2e/0.bin")}) {
            auto manifest=QJsonDocument::fromJson(bytes(fixture("basic"))).object();
            auto shards=manifest["shards"].toObject();auto shard=shards["0"].toObject();shard["url"]=relative;shards["0"]=shard;manifest["shards"]=shards;
            QFile file(temporary.filePath("manifest.json"));QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate));file.write(QJsonDocument(manifest).toJson());file.close();
            QVERIFY(!PlaceRuntimeStore::open(file.fileName(),error));QVERIFY(error.contains("traversal"));
        }
    }
    void translatedFlatCameraQueriesTheVisibleMidpoint() {
        QTemporaryDir temporary;QVERIFY(temporary.isValid());
        auto manifest=QJsonDocument::fromJson(bytes(fixture("basic"))).object();auto stages=manifest["stages"].toArray();auto stage=stages[0].toObject();stage["columns"]=64;stage["rows"]=32;stages[0]=stage;manifest["stages"]=stages;
        // Runtime synthetic grid only; the pinned source fixture stays byte-exact.
        const auto tile=manifest["tiles"].toObject()["0/0-0"];manifest["tiles"]=QJsonObject{{"0/32-16",tile}};
        QFile file(temporary.filePath("manifest.json"));QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(manifest).toJson());file.close();
        QVERIFY(QFile::copy(QFileInfo(fixture("basic")).dir().filePath("0.bin"),temporary.filePath("0.bin")));
        QString error;const auto source=PlaceRuntimeStore::open(file.fileName(),error);QVERIFY2(source,qPrintable(error));
        auto view=viewport();view.view.scale=1000;view.view.centerLongitude=150;view.view.translateX=200+1000*150*3.14159265358979323846/180;
        const auto result=source->queryViewport(view);QCOMPARE(result.tileCount,std::size_t(1));
        QVERIFY(std::any_of(result.records.begin(),result.records.end(),[](const auto& record){return record.sourceId=="capital";}));
    }
    void terminalUtf8SequenceCannotPublishDecodedRecords() {
        auto payload=bytes(QFileInfo(fixture("basic")).dir().filePath("0.bin"));
        const auto poolSize=qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(payload.constData()+12));
        // One unused length-delimited pool entry is still part of the decoded tile.
        payload.append(QByteArray::fromHex("01000000e2"));
        qToLittleEndian(poolSize+quint32(5),reinterpret_cast<uchar*>(payload.data()+12));
        qToLittleEndian(quint32(payload.size()),reinterpret_cast<uchar*>(payload.data()+16));
        QVERIFY_EXCEPTION_THROWN(PlaceRuntimeStore::decodeTile(payload),std::exception);
    }
    void warmedPhysicalRangeCannotBypassAnotherDescriptorHash() {
        QTemporaryDir temporary;QVERIFY(temporary.isValid());auto manifest=QJsonDocument::fromJson(bytes(fixture("basic"))).object();
        auto search=manifest["search"].toObject();auto pages=search[QString::fromUtf8("서울")].toArray();auto wrong=pages[0].toObject();wrong["sha256"]=QString(64,'0');pages[0]=wrong;search[QString::fromUtf8("서울")]=pages;manifest["search"]=search;
        QFile file(temporary.filePath("manifest.json"));QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(manifest).toJson());file.close();
        QVERIFY(QFile::copy(QFileInfo(fixture("basic")).dir().filePath("0.bin"),temporary.filePath("0.bin")));
        QString error;const auto source=PlaceRuntimeStore::open(file.fileName(),error);QVERIFY2(source,qPrintable(error));
        QCOMPARE(source->queryViewport(viewport()).records.size(),std::size_t(2));const auto warmed=source->stats().cacheBytes;
        // Native integrity hardening: fixed Web accepts this malformed warm-cache descriptor.
        QVERIFY_EXCEPTION_THROWN(source->search(QString::fromUtf8("서울")),std::exception);
        QCOMPARE(source->stats().cacheBytes,warmed);QCOMPARE(source->queryViewport(viewport()).records.size(),std::size_t(2));
    }
    void exactWebCodecAndBoundedViewport() {
        auto s=store("basic");const auto result=s->queryViewport(viewport());
        QCOMPARE(result.records.size(),std::size_t(2));
        QCOMPARE(result.records[0].id,QString("builtin:place:synthetic:capital"));
        QCOMPARE(result.records[0].name,QString::fromUtf8("서울"));
        QCOMPARE(result.candidatesExamined,std::size_t(4));
        QCOMPARE(result.tileCount,std::size_t(1));
        QCOMPARE(s->stats().queryCount,quint64(1));
        const auto reads=s->stats().fileReadCount;s->queryViewport(viewport());
        QCOMPARE(s->stats().fileReadCount,reads);
    }
    void policyZoomAndProjectionCulling() {
        auto s=store("basic");QCOMPARE(s->queryViewport(viewport(1)).records.size(),std::size_t(1));
        auto v=viewport();v.view.mode=ProjectionMode::Globe;v.view.rotationRoll=75;
        const auto result=s->queryViewport(v);
        QVERIFY(std::none_of(result.records.begin(),result.records.end(),[](const auto& r){return r.sourceId=="back";}));
        v.safeLeft=201;QCOMPARE(s->queryViewport(v).records.size(),std::size_t(0));
    }
    void overscanCannotStarveVisibleRecords() {
        auto s=store("overscan");auto r=s->queryViewport(viewport());
        QCOMPARE(r.records.size(),std::size_t(1));QCOMPARE(r.records[0].sourceId,QString("capital"));
        QVERIFY(r.candidatesExamined>PlaceRuntimeLimits::Candidates);
    }
    void denseCandidateCapAndDeduplication() {
        auto s=store("dense");auto r=s->queryViewport(viewport());
        QCOMPARE(r.records.size(),PlaceRuntimeLimits::Candidates);
        QCOMPARE(r.candidatesExamined,std::size_t(2048));
        QCOMPARE(r.records.front().sourceId,QString("02047"));
        std::set<QString> ids;for(const auto& r:r.records)QVERIFY(ids.insert(r.id).second);
        QVERIFY(s->stats().cacheBytes<=s->stats().cacheBudget);
    }
    void prefixSearchUsesCanonicalNames() {
        auto s=store("basic");QVERIFY(s->search(QString::fromUtf8("서")).records.empty());
        const auto r=s->search(QString::fromUtf8("  서울  "));QCOMPARE(r.records.size(),std::size_t(2));
        QVERIFY(!r.truncated);
    }
    void cancellationCannotInsertCache() {
        auto s=store("basic");QVERIFY_EXCEPTION_THROWN(s->queryViewport(viewport(),[]{return true;}),PlaceRuntimeCancelled);
        QCOMPARE(s->stats().cacheBytes,std::size_t(0));
        QCOMPARE(s->stats().fileReadCount,quint64(0));
        // Cancellation checkpoints run within the production local-load path.
        int checkpoints=0;QVERIFY_EXCEPTION_THROWN(s->queryViewport(viewport(),[&]{return ++checkpoints>=5;}),PlaceRuntimeCancelled);
        QCOMPARE(s->stats().cacheBytes,std::size_t(0));
    }
    void zeroBudgetNeverCachesDecodedOrShardData() {
        auto s=store("basic",0);QCOMPARE(s->queryViewport(viewport()).records.size(),std::size_t(2));
        QCOMPARE(s->stats().cacheBytes,std::size_t(0));
        const auto reads=s->stats().fileReadCount;s->queryViewport(viewport());QVERIFY(s->stats().fileReadCount>reads);
    }
    void strictCodecRejectsMalformedPayload() {
        const auto good=bytes(QFileInfo(fixture("basic")).dir().filePath("0.bin"));
        auto broken=good;broken[0]=0;QVERIFY_EXCEPTION_THROWN(PlaceRuntimeStore::decodeTile(broken),std::exception);
        broken=good;broken.chop(1);QVERIFY_EXCEPTION_THROWN(PlaceRuntimeStore::decodeTile(broken),std::exception);
        broken=good;broken[32+32]=char(255);QVERIFY_EXCEPTION_THROWN(PlaceRuntimeStore::decodeTile(broken),std::exception);
        broken=good;broken[32+36]=char(255);broken[32+37]=char(255);QVERIFY_EXCEPTION_THROWN(PlaceRuntimeStore::decodeTile(broken),std::exception);
        broken=good;broken[8]=char(255);broken[9]=char(255);QVERIFY_EXCEPTION_THROWN(PlaceRuntimeStore::decodeTile(broken),std::exception);
        broken=good;const auto pool=32+56*qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(good.constData()+8));
        broken[pool+4]=char(255);QVERIFY_EXCEPTION_THROWN(PlaceRuntimeStore::decodeTile(broken),std::exception);
        broken=good;quint64 infinityBits=0x7ff0000000000000ULL;qToLittleEndian(infinityBits,reinterpret_cast<uchar*>(broken.data()+32));
        QVERIFY_EXCEPTION_THROWN(PlaceRuntimeStore::decodeTile(broken),std::exception);
    }
    void manifestRejectsOversizedOrUnhashedTilesBeforeReads() {
        QTemporaryDir temp;QVERIFY(temp.isValid());auto raw=QJsonDocument::fromJson(bytes(fixture("basic"))).object();
        auto shards=raw["shards"].toObject();auto shard=shards["0"].toObject();shard["bytes"]=int(PlaceRuntimeLimits::ShardBytes+1);shards["0"]=shard;raw["shards"]=shards;
        QFile f(temp.filePath("manifest.json"));QVERIFY(f.open(QIODevice::WriteOnly));f.write(QJsonDocument(raw).toJson());f.close();
        QString error;QVERIFY(!PlaceRuntimeStore::open(f.fileName(),error));QVERIFY(!error.isEmpty());
        raw=QJsonDocument::fromJson(bytes(fixture("basic"))).object();auto tiles=raw["tiles"].toObject();auto tile=tiles["0/0-0"].toObject();tile.remove("sha256");tiles["0/0-0"]=tile;raw["tiles"]=tiles;
        QVERIFY(f.open(QIODevice::WriteOnly|QIODevice::Truncate));f.write(QJsonDocument(raw).toJson());f.close();QVERIFY(!PlaceRuntimeStore::open(f.fileName(),error));
    }
    void tileIntegrityFailureCannotPublishOrCache() {
        QTemporaryDir temp;QVERIFY(temp.isValid());QFile manifest(temp.filePath("manifest.json"));QVERIFY(manifest.open(QIODevice::WriteOnly));manifest.write(bytes(fixture("basic")));manifest.close();
        auto payload=bytes(QFileInfo(fixture("basic")).dir().filePath("0.bin"));payload[payload.size()-1]=char(payload.back()^1);
        QFile shard(temp.filePath("0.bin"));QVERIFY(shard.open(QIODevice::WriteOnly));shard.write(payload);shard.close();
        QString error;auto s=PlaceRuntimeStore::open(manifest.fileName(),error);QVERIFY(s);
        QVERIFY_EXCEPTION_THROWN(s->queryViewport(viewport()),std::exception);QCOMPARE(s->stats().cacheBytes,std::size_t(0));
    }
    void smallBudgetEvictsAndNeverOverflowsSettledOwnership() {
        auto s=store("dense",80*1024);s->queryViewport(viewport());const auto first=s->stats();
        QVERIFY(first.evictions>0);QVERIFY(first.cacheBytes<=first.cacheBudget);
        s->queryViewport(viewport());QVERIFY(s->stats().fileReadCount>first.fileReadCount);QVERIFY(s->stats().cacheBytes<=first.cacheBudget);
    }
    void builtinIdsAndUnobservedSourceCountStaySeparate() {
        QVERIFY(PlaceRuntimeStore::isBuiltinId("builtin:place:synthetic:capital"));QVERIFY(!PlaceRuntimeStore::isBuiltinId("label-user"));
        auto s=store("basic");QVERIFY(!s->stats().sourceRecordCount);QCOMPARE(s->stats().manifestTileCount,std::size_t(1));
        QVERIFY(s->stats().sourceResidentBytes>0);QVERIFY(s->stats().sourceResidentBytes>s->stats().cacheBytes);
    }
};
QTEST_GUILESS_MAIN(PlaceRuntimeStoreTests)
#include "place_runtime_store_tests.moc"

#include "hydroruntimeprovider.h"
#include "hydroassetreader.h"
#include "projectcodec.h"
#include <pandoeditor/jobs.h>
#include <QtTest>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <zlib.h>
#include <future>
#include <thread>
#include <chrono>
#include <limits>

using namespace pandoeditor;
namespace {
QString fixture() { return QStringLiteral(WEB_HYDRO_FIXTURE)+"/v0.13.1/manifest.json"; }
Geometry line(double x=40,double y=0) { Geometry g;g.type="LineString";g.lines={{{x,y},{x+.1,y+.1}}};return g; }
Geometry polygon(MultiPolygon polygons) { Geometry g;g.type=polygons.size()==1?"Polygon":"MultiPolygon";g.polygons=std::move(polygons);return g; }
const std::vector<GeoBounds> splitWindow{{40,0,40.1,.1,false}};
const std::vector<GeoBounds> world{{-180,-90,180,90,false}};
const std::vector<GeoBounds> emptyWindow{{80,30,81,31,false}};
QByteArray read(const QString& path) { QFile f(path);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("test read");return f.readAll(); }
void write(const QString& path,const QByteArray& data) { QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(data)!=data.size())throw std::runtime_error("test write"); }
QByteArray gzip(const QByteArray& bytes) {
    z_stream s{};if(deflateInit2(&s,Z_BEST_SPEED,Z_DEFLATED,MAX_WBITS+16,8,Z_DEFAULT_STRATEGY)!=Z_OK)throw std::runtime_error("test gzip init");
    s.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data()));s.avail_in=bytes.size();QByteArray result;
    int status;do {char block[32768];s.next_out=reinterpret_cast<Bytef*>(block);s.avail_out=sizeof block;
        status=deflate(&s,Z_FINISH);result.append(block,sizeof(block)-s.avail_out);}while(status==Z_OK);
    deflateEnd(&s);if(status!=Z_STREAM_END)throw std::runtime_error("test gzip");return result;
}
void number(QByteArray& bytes,std::uint64_t n,int count) {for(int i=0;i<count;++i)bytes.append(char((n>>(i*8))&255));}
void varint(QByteArray& b,quint32 value) {while(value>=128){b.append(char((value&127)|128));value>>=7;}b.append(char(value));}
void setNumber(QByteArray& bytes,int offset,quint32 n,int count=4) {for(int i=0;i<count;++i)bytes[offset+i]=char((n>>(8*i))&255);}
QByteArray encodeIndex(const HydroIndex& index) {
    QByteArray b("AWI4");number(b,4,2);number(b,0,2);number(b,index.tilePacks.size(),4);number(b,index.logicalPacks.size(),4);number(b,index.packSpecs.size(),4);
    for(const auto& [tile,ids]:index.tilePacks){number(b,tile.stage,1);number(b,tile.x,2);number(b,tile.y,2);number(b,ids.size(),2);for(auto id:ids)number(b,id,4);}
    for(const auto& [logical,ids]:index.logicalPacks){number(b,logical,4);number(b,ids.size(),2);for(auto id:ids)number(b,id,4);}
    for(const auto& [id,s]:index.packSpecs){number(b,id,4);number(b,s.shard,2);number(b,s.offset,4);number(b,s.length,4);number(b,s.stage,1);}return gzip(b);
}
struct FixtureCopy {
    QTemporaryDir dir;QString manifestPath;QJsonObject manifest;HydroIndex index;
    FixtureCopy() {
        if(!dir.isValid())throw std::runtime_error("test temp dir");
        QDirIterator it(QStringLiteral(WEB_HYDRO_FIXTURE),QDir::Files,QDirIterator::Subdirectories);
        while(it.hasNext()){const auto from=it.next(),to=dir.path()+"/"+QDir(QStringLiteral(WEB_HYDRO_FIXTURE)).relativeFilePath(from);
            QDir().mkpath(QFileInfo(to).path());if(!QFile::copy(from,to))throw std::runtime_error("test copy");}
        manifestPath=dir.path()+"/v0.13.1/manifest.json";manifest=QJsonDocument::fromJson(read(manifestPath)).object();
        const auto m=readHydroManifest(manifestPath);QString error;const auto bytes=readHydroAsset(m.index,true,error);
        if(!error.isEmpty())throw std::runtime_error(error.toStdString());
        index=decodeHydroIndex({reinterpret_cast<const std::uint8_t*>(bytes.constData()),std::size_t(bytes.size())},{std::uint64_t(m.shards[0].asset.bytes)});
    }
    QString path(const QString& relative) const {return dir.path()+"/"+relative;}
    void save() {write(manifestPath,QJsonDocument(manifest).toJson());}
    QJsonObject replaceAsset(QJsonObject spec,const QString& relative,const QByteArray& bytes) {
        write(path(relative),bytes);spec["bytes"]=double(bytes.size());spec["sha256"]=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());return spec;
    }
    void saveIndex() {manifest["index"]=replaceAsset(manifest["index"].toObject(),"v0.13.0/index.bin.gz",encodeIndex(index));save();}
    void saveShard(const QByteArray& bytes) {auto shards=manifest["shards"].toArray();shards[0]=replaceAsset(shards[0].toObject(),"v0.13.0/shards/s0.bin",bytes);manifest["shards"]=shards;save();}
    void saveDetail(const QByteArray& decoded) {auto metadata=manifest["metadata"].toObject();metadata["detail"]=replaceAsset(metadata["detail"].toObject(),"v0.13.0/metadata-detail.json.gz",gzip(decoded));manifest["metadata"]=metadata;save();}
    void secondShardForLastFragment() {
        auto& spec=index.packSpecs.at(5);const auto bytes=read(path("v0.13.0/shards/s0.bin")).mid(spec.offset,spec.length);
        auto shards=manifest["shards"].toArray();auto first=shards[0].toObject();first["packs"]=5;shards[0]=first;
        shards.append(replaceAsset(QJsonObject{{"id",1},{"packs",1},{"url","../v0.13.0/shards/s1.bin"}},"v0.13.0/shards/s1.bin",bytes));
        manifest["shards"]=shards;spec.shard=1;spec.offset=0;saveIndex();
    }
    void largeSecondRiver() {
        // A real valid AWHF line, large enough to observe cancellation between
        // the first completed logical river and the second decoder completion.
        constexpr int count=2000000;QString error;auto source=read(path("v0.13.0/shards/s0.bin"));QByteArray next;
        for(auto& [id,spec]:index.packSpecs){auto packed=source.mid(spec.offset,spec.length);
            if(id==1){auto decoded=inflateHydroGzip(packed,error);decoded.resize(56);decoded[22]=1;
                QByteArray geometry;varint(geometry,1);varint(geometry,count);varint(geometry,20000000);varint(geometry,0);geometry+=QByteArray((count-1)*2,0);
                QByteArray widths;varint(widths,1);varint(widths,count);varint(widths,500);widths+=QByteArray(count-1,0);
                setNumber(decoded,48,geometry.size());setNumber(decoded,52,widths.size());decoded+=geometry;decoded+=widths;packed=gzip(decoded);}
            spec.offset=next.size();spec.length=packed.size();next+=packed;}
        saveShard(next);saveIndex();
    }
    void makeFirstLogical(quint32 logical,const QString& awId) {
        auto ids=index.logicalPacks.at(1);index.logicalPacks.erase(1);index.logicalPacks[logical]=ids;
        QString error;auto m=readHydroManifest(manifestPath);auto core=QJsonDocument::fromJson(readHydroAsset(m.metadataCore,true,error)).object();auto rows=core["features"].toArray();
        auto first=rows[0].toObject();first["logicalFid"]=double(logical);first["awId"]=awId;rows[0]=first;core["features"]=rows;
        auto metadata=manifest["metadata"].toObject();metadata["core"]=replaceAsset(metadata["core"].toObject(),"v0.13.1/metadata-core.json.gz",gzip(QJsonDocument(core).toJson()));manifest["metadata"]=metadata;
        QByteArray shard=read(path("v0.13.0/shards/s0.bin")),next;
        for(auto& [id,spec]:index.packSpecs){auto packed=shard.mid(spec.offset,spec.length);
            if(id==0){auto decoded=inflateHydroGzip(packed,error);setNumber(decoded,16,logical);packed=gzip(decoded);}
            spec.offset=next.size();spec.length=packed.size();next+=packed;}
        saveShard(next);saveIndex();
    }
};
Project sampleProject() {
    Project p;p.replace(std::vector<Country>{{"A","Alpha",{{{{0,0},{2,0},{2,2},{0,2},{0,0}}}},0x123456}});return p;
}
struct Job {
    Project project=sampleProject();JobScheduler scheduler;JobTicket ticket;
    Job():ticket(scheduler.enqueue(project.snapshot(),"river-source")){scheduler.takeNext();}
    HydroRiverSourceResult run(HydroRuntimeProvider& p,std::vector<GeoBounds> bounds=splitWindow,std::vector<EditedRiverValue> edits={}) {
        return p.riverPartitionSourceJob(std::move(bounds),std::move(edits))(ticket.token());
    }
};
void compareBounds(const std::vector<GeoBounds>& actual,const std::vector<GeoBounds>& expected) {
    QCOMPARE(actual.size(),expected.size());for(std::size_t i=0;i<actual.size();++i){QCOMPARE(actual[i].west,expected[i].west);QCOMPARE(actual[i].south,expected[i].south);QCOMPARE(actual[i].east,expected[i].east);QCOMPARE(actual[i].north,expected[i].north);QVERIFY(!actual[i].wrapsDateline);}
}
}
class HydroRiverSourceTests:public QObject {
    Q_OBJECT
private slots:
    void sourceIdentityAndCapturedLifetime() {
        HydroRuntimeProvider provider;QString error;QVERIFY(!provider.sourceIdentity());QVERIFY(!provider.riverPartitionSourceJob({},{}));
        QVERIFY_EXCEPTION_THROWN(provider.queryLogicalRivers(world),std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(provider.riverPartitionAssetRequirements(world),std::runtime_error);
        QVERIFY2(provider.open(fixture(),"p",false,error),qPrintable(error));auto first=*provider.sourceIdentity();
        const auto manifest=readHydroManifest(fixture());QCOMPARE(first.dataset,manifest.dataset);QCOMPARE(first.version,manifest.version);QCOMPARE(first.indexSha256,manifest.index.sha256);QVERIFY(first.generation>0);
        auto old=provider.riverPartitionSourceJob(splitWindow,{});QVERIFY(!provider.open("/does-not-exist/manifest.json","p",false,error));QVERIFY(*provider.sourceIdentity()==first);
        QVERIFY(provider.open(fixture(),"p",false,error));QVERIFY(provider.sourceIdentity()->generation>first.generation);
        provider.close("p");QVERIFY(!provider.sourceIdentity());Job job;auto result=old(job.ticket.token());QCOMPARE(result.status,HydroRiverSourceStatus::Ready);QVERIFY(result.identity==first);QCOMPARE(result.features.size(),std::size_t(1));
        std::function<HydroRiverSourceResult(const JobToken&)> orphan;
        {HydroRuntimeProvider temporary;QVERIFY(temporary.open(fixture(),"p",false,error));orphan=temporary.riverPartitionSourceJob(splitWindow,{});}
        QCOMPARE(orphan(job.ticket.token()).status,HydroRiverSourceStatus::Ready);
    }
    void discoveryAndFullGeometryIgnoreViewport() {
        HydroRuntimeProvider p;QString error;QVERIFY(p.open(fixture(),"p",false,error));
        QCOMPARE(p.queryLogicalRivers(world),(std::vector<quint32>{1,2,5}));
        QCOMPARE(p.queryLogicalRivers({{41,1,42,2,false},{0,0,0,0,false},{40,0,41,1,false},{0,0,2,1,false}}),(std::vector<quint32>{1,5}));
        QCOMPARE(p.queryLogicalRivers({{20,0,22,2,false}}).size(),std::size_t(0));
        Job job;const auto before=projectcodec::encode(job.project);auto result=job.run(p);QCOMPARE(result.status,HydroRiverSourceStatus::Ready);
        QCOMPARE(result.discoveredLogicalIds,(std::vector<quint32>{5}));QCOMPARE(result.diagnostics.discoveredLogicalRivers,std::size_t(1));QCOMPARE(result.diagnostics.loadedRivers,std::size_t(1));QCOMPARE(result.diagnostics.failedRiverLoads,std::size_t(0));
        auto feature=result.features.at(0);QCOMPARE(feature.id,QString("fixture:5"));QCOMPARE(feature.pandolabId,feature.id);QCOMPARE(*feature.logicalFid,quint32(5));
        QCOMPARE(feature.geometry.type,std::string("MultiLineString"));QCOMPARE(feature.geometry.lines.size(),std::size_t(2));QCOMPARE(feature.geometry.lines.back().back().x,42.);QCOMPARE(feature.geometry.lines.back().back().y,2.);
        QVERIFY(!p.frame());p.requestViewport({1,800,500,1500,-120,50});QTRY_VERIFY(p.frame()!=nullptr);QVERIFY(p.frame()->packIds.empty());
        QCOMPARE(job.run(p).features.at(0).geometry.lines.back().back().x,42.);
        QCOMPARE(projectcodec::encode(job.project),before);QVERIFY(!job.project.dirty());QVERIFY(!job.project.canUndo());QCOMPARE(job.project.revision(),std::uint64_t(0));
    }
    void boundsMatchExecutedWebAdapter() {
        // Expected values executed from pinned web createRiverCandidates and coordinateBounds,
        // not core's geographic/shortest-arc bounds helper.
        const double nan=std::numeric_limits<double>::quiet_NaN(),inf=std::numeric_limits<double>::infinity();
        const std::vector<Geometry> inputs{
            polygon({{{{0,0},{2,0},{2,1},{0,0}}}}),polygon({{{{0,0},{2,1}}},{{{10,2},{12,4}}}}),
            polygon({{{{0,0},{1,1}},{{5,-3},{7,4}}}}),polygon({{{{170,-2},{-170,3},{175,4}}}}),
            polygon({{{{-90,0},{90,2}}}}),polygon({{{{180,0},{-180,1}}}}),
            polygon({{{{170,89},{-170,90},{175,89}}}}),polygon({{{{nan,1},{4,inf},{7,3},{8,4}}}})};
        const std::vector<std::vector<GeoBounds>> expected{{{0,0,2,1}},{{0,0,2,1},{10,2,12,4}},{{0,-3,7,4}},
            {{170,-2,180,4},{-180,-2,-170,4}},{{-90,0,90,2}},{{180,0,180,1},{-180,0,-180,1}},{{170,89,180,90},{-180,89,-170,90}},{{7,3,8,4}}};
        for(std::size_t i=0;i<inputs.size();++i)compareBounds(riverPartitionQueryBounds(inputs[i]),expected[i]);
        QVERIFY(riverPartitionQueryBounds(line()).empty());QVERIFY(riverPartitionQueryBounds(polygon({{{{nan,0}}}})).empty());
        QVERIFY(riverPartitionBoundsOverlap({0,0,1,1},{1,1,2,2}));QVERIFY(!riverPartitionBoundsOverlap({180,0,180,1},{-180,0,-180,1}));
        Geometry edit;edit.type="MultiLineString";edit.lines={{{-179,1},{179,2}},{{5,-3},{7,4}}};
        compareBounds({*riverPartitionEditBounds(edit)},{{-179,-3,179,4}});QVERIFY(!riverPartitionEditBounds(Geometry{}));
    }
    void overlayUsesActualKeysAndOwnsCopiedEdits() {
        HydroRuntimeProvider p;QString error;QVERIFY(p.open(fixture(),"p",false,error));Job job;
        std::vector<EditedRiverValue> edits{{"fixture:5",line(40,.02),{}},{"new-uuid",line(40,.03),QString("fixture:5")},{"outside",line(100,40),{}}};
        auto work=p.riverPartitionSourceJob(world,edits);edits[0].geometry.lines[0][0].y=80;
        auto result=work(job.ticket.token());QCOMPARE(result.status,HydroRiverSourceStatus::Ready);QCOMPARE(result.features.size(),std::size_t(5));
        QCOMPARE(result.features[0].id,QString("fixture:1"));QCOMPARE(result.features[1].id,QString("fixture:2"));QCOMPARE(result.features[2].id,QString("fixture:5"));
        QCOMPARE(result.features[2].geometry.lines[0][0].y,.02);QVERIFY(!result.features[2].logicalFid);QCOMPARE(result.features[3].id,QString("new-uuid"));QCOMPARE(*result.features[3].sourceFeatureId,QString("fixture:5"));
        result=job.run(p,splitWindow,{{"new-uuid",line(),QString("fixture:5")},{"outside",line(100,40),{}}});
        QCOMPARE(result.features.size(),std::size_t(2));QCOMPARE(result.features[0].id,QString("fixture:5"));QCOMPARE(result.features[1].id,QString("new-uuid"));
        Geometry wrapped;wrapped.type="LineString";wrapped.lines={{{-179,0},{179,1}}};result=job.run(p,splitWindow,{{"wrapped",wrapped,{}}});QCOMPARE(result.features.size(),std::size_t(2));
    }
    void logicalZeroPreservesAwId() {
        FixtureCopy copy;copy.makeFirstLogical(0,"hydro:zero");HydroRuntimeProvider p;QString error;QVERIFY2(p.open(copy.manifestPath,"p",false,error),qPrintable(error));
        QCOMPARE(p.queryLogicalRivers(world),(std::vector<quint32>{0,2,5}));Job job;auto result=job.run(p,{{0,0,0,0}});
        QCOMPARE(result.status,HydroRiverSourceStatus::Ready);QCOMPARE(result.features[0].id,QString("hydro:zero"));QCOMPARE(*result.features[0].logicalFid,quint32(0));
    }
    void discoveryUsesNumericRatherThanLexicalOrder() {
        FixtureCopy copy;copy.makeFirstLogical(10,"fixture:10");HydroRuntimeProvider p;QString error;QVERIFY(p.open(copy.manifestPath,"p",false,error));
        QCOMPARE(p.queryLogicalRivers(world),(std::vector<quint32>{2,5,10}));Job job;const auto result=job.run(p,world);
        QCOMPARE(result.status,HydroRiverSourceStatus::Ready);QCOMPARE(result.features[0].id,QString("fixture:2"));QCOMPARE(result.features[1].id,QString("fixture:5"));QCOMPARE(result.features[2].id,QString("fixture:10"));
    }
    void exactAssetDescriptorsAreNotViewportInventoryGuesses() {
        FixtureCopy copy;copy.manifest["dataset"]="foreign-fixture";copy.save();HydroRuntimeProvider p;QString error;QVERIFY(p.open(copy.manifestPath,"p",false,error));
        const auto requirements=p.riverPartitionAssetRequirements(splitWindow);QCOMPARE(requirements.size(),std::size_t(2));
        QCOMPARE(requirements[0].physicalRelativePath,QString("hydro/v0.13.0/metadata-detail.json.gz"));QCOMPARE(requirements[1].physicalRelativePath,QString("hydro/v0.13.0/shards/s0.bin"));
        const auto m=readHydroManifest(copy.manifestPath);QCOMPARE(requirements[0].asset.path,m.metadataDetail.path);QCOMPARE(requirements[0].asset.bytes,m.metadataDetail.bytes);QCOMPARE(requirements[0].asset.sha256,m.metadataDetail.sha256);
        QCOMPARE(requirements[1].asset.path,m.shards[0].asset.path);QCOMPARE(requirements[1].asset.assetRoot,m.assetRoot);QCOMPARE(requirements[1].asset.sha256,m.shards[0].asset.sha256);
        QCOMPARE(p.sourceIdentity()->dataset,QString("foreign-fixture"));QVERIFY(requirements[1].asset.path.startsWith(copy.dir.path()+"/"));QVERIFY(p.riverPartitionAssetRequirements(emptyWindow).empty());
        QCOMPARE(p.riverPartitionAssetRequirements(world).size(),std::size_t(2));
    }
    void fullLogicalAssetsIncludeOutOfBoundsShard() {
        FixtureCopy copy;copy.secondShardForLastFragment();HydroRuntimeProvider p;QString error;QVERIFY(p.open(copy.manifestPath,"p",false,error));
        const auto assets=p.riverPartitionAssetRequirements(splitWindow);QCOMPARE(assets.size(),std::size_t(3));
        QCOMPARE(assets[1].physicalRelativePath,QString("hydro/v0.13.0/shards/s0.bin"));QCOMPARE(assets[2].physicalRelativePath,QString("hydro/v0.13.0/shards/s1.bin"));
        Job job;auto result=job.run(p);QCOMPARE(result.status,HydroRiverSourceStatus::Ready);QCOMPARE(result.features[0].geometry.lines.back().back().x,42.);
        QVERIFY(QFile::remove(copy.path("v0.13.0/shards/s1.bin")));QVERIFY(p.open(copy.manifestPath,"p",false,error));result=job.run(p,world);
        QCOMPARE(result.status,HydroRiverSourceStatus::Ready);QCOMPARE(result.failedLogicalIds,(std::vector<quint32>{5}));QCOMPARE(result.features.size(),std::size_t(2));
        QCOMPARE(result.failures[0].assetPath,copy.path("v0.13.0/shards/s1.bin"));
    }
    void emptyDiscoveryAndEditOnlySkipMissingDetail() {
        FixtureCopy copy;QVERIFY(QFile::remove(copy.path("v0.13.0/metadata-detail.json.gz")));HydroRuntimeProvider p;QString error;QVERIFY(p.open(copy.manifestPath,"p",false,error));Job job;
        auto result=job.run(p,emptyWindow);QCOMPARE(result.status,HydroRiverSourceStatus::Ready);QVERIFY(result.features.empty());QVERIFY(result.failures.empty());
        result=job.run(p,emptyWindow,{{"edit-only",line(80,30),{}}});QCOMPARE(result.status,HydroRiverSourceStatus::Ready);QCOMPARE(result.features.size(),std::size_t(1));QCOMPARE(result.diagnostics.discoveredLogicalRivers,std::size_t(0));
    }
    void missingOrCorruptDetailFailsEveryDiscoveredRiver_data() {
        QTest::addColumn<QString>("damage");QTest::newRow("missing")<<QString("missing");QTest::newRow("hash")<<QString("hash");
        QTest::newRow("gzip")<<QString("gzip");QTest::newRow("metadata-json")<<QString("json");
    }
    void missingOrCorruptDetailFailsEveryDiscoveredRiver() {
        QFETCH(QString,damage);FixtureCopy copy;const auto path=copy.path("v0.13.0/metadata-detail.json.gz");
        if(damage=="missing")QVERIFY(QFile::remove(path));
        else if(damage=="json")copy.saveDetail("not metadata JSON");
        else {auto bytes=read(path);bytes[0]=0;
            if(damage=="hash")write(path,bytes);
            else {auto metadata=copy.manifest["metadata"].toObject();metadata["detail"]=copy.replaceAsset(metadata["detail"].toObject(),"v0.13.0/metadata-detail.json.gz",bytes);copy.manifest["metadata"]=metadata;copy.save();}}
        HydroRuntimeProvider p;QString error;QVERIFY(p.open(copy.manifestPath,"p",false,error));p.setCacheBudget(0);Job job;
        auto result=job.run(p,world,{{"edit",line(),{}}});QCOMPARE(result.status,HydroRiverSourceStatus::SourceError);QCOMPARE(result.failedLogicalIds,(std::vector<quint32>{1,2,5}));QCOMPARE(result.failures.size(),std::size_t(3));QVERIFY(result.features.empty());
        QCOMPARE(result.diagnostics.failedRiverLoads,std::size_t(3));QVERIFY(!result.failures[0].detail.isEmpty());QCOMPARE(result.failures[0].assetPath,copy.path("v0.13.0/metadata-detail.json.gz"));
        QCOMPARE(p.resourceCacheSnapshot().pendingCount,std::size_t(0));QCOMPARE(p.cachedPackCount(),std::size_t(0));QCOMPARE(p.resourceCacheSnapshot().failureCount,std::uint64_t(1));
    }
    void partialPackFailureKeepsIndependentSuccessAndRejectsIncompleteRiver() {
        FixtureCopy copy;auto shard=read(copy.path("v0.13.0/shards/s0.bin"));shard[copy.index.packSpecs.at(5).offset]=0;copy.saveShard(shard);
        HydroRuntimeProvider p;QString error;QVERIFY(p.open(copy.manifestPath,"p",false,error));p.setCacheBudget(0);Job job;auto result=job.run(p,world);
        QCOMPARE(result.status,HydroRiverSourceStatus::Ready);QCOMPARE(result.failedLogicalIds,(std::vector<quint32>{5}));QCOMPARE(result.features.size(),std::size_t(2));QCOMPARE(result.features[0].id,QString("fixture:1"));QCOMPARE(result.features[1].id,QString("fixture:2"));
        QCOMPARE(result.diagnostics.loadedRivers,std::size_t(2));QCOMPARE(result.diagnostics.failedRiverLoads,std::size_t(1));QCOMPARE(result.failures[0].assetPath,copy.path("v0.13.0/shards/s0.bin"));QCOMPARE(p.cachedPackCount(),std::size_t(0));QCOMPARE(p.resourceCacheSnapshot().pendingCount,std::size_t(0));
        result=job.run(p,splitWindow,{{"edit",line(),{}}});QCOMPARE(result.status,HydroRiverSourceStatus::SourceError);QVERIFY(result.features.empty());
    }
    void missingShardAndIncompleteFragmentSetAreErrors() {
        FixtureCopy copy;copy.index.logicalPacks[5]={4};copy.saveIndex();HydroRuntimeProvider p;QString error;QVERIFY(p.open(copy.manifestPath,"p",false,error));Job job;
        auto result=job.run(p);QCOMPARE(result.status,HydroRiverSourceStatus::SourceError);QVERIFY(result.failures[0].detail.contains("incomplete"));
        QVERIFY(QFile::remove(copy.path("v0.13.0/shards/s0.bin")));QVERIFY(p.open(copy.manifestPath,"p",false,error));result=job.run(p,world);QCOMPARE(result.status,HydroRiverSourceStatus::SourceError);QCOMPARE(result.failedLogicalIds,(std::vector<quint32>{1,2,5}));
    }
    void invalidQueriesAreExplicitAndCancellationWins() {
        HydroRuntimeProvider p;QString error;QVERIFY(p.open(fixture(),"p",false,error));Job job;
        for(const auto b:std::vector<GeoBounds>{{1,0,0,1},{0,1,1,0},{0,0,1,1,true},{0,0,std::numeric_limits<double>::infinity(),1}}){
            QVERIFY_EXCEPTION_THROWN(p.queryLogicalRivers({b}),std::invalid_argument);QVERIFY_EXCEPTION_THROWN(p.riverPartitionAssetRequirements({b}),std::invalid_argument);QCOMPARE(job.run(p,{b}).status,HydroRiverSourceStatus::Error);}
        QVERIFY(job.scheduler.cancel(job.ticket.id()));QCOMPARE(job.run(p).status,HydroRiverSourceStatus::Cancelled);QCOMPARE(job.run(p,{{1,0,0,1}}).status,HydroRiverSourceStatus::Cancelled);QCOMPARE(p.resourceCacheSnapshot().pendingCount,std::size_t(0));
    }
    void cancellationDuringRealWorkReleasesCopyProtection_data() {
        QTest::addColumn<QString>("phase");QTest::newRow("detail-read")<<QString("detail");
        QTest::newRow("between-logical-loads")<<QString("logical");QTest::newRow("after-decode-overlay")<<QString("overlay");
    }
    void cancellationDuringRealWorkReleasesCopyProtection() {
        QFETCH(QString,phase);FixtureCopy copy;std::vector<EditedRiverValue> edits;
        if(phase=="detail") {QString error;const auto m=readHydroManifest(copy.manifestPath);auto bytes=readHydroAsset(m.metadataDetail,true,error);bytes+=QByteArray(32*1024*1024,' ');copy.saveDetail(bytes);}
        if(phase=="logical")copy.largeSecondRiver();
        if(phase=="overlay"){auto geometry=line();geometry.lines[0].resize(2000000,{40,0});edits.push_back({"large-edit",std::move(geometry),{}});}
        HydroRuntimeProvider p;QString error;QVERIFY(p.open(copy.manifestPath,"p",false,error));p.setCacheBudget(0);Job job;
        auto work=p.riverPartitionSourceJob(world,std::move(edits));const int target=phase=="detail"?5:phase=="logical"?33:85;
        auto future=std::async(std::launch::async,[&]{return work(job.ticket.token());});
        QElapsedTimer wait;wait.start();
        while(job.ticket.token().progress()<target&&wait.elapsed()<5000&&future.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)std::this_thread::yield();
        const auto progress=job.ticket.token().progress();
        // Altering viewport/edit protections must not remove the source's own
        // token. No sleeps or production test hooks select the cancellation point.
        const auto pending=p.resourceCacheSnapshot().pendingCount;
        if(phase!="detail"){p.clearPinned();p.setSelectedLogicals({});p.requestViewport({1,800,500,1500,20,1});}
        const auto protectedPacks=p.resourceCacheSnapshot().protectionCounts[std::size_t(ResourceProtection::InFlight)];
        const bool cancelled=job.scheduler.cancel(job.ticket.id());auto result=future.get();
        QVERIFY2(progress>=target&&progress<100,qPrintable(QString("missed cancellation phase: %1").arg(progress)));QVERIFY(pending>=1);QVERIFY(cancelled);
        QCOMPARE(result.status,HydroRiverSourceStatus::Cancelled);QVERIFY(result.features.empty());QVERIFY(result.failedLogicalIds.empty());
        if(phase!="detail"){QVERIFY(protectedPacks>=1);QTRY_VERIFY(p.frame()!=nullptr&&p.frame()->packIds.empty());}
        QCOMPARE(p.resourceCacheSnapshot().pendingCount,std::size_t(0));QCOMPARE(p.cachedPackCount(),std::size_t(0));
        QCOMPARE(p.resourceCacheSnapshot().protectionCounts[std::size_t(ResourceProtection::InFlight)],std::size_t(0));
        QCOMPARE(p.resourceCacheSnapshot().failureCount,std::uint64_t(1));
    }
    void sourceCopyDoesNotClearSelectedEditingOrVisibleProtection() {
        HydroRuntimeProvider p;QString error;QVERIFY(p.open(fixture(),"p",false,error));p.setSelectedLogical(1);QVERIFY(p.pinLogical(5));p.setCacheBudget(0);Job job;
        QCOMPARE(job.run(p,world).status,HydroRiverSourceStatus::Ready);QCOMPARE(p.cachedPackCount(),std::size_t(3));auto snapshot=p.resourceCacheSnapshot();QCOMPARE(snapshot.pendingCount,std::size_t(0));
        QCOMPARE(snapshot.protectionCounts[std::size_t(ResourceProtection::Selected)],std::size_t(1));QCOMPARE(snapshot.protectionCounts[std::size_t(ResourceProtection::Editing)],std::size_t(2));QCOMPARE(snapshot.protectionCounts[std::size_t(ResourceProtection::InFlight)],std::size_t(0));
        p.requestViewport({7.5,800,500,1500,20,1});QTRY_VERIFY(p.frame()!=nullptr&&p.frame()->packIds.size()==6);QCOMPARE(job.run(p).status,HydroRiverSourceStatus::Ready);QCOMPARE(p.cachedPackCount(),std::size_t(6));
        p.clearPinned();p.setSelectedLogical({});p.requestViewport({1,800,500,1500,20,1});QTRY_VERIFY(p.frame()!=nullptr&&p.frame()->packIds.empty());QCOMPARE(p.cachedPackCount(),std::size_t(0));
    }
};
QTEST_GUILESS_MAIN(HydroRiverSourceTests)
#include "hydro_river_source_tests.moc"

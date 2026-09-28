#include "projectpreviewcache.h"
#include "projectcodec.h"
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class ProjectPreviewCacheTests final : public QObject {
    Q_OBJECT
private:
    static QByteArray sample() {
        QFile file(":/assets/sample.pando.json");
        if(!file.open(QIODevice::ReadOnly))return {};
        return file.readAll();
    }
private slots:
    void identityIncludesEveryRequiredInput() {
        const auto a=ProjectPreviewIdentity::from("source-a",ProjectPreviewService::AlgorithmVersion,"one");
        QCOMPARE(a,ProjectPreviewIdentity::from("source-a",ProjectPreviewService::AlgorithmVersion,"one"));
        QVERIFY(!(a==ProjectPreviewIdentity::from("source-b",ProjectPreviewService::AlgorithmVersion,"one")));
        QVERIFY(!(a==ProjectPreviewIdentity::from("source-a","next","one")));
        QVERIFY(!(a==ProjectPreviewIdentity::from("source-a",ProjectPreviewService::AlgorithmVersion,"two")));
    }
    void idleSchedulingUsesOnlyPersistedBytes() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        ProjectPreviewService service(dir.filePath("preview.json"));
        QSignalSpy ready(&service,&ProjectPreviewService::previewReady);
        const auto persisted=sample();QVERIFY(!persisted.isEmpty());
        auto unsaved=persisted;unsaved.replace("Germany","UNSAVED");
        service.schedule(persisted,"source-a");
        QTest::qWait(ProjectPreviewService::IdleDelayMs-100);
        QCOMPARE(ready.count(),0);
        service.schedule(persisted,"source-a");
        QTest::qWait(ProjectPreviewService::IdleDelayMs-100);
        QCOMPARE(ready.count(),0);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,2500);
        const auto preview=ready.takeFirst().at(0).toByteArray();
        QVERIFY(!preview.isEmpty());
        QVERIFY(!preview.contains("UNSAVED"));
        QVERIFY(projectcodec::decode(preview).documentId==projectcodec::decode(persisted).documentId);
    }
    void cacheCapStaleGenerationAndCanonicalFallback() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        ProjectPreviewCache cache(dir.filePath("preview.json"));
        const auto identity=ProjectPreviewIdentity::from("source",ProjectPreviewService::AlgorithmVersion,"project");
        QVERIFY(cache.store(identity,"preview"));
        QCOMPARE(cache.loadOrCanonical(identity,"canonical"),QByteArray("preview"));
        const auto other=ProjectPreviewIdentity::from("source",ProjectPreviewService::AlgorithmVersion,"other");
        QCOMPARE(cache.loadOrCanonical(other,"canonical"),QByteArray("canonical"));
        QVERIFY(!cache.store(other,QByteArray(ProjectPreviewCache::MaximumPreviewBytes+1,'x')));
        QCOMPARE(cache.loadOrCanonical(identity,"canonical"),QByteArray("preview"));

        ProjectPreviewService service(dir.filePath("service.json"));
        const auto oldGeneration=service.generation();
        service.schedule("old","source");
        const auto scheduled=service.generation();
        service.schedule("new","source");
        QVERIFY(service.generation()>scheduled);
        QVERIFY(!service.commitGenerated(scheduled,identity,"stale"));
        QVERIFY(!service.commitGenerated(oldGeneration,identity,"older"));

        QFile corrupt(dir.filePath("preview.json"));QVERIFY(corrupt.open(QIODevice::WriteOnly|QIODevice::Truncate));
        corrupt.write("broken");corrupt.close();
        QCOMPARE(cache.loadOrCanonical(identity,"canonical"),QByteArray("canonical"));
        QVERIFY(QFile::exists(dir.filePath("preview.json.corrupt")));
    }
};

QTEST_GUILESS_MAIN(ProjectPreviewCacheTests)
#include "project_preview_cache_tests.moc"

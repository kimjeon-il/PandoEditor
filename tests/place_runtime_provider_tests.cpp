#include "placeruntimeprovider.h"
#include <QFile>
#include <QSignalSpy>
#include <QtTest>

namespace {
QString fixture() {return QFINDTESTDATA("fixtures/web-place-runtime-source/synthetic/basic/manifest.json");}
PlaceViewport viewport(double zoom=3) {PlaceViewport v;v.zoom=zoom;v.view.viewportWidth=400;v.view.viewportHeight=300;v.view.scale=100;v.view.translateX=200;v.view.translateY=150;return v;}
}
class PlaceRuntimeProviderTests:public QObject {
    Q_OBJECT
private slots:
    void failedReplacementPreservesPublishedIdentity() {
        PlaceRuntimeProvider p;QString error;QVERIFY(p.open(fixture(),"project-A",error));
        QSignalSpy accepted(&p,&PlaceRuntimeProvider::snapshotChanged);p.requestViewport(viewport());QTRY_COMPARE(accepted.count(),1);
        const auto identity=p.sourceIdentity();const auto before=p.snapshot();QVERIFY(before);
        QVERIFY(!p.open("missing-manifest.json","project-B",error));QVERIFY(!error.isEmpty());
        QCOMPARE(p.sourceIdentity(),identity);QCOMPARE(p.snapshot(),before);
    }
    void movementDefersQueryAndPreservesTransientSnapshot() {
        PlaceRuntimeProvider p;QString error;QVERIFY(p.open(fixture(),"A",error));
        QSignalSpy accepted(&p,&PlaceRuntimeProvider::snapshotChanged);p.requestViewport(viewport());QTRY_COMPARE(accepted.count(),1);
        const auto before=p.snapshot();const auto queries=p.storeStats().queryCount;
        p.beginInteraction();p.requestViewport(viewport(1));QTest::qWait(10);
        QCOMPARE(p.snapshot(),before);QCOMPARE(p.storeStats().queryCount,queries);
        p.settle();QTRY_COMPARE(accepted.count(),2);QCOMPARE(p.snapshot()->records.size(),std::size_t(1));
    }
    void latestRequestAndProjectReplacementRejectStalePublication() {
        PlaceRuntimeProvider p;QString error;QVERIFY(p.open(fixture(),"A",error));
        QSignalSpy accepted(&p,&PlaceRuntimeProvider::snapshotChanged);p.requestViewport(viewport(3));p.requestViewport(viewport(1));
        QTRY_COMPARE(p.stats().pendingCount,std::size_t(0));QCOMPARE(accepted.count(),1);QCOMPARE(p.snapshot()->records.size(),std::size_t(1));
        const auto generation=p.sourceIdentity()->generation;p.requestViewport(viewport(3));QVERIFY(p.open(fixture(),"B",error));
        QCOMPARE(p.sourceIdentity()->generation,generation+1);QVERIFY(!p.snapshot());
        QTRY_COMPARE(p.stats().pendingCount,std::size_t(0));QCOMPARE(accepted.count(),1);
        QCOMPARE(p.stats().stalePublicationCount,quint64(0));QVERIFY(p.stats().staleCompletionCount>=1);
    }
    void closeCancelsPendingWorkAndDropsIdentity() {
        PlaceRuntimeProvider p;QString error;QVERIFY(p.open(fixture(),"A",error));p.requestViewport(viewport());p.close("B");
        QVERIFY(!p.isOpen());QVERIFY(!p.sourceIdentity());QVERIFY(!p.snapshot());
        QTRY_COMPARE(p.stats().pendingCount,std::size_t(0));QCOMPARE(p.stats().stalePublicationCount,quint64(0));
    }
    void burstsKeepOneRunningAndOneLatestQueuedJobPerKind() {
        PlaceRuntimeProvider p;QString error;QVERIFY(p.open(fixture(),"A",error));QSignalSpy accepted(&p,&PlaceRuntimeProvider::snapshotChanged);
        for(int i=0;i<100;++i){p.requestViewport(viewport(i%2?1:3));QVERIFY(p.stats().pendingCount<=1);QVERIFY(p.stats().queuedCount<=1);}
        QTRY_COMPARE(p.stats().pendingCount,std::size_t(0));QCOMPARE(accepted.count(),1);QCOMPARE(p.snapshot()->records.size(),std::size_t(1));
        QCOMPARE(p.stats().queuedCount,std::size_t(0));QVERIFY(p.stats().coalescedViewportRequests>0);
    }
    void boundedRetentionProtectsPrimaryAndCopyDoesNotMutateSource() {
        PlaceRuntimeProvider p;QString error;QVERIFY(p.open(fixture(),"A",error));QSignalSpy accepted(&p,&PlaceRuntimeProvider::snapshotChanged);p.requestViewport(viewport());QTRY_COMPARE(accepted.count(),1);
        auto source=p.snapshot()->records.front();p.retain(source);p.setProtectedIds({source.id});
        for(int i=0;i<300;++i){auto r=source;r.sourceId=QString::number(i);r.id="builtin:place:synthetic:"+r.sourceId;p.retain(r);}
        QVERIFY(p.recordById(source.id));QCOMPARE(p.stats().retainedRecords,PlaceRuntimeLimits::RetainedRecords);
        auto copy=*p.recordById(source.id);copy.name="edited";QCOMPARE(p.recordById(source.id)->name,source.name);
    }
    void selectedRecordSurvivesLeavingViewport() {
        PlaceRuntimeProvider p;QString error;QVERIFY(p.open(fixture(),"A",error));QSignalSpy accepted(&p,&PlaceRuntimeProvider::snapshotChanged);
        p.requestViewport(viewport());QTRY_COMPARE(accepted.count(),1);
        const auto id=QString("builtin:place:synthetic:city");p.setProtectedIds({id});p.requestViewport(viewport(1));QTRY_COMPARE(accepted.count(),2);
        QCOMPARE(p.snapshot()->records.size(),std::size_t(1));QVERIFY(p.recordById(id));
    }
    void canonicalSearchAndCancelDoNotReplaceViewport() {
        PlaceRuntimeProvider p;QString error;QVERIFY(p.open(fixture(),"A",error));QSignalSpy found(&p,&PlaceRuntimeProvider::searchCompleted);
        p.search(QString::fromUtf8("서울"));QTRY_COMPARE(found.count(),1);QCOMPARE(p.searchResults().size(),std::size_t(2));QVERIFY(!p.snapshot());
        p.search(QString::fromUtf8("서울"));p.cancelSearch();QTRY_COMPARE(p.stats().pendingCount,std::size_t(0));QCOMPARE(found.count(),1);
        p.beginInteraction();QVERIFY(!p.search(QString::fromUtf8("서울")));QCOMPARE(found.count(),1);
    }
};
QTEST_GUILESS_MAIN(PlaceRuntimeProviderTests)
#include "place_runtime_provider_tests.moc"

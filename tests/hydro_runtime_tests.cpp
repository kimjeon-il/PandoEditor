#include "hydroloadscheduler.h"
#include "hydroruntimeprovider.h"
#include <QtTest>
#include <QSemaphore>
#include <QSignalSpy>

class HydroRuntimeTests:public QObject {
    Q_OBJECT
private slots:
    void loadsPhysicalFeaturesWithoutMutatingDocument() {
        HydroRuntimeProvider provider;
        QString error;
        QVERIFY2(provider.open(QStringLiteral(WEB_HYDRO_FIXTURE)+"/v0.13.1/manifest.json",
                               "project-A",false,error),qPrintable(error));
        QSignalSpy accepted(&provider,&HydroRuntimeProvider::frameChanged);
        provider.requestViewport({7.5,800,500,1500,20,1});
        QTRY_VERIFY_WITH_TIMEOUT(accepted.count()>=1 && provider.frame()!=nullptr,5000);
        QCOMPARE(provider.frame()->features.size(),std::size_t(6));
        std::vector<pandoeditor::HydroPhysicalFeature> fragments;
        for(const auto& feature:provider.frame()->features)if(feature.logicalFid==5)fragments.push_back(feature);
        const auto merged=pandoeditor::mergeHydroLogicalFragments(std::move(fragments));
        QCOMPARE(merged.type,std::string("LineString"));
        QCOMPARE(merged.lines.size(),std::size_t(1));
        QCOMPARE(merged.lines.front().size(),std::size_t(3));
        QCOMPARE(provider.frame()->packIds.size(),std::size_t(6));
        QCOMPARE(provider.cachedPackCount(),std::size_t(6));
        QVERIFY(provider.cachedBytes()>0);
        QVERIFY(provider.pinLogical(5));
        provider.clearPinned();
        QVERIFY(provider.coreMetadata()!=nullptr);
        QCOMPARE(provider.coreMetadata()->size(),6);
    }
    void staleViewportResultCannotReplaceNewFrame() {
        HydroLoadScheduler scheduler;
        scheduler.resetDataset("project-A");
        QSignalSpy accepted(&scheduler,&HydroLoadScheduler::frameAccepted);
        QSemaphore started,release;
        scheduler.requestViewport([&]{
            started.release();release.acquire();
            auto frame=std::make_shared<HydroRuntimeFrame>();frame->packIds={1};return frame;
        });
        QVERIFY(started.tryAcquire(1,5000));
        scheduler.requestViewport([]{
            auto frame=std::make_shared<HydroRuntimeFrame>();frame->packIds={2};return frame;
        });
        QTRY_COMPARE_WITH_TIMEOUT(accepted.count(),1,5000);
        QCOMPARE(scheduler.frame()->packIds,(std::vector<std::uint32_t>{2}));
        release.release();
        QTest::qWait(100);
        QCOMPARE(accepted.count(),1);
        QCOMPARE(scheduler.frame()->packIds,(std::vector<std::uint32_t>{2}));
    }
    void oldProjectResultCannotAppearInNewProject() {
        HydroLoadScheduler scheduler;
        scheduler.resetDataset("project-A");
        QSignalSpy accepted(&scheduler,&HydroLoadScheduler::frameAccepted);
        QSemaphore started,release;
        scheduler.requestViewport([&]{
            started.release();release.acquire();
            auto frame=std::make_shared<HydroRuntimeFrame>();frame->packIds={1};return frame;
        });
        QVERIFY(started.tryAcquire(1,5000));
        scheduler.resetDataset("project-B");
        release.release();QTest::qWait(100);
        QCOMPARE(accepted.count(),0);
        QVERIFY(!scheduler.frame());
        scheduler.requestViewport([]{
            auto frame=std::make_shared<HydroRuntimeFrame>();frame->packIds={3};return frame;
        });
        QTRY_COMPARE_WITH_TIMEOUT(accepted.count(),1,5000);
        QCOMPARE(scheduler.frame()->packIds,(std::vector<std::uint32_t>{3}));
    }
    void failedViewportKeepsLastGoodFrame() {
        HydroLoadScheduler scheduler;
        scheduler.resetDataset("project-A");
        QSignalSpy accepted(&scheduler,&HydroLoadScheduler::frameAccepted);
        QSignalSpy failed(&scheduler,&HydroLoadScheduler::loadFailed);
        scheduler.requestViewport([]{
            auto frame=std::make_shared<HydroRuntimeFrame>();frame->packIds={4};return frame;
        });
        QTRY_COMPARE_WITH_TIMEOUT(accepted.count(),1,5000);
        scheduler.requestViewport([]() -> std::shared_ptr<const HydroRuntimeFrame> {
            throw std::runtime_error("damaged pack");
        });
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,5000);
        QCOMPARE(scheduler.frame()->packIds,(std::vector<std::uint32_t>{4}));
    }
};
QTEST_GUILESS_MAIN(HydroRuntimeTests)
#include "hydro_runtime_tests.moc"

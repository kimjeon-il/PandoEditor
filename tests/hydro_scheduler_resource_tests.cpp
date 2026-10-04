#include "hydroloadscheduler.h"
#include <QtTest>
#include <QSemaphore>
class HydroSchedulerResourceTests:public QObject {
 Q_OBJECT
private slots:
 void pendingAndFailureFinishOnce(){
  HydroLoadScheduler scheduler;scheduler.resetDataset("A");QSemaphore release;
  scheduler.requestViewport([&]{release.acquire();return std::make_shared<HydroRuntimeFrame>();});
  QCOMPARE(scheduler.resourceCacheSnapshot().pendingCount,std::size_t(1));
  release.release();QTRY_COMPARE(scheduler.resourceCacheSnapshot().pendingCount,std::size_t(0));
  scheduler.requestViewport([]()->std::shared_ptr<const HydroRuntimeFrame>{throw std::runtime_error("failed");});
  QTRY_COMPARE(scheduler.resourceCacheSnapshot().failureCount,std::uint64_t(1));
  QVERIFY(scheduler.frame()!=nullptr);
 }
 void staleProjectCannotFinishNewPending(){
  HydroLoadScheduler scheduler;scheduler.resetDataset("A");QSemaphore oldRelease,newRelease;
  scheduler.requestViewport([&]{oldRelease.acquire();return std::make_shared<HydroRuntimeFrame>();});
  scheduler.resetDataset("B");
  scheduler.requestViewport([&]{newRelease.acquire();return std::make_shared<HydroRuntimeFrame>();});
  oldRelease.release();QTRY_COMPARE(scheduler.resourceCacheSnapshot().staleCompletionCount,std::uint64_t(1));
  QCOMPARE(scheduler.resourceCacheSnapshot().pendingCount,std::size_t(1));QVERIFY(!scheduler.frame());
  newRelease.release();QTRY_COMPARE(scheduler.resourceCacheSnapshot().pendingCount,std::size_t(0));QVERIFY(scheduler.frame());
 }
};
QTEST_GUILESS_MAIN(HydroSchedulerResourceTests)
#include "hydro_scheduler_resource_tests.moc"

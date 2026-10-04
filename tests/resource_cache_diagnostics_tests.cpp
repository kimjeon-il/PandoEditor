#include "resourcecachecoordinator.h"
#include <QtTest>
class ResourceCacheDiagnosticsTests:public QObject {
 Q_OBJECT
private slots:
 void subsetsAreNotAddedAndUnknownIsExplicit(){
  ResourceCacheCoordinator c;pandoeditor::ResourceCacheSnapshot a;a.residentBytes=100;a.protectedBytes=80;a.activeBytes=60;
  c.setDomain("terrain",a,"bounded");a.residentBytes=200;c.setDomain("world",a,"compatibilityWorkingSet");
  const auto result=c.snapshot();QCOMPARE(result.value("cacheOwnedCpuBytes").toULongLong(),qulonglong(300));
  const auto terrain=result.value("terrain").toMap();QCOMPARE(terrain.value("protectedBytes").toULongLong(),qulonglong(80));
  QVERIFY(!terrain.value("retiredBytesAvailable").toBool());QVERIFY(!terrain.value("retiredBytes").isValid());
 }
 void oldSourceAndContextAreRejected(){
  ResourceCacheCoordinator c;c.setQsgSource(12);
  QVERIFY(c.acceptQsgSnapshot(12,3,{{"liveResourceBytes",400}}));
  QVERIFY(!c.acceptQsgSnapshot(12,2,{{"liveResourceBytes",999}}));
  c.setQsgSource(14);QVERIFY(!c.acceptQsgSnapshot(12,4,{{"liveResourceBytes",999}}));
  QVERIFY(c.acceptQsgSnapshot(14,1,{{"liveResourceBytes",50}}));
  const auto result=c.snapshot();QCOMPARE(result.value("qsg").toMap().value("liveResourceBytes").toInt(),50);
  QCOMPARE(result.value("cacheOwnedCpuBytes").toULongLong(),qulonglong(0));
 }
};
QTEST_GUILESS_MAIN(ResourceCacheDiagnosticsTests)
#include "resource_cache_diagnostics_tests.moc"

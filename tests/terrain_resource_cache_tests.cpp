#include "terrainprovider.h"
#include "terraindisplaystate.h"
#include "terrainimageprovider.h"
#include "geographicimageitem.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
class TerrainResourceCacheTests:public QObject {
 Q_OBJECT
private slots:
 void partialViewsDoNotAccumulateHistory(){
  TerrainDisplayState state;const QVariant a="A",b="B",c="C";
  QCOMPARE(state.publish({a},true),QVariantList{a});
  QCOMPARE(state.publish({b},false),(QVariantList{b,a}));
  QCOMPARE(state.publish({c},false),(QVariantList{c,a}));
  QCOMPARE(state.publish({c},true),QVariantList{c});
 }

 void sharedVariantsAndSoftProtection(){
  QTemporaryDir dir;QVERIFY(dir.isValid());QDir(dir.path()).mkpath("terrain/v0.12.6/0");
  QImage tile(16,16,QImage::Format_ARGB32);tile.fill(QColor(240,60,20));
  QVERIFY(tile.save(dir.path()+"/terrain/v0.12.6/0/0-0.webp","webp"));
  QJsonArray levels;for(int i=0;i<5;++i)levels.append(QJsonObject{{"id",i},{"width",1024},{"height",1024},{"columns",1},{"rows",1},{"tileSize",1024}});
  QJsonObject manifest{{"version","0.12.6"},{"crs","EPSG:4326"},{"tileFormat","lossless WebP RGBA"},{"gutter",1},{"levels",levels}};
  auto owned=std::make_shared<TerrainTileProvider>(QJsonDocument(manifest).toJson(),dir.path());auto& provider=*owned;QVERIFY(provider.available());
  auto color=provider.loadTile(0,0,0,false);auto again=provider.loadTile(0,0,0,false);QVERIFY(!color.isNull());QCOMPARE(color.constBits(),again.constBits());
  auto gray=provider.loadTile(0,0,0,true);auto same=provider.loadTile(0,0,0,true);QCOMPARE(gray.constBits(),same.constBits());QVERIFY(gray.constBits()!=color.constBits());
  QCOMPARE(qRed(gray.pixel(0,0)),qGreen(gray.pixel(0,0)));QVERIFY(qRed(color.pixel(0,0))!=qGreen(color.pixel(0,0)));
  QCOMPARE(provider.resourceCacheSnapshot().residentCount,std::size_t(2));
  TerrainImageBridge bridge;bridge.setSource(owned);
  const auto hits=provider.resourceCacheSnapshot().hitCount;
  GeographicImageItem first,wrapped;first.setTerrainBridge(&bridge);wrapped.setTerrainBridge(&bridge);
  first.setTerrainTile({{"level",0},{"column",0},{"row",0}});
  wrapped.setTerrainTile({{"level",0},{"column",0},{"row",0}});wrapped.setWest(180);
  QVERIFY(provider.resourceCacheSnapshot().hitCount>=hits+2);

  TerrainTileSpec spec;spec.path=dir.path()+"/terrain/v0.12.6/0/0-0.webp";
  provider.protectVisible({spec},true);provider.setCacheBudget(0);
  QCOMPARE(provider.resourceCacheSnapshot().residentCount,std::size_t(1));QVERIFY(provider.cachedBytes()>0);
  first.setColorMode("gray");wrapped.setColorMode("gray");
  provider.switchVisibleVariant(false);
  const auto hitsBeforeSwitch=provider.resourceCacheSnapshot().hitCount;
  first.setColorMode("color");wrapped.setColorMode("color");
  QCOMPARE(provider.resourceCacheSnapshot().residentCount,std::size_t(1));
  QVERIFY(provider.resourceCacheSnapshot().hitCount>hitsBeforeSwitch);
  TerrainTileSpec missing;missing.level=1;missing.path=dir.path()+"/terrain/v0.12.6/1/0-0.webp";
  provider.protectVisible({missing},false);
  QCOMPARE(provider.resourceCacheSnapshot().pendingCount,std::size_t(1));
  QCOMPARE(provider.resourceCacheSnapshot().residentCount,std::size_t(1));
  QVERIFY(provider.loadTile(missing,false).isNull());
  QVERIFY(provider.resourceCacheSnapshot().failureCount>0);
  QCOMPARE(provider.resourceCacheSnapshot().residentCount,std::size_t(1));
  provider.switchVisibleVariant(true);
  const auto fallbackGray=provider.loadTile(spec,true);
  const auto sameFallbackGray=provider.loadTile(spec,true);
  QVERIFY(!fallbackGray.isNull());QCOMPARE(fallbackGray.constBits(),sameFallbackGray.constBits());
  provider.protectVisible({});QCOMPARE(provider.cachedBytes(),std::size_t(0));
 }
};
QTEST_MAIN(TerrainResourceCacheTests)
#include "terrain_resource_cache_tests.moc"

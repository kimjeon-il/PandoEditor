#include "countrylabelanchors.h"
#include <QFile>
#include <QSignalSpy>
#include <QTest>

class CountryLabelAnchorTests final : public QObject {
    Q_OBJECT
private slots:
    void deletedAndReplacedGeometryCannotKeepOrPublishAnchors() {
        CountryLabelAnchors anchors;pandoeditor::Geometry geometry;geometry.type="Polygon";
        geometry.polygons={{{{0,0},{10,0},{10,10},{0,10},{0,0}}}};
        const pandoeditor::GeometryRef first{"shape-A",1},second{"shape-B",1};
        auto token=anchors.recompute("owner",geometry,first);
        anchors.invalidateOwner("owner");
        QVERIFY(!anchors.commitDerived(token,"owner",first,pandoeditor::Point{5,5}));
        token=anchors.recompute("owner",geometry,first);
        QVERIFY(anchors.commitDerived(token,"owner",first,pandoeditor::Point{5,5}));
        auto replacement=anchors.recompute("owner",geometry,second);
        QVERIFY(!anchors.anchor("owner","").has_value());
        QVERIFY(!anchors.commitDerived(replacement,"owner",first,pandoeditor::Point{7,7}));
        QVERIFY(!anchors.commitDerived(replacement,"owner",second,std::nullopt));
        QVERIFY(!anchors.anchor("owner","").has_value());
        QCOMPARE(anchors.resourceCacheSnapshot().pendingCount,std::size_t(0));
    }

    void projectScopeRejectsSameOwnerVersionAndConsumesCompletion() {
        CountryLabelAnchors anchors;
        pandoeditor::Geometry geometry;geometry.type="Polygon";
        geometry.polygons={{{{0,0},{10,0},{10,10},{0,10},{0,0}}}};
        anchors.setProjectScope("project-A");
        const auto old=anchors.recompute("owner",geometry,1);
        anchors.setProjectScope("project-B");
        const auto fresh=anchors.recompute("owner",geometry,1);
        QVERIFY(!anchors.commitDerived(old,"owner",1,pandoeditor::Point{2,2}));
        QVERIFY(anchors.commitDerived(fresh,"owner",1,pandoeditor::Point{5,5}));
        QVERIFY(!anchors.commitDerived(fresh,"owner",1,pandoeditor::Point{7,7}));
        QCOMPARE(anchors.anchor("owner","")->x,5.0);
        anchors.setProjectScope("project-C");
        QVERIFY(!anchors.anchor("owner","").has_value());
    }

    void pinnedCorpusHasAllSourceCountries() {
        QFile file(QString::fromLatin1(PANDOEDITOR_LABEL_ANCHOR_RESOURCE));QVERIFY(file.open(QIODevice::ReadOnly));
        CountryLabelAnchors anchors(file.readAll());
        QCOMPARE(anchors.version(),QString("0.10.1"));
        QCOMPARE(anchors.method(),QString("largest-polygon-polylabel"));
        QCOMPARE(anchors.fixedCount(),258);
        const auto indonesia=anchors.fixed("IDN");QVERIFY(indonesia.has_value());
        QCOMPARE(indonesia->x,114.134873);QCOMPARE(indonesia->y,-0.949556);
        const auto bajoNuevo=anchors.fixed("BJN"),serranilla=anchors.fixed("SER");
        QVERIFY(bajoNuevo.has_value());QVERIFY(serranilla.has_value());
        QCOMPARE(bajoNuevo->x,-78.638119);QCOMPARE(bajoNuevo->y,15.864461);
        QCOMPARE(serranilla->x,-79.987866);QCOMPARE(serranilla->y,15.79501);
    }
    void derivedAnchorHandlesAntimeridian() {
        pandoeditor::Geometry geometry;geometry.type="MultiPolygon";
        geometry.polygons={{{{179,-10},{-179,-10},{-179,10},{179,10},{179,-10}}}};
        const auto anchor=CountryLabelAnchors::derive(geometry);QVERIFY(anchor.has_value());
        QVERIFY(std::abs(std::abs(anchor->x)-180)<1.0);
        QVERIFY(std::abs(anchor->y)<1.0);
    }
    void staleDerivedResultIsRejected() {
        CountryLabelAnchors anchors;
        pandoeditor::Geometry first;first.type="MultiPolygon";
        first.polygons={{{{0,0},{10,0},{10,10},{0,10},{0,0}}}};
        auto second=first;for(auto& point:second.polygons[0][0])point.x+=20;
        const auto old=anchors.recompute("owner",first,1);
        const auto current=anchors.recompute("owner",second,2);
        QVERIFY(current>old);
        QVERIFY(!anchors.commitDerived(old,"owner",1,pandoeditor::Point{5,5}));
        QVERIFY(anchors.commitDerived(current,"owner",2,pandoeditor::Point{25,5}));
        const auto point=anchors.anchor("owner","");QVERIFY(point.has_value());QCOMPARE(point->x,25.0);
    }
};

QTEST_GUILESS_MAIN(CountryLabelAnchorTests)
#include "country_label_anchor_tests.moc"

#include "countrylabelanchors.h"
#include <QFile>
#include <QSignalSpy>
#include <QTest>

class CountryLabelAnchorTests final : public QObject {
    Q_OBJECT
private slots:
    void pinnedCorpusHasAllSourceCountries() {
        QFile file(":/world/country-label-anchors-v0.10.1.json");QVERIFY(file.open(QIODevice::ReadOnly));
        CountryLabelAnchors anchors(file.readAll());
        QCOMPARE(anchors.version(),QString("0.10.1"));
        QCOMPARE(anchors.method(),QString("largest-polygon-polylabel"));
        QCOMPARE(anchors.fixedCount(),258);
        const auto indonesia=anchors.fixed("IDN");QVERIFY(indonesia.has_value());
        QCOMPARE(indonesia->x,114.134873);QCOMPARE(indonesia->y,-0.949556);
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

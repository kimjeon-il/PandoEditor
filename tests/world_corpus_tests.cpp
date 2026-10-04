#include "world_fixture_loader.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>
#include <algorithm>
#include <stdexcept>

namespace {
const QString root=QStringLiteral(M71_WORLD_FIXTURE);
QByteArray bytes(const QString& name) {
    QFile file(root+QLatin1Char('/')+name);
    if(!file.open(QIODevice::ReadOnly)) throw std::runtime_error("missing M7.1 fixture");
    return file.readAll();
}
QJsonArray features(const QString& name) {
    return QJsonDocument::fromJson(bytes(name)).object().value("features").toArray();
}
const pandoeditor::TerritorialUnit* unit(const pandoeditor::ProjectDocument& d,const std::string& id) {
    const auto it=std::find_if(d.units.begin(),d.units.end(),[&](const auto& value){return value.id==id;});
    return it==d.units.end()?nullptr:&*it;
}
const pandoeditor::GenericFeature* generic(const pandoeditor::ProjectDocument& d,const std::string& id) {
    const auto it=std::find_if(d.genericFeatures.begin(),d.genericFeatures.end(),[&](const auto& value){return value.id==id;});
    return it==d.genericFeatures.end()?nullptr:&*it;
}
// The generated GeoJSON is compact JSON.stringify output; hash the exact
// geometry bytes, rather than Qt's reordered QJsonObject serialization.
QByteArray rawGeometry(const QByteArray& fixture,const QByteArray& id) {
    const auto start=fixture.indexOf("\"id\":\""+id+"\"");
    if(start<0) throw std::runtime_error("country id absent from fixture bytes");
    const auto marker=fixture.indexOf("\"geometry\":",start);
    const auto begin=fixture.indexOf('{',marker);
    if(marker<0 || begin<0) throw std::runtime_error("geometry absent from fixture bytes");
    int depth=0;
    for(int i=begin;i<fixture.size();++i) {
        if(fixture.at(i)=='{') ++depth;
        if(fixture.at(i)=='}' && --depth==0) return fixture.mid(begin,i-begin+1);
    }
    throw std::runtime_error("unclosed geometry in fixture bytes");
}
}

class WorldCorpusTests : public QObject {
    Q_OBJECT
private slots:
    void manifestMatchesPinnedSources() {
        const auto m=m71fixture::readCorpusManifest(root);
        QCOMPARE(m.value("schema").toString(),QStringLiteral("pandoeditor-m71-world-corpus"));
        QCOMPARE(m.value("version").toInt(),1);
        QCOMPARE(m.value("pandoEditorBaseline").toString(),QStringLiteral("168fb7cf7ecde29544e9f65e10fca6a2530d1de0"));
        QCOMPARE(m.value("worldMapCommit").toString(),QStringLiteral("c0bd31d13dc8495593d78cf51f7cc195de7c9469"));
        QCOMPARE(m.value("upstreamReference").toObject().value("featureCount").toInt(),258);
        QCOMPARE(m.value("upstreamReference").toObject().value("positionCount").toInt(),548454);
        QCOMPARE(m.value("sources").toObject().value("countries").toObject().value("gitBlobSha").toString(),
                 QStringLiteral("d79abcb4a47d49f188e0601c3721d9e10e6c7f52"));
    }
    void countryIdsAndOrderAreExact() {
        const QStringList expected={"DEU","RUS","FJI","KIR","USA","FRA","IDN","PHL","ATA","ZAF","LSO","CHL","NOR"};
        QStringList actual;
        for(const auto& entry:features("countries.geojson")) actual << entry.toObject().value("id").toString();
        QCOMPARE(actual,expected);
        const auto d=m71fixture::loadWorldCorpusProject(root);
        QCOMPARE(d.units.size(),std::size_t(15));
        for(std::size_t i=0;i<13;++i) QCOMPARE(QString::fromStdString(d.units[i].id),expected[int(i)]);
    }
    void countryGeometryHashesAndCountsMatchManifest() {
        const auto m=m71fixture::readCorpusManifest(root);
        const auto d=m71fixture::loadWorldCorpusProject(root);
        const auto records=m.value("countries").toArray();
        const auto raw=bytes("countries.geojson");
        QCOMPARE(records.size(),13);
        for(const auto& value:records) {
            const auto record=value.toObject();const auto id=record.value("id").toString();
            const auto* country=unit(d,id.toStdString());QVERIFY(country);
            const auto geometry=d.geometries.get(pandoeditor::staticGeometryBinding(d,country->id).geometryRef);QVERIFY(geometry);
            const auto s=m71fixture::geometryStats(*geometry);
            QCOMPARE(QString::fromStdString(geometry->type),record.value("geometryType").toString());
            QCOMPARE(s.polygonCount,std::size_t(record.value("polygonCount").toInt()));
            QCOMPARE(s.ringCount,std::size_t(record.value("ringCount").toInt()));
            QCOMPARE(s.holeCount,std::size_t(record.value("holeCount").toInt()));
            QCOMPARE(s.coordinateCount,std::size_t(record.value("coordinateCount").toInt()));
            const auto bounds=record.value("bounds").toArray();
            QCOMPARE(s.west,bounds.at(0).toDouble());QCOMPARE(s.south,bounds.at(1).toDouble());
            QCOMPARE(s.east,bounds.at(2).toDouble());QCOMPARE(s.north,bounds.at(3).toDouble());
            QCOMPARE(s.maxLongitudeJump,record.value("maxLongitudeJump").toDouble());
            const auto sha=QCryptographicHash::hash(rawGeometry(raw,id.toUtf8()),QCryptographicHash::Sha256).toHex();
            QCOMPARE(QString::fromLatin1(sha),record.value("geometrySha256").toString());
        }
    }
    void datelineSentinelPreservesRawLongitudesAndHole() {
        const auto d=m71fixture::loadWorldCorpusProject(root);const auto* item=generic(d,"DATELINE");QVERIFY(item);
        const auto geometry=d.geometries.get(item->geometry);QVERIFY(geometry);
        const auto stats=m71fixture::geometryStats(*geometry);
        QCOMPARE(stats.polygonCount,std::size_t(1));QCOMPARE(stats.ringCount,std::size_t(2));
        QCOMPARE(stats.holeCount,std::size_t(1));QVERIFY(stats.maxLongitudeJump>180.0);
        QCOMPARE(geometry->polygons.at(0).at(0).at(0).x,179.0);
        QCOMPARE(geometry->polygons.at(0).at(0).at(1).x,-179.0);
        QCOMPARE(geometry->polygons.at(0).at(1).at(0).x,179.5);
        QCOMPARE(geometry->polygons.at(0).at(1).at(1).x,-179.5);
    }
    void polarSentinelPreservesBothHemispheres() {
        const auto d=m71fixture::loadWorldCorpusProject(root);const auto* item=generic(d,"POLAR");QVERIFY(item);
        const auto geometry=d.geometries.get(item->geometry);QVERIFY(geometry);
        QCOMPARE(geometry->polygons.size(),std::size_t(2));
        const auto s=m71fixture::geometryStats(*geometry);
        QVERIFY(s.north>=89.8);QVERIFY(s.south<=-89.8);
    }
    void southAfricaRetainsHoleAndLesothoRemainsIndependent() {
        const auto d=m71fixture::loadWorldCorpusProject(root);const auto* zaf=unit(d,"ZAF");const auto* lso=unit(d,"LSO");
        QVERIFY(zaf);QVERIFY(lso);QVERIFY(zaf!=lso);
        QVERIFY(m71fixture::geometryStats(*d.geometries.get(pandoeditor::staticGeometryBinding(d,zaf->id).geometryRef)).holeCount>=1);
        QCOMPARE(m71fixture::geometryStats(*d.geometries.get(pandoeditor::staticGeometryBinding(d,lso->id).geometryRef)).polygonCount,std::size_t(1));
    }
    void mixedCompositionValidatesAsProjectDocument() {
        const auto d=m71fixture::loadWorldCorpusProject(root);
        QCOMPARE(d.hydro.size(),std::size_t(2));QCOMPARE(d.distributionLayers.size(),std::size_t(3));
        QCOMPARE(d.distributionEntries.size(),std::size_t(3));
        QCOMPARE(d.genericFeatures.size(),std::size_t(5));QCOMPARE(d.labels.size(),std::size_t(2));
        QCOMPARE(d.geometries.versions().size(),std::size_t(27));
        const auto manual=d.presentation.webPresentation.labelSettings.find({"label","M71-LABEL-MANUAL"});
        QVERIFY(manual!=d.presentation.webPresentation.labelSettings.end());
        QVERIFY(manual->second.pinned);QVERIFY(manual->second.manualPosition.has_value());
        const auto automatic=d.presentation.webPresentation.labelSettings.find({"label","M71-LABEL-AUTO"});
        QVERIFY(automatic==d.presentation.webPresentation.labelSettings.end()||!automatic->second.pinned);
        QVERIFY(!pandoeditor::validateDocument(d).objects.empty());
    }
    void fixtureLoadDoesNotRequireNativeProjectCodec() {
        auto d=m71fixture::loadWorldCorpusProject(root);
        // The corpus loader constructs a canonical document without a wire-format marker.
        QCOMPARE(d.documentId,std::string("m71-world-rendering"));
        QVERIFY(!pandoeditor::validateDocument(d).objects.empty());
    }
};
QTEST_MAIN(WorldCorpusTests)
#include "world_corpus_tests.moc"

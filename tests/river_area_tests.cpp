#include "riverareacalculator.h"
#include <QtTest>
#include <QFile>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <future>
#include <algorithm>
#include <cmath>
#include <limits>
using namespace pandoeditor;
namespace {
Geometry decoded(const QByteArray& json) {
    const auto row=QJsonDocument::fromJson(json).object();
    Geometry result;result.type=row["type"].toString().toStdString();
    auto polygons=row["coordinates"].toArray();if(result.type=="Polygon")polygons=QJsonArray{polygons};
    for(const auto& p:polygons){Polygon polygon;for(const auto& r:p.toArray()){Ring ring;
        for(const auto& v:r.toArray()){auto xy=v.toArray();ring.push_back({xy[0].toDouble(),xy[1].toDouble()});}
        polygon.push_back(std::move(ring));}result.polygons.push_back(std::move(polygon));}return result;
}
QByteArray encoded(const Geometry& value) {
    QJsonArray polygons;for(const auto& polygon:value.polygons){QJsonArray rings;for(const auto& ring:polygon){QJsonArray points;
        for(const auto& p:ring)points.append(QJsonArray{p.x,p.y});rings.append(points);}polygons.append(rings);}
    QJsonArray lines;for(const auto& line:value.lines){QJsonArray points;for(const auto& p:line)points.append(QJsonArray{p.x,p.y});lines.append(points);}
    QJsonArray points;for(const auto& p:value.points)points.append(QJsonArray{p.x,p.y});
    return QJsonDocument(QJsonObject{{"type",QString::fromStdString(value.type)},{"polygons",polygons},{"lines",lines},{"points",points}}).toJson(QJsonDocument::Compact);
}
Geometry square() {return decoded(R"({"type":"Polygon","coordinates":[[[0,0],[0,1],[1,1],[1,0],[0,0]]]})");}
}
class RiverAreaTests:public QObject {
    Q_OBJECT
private slots:
    void init() {
        QTest::failOnWarning(QRegularExpression(".*d3\\.min\\.js.*"));
    }
    void pinnedSphericalMetrics_data() {
        QTest::addColumn<QByteArray>("input");QTest::addColumn<double>("expected");
        // Untouched pinned D3 reference values. Spherical km², with original
        // winding/hole/dateline semantics; not planar degree-area conversions.
        QTest::newRow("clockwise-square")<<QByteArray(R"({"type":"Polygon","coordinates":[[[0,0],[0,1],[1,1],[1,0],[0,0]]]})")<<12364.031909465642;
        QTest::newRow("counterclockwise-complement")<<QByteArray(R"({"type":"Polygon","coordinates":[[[0,0],[1,0],[1,1],[0,1],[0,0]]]})")<<510053516.9409623;
        QTest::newRow("opposite-winding-hole")<<QByteArray(R"({"type":"Polygon","coordinates":[[[0,0],[0,2],[2,2],[2,0],[0,0]],[[0.5,0.5],[1.5,0.5],[1.5,1.5],[0.5,1.5],[0.5,0.5]]]})")<<37089.736376091234;
        QTest::newRow("multipolygon-latitude")<<QByteArray(R"({"type":"MultiPolygon","coordinates":[[[[0,0],[0,1],[1,1],[1,0],[0,0]]],[[[10,45],[10,46],[11,46],[11,45],[10,45]]]]})")<<21030.09073797155;
        QTest::newRow("antimeridian")<<QByteArray(R"({"type":"Polygon","coordinates":[[[179,10],[179,11],[-179,11],[-179,10],[179,10]]]})")<<24316.52184605573;
        QTest::newRow("microscopic-positive")<<QByteArray(R"({"type":"Polygon","coordinates":[[[20,45],[20,45.000001],[20.000001,45.000001],[20.000001,45],[20,45]]]})")<<8.742912842156262e-9;
        QTest::newRow("empty-multipolygon")<<QByteArray(R"({"type":"MultiPolygon","coordinates":[]})")<<0.;
    }
    void pinnedSphericalMetrics() {
        QFETCH(QByteArray,input);QFETCH(double,expected);const auto geometry=decoded(input);const auto before=encoded(geometry);
        const auto result=std::async(std::launch::async,[geometry]{return calculateRiverAreaKm2(geometry);}).get();
        QVERIFY2(result.succeeded(),qPrintable(result.detail));
        // Initial values are a Node reference, independently evaluated from the
        // exact fixture, with floating-point allowance only (no geometry tolerance).
        QVERIFY2(std::abs(result.areaKm2-expected)<=std::max(1e-20,std::abs(expected)*2e-14),
            qPrintable(QString("actual %1 expected %2").arg(result.areaKm2,0,'g',17).arg(expected,0,'g',17)));
        QCOMPARE(encoded(geometry),before);
    }
    void repeatedPrivateWorkersRetainExactValuesAndInput() {
        const auto geometry=square();const auto before=encoded(geometry);
        const auto first=std::async(std::launch::async,[geometry]{return calculateRiverAreaKm2(geometry);}).get();
        QVERIFY2(first.succeeded(),qPrintable(first.detail));
        auto one=std::async(std::launch::async,[geometry]{return calculateRiverAreaKm2(geometry);});
        auto two=std::async(std::launch::async,[geometry]{return calculateRiverAreaKm2(geometry);});
        const auto a=one.get(),b=two.get();QVERIFY(a.succeeded());QVERIFY(b.succeeded());
        QCOMPARE(a.areaKm2,first.areaKm2);QCOMPARE(b.areaKm2,first.areaKm2);QCOMPARE(encoded(geometry),before);
    }
    void rejectsMalformedOrNonfiniteInput() {
        std::vector<Geometry> malformed;
        auto g=square();g.type="LineString";malformed.push_back(g);
        g=square();g.polygons.push_back(g.polygons[0]);malformed.push_back(g);
        g=square();g.polygons[0].clear();malformed.push_back(g);
        g=square();g.polygons[0][0].pop_back();malformed.push_back(g);
        g=square();g.polygons[0][0]={{0,0},{1,1},{0,0}};malformed.push_back(g);
        g=square();g.polygons[0][0][1].x=std::numeric_limits<double>::infinity();malformed.push_back(g);
        g=square();g.polygons[0][0][1].y=std::numeric_limits<double>::quiet_NaN();malformed.push_back(g);
        g=square();g.polygons[0][0][1].x=181;malformed.push_back(g);
        g=square();g.polygons[0][0][1].y=91;malformed.push_back(g);
        g=square();g.lines={{{0,0},{1,1}}};malformed.push_back(g);
        g=square();g.points={{0,0}};malformed.push_back(g);
        g=square();g.polygons.clear();malformed.push_back(g);
        for(const auto& value:malformed){const auto result=calculateRiverAreaKm2(value);
            QCOMPARE(result.status,RiverPartitionStatus::Failed);QVERIFY(!result.detail.isEmpty());QCOMPARE(result.areaKm2,0.);}
    }
    void cancellationWinsBeforeAndAfterCalculation() {
        const auto geometry=square();const auto early=calculateRiverAreaKm2(geometry,[]{return true;});
        QCOMPARE(early.status,RiverPartitionStatus::Cancelled);QVERIFY(early.detail.isEmpty());QCOMPARE(early.areaKm2,0.);
        int completeChecks=0;const auto completed=calculateRiverAreaKm2(geometry,[&]{++completeChecks;return false;});
        QVERIFY(completed.succeeded());QVERIFY(completeChecks>=4);
        int checks=0;const auto late=calculateRiverAreaKm2(geometry,[&]{return ++checks>=completeChecks;});
        QCOMPARE(late.status,RiverPartitionStatus::Cancelled);QVERIFY(late.detail.isEmpty());QCOMPARE(late.areaKm2,0.);
        auto invalid=geometry;invalid.type="invalid";
        QCOMPARE(calculateRiverAreaKm2(invalid,[]{return true;}).status,RiverPartitionStatus::Cancelled);
    }
    void runtimeLoadsByteVerifiedOriginalAndApprovedAdapter() {
        const auto result=calculateRiverAreaKm2(square());QVERIFY2(result.succeeded(),qPrintable(result.detail));
        QFile original(":/river/original/d3.min.js");QVERIFY(original.open(QIODevice::ReadOnly));const auto bytes=original.readAll();
        QCOMPARE(bytes.size(),qsizetype(151144));
        QCOMPARE(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex(),
            QByteArray("4cdf92091ed0cfdd8b862af1c6d4744bd0458e746b92c1bbd5403a1143ecd538"));
        QFile adapted(":/river/adapted/d3.min.js");QVERIFY(adapted.open(QIODevice::ReadOnly));const auto generated=adapted.readAll();
        QCOMPARE(generated.size(),qsizetype(158242));
        const auto generatedSha256=QCryptographicHash::hash(generated,QCryptographicHash::Sha256).toHex();
        QCOMPARE(generatedSha256,QByteArray("f273f409d8b1ba4c35d98fb06ca56f06726359d792ad719400d0e46a8c15cd85"));
        QFile provenance(":/river/d3-provenance.json");QVERIFY(provenance.open(QIODevice::ReadOnly));
        const auto manifest=QJsonDocument::fromJson(provenance.readAll()).object();
        QCOMPARE(manifest["sha256"].toString().toLatin1(),QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
        const auto adapter=manifest["adapter"].toObject();
        QCOMPARE(adapter["sha256"].toString().toLatin1(),generatedSha256);
        QCOMPARE(adapter["bytes"].toInt(),158242);QCOMPARE(adapter["modifiedScopeCount"].toInt(),531);
        QCOMPARE(adapter["insertedVarCount"].toInt(),2409);
        QCOMPARE(adapter["parser"].toObject()["version"].toString(),QString("8.15.0"));
    }
};
QTEST_GUILESS_MAIN(RiverAreaTests)
#include "river_area_tests.moc"

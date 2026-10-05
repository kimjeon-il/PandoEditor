#include "riverareacalculator.h"
#include "m972_metric_display_contract.h"
#include <QJSEngine>
#include <cstring>
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
QJsonObject metricSourcePins() {return {{"controller/d3.min.js","4cdf92091ed0cfdd8b862af1c6d4744bd0458e746b92c1bbd5403a1143ecd538"},{"controller/polygon-geometry.js","cc987c4076861a02a5d60720ebf536908175a9f1a605a86701ae4cf50c3a3fb5"},{"controller/app-territory-components.js","c82290d2e3601a4d07b6d94746a665bfad9a49c1020f447a0ace90740ef226d9"}};}
QString metricBits(double value) {quint64 bits;std::memcpy(&bits,&value,sizeof bits);return QString::number(bits,16).rightJustified(16,'0');}
struct SyntheticMetricEvidence {Geometry geometry;double nativeArea=0,browserArea=0;QJsonObject diagnostic;};
SyntheticMetricEvidence syntheticMetricEvidence(bool oneStepDifferent=false) {
    SyntheticMetricEvidence fixture;fixture.geometry=square();fixture.nativeArea=calculateRiverAreaKm2(fixture.geometry).areaKm2;
    QJSEngine engine;QFile source(":/river/adapted/d3.min.js");source.open(QIODevice::ReadOnly);engine.evaluate(QString::fromUtf8(source.readAll()));
    const auto input=QJsonDocument::fromJson(R"({"coordinates":[[[0,0],[0,1],[1,1],[1,0],[0,0]]],"type":"Polygon"})").object();
    auto value=engine.globalObject().property("JSON").property("parse").call({QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Compact))});
    const auto canonical=engine.globalObject().property("JSON").property("stringify").call({value}).toString().toUtf8();
    double steradians=engine.globalObject().property("d3").property("geo").property("area").call({value}).toNumber();
    if(oneStepDifferent)steradians=std::nextafter(steradians,0.);
    const double squared=engine.evaluate("6371.0088**2").toNumber();
    const auto multiply=engine.evaluate("(function(a,b){return a*b;})");fixture.browserArea=multiply.call({steradians,squared}).toNumber();
    const QJsonObject measure{{"steradians",steradians},{"radiusSquared",squared},{"productKm2",fixture.browserArea},{"clampedKm2",fixture.browserArea},{"formatted","12,364 km²"},{"float64",QJsonObject{{"steradians",metricBits(steradians)},{"radiusSquared",metricBits(squared)},{"productKm2",metricBits(fixture.browserArea)}}}};
    const auto hash=QString::fromLatin1(QCryptographicHash::hash(canonical,QCryptographicHash::Sha256).toHex());
    fixture.diagnostic={{"raw",measure},{"normalized",measure},{"inputSha256",hash},{"normalizedInputSha256",hash},{"normalizedGeometry",input},
        {"sourceIdentity",QJsonObject{{"d3Sha256",metricSourcePins()["controller/d3.min.js"]},{"normalizerSha256",metricSourcePins()["controller/polygon-geometry.js"]},{"formatterSha256",metricSourcePins()["controller/app-territory-components.js"]},{"formatterEntrypoint","app-territory-components.formatTerritoryArea"}}}};return fixture;
}

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
    void displayContractAcceptsTextWithoutRelabelingRawEquality() {
        const auto source=m972test::loadProductionMapViewSource();QVERIFY(!source.isEmpty());
        for(const bool different:{false,true}) {
            const auto fixture=syntheticMetricEvidence(different);
            const auto result=m972test::validateMetricDisplayContract(fixture.diagnostic,fixture.geometry,fixture.nativeArea,fixture.browserArea,metricSourcePins(),source);
            QVERIFY2(result.accepted,qPrintable(result.detail));QCOMPARE(result.nativeFormattedText,QString("12,364 km²"));
            QCOMPARE(result.browserFormattedText,result.nativeFormattedText);QCOMPARE(result.rawScalarEqual,fixture.nativeArea==fixture.browserArea);
            if(different){QVERIFY(!result.rawScalarEqual);QVERIFY(result.ulpDistance!="0");}
            QCOMPARE(result.toJson()["rawScalarEqual"].toBool(),result.rawScalarEqual);
        }
    }
    void displayContractRejectsRawInvalidAndForgedLargeValues() {
        const auto source=m972test::loadProductionMapViewSource();const auto fixture=syntheticMetricEvidence();QVERIFY(m972test::validateMetricDisplayContract(fixture.diagnostic,fixture.geometry,fixture.nativeArea,fixture.browserArea,metricSourcePins(),source).accepted);
        const std::vector<QJsonValue> invalid{QJsonValue(),QJsonValue::Undefined,QJsonValue("12364.031909465642"),QJsonValue(true),QJsonValue(-1.),QJsonValue(-std::numeric_limits<double>::denorm_min()),QJsonValue(std::numeric_limits<double>::quiet_NaN()),QJsonValue(std::numeric_limits<double>::infinity()),QJsonValue(-std::numeric_limits<double>::infinity()),QJsonValue(std::numeric_limits<double>::max()),QJsonValue(1e308),QJsonValue(9007199254740992.)};
        for(const auto& value:invalid){const auto badNative=m972test::validateMetricDisplayContract(fixture.diagnostic,fixture.geometry,value,fixture.browserArea,metricSourcePins(),source);QVERIFY(!badNative.accepted);QVERIFY(!badNative.detail.isEmpty());
            const auto badBrowser=m972test::validateMetricDisplayContract(fixture.diagnostic,fixture.geometry,fixture.nativeArea,value,metricSourcePins(),source);QVERIFY(!badBrowser.accepted);QVERIFY(!badBrowser.detail.isEmpty());}
    }
    void displayContractRejectsMissingStaleSourceAndBinaryEvidence() {
        const auto source=m972test::loadProductionMapViewSource();const auto fixture=syntheticMetricEvidence();QVERIFY(m972test::validateMetricDisplayContract(fixture.diagnostic,fixture.geometry,fixture.nativeArea,fixture.browserArea,metricSourcePins(),source).accepted);
        std::vector<QJsonObject> bad;
        bad.push_back({});auto diagnostic=fixture.diagnostic;diagnostic.remove("raw");bad.push_back(diagnostic);
        diagnostic=fixture.diagnostic;diagnostic.remove("normalized");bad.push_back(diagnostic);
        diagnostic=fixture.diagnostic;diagnostic.remove("sourceIdentity");bad.push_back(diagnostic);
        diagnostic=fixture.diagnostic;auto identity=diagnostic["sourceIdentity"].toObject();identity["formatterEntrypoint"]="mirrored-formatter";diagnostic["sourceIdentity"]=identity;bad.push_back(diagnostic);
        for(const auto& section:{"raw","normalized"})for(const auto& field:{"steradians","radiusSquared","productKm2","clampedKm2"}) {
            for(const QJsonValue value:{QJsonValue(),QJsonValue("0"),QJsonValue(-1.),QJsonValue(std::numeric_limits<double>::infinity())}) {
                diagnostic=fixture.diagnostic;auto row=diagnostic[section].toObject();row[field]=value;diagnostic[section]=row;bad.push_back(diagnostic);
            }
        }
        diagnostic=fixture.diagnostic;auto normalizedMeasure=diagnostic["normalized"].toObject();auto normalizedHex=normalizedMeasure["float64"].toObject();normalizedHex["productKm2"]="0000000000000000";normalizedMeasure["float64"]=normalizedHex;diagnostic["normalized"]=normalizedMeasure;bad.push_back(diagnostic);
        diagnostic=fixture.diagnostic;diagnostic["inputSha256"]="stale";bad.push_back(diagnostic);
        diagnostic=fixture.diagnostic;diagnostic["normalizedInputSha256"]="stale";bad.push_back(diagnostic);
        diagnostic=fixture.diagnostic;diagnostic["normalizedGeometry"]=QJsonObject{};bad.push_back(diagnostic);
        diagnostic=fixture.diagnostic;auto raw=diagnostic["raw"].toObject();raw["float64"]=QJsonObject{};diagnostic["raw"]=raw;bad.push_back(diagnostic);
        diagnostic=fixture.diagnostic;raw=diagnostic["raw"].toObject();raw["radiusSquared"]=1.;diagnostic["raw"]=raw;bad.push_back(diagnostic);
        diagnostic=fixture.diagnostic;raw=diagnostic["raw"].toObject();raw["productKm2"]=std::numeric_limits<double>::max();diagnostic["raw"]=raw;bad.push_back(diagnostic);
        for(const auto& value:bad)QVERIFY(!m972test::validateMetricDisplayContract(value,fixture.geometry,fixture.nativeArea,fixture.browserArea,metricSourcePins(),source).accepted);
        for(const auto& key:metricSourcePins().keys()){auto pins=metricSourcePins();pins[key]="wrong";QVERIFY(!m972test::validateMetricDisplayContract(fixture.diagnostic,fixture.geometry,fixture.nativeArea,fixture.browserArea,pins,source).accepted);}
        auto moved=fixture.geometry;moved.polygons[0][0][1].x=std::nextafter(moved.polygons[0][0][1].x,1.);
        QVERIFY(!m972test::validateMetricDisplayContract(fixture.diagnostic,moved,fixture.nativeArea,fixture.browserArea,metricSourcePins(),source).accepted);
        QCOMPARE(m972test::validateMetricDisplayContract(fixture.diagnostic,fixture.geometry,fixture.nativeArea,fixture.browserArea,metricSourcePins(),source,[]{return true;}).accepted,false);
    }
    void displayContractRejectsFormatterMutantsAndUnsupportedSource() {
        const auto source=m972test::loadProductionMapViewSource();const auto fixture=syntheticMetricEvidence();QVERIFY(m972test::validateMetricDisplayContract(fixture.diagnostic,fixture.geometry,fixture.nativeArea,fixture.browserArea,metricSourcePins(),source).accepted);
        for(const auto& text:{QString("12,364.03 km²"),QString("12364 km²"),QString("12.364 km²"),QString("12,364 m²"),QString("12,364"),QString("12,364.00 km²"),QString("0 km²")}) {
            auto diagnostic=fixture.diagnostic;auto raw=diagnostic["raw"].toObject();raw["formatted"]=text;diagnostic["raw"]=raw;
            const auto rejected=m972test::validateMetricDisplayContract(diagnostic,fixture.geometry,fixture.nativeArea,fixture.browserArea,metricSourcePins(),source);
            QVERIFY(!rejected.accepted);QVERIFY(rejected.rawScalarEqual);QCOMPARE(rejected.ulpDistance,QString("0"));
        }
        std::vector<QByteArray> mutants{QByteArray{},QByteArray("not QML")};
        auto changed=source;changed.replace("areaKm2<10 ? 2 : areaKm2<100 ? 1 : 0","2");mutants.push_back(changed);
        changed=source;changed.replace("+\" km²\"","+\" m²\"");mutants.push_back(changed);
        changed=source;changed.replace("Qt.locale(\"ko-KR\")","Qt.locale(\"de-DE\")");mutants.push_back(changed);
        for(const auto& value:mutants)QVERIFY(!m972test::validateMetricDisplayContract(fixture.diagnostic,fixture.geometry,fixture.nativeArea,fixture.browserArea,metricSourcePins(),value).accepted);
    }
    void displayContractRejectsCoherentBrowserForgeryWithCopiedLabel() {
        const auto source=m972test::loadProductionMapViewSource();const auto fixture=syntheticMetricEvidence();
        QVERIFY(m972test::validateMetricDisplayContract(fixture.diagnostic,fixture.geometry,fixture.nativeArea,fixture.browserArea,metricSourcePins(),source).accepted);
        auto diagnostic=fixture.diagnostic;QJSEngine engine;const auto multiply=engine.evaluate("(function(a,b){return a*b;})");
        for(const double fakeSteradians:{1e250,1e280,1e300}) {
            const double radius=diagnostic["raw"].toObject()["radiusSquared"].toDouble(),product=multiply.call({fakeSteradians,radius}).toNumber();QVERIFY(std::isfinite(product));
            auto fake=fixture.diagnostic;
            for(const auto& section:{"raw","normalized"}){auto row=fake[section].toObject();row["steradians"]=fakeSteradians;row["productKm2"]=product;row["clampedKm2"]=product;
                row["float64"]=QJsonObject{{"steradians",metricBits(fakeSteradians)},{"radiusSquared",metricBits(radius)},{"productKm2",metricBits(product)}};fake[section]=row;}
            const auto rejected=m972test::validateMetricDisplayContract(fake,fixture.geometry,fixture.nativeArea,product,metricSourcePins(),source);
            QVERIFY2(!rejected.accepted,"Coherent forged browser metrics with a copied native label must fail closed");
        }
    }
    void displayContractReportsCapturedJsonBitsWithoutInventingSignedZero() {
        // QJsonValue canonicalizes signed zero. Diagnostics describe captured
        // JSON values, not the unavailable pre-serialization sign bit.
        const QJsonValue positive(0.0),negative(-0.0);
        const auto result=m972test::validateMetricDisplayContract({},square(),positive,negative,metricSourcePins(),m972test::loadProductionMapViewSource());
        QVERIFY(!result.accepted);
        QCOMPARE(result.nativeFloat64,metricBits(positive.toDouble()));
        QCOMPARE(result.browserFloat64,metricBits(negative.toDouble()));
        QCOMPARE(result.rawScalarEqual,result.nativeFloat64==result.browserFloat64);
        QCOMPARE(result.ulpDistance,QString("0"));
        // The bit utility itself remains exact when raw doubles are available.
        QVERIFY(metricBits(0.0)!=metricBits(-0.0));
    }
    void actualProductionFormatterCoversPrecisionAndZeroTrimBoundaries() {
        const auto source=m972test::loadProductionMapViewSource();
        const std::vector<std::pair<double,QString>> values{{0.,"0 km²"},{1.,"1 km²"},{1.25,"1.25 km²"},{9.994,"9.99 km²"},{10.,"10 km²"},{10.1,"10.1 km²"},{99.94,"99.9 km²"},{100.,"100 km²"},{100.5,"101 km²"},{12364.,"12,364 km²"}};
        for(const auto& value:values){const auto formatted=m972test::evaluateProductionMetricFormatter(value.first,source);QVERIFY2(formatted.ok,qPrintable(formatted.detail));QCOMPARE(formatted.text,value.second);}
        auto withoutTrim=source;withoutTrim.replace(".replace(/(\\.\\d*?)0+$/, \"$1\").replace(/\\.$/, \"\")","");
        const auto changed=m972test::evaluateProductionMetricFormatter(1.,withoutTrim);QVERIFY(!changed.ok||changed.text!=QString("1 km²"));
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

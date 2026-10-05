#include "splitgeometrynormalizer.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJSEngine>
#include <QtTest>
#include <future>
#include <limits>
using namespace pandoeditor;
namespace {
Geometry geometry(const QJsonObject& object) {
    Geometry result;result.type=object["type"].toString().toStdString();
    auto polygons=object["coordinates"].toArray();
    if(result.type=="Polygon")polygons=QJsonArray{polygons};
    for(const auto& p:polygons){Polygon polygon;for(const auto& r:p.toArray()){Ring ring;
        for(const auto& point:r.toArray()){auto c=point.toArray();ring.push_back({c[0].toDouble(),c[1].toDouble()});}
        polygon.push_back(std::move(ring));}result.polygons.push_back(std::move(polygon));}
    return result;
}
QJsonObject json(const Geometry& geometry) {
    QJsonArray polygons;for(const auto& polygon:geometry.polygons){QJsonArray rings;
        for(const auto& ring:polygon){QJsonArray points;for(const auto& point:ring)points.append(QJsonArray{point.x,point.y});rings.append(points);}polygons.append(rings);}
    return {{"type",QString::fromStdString(geometry.type)},{"coordinates",geometry.type=="Polygon"?polygons[0]:QJsonValue(polygons)}};
}
Geometry crossing() {Geometry value;value.type="Polygon";value.polygons={{{{179,-2},{179,2},{-179,2},{-179,-2},{179,-2}}}};return value;}
Geometry wide() {Geometry value;value.type="Polygon";value.polygons={{{{-170,0},{-170,10},{170,10},{170,0},{-170,0}}}};return value;}
}
class SplitGeometryNormalizerTests:public QObject {
    Q_OBJECT
private slots:
    void exactApprovedSourceObservations_data() {
        QTest::addColumn<QJsonObject>("row");
        QFile file(QStringLiteral(M973_SPLIT_NORMALIZER_FIXTURE));QVERIFY(file.open(QIODevice::ReadOnly));
        const auto fixture=QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(fixture["behavioralCommit"].toString(),QString("07d3e2053c71573e11c5cf89151f5f6686038511"));
        QCOMPARE(fixture["definitions"].toArray().size(),8);
        const auto rows=fixture["cases"].toArray();QCOMPARE(rows.size(),63);
        for(const auto& value:rows){const auto row=value.toObject();QTest::newRow(qPrintable(row["id"].toString()))<<row;}
    }
    void exactApprovedSourceObservations() {
        QFETCH(QJsonObject,row);const auto input=geometry(row["input"].toObject());const auto before=json(input);
        const auto output=row["operation"].toString()=="wrap"?wrapSplitGeometry(input):normalizeSplitClippedGeometry(input);
        QVERIFY2(output.succeeded(),output.detail.c_str());QVERIFY(output.inputUnchanged);QCOMPARE(json(input),before);
        const auto expected=row["expected"];
        if(expected.isNull()||expected.toObject()["coordinates"].toArray().isEmpty()){
            QCOMPARE(output.status,GeometryOperationStatus::Empty);QVERIFY(output.geometry.polygons.empty());
        }else{QCOMPARE(output.status,GeometryOperationStatus::Completed);QCOMPARE(json(output.geometry),expected.toObject());}
    }
    void wrappedDatelineHasExactStripCoordinates() {
        const auto output=wrapSplitGeometry(crossing());QVERIFY2(output.succeeded(),output.detail.c_str());
        QCOMPARE(output.status,GeometryOperationStatus::Completed);QVERIFY(output.inputUnchanged);
        const auto expected=QJsonDocument::fromJson(R"({"type":"MultiPolygon","coordinates":[[[[179,2],[180,2],[180,-2],[179,-2],[179,2]]],[[[-180,2],[-179,2],[-179,-2],[-180,-2],[-180,2]]]]})").object();
        QCOMPARE(json(output.geometry),expected);
    }
    void clippedPlanarEdgesAreSubdividedWithoutRounding() {
        const auto output=normalizeSplitClippedGeometry(wide());QVERIFY2(output.succeeded(),output.detail.c_str());
        const auto expected=QJsonDocument::fromJson(R"({"type":"Polygon","coordinates":[[[-170,0],[-170,10],[0,10],[170,10],[170,0],[0,0],[-170,0]]]})").object();
        QCOMPARE(json(output.geometry),expected);QVERIFY(output.inputUnchanged);
        const auto wrapped=wrapSplitGeometry(output.geometry);QVERIFY(wrapped.succeeded());QCOMPARE(json(wrapped.geometry),expected);
    }
    void cancellationDiscardsAllValuesAtEveryBoundary() {
        for(const auto operation:{wrapSplitGeometry,normalizeSplitClippedGeometry}) {
            int checks=0;const auto completed=operation(crossing(),[&]{++checks;return false;});QVERIFY(completed.succeeded());QVERIFY(checks>3);
            for(int stop=1;stop<=checks;++stop){int current=0;const auto result=operation(crossing(),[&]{return ++current>=stop;});
                QCOMPARE(result.status,GeometryOperationStatus::Cancelled);QVERIFY(result.geometry.polygons.empty());QVERIFY(result.detail.empty());QVERIFY(!result.inputUnchanged);}
        }
    }
    void emptyAndInvalidInputsAreDistinct() {
        Geometry empty;for(const auto operation:{wrapSplitGeometry,normalizeSplitClippedGeometry}) {
            const auto result=operation(empty,{});QCOMPARE(result.status,GeometryOperationStatus::Empty);QVERIFY(result.succeeded());QVERIFY(result.inputUnchanged);
            auto invalid=crossing();invalid.type="LineString";QCOMPARE(operation(invalid,{}).status,GeometryOperationStatus::Failed);
            invalid=crossing();invalid.points.push_back({0,0});QCOMPARE(operation(invalid,{}).status,GeometryOperationStatus::Failed);
            invalid=crossing();invalid.polygons[0][0][0].x=std::numeric_limits<double>::infinity();const auto failed=operation(invalid,{});
            QCOMPARE(failed.status,GeometryOperationStatus::Failed);QVERIFY(!failed.detail.empty());QVERIFY(failed.geometry.polygons.empty());
            QCOMPARE(operation(invalid,[]{return true;}).status,GeometryOperationStatus::Cancelled);
        }
        Geometry degenerate;degenerate.type="Polygon";degenerate.polygons={{{{0,0},{1,1},{0,0}}}};
        QCOMPARE(normalizeSplitClippedGeometry(degenerate).status,GeometryOperationStatus::Empty);
    }
    void fullApprovedExportsAreLoadedWithoutPruning() {
        QJSEngine engine;loadApprovedSplitPolygonGeometry(engine);
        const auto api=engine.globalObject().property("PandoLabPolygonGeometry");
        for(const auto name:{"ensureClosedRing","hasCanonicalPolygonWinding","normalizePolygonGeometry","normalizeClippedPolygonGeometry","wrapPolygonGeometry","orientRing","ringDistinctCoordinateCount","ringSignedArea"})QVERIFY2(api.property(name).isCallable(),name);
    }
    void freshWorkerEnginesOwnTheirResultsAndDoNotMutateInput() {
        const auto input=crossing();const auto before=json(input);
        auto first=std::async(std::launch::async,[input]{return wrapSplitGeometry(input);});
        auto second=std::async(std::launch::async,[input]{return wrapSplitGeometry(input);});
        const auto a=first.get(),b=second.get();QVERIFY2(a.succeeded(),a.detail.c_str());QVERIFY2(b.succeeded(),b.detail.c_str());
        QCOMPARE(json(a.geometry),json(b.geometry));QCOMPARE(json(input),before);QVERIFY(a.inputUnchanged);QVERIFY(b.inputUnchanged);
    }
};
QTEST_GUILESS_MAIN(SplitGeometryNormalizerTests)
#include "split_geometry_normalizer_tests.moc"

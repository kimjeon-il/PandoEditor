#include "cutgeometrycalculator.h"
#include "territorycutadapter.h"
#include <cstring>
#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <future>
using namespace pandoeditor;
namespace {
QJsonArray corpus() {
    QFile file(QStringLiteral(M973_CUT_FIXTURE));
    if(!file.open(QIODevice::ReadOnly))return {};
    return QJsonDocument::fromJson(file.readAll()).array();
}
Geometry decodedGeometry(const QJsonObject& value) {
    Geometry geometry;geometry.type=value["type"].toString().toStdString();auto polygons=value["coordinates"].toArray();if(geometry.type=="Polygon")polygons=QJsonArray{polygons};
    for(const auto& polygon:polygons){Polygon rings;for(const auto& ring:polygon.toArray()){Ring points;for(const auto& point:ring.toArray()){const auto xy=point.toArray();points.push_back({xy[0].toDouble(),xy[1].toDouble()});}rings.push_back(std::move(points));}geometry.polygons.push_back(std::move(rings));}return geometry;
}
TerritoryCutRequest typedRequest(const QJsonObject& input) {
    TerritoryCutRequest r;r.source=decodedGeometry(input["source"].toObject());
    for(const auto& point:input["coords"].toArray()){const auto xy=point.toArray();r.coordinates.push_back({xy[0].toDouble(),xy[1].toDouble()});}
    const auto view=input["view"].toObject();r.view.mode=view["kind"].toString()=="globe"?ProjectionMode::Globe:ProjectionMode::Flat;
    r.view.scale=view["scale"].toDouble();const auto t=view["translate"].toArray(),a=view["rotate"].toArray(),c=view["center"].toArray();
    r.view.translateX=t[0].toDouble();r.view.translateY=t[1].toDouble();r.view.rotationLongitude=a[0].toDouble();r.view.rotationLatitude=a[1].toDouble();r.view.rotationRoll=a[2].toDouble();
    r.view.centerLongitude=c[0].toDouble();r.view.centerLatitude=c[1].toDouble();const auto size=view["size"].toObject();r.view.viewportWidth=size["width"].toDouble();r.view.viewportHeight=size["height"].toDouble();r.coarsePointer=view["coarsePointer"].toBool();return r;
}
bool exactGeometry(const Geometry& a,const Geometry& b) {
    if(a.type!=b.type||a.polygons.size()!=b.polygons.size())return false;
    for(std::size_t p=0;p<a.polygons.size();++p){if(a.polygons[p].size()!=b.polygons[p].size())return false;
        for(std::size_t r=0;r<a.polygons[p].size();++r){if(a.polygons[p][r].size()!=b.polygons[p][r].size())return false;
            for(std::size_t i=0;i<a.polygons[p][r].size();++i){const auto& l=a.polygons[p][r][i];const auto& v=b.polygons[p][r][i];if(std::memcmp(&l.x,&v.x,sizeof(double))||std::memcmp(&l.y,&v.y,sizeof(double)))return false;}}}
    return true;
}
QJsonObject payload() {return corpus()[0].toObject()["payload"].toObject();}
}
class CutGeometryCalculatorTests : public QObject {
    Q_OBJECT
private slots:
    void typedAdapterMatchesActualOwnedKernelResults() {
        auto cases=corpus();auto pending=payload();pending["coords"]=QJsonArray{QJsonArray{1,2}};cases.append(QJsonObject{{"payload",pending}});
        for(const auto& row:cases) {
            const auto input=row.toObject()["payload"].toObject();const auto raw=prepareCutGeometry(input);QVERIFY2(raw.succeeded(),qPrintable(raw.detail));
            const auto typed=prepareTerritoryLineCandidates(typedRequest(input));
            if(!raw.result["valid"].toBool()){QCOMPARE(typed.status,GeometryOperationStatus::Empty);QCOMPARE(typed.detail,raw.result["message"].toString().toStdString());QVERIFY(typed.candidates.empty());continue;}
            const auto candidates=raw.result["split"].toObject()["candidates"].toArray();
            QCOMPARE(typed.status,candidates.empty()?GeometryOperationStatus::Empty:GeometryOperationStatus::Completed);QCOMPARE(typed.candidates.size(),std::size_t(candidates.size()));QVERIFY(typed.detail.empty());
            for(int i=0;i<candidates.size();++i){const auto candidate=candidates[i].toObject();const auto& actual=typed.candidates[std::size_t(i)];
                QCOMPARE(actual.id,candidate["id"].toString().toStdString());QVERIFY(actual.area);QCOMPARE(*actual.area,candidate["area"].toDouble());QVERIFY(exactGeometry(actual.geometry,decodedGeometry(candidate["geometry"].toObject())));}
        }
        int checks=0;QVERIFY(prepareCutGeometry(payload(),[&]{++checks;return false;}).succeeded());
        for(int stop=1;stop<=checks;++stop){int polls=0;const auto result=prepareTerritoryLineCandidates(typedRequest(payload()),[&]{return ++polls>=stop;});
            QCOMPARE(result.status,GeometryOperationStatus::Cancelled);QCOMPARE(polls,stop);QVERIFY(result.detail.empty());QVERIFY(result.candidates.empty());}
    }

    void orderedFullWorkerResultMatchesRecordedNodeCases_data() {
        QTest::addColumn<QJsonObject>("input");QTest::addColumn<QJsonObject>("expected");
        const auto rows=corpus();QCOMPARE(rows.size(),6);
        for(const auto& value:rows){const auto row=value.toObject();QTest::newRow(qPrintable(row["id"].toString()))<<row["payload"].toObject()<<row["result"].toObject();}
    }
    void orderedFullWorkerResultMatchesRecordedNodeCases() {
        QFETCH(QJsonObject,input);QFETCH(QJsonObject,expected);const auto before=input;
        const auto output=prepareCutGeometry(input);
        QVERIFY2(output.succeeded(),qPrintable(output.detail));
        // Existing corpus is a Node regression oracle, not browser evidence.
        // Compare every field and ordered coordinate without normalization.
        QCOMPARE(output.result,expected);
        QCOMPARE(QJsonDocument::fromJson(output.json).object(),output.result);
        QVERIFY(output.inputUnchanged);QCOMPARE(input,before);
    }
    void assessmentWithoutPreviewRetainsSnapsButNoCandidates() {
        auto input=payload();input["buildPreview"]=false;
        auto expected=corpus()[0].toObject()["result"].toObject();expected.remove("split");
        const auto result=prepareCutGeometry(input);QVERIFY2(result.succeeded(),qPrintable(result.detail));
        QCOMPARE(result.result,expected);QVERIFY(result.inputUnchanged);
    }
    void pendingLinePreservesExactResult() {
        auto input=payload();input["coords"]=QJsonArray{QJsonArray{1,2}};
        const auto result=prepareCutGeometry(input);QVERIFY2(result.succeeded(),qPrintable(result.detail));
        QCOMPARE(result.result,QJsonObject({{"line",input["coords"]},{"snaps",QJsonObject{{"start",QJsonValue::Null},{"end",QJsonValue::Null}}},
            {"status","pending"},{"valid",false},{"message",""},{"issues",QJsonArray{}}}));
    }
    void cancellationDiscardsPartialValuesAtEveryBoundary() {
        int checks=0;const auto completed=prepareCutGeometry(payload(),[&]{++checks;return false;});QVERIFY(completed.succeeded());QVERIFY(checks>3);
        for(int stop=1;stop<=checks;++stop){int current=0;const auto result=prepareCutGeometry(payload(),[&]{return ++current>=stop;});
            QCOMPARE(result.status,CutGeometryStatus::Cancelled);QVERIFY(result.result.isEmpty());QVERIFY(result.json.isEmpty());QVERIFY(result.detail.isEmpty());QVERIFY(!result.inputUnchanged);}
    }
    void malformedRequestFailsAndCancellationWins() {
        const auto result=prepareCutGeometry({});QCOMPARE(result.status,CutGeometryStatus::Failed);
        QVERIFY(!result.detail.isEmpty());QVERIFY(result.result.isEmpty());QVERIFY(result.json.isEmpty());
        QCOMPARE(prepareCutGeometry({},[]{return true;}).status,CutGeometryStatus::Cancelled);
    }
    void freshWorkerEnginesProduceOwnedResults() {
        const auto input=payload();
        auto a=std::async(std::launch::async,[input]{return prepareCutGeometry(input);});
        auto b=std::async(std::launch::async,[input]{return prepareCutGeometry(input);});
        const auto first=a.get(),second=b.get();QVERIFY2(first.succeeded(),qPrintable(first.detail));QVERIFY2(second.succeeded(),qPrintable(second.detail));
        QCOMPARE(first.result,second.result);QVERIFY(first.inputUnchanged);QVERIFY(second.inputUnchanged);
    }
};
QTEST_GUILESS_MAIN(CutGeometryCalculatorTests)
#include "cut_geometry_calculator_tests.moc"

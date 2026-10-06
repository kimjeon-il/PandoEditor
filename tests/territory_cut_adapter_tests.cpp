#include "territorycutadapter.h"
#include "cutgeometrycalculator.h"
#include "commandjobrunner.h"
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSemaphore>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>

using namespace pandoeditor;
namespace {
// Link substitution observes this app transport seam without changing the real
// cut runtime or pretending a scripted response is geometric/browser evidence.
bool exact(double a,double b) {return std::memcmp(&a,&b,sizeof(double))==0;}
bool exact(const Point& a,const Point& b) {return exact(a.x,b.x)&&exact(a.y,b.y);}
bool exact(const Ring& a,const Ring& b) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)if(!exact(a[i],b[i]))return false;return true;
}
bool exact(const std::vector<Polygon>& a,const std::vector<Polygon>& b) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i){if(a[i].size()!=b[i].size())return false;for(std::size_t j=0;j<a[i].size();++j)if(!exact(a[i][j],b[i][j]))return false;}return true;
}
std::function<CutGeometryResult(const QJsonObject&,const GeometryCancellation&)> worker;
Geometry shape(bool multi=false) {
    Geometry g;g.type=multi?"MultiPolygon":"Polygon";
    g.polygons={{{{-0.0,1.125},{2.25,1.125},{2.25,3.5},{-0.0,1.125}}},{{{4,5},{6,5},{6,7},{4,5}}}};
    if(!multi)g.polygons.resize(1);return g;
}
QJsonObject jsonShape(bool multi=false) {
    const QJsonArray first{QJsonArray{QJsonArray{-0.0,1.125},QJsonArray{2.25,1.125},QJsonArray{2.25,3.5},QJsonArray{-0.0,1.125}}};
    const QJsonArray second{QJsonArray{QJsonArray{4,5},QJsonArray{6,5},QJsonArray{6,7},QJsonArray{4,5}}};
    return {{"type",multi?"MultiPolygon":"Polygon"},{"coordinates",multi?QJsonArray{first,second}:first}};
}
TerritoryCutRequest request(bool multi=false,bool globe=false,bool coarse=false) {
    TerritoryCutRequest r;r.source=shape(multi);r.coordinates={{-1.125,2.25},{8.75,-3.5}};r.coarsePointer=coarse;
    r.view.mode=globe?ProjectionMode::Globe:ProjectionMode::Flat;
    r.view.scale=123.456789;r.view.translateX=-0.0;r.view.translateY=456.75;
    r.view.rotationLongitude=12.25;r.view.rotationLatitude=-33.5;r.view.rotationRoll=9.875;
    r.view.centerLongitude=-7.125;r.view.centerLatitude=4.25;
    r.view.viewportWidth=1023.5;r.view.viewportHeight=767.25;
    r.view.devicePixelRatio=3.25;r.view.revision=42;return r;
}
QJsonObject expectedPayload(bool multi=false,bool globe=false,bool coarse=false) {
    return {{"source",jsonShape(multi)},{"coords",QJsonArray{QJsonArray{-1.125,2.25},QJsonArray{8.75,-3.5}}},
        {"view",QJsonObject{{"kind",globe?"globe":"flat"},{"scale",123.456789},{"translate",QJsonArray{-0.0,456.75}},
            {"rotate",QJsonArray{12.25,-33.5,9.875}},{"center",QJsonArray{-7.125,4.25}},
            {"size",QJsonObject{{"width",1023.5},{"height",767.25}}},
            {"snapDistance",QJsonObject{{"mouse",10},{"touch",18}}},{"coarsePointer",coarse}}},
        {"buildPreview",true}};
}
CutGeometryResult completed(QJsonObject result) {
    CutGeometryResult r;r.status=CutGeometryStatus::Completed;r.result=std::move(result);
    r.json=QJsonDocument(r.result).toJson(QJsonDocument::Compact);r.inputUnchanged=true;return r;
}
QJsonObject candidate(QString id,QJsonObject geometry,QJsonValue area=QJsonValue::Undefined) {
    QJsonObject row{{"id",id},{"geometry",geometry}};if(!area.isUndefined())row["area"]=area;return row;
}
QJsonObject validResult(QJsonArray candidates) {
    return {{"valid",true},{"status","ready"},{"message","ignored valid message"},{"split",QJsonObject{{"candidates",candidates}}}};
}
QJsonObject malformedGeometry() {
    return {{"type","Polygon"},{"coordinates",QJsonArray{QJsonArray{QJsonArray{1,2,3}}}}};
}
struct Gate {QSemaphore started,release;};
struct Release {std::shared_ptr<Gate> gate;~Release(){gate->release.release();}};
Project project() {Project p;p.replace(std::vector<Country>{{"A","A",shape().polygons,0}});return p;}
}
namespace pandoeditor {
CutGeometryResult prepareCutGeometry(const QJsonObject& payload,const GeometryCancellation& cancelled) {
    if(!worker)throw std::runtime_error("UNSCRIPTED_TEST_CUT_WORKER");return worker(payload,cancelled);
}
}
class TerritoryCutAdapterTests:public QObject {
    Q_OBJECT
private slots:
    void exactPayloadAndCancellationForwarding() {
        for(const bool multi:{false,true})for(const bool globe:{false,true})for(const bool coarse:{false,true}) {
            const auto input=request(multi,globe,coarse);const auto before=input;int calls=0,polls=0;QJsonObject observed;
            worker=[&](const QJsonObject& payload,const GeometryCancellation& cancelled) {
                ++calls;observed=payload;
                if(cancelled&&cancelled()){}return completed({{"valid",false},{"message","pending-detail"}});
            };
            const auto result=prepareTerritoryLineCandidates(input,[&]{++polls;return false;});
            QCOMPARE(observed,expectedPayload(multi,globe,coarse));QCOMPARE(calls,1);QCOMPARE(polls,1);QCOMPARE(result.status,GeometryOperationStatus::Empty);QCOMPARE(result.detail,std::string("pending-detail"));
            QCOMPARE(input.source.type,before.source.type);QVERIFY(exact(input.source.polygons,before.source.polygons));QVERIFY(exact(input.coordinates,before.coordinates));
            QCOMPARE(input.view.scale,before.view.scale);QCOMPARE(input.coarsePointer,before.coarsePointer);
        }
    }
    void pendingInvalidAndRuntimeStatusMapping() {
        for(const QString status:{QString("pending"),QString("invalid"),QString("ready")}) {
            worker=[&](const QJsonObject&,const GeometryCancellation&) {return completed({{"valid",false},{"status",status},{"message","source message"},{"split",QJsonObject{{"candidates",QJsonArray{candidate("ignored",jsonShape())}}}}});};
            const auto result=prepareTerritoryLineCandidates(request());QCOMPARE(result.status,GeometryOperationStatus::Empty);QCOMPARE(result.detail,std::string("source message"));QVERIFY(result.candidates.empty());
        }
        worker=[](const QJsonObject&,const GeometryCancellation&) {CutGeometryResult r;r.status=CutGeometryStatus::Cancelled;r.detail="ignored cancel detail";r.result=validResult({candidate("ignored",jsonShape())});return r;};
        auto result=prepareTerritoryLineCandidates(request());QCOMPARE(result.status,GeometryOperationStatus::Cancelled);QVERIFY(result.detail.empty());QVERIFY(result.candidates.empty());
        for(const QString detail:{QString{},QString("runtime exact failure")}) {
            worker=[&](const QJsonObject&,const GeometryCancellation&) {CutGeometryResult r;r.detail=detail;r.result=validResult({candidate("ignored",jsonShape())});return r;};
            result=prepareTerritoryLineCandidates(request());QCOMPARE(result.status,GeometryOperationStatus::Failed);QCOMPARE(result.detail,detail.toStdString());QVERIFY(result.candidates.empty());
        }
    }
    void candidateOrderIdentityCoordinatesAndDecoderDefaults() {
        const QJsonArray candidates{candidate("second-id",jsonShape(true),2.875),candidate("first-id",jsonShape(),QJsonValue::Null),candidate("",jsonShape(),"7"),candidate("last-id",jsonShape())};
        worker=[&](const QJsonObject&,const GeometryCancellation&) {auto result=validResult(candidates);result["status"]="pending";return completed(result);};
        const auto result=prepareTerritoryLineCandidates(request());QCOMPARE(result.status,GeometryOperationStatus::Completed);QVERIFY(result.detail.empty());QCOMPARE(result.candidates.size(),std::size_t(4));
        for(std::size_t i=0;i<4;++i) {
            const auto& c=result.candidates[i];const auto original=candidates[int(i)].toObject();
            QCOMPARE(c.id,original["id"].toString().toStdString());QCOMPARE(c.geometry.type,shape(i==0).type);auto expected=shape(i==0);for(auto& polygon:expected.polygons)for(auto& ring:polygon)for(auto& point:ring)if(point.x==0)point.x=0.0;
            // QJsonValue stores integral -0.0 as zero, exactly as the legacy decoder.
            QVERIFY(exact(c.geometry.polygons,expected.polygons));
            QVERIFY(c.area);QCOMPARE(*c.area,original["area"].toDouble());
        }
        worker=[](const QJsonObject&,const GeometryCancellation&) {return completed(validResult({}));};
        QCOMPARE(prepareTerritoryLineCandidates(request()).status,GeometryOperationStatus::Empty);
        worker=[](const QJsonObject&,const GeometryCancellation&) {return completed({{"valid",true}});};
        QCOMPARE(prepareTerritoryLineCandidates(request()).status,GeometryOperationStatus::Empty);
        const QJsonObject defaults{{"type","Polygon"},{"coordinates",QJsonArray{QJsonArray{QJsonArray{"x",true},QJsonArray{QJsonValue::Null,QJsonObject{}}}}}};
        worker=[&](const QJsonObject&,const GeometryCancellation&) {auto row=candidate("",defaults);row["id"]=17;return completed(validResult({row}));};
        const auto decoded=prepareTerritoryLineCandidates(request());QVERIFY(decoded.candidates.front().id.empty());
        QVERIFY(exact(decoded.candidates.front().geometry.polygons.front().front(),Ring({{0,0},{0,0}})));
    }
    void malformedCoordinatesAndWorkerExceptionsEscape() {
        worker=[](const QJsonObject&,const GeometryCancellation&) {return completed(validResult({candidate("bad",malformedGeometry())}));};
        bool threw=false;try {(void)prepareTerritoryLineCandidates(request());}catch(const std::runtime_error& error){threw=std::string(error.what())=="INVALID_CUT_COORDINATE";}QVERIFY(threw);
        worker=[](const QJsonObject&,const GeometryCancellation&)->CutGeometryResult {throw std::runtime_error("cut-adapter-exception");};
        threw=false;try {(void)prepareTerritoryLineCandidates(request());}catch(const std::runtime_error& error){threw=std::string(error.what())=="cut-adapter-exception";}QVERIFY(threw);
    }
    void decodeExceptionBecomesExistingGeometryJobFailure() {
        auto p=project();CommandJobRunner runner([&]()->const Project&{return p;});bool done=false;
        worker=[](const QJsonObject&,const GeometryCancellation&) {return completed(validResult({candidate("bad",malformedGeometry())}));};
        runner.submitGeometry(p.snapshot(),"typed-cut",[input=request()](const ProjectSnapshot&,const JobToken& token)->GeometryJobResult {
            return prepareTerritoryLineCandidates(input,[&]{return token.cancelled();});
        },[&](auto,auto disposition,GeometryJobResult result){
            QCOMPARE(disposition,JobDisposition::Accepted);const auto* failure=std::get_if<GeometryJobFailure>(&result);QVERIFY(failure);
            QCOMPARE(failure->error,CommandError::PrepareFailed);QCOMPARE(failure->detail,std::string("INVALID_CUT_COORDINATE"));done=true;
        });QTRY_VERIFY_WITH_TIMEOUT(done,10000);QCOMPARE(p.revision(),std::uint64_t(0));
    }
    void queuedTypedRequestOwnsCapturedInputs() {
        auto p=project();CommandJobRunner runner([&]()->const Project&{return p;});auto gate=std::make_shared<Gate>();Release release{gate};bool done=false;QJsonObject observed;
        runner.submitGeometry(p.snapshot(),"blocker",[gate](const ProjectSnapshot&,const JobToken&)->GeometryJobResult {gate->started.release();gate->release.acquire();return TerritorySelectionDraftResult{};},{});
        QTRY_VERIFY_WITH_TIMEOUT(gate->started.available(),10000);
        auto input=request(true,true,true);worker=[&](const QJsonObject& payload,const GeometryCancellation&) {observed=payload;return completed(validResult({}));};
        runner.submitGeometry(p.snapshot(),"captured",[input](const ProjectSnapshot&,const JobToken&)->GeometryJobResult {return prepareTerritoryLineCandidates(input);},[&](auto,auto disposition,GeometryJobResult result){QCOMPARE(disposition,JobDisposition::Accepted);QVERIFY(std::holds_alternative<TerritorySelectionDraftResult>(result));done=true;});
        QCOMPARE(runner.queueDepth(),std::size_t(1));input.source=shape();input.coordinates.clear();input.view={};input.coarsePointer=false;
        gate->release.release();QTRY_VERIFY_WITH_TIMEOUT(done,10000);QCOMPARE(observed,expectedPayload(true,true,true));
    }
};
QTEST_GUILESS_MAIN(TerritoryCutAdapterTests)
#include "territory_cut_adapter_tests.moc"

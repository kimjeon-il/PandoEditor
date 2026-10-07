#include "geometrycalculator.h"
#include "cutgeometrycalculator.h"
#include <QJsonArray>
#include <pandoeditor/geometrypredicates.h>
#include <QtTest>
#include <future>
#include <limits>
using namespace pandoeditor;
namespace {
Geometry box(double left,double bottom,double right,double top) {
    Geometry g;g.type="Polygon";
    g.polygons={{{{left,bottom},{right,bottom},{right,top},{left,top},{left,bottom}}}};
    return g;
}
}
class GeometryCalculatorTests : public QObject {
    Q_OBJECT
private slots:
    void overlapAndUnchangedInput() {
        const auto a=box(0,0,4,4),b=box(2,0,6,4);
        for(const auto operation:{GeometryOperation::Union,GeometryOperation::Difference,GeometryOperation::Intersection}) {
            const auto result=calculateGeometry({operation,a,b});
            QVERIFY2(result.status==GeometryOperationStatus::Completed,result.detail.c_str());
            QCOMPARE(planarArea(result.geometry),operation==GeometryOperation::Union?24.:8.);
        }
        QCOMPARE(a.polygons[0][0][1].x,4.);QCOMPARE(planarArea(a),16.);
    }
    void disjointTouchAndEmpty() {
        const auto a=box(0,0,2,2);
        const auto joined=calculateGeometry({GeometryOperation::Union,a,box(4,0,6,2)});
        QVERIFY(joined.succeeded());QCOMPARE(joined.geometry.polygons.size(),std::size_t(2));
        QCOMPARE(planarArea(joined.geometry),8.);
        for(const auto b:{box(2,0,4,2),box(4,0,6,2)})
            QVERIFY(calculateGeometry({GeometryOperation::Intersection,a,b}).status==GeometryOperationStatus::Empty);
        QVERIFY(calculateGeometry({GeometryOperation::Difference,a,a}).status==GeometryOperationStatus::Empty);
    }
    void holesAndMultiPolygon() {
        const auto ring=calculateGeometry({GeometryOperation::Difference,box(0,0,10,10),box(2,2,8,8)});
        QVERIFY2(ring.status==GeometryOperationStatus::Completed,ring.detail.c_str());
        QCOMPARE(ring.geometry.polygons[0].size(),std::size_t(2));
        QCOMPARE(planarArea(ring.geometry),64.);
        const auto joined=calculateGeometry({GeometryOperation::Union,ring.geometry,box(12,0,14,2)});
        QVERIFY(joined.succeeded());QCOMPARE(planarArea(joined.geometry),68.);
        QCOMPARE(joined.geometry.polygons.size(),std::size_t(2));
        const auto clipped=calculateGeometry({GeometryOperation::Intersection,joined.geometry,box(0,0,15,15)});
        QVERIFY(clipped.succeeded());QCOMPARE(planarArea(clipped.geometry),68.);
    }
    void multiOperandUnionAvoidsIntermediateResults() {
        GeometryOperationRequest request{GeometryOperation::Union,{},{}};
        request.operands={box(0,0,2,2),box(1,0,3,2),box(2,0,4,2)};
        const auto result=calculateGeometry(request);
        QVERIFY2(result.status==GeometryOperationStatus::Completed,result.detail.c_str());
        QCOMPARE(planarArea(result.geometry),8.);
        QCOMPARE(result.geometry.polygons.size(),std::size_t(1));
    }
    void cancellationAndInvalidInput() {
        const auto a=box(0,0,4,4);
        QVERIFY(calculateGeometry({GeometryOperation::Union,a,a},[]{return true;}).status==GeometryOperationStatus::Cancelled);
        int checkpoints=0;
        QVERIFY(calculateGeometry({GeometryOperation::Union,a,a},[&]{return ++checkpoints>=3;}).status==GeometryOperationStatus::Cancelled);
        auto invalid=a;invalid.polygons[0][0][1].x=std::numeric_limits<double>::infinity();
        const auto failed=calculateGeometry({GeometryOperation::Union,invalid,a});
        QVERIFY(failed.status==GeometryOperationStatus::Failed);QVERIFY(!failed.detail.empty());
        QVERIFY(calculateGeometry({GeometryOperation::Union,{},a}).status==GeometryOperationStatus::Failed);
    }
    void engineLivesInWorker() {
        const auto request=GeometryOperationRequest{GeometryOperation::Union,box(0,0,2,2),box(1,0,3,2)};
        auto future=std::async(std::launch::async,[request]{return calculateGeometry(request);});
        const auto result=future.get();
        QVERIFY2(result.succeeded(),result.detail.c_str());QCOMPARE(planarArea(result.geometry),6.);
    }
    void scalarPredicatesKeepStrictInputsAndCancellation() {
        const auto a=box(0,0,4,4),b=box(2,0,6,4);
        QCOMPARE(calculateGeometryArea({GeometryOperation::Intersection,a,b}).area,8.);
        QCOMPARE(calculateGeometryArea({GeometryOperation::Difference,a,a}).status,GeometryOperationStatus::Empty);
        QCOMPARE(calculateGeometryArea({GeometryOperation::Intersection,a,b},[]{return true;}).status,GeometryOperationStatus::Cancelled);
        auto bad=a;bad.polygons[0][0][1].x=std::numeric_limits<double>::infinity();
        QCOMPARE(calculateGeometryArea({GeometryOperation::Intersection,bad,b}).status,GeometryOperationStatus::Failed);
        Geometry degenerate;degenerate.polygons={{{{0,0},{1,0},{2,0},{0,0}}}};
        QCOMPARE(calculateGeometryArea({GeometryOperation::Intersection,degenerate,b}).status,GeometryOperationStatus::Failed);
        QVERIFY_EXCEPTION_THROWN((GeometryStore{}.insert({"invalid",1},degenerate)),std::invalid_argument);
    }
    void boundedTransactionPreservesResultsAndFailureIsolation() {
        const auto calculate=makeTransactionGeometryCalculator();const auto a=box(0,0,4,4),b=box(2,0,6,4);
        for(const auto operation:{GeometryOperation::Union,GeometryOperation::Difference,GeometryOperation::Intersection}) {
            const GeometryOperationRequest request{operation,a,b};const auto expected=calculateGeometry(request),actual=calculate(request,{});
            QCOMPARE(actual.status,expected.status);QCOMPARE(actual.geometry.type,expected.geometry.type);
            QCOMPARE(actual.geometry.polygons.size(),expected.geometry.polygons.size());
            for(std::size_t p=0;p<actual.geometry.polygons.size();++p) {
                QCOMPARE(actual.geometry.polygons[p].size(),expected.geometry.polygons[p].size());
                for(std::size_t r=0;r<actual.geometry.polygons[p].size();++r) {
                    QCOMPARE(actual.geometry.polygons[p][r].size(),expected.geometry.polygons[p][r].size());
                    for(std::size_t i=0;i<actual.geometry.polygons[p][r].size();++i) {
                        QCOMPARE(actual.geometry.polygons[p][r][i].x,expected.geometry.polygons[p][r][i].x);
                        QCOMPARE(actual.geometry.polygons[p][r][i].y,expected.geometry.polygons[p][r][i].y);
                    }
                }
            }
        }
        QVERIFY(calculate({GeometryOperation::Union,{},a},{}).status==GeometryOperationStatus::Failed);
        QVERIFY(calculate({GeometryOperation::Union,a,b},[]{return true;}).status==GeometryOperationStatus::Cancelled);
        const auto next=calculate({GeometryOperation::Difference,a,a},{});QCOMPARE(next.status,GeometryOperationStatus::Empty);
        const auto foreign=std::async(std::launch::async,[&]{return calculate({GeometryOperation::Union,a,b},{});}).get();
        QCOMPARE(foreign.status,GeometryOperationStatus::Failed);QCOMPARE(foreign.detail,std::string("GEOMETRY_TRANSACTION_THREAD_MISMATCH"));
        QCOMPARE(calculate({GeometryOperation::Intersection,a,b},{}).status,GeometryOperationStatus::Completed);
    }
    void cutLineReconstructsTwoExactRings() {
        const QJsonObject source{{"type","Polygon"},{"coordinates",QJsonArray{QJsonArray{QJsonArray{0,0},QJsonArray{0,10},QJsonArray{10,10},QJsonArray{10,0},QJsonArray{0,0}}}}};
        const QJsonObject view{{"kind","flat"},{"scale",500},{"translate",QJsonArray{512,384}},{"rotate",QJsonArray{0,0,0}},{"center",QJsonArray{5,5}},{"size",QJsonObject{{"width",1024},{"height",768}}},{"snapDistance",QJsonObject{{"mouse",10},{"touch",18}}},{"coarsePointer",false}};
        auto result=prepareCutGeometry({{"source",source},{"coords",QJsonArray{QJsonArray{5,-2},QJsonArray{5,12}}},{"view",view},{"buildPreview",true}});
        QVERIFY2(result.succeeded(),qPrintable(result.detail));QVERIFY(result.result["valid"].toBool());QCOMPARE(result.result["split"].toObject()["candidates"].toArray().size(),2);
        result=prepareCutGeometry({{"source",source},{"coords",QJsonArray{QJsonArray{-2,-2},QJsonArray{-1,-1}}},{"view",view},{"buildPreview",true}});
        QVERIFY(result.succeeded());QVERIFY(!result.result["valid"].toBool());
    }
};
QTEST_GUILESS_MAIN(GeometryCalculatorTests)
#include "geometry_calculator_tests.moc"

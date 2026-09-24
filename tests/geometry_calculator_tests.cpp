#include "geometrycalculator.h"
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
    void cutLineReconstructsTwoExactRings() {
        const auto source=box(0,0,10,10);const auto result=splitGeometryByLine(source,{{5,-2},{5,12}});
        QVERIFY2(result.succeeded(),result.detail.c_str());QCOMPARE(planarArea(result.candidates[0]),50.);QCOMPARE(planarArea(result.candidates[1]),50.);
        const auto combined=calculateGeometry({GeometryOperation::Union,result.candidates[0],result.candidates[1]});QVERIFY(combined.succeeded());QCOMPARE(planarArea(combined.geometry),100.);
        QVERIFY(splitGeometryByLine(source,{{-2,-2},{-1,-1}}).status==GeometryOperationStatus::Failed);
    }
};
QTEST_GUILESS_MAIN(GeometryCalculatorTests)
#include "geometry_calculator_tests.moc"

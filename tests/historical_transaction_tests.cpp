#include "historicaltransaction.h"
#include "geometrycalculator.h"
#include <pandoeditor/project.h>
#include <pandoeditor/geometrypredicates.h>
#include <QtTest>

using namespace pandoeditor;
namespace {
Geometry box(double x,double y,double w,double h) {
    Geometry g;g.type="Polygon";
    g.polygons={{{{x,y},{x+w,y},{x+w,y+h},{x,y+h},{x,y}}}};
    return g;
}
HistoricalLibrary catalog(Geometry shape) {
    HistoricalEntity e;e.libraryId="historical-country:test";e.canonicalName="Historical";
    e.instantiation.mode="territory-replacement";
    e.instantiation.countryNameUpdates={{"A","After"}};
    HistoricalGeometryVersion v;v.id="v1";v.geometry=std::move(shape);
    e.geometryVersions.push_back(std::move(v));
    return HistoricalLibrary(2,{e},{});
}
Project project() {
    Project p;ProjectDocument d({{"A","Before",box(0,0,10,10).polygons,0x223344}},{{"countries","Countries"}});
    p.replace(std::move(d));return p;
}
}
class HistoricalTransactionTests:public QObject {
    Q_OBJECT
private slots:
    void replacementAndNameAreOneUndo() {
        auto p=project();auto source=catalog(box(0,0,4,10));
        auto plan=prepareHistoricalTransaction(p.snapshot(),source,{{"historical-country:test","1945"}},calculateGeometry);
        QCOMPARE(plan.territoryReplacements.size(),std::size_t(1));
        QCOMPARE(planarArea(plan.territoryReplacements.front().geometry),60.);
        QCOMPARE(p.document().units.front().name,std::string("Before"));
        CommandArguments args;args.action=plan;
        auto prepared=CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,"historical.instantiate",args));
        QVERIFY2(prepared.ok(),prepared.detail.c_str());
        QVERIFY(CommandProcessor::confirm(p,*prepared.preview).changed());
        QCOMPARE(p.document().units.front().name,std::string("After"));
        QCOMPARE(planarArea(*p.document().geometries.get(p.document().units.front().geometry)),60.);
        QVERIFY(p.undo());
        QCOMPARE(p.document().units.size(),std::size_t(1));
        QCOMPARE(p.document().units.front().name,std::string("Before"));
        QCOMPARE(planarArea(*p.document().geometries.get(p.document().units.front().geometry)),100.);
    }
    void cancelAndErasurePreserveProject() {
        auto p=project();auto source=catalog(box(0,0,4,10));
        bool cancelled=false;
        try { (void)prepareHistoricalTransaction(p.snapshot(),source,{{"historical-country:test","1945"}},calculateGeometry,
            []{return true;}); }
        catch(const std::runtime_error&){cancelled=true;}
        QVERIFY(cancelled);QCOMPARE(p.document().units.size(),std::size_t(1));
        auto erasure=catalog(box(0,0,10,10));bool blocked=false;
        try { (void)prepareHistoricalTransaction(p.snapshot(),erasure,{{"historical-country:test","1945"}},calculateGeometry); }
        catch(const std::invalid_argument&){blocked=true;}
        QVERIFY(blocked);QCOMPARE(p.document().units.front().name,std::string("Before"));
    }
};
QTEST_MAIN(HistoricalTransactionTests)
#include "historical_transaction_tests.moc"

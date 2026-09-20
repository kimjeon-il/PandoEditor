#include "territorialgeometry.h"
#include "projectcodec.h"
#include "editorcontroller.h"
#include <pandoeditor/geometrypredicates.h>
#include <pandoeditor/presentationcommands.h>
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <array>
using namespace pandoeditor;
namespace {
Geometry square(double x,double y,double n) {Geometry g;g.polygons={{{{x,y},{x+n,y},{x+n,y+n},{x,y+n},{x,y}}}};return g;}
Geometry rectangle(double x,double y,double width,double height) {Geometry g;g.polygons={{{{x,y},{x+width,y},{x+width,y+height},{x,y+height},{x,y}}}};return g;}
ProjectDocument fixture() {
    ProjectDocument d({{"A","A",square(0,0,10).polygons,0xabcdef},{"B","B",square(20,0,10).polygons,0x123456}},{{"countries","Countries"}});
    for(const auto id:{"P","S","C"}) {
        const GeometryRef g{id,1};d.geometries.insert(g,square(id==std::string("P")?1:2,id==std::string("P")?1:2,id==std::string("P")?8:id==std::string("S")?2:1));
        d.units.push_back({id,id,"",UnitKind::Subunit,g});d.units.back().coverageMode="partition";
        d.presentation.objectStyles[territorialRef(id)]={};
        d.relations.push_back({std::string("r-")+id,territorialRef(id),territorialRef(id==std::string("P")?"A":id==std::string("S")?"P":"S"),territorialRef("A")});
    }
    // No native membership: imported web objects are valid geometry owners.
    d.presentation.membership.clear();validateDocument(d);return d;
}
double area(const Project& p,const std::string& id) {return planarArea(*p.document().geometries.get(p.document().units.at(p.index().objects.at(territorialRef(id))).geometry));}
PrepareResult prepare(Project& p,const TerritorialMutationIntent& intent) {
    auto plan=CommandProcessor::planTerritorial(p,intent);
    if(!plan.ok()){PrepareResult error;error.detail=plan.detail;return error;}
    JobScheduler jobs;const auto ticket=jobs.enqueue(p.snapshot(),"test");jobs.takeNext();
    return prepareTerritorialGeometry(p.snapshot(),*plan.plan,ticket.token());
}
}
class TerritorialGeometryTests:public QObject {
    Q_OBJECT
private slots:
    void retainedReferencesPresentationAndStale(){
        auto d=fixture();PreservedExtension e;e.id="generic";e.jsonPointer="/genericFeatures";
        e.payload=R"([{"properties":{"ownerId":"A","topologyGroup":"land:A"},"number":1e+09,"order":[3,1,2]}])";
        e.dependencyKnowledge="known";e.dependencies={territorialRef("A")};e.forbiddenEffects={"geometry","relation","convert"};d.extensions.push_back(e);
        Project p;p.replace(d);auto result=prepare(p,ConvertTerritorialTypeIntent{territorialRef("A"),UnitKind::Subunit,territorialRef("B"),territorialRef("B"),"new-A"});
        QVERIFY2(result.ok(),result.detail.c_str());QVERIFY(result.preview);
        QVERIFY(PresentationCommandProcessor::apply(p,SetPresentationVisibility{"countryFlags",false})==PresentationResult::Applied);
        QVERIFY(PresentationCommandProcessor::apply(p,SetScopedVisibility{"countries",{territorialRef("A")},false})==PresentationResult::Applied);
        QVERIFY(CommandProcessor::confirm(p,*result.preview).ok());
        QVERIFY(!itemVisible(p.document().presentation.webPresentation,"subunits","new-A"));
        const auto saved=projectcodec::encode(p);QVERIFY(saved.contains("1e+09"));QVERIFY(saved.contains("[3,1,2]"));
        const auto payload=QByteArray::fromStdString(p.document().extensions.front().payload);QVERIFY(payload.contains("land:B"));QVERIFY(payload.contains("\"ownerId\":\"B\""));
        Project reopened;reopened.replace(projectcodec::decode(saved));QCOMPARE(projectcodec::encode(reopened),saved);
        QVERIFY(p.undo());QCOMPARE(p.document().extensions.front().payload,e.payload);QVERIFY(!groupVisible(p.document().presentation.webPresentation,"countryFlags"));
        auto stale=prepare(p,TransferSubunitIntent{territorialRef("S"),territorialRef("B")});QVERIFY2(stale.ok(),stale.detail.c_str());
        QVERIFY(p.renameCountry("A","changed"));const auto before=projectcodec::encode(p);
        QVERIFY(CommandProcessor::confirm(p,*stale.preview).error==CommandError::StaleRevision);QCOMPARE(projectcodec::encode(p),before);
        auto opaque=fixture();e.jsonPointer="/future";e.dependencyKnowledge="unknown";opaque.extensions={e};p.replace(opaque);
        QVERIFY(!prepare(p,TransferSubunitIntent{territorialRef("S"),territorialRef("B")}).ok());
    }
    void transferAndConversions_data(){QTest::addColumn<int>("operation");QTest::newRow("transfer")<<0;QTest::newRow("promote")<<1;QTest::newRow("convert")<<2;}
    void transferAndConversions(){
        QFETCH(int,operation);Project p;p.replace(fixture());const auto before=projectcodec::encode(p);
        TerritorialMutationIntent intent=TransferSubunitIntent{territorialRef("S"),territorialRef("B")};
        if(operation==1)intent=ConvertTerritorialTypeIntent{territorialRef("S"),UnitKind::Country,{},{},{}};
        if(operation==2)intent=ConvertTerritorialTypeIntent{territorialRef("A"),UnitKind::Subunit,territorialRef("B"),territorialRef("B"),"new-A"};
        auto result=prepare(p,intent);QVERIFY2(result.ok(),result.detail.c_str());QVERIFY(result.preview);QCOMPARE(projectcodec::encode(p),before);
        QVERIFY(CommandProcessor::confirm(p,*result.preview).ok());
        if(operation<2){QCOMPARE(area(p,"A"),96.);QCOMPARE(area(p,"P"),60.);QCOMPARE(area(p,"S"),4.);}
        if(operation==0){QCOMPARE(area(p,"B"),104.);QVERIFY(effectiveRelation(p.document(),"C",19450101)->sovereign==territorialRef("B"));}
        if(operation==1){QVERIFY(p.document().units.at(p.index().objects.at(territorialRef("S"))).kind==UnitKind::Country);QVERIFY(effectiveRelation(p.document(),"C",19450101)->sovereign==territorialRef("S"));}
        if(operation==2){QCOMPARE(area(p,"B"),200.);QVERIFY(!p.index().objects.count(territorialRef("A")));QCOMPARE(area(p,"new-A"),100.);QVERIFY(effectiveRelation(p.document(),"P",19450101)->parent==territorialRef("new-A"));}
        const auto after=projectcodec::encode(p);Project reopened;reopened.replace(projectcodec::decode(after));QCOMPARE(projectcodec::encode(reopened),after);
        QVERIFY(p.undo());QCOMPARE(projectcodec::encode(p),before);QVERIFY(p.redo());QCOMPARE(projectcodec::encode(p),after);
    }
    void completeCountryAndCancelledJobsAreRejected(){
        Project p;auto d=fixture();d.units[2].geometry=d.units[0].geometry;p.replace(d);const auto before=projectcodec::encode(p);
        auto result=prepare(p,TransferSubunitIntent{territorialRef("P"),territorialRef("B")});QVERIFY(!result.ok());QCOMPARE(projectcodec::encode(p),before);
        const auto plan=CommandProcessor::planTerritorial(p,TransferSubunitIntent{territorialRef("S"),territorialRef("B")});QVERIFY(plan.ok());
        JobScheduler jobs;const auto job=jobs.enqueue(p.snapshot(),"cancel");jobs.takeNext();jobs.cancel(job.id());
        QVERIFY(!prepareTerritorialGeometry(p.snapshot(),*plan.plan,job.token()).ok());QCOMPARE(projectcodec::encode(p),before);
    }
    void controllerPreviewConfirmCancel_data(){QTest::addColumn<bool>("mobile");QTest::newRow("desktop")<<false;QTest::newRow("mobile")<<true;}
    void controllerPreviewConfirmCancel(){
        QFETCH(bool,mobile);QTemporaryDir dir;Project p;p.replace(fixture());QFile f(dir.filePath("map.json"));QVERIFY(f.open(QIODevice::WriteOnly));f.write(projectcodec::encode(p));f.close();
        EditorController c({mobile,dir.filePath("private.json")});QVERIFY(c.openFile(QUrl::fromLocalFile(f.fileName())));
        QVERIFY(c.selectObject({{"domain","territorial"},{"id","S"}},"replace","test"));const auto before=c.documentBytes();
        QVERIFY(c.transferSelectedSubunit("B"));c.cancelStructureMutation();QVERIFY(!c.structureDialogOpen());QCOMPARE(c.documentBytes(),before);QCOMPARE(c.selectedId(),QString("S"));
        QVERIFY(c.transferSelectedSubunit("B"));QTRY_VERIFY_WITH_TIMEOUT(!c.structureState()["calculating"].toBool(),10000);
        QVERIFY2(!c.structureState()["geometryRequired"].toBool(),qPrintable(c.structureState()["detail"].toString()));QCOMPARE(c.documentBytes(),before);
        QVERIFY(!c.structureState()["previewPaths"].toList().empty());c.cancelStructureMutation();QCOMPARE(c.documentBytes(),before);
        QVERIFY(c.transferSelectedSubunit("B"));QTRY_VERIFY_WITH_TIMEOUT(!c.structureState()["calculating"].toBool(),10000);
        QVERIFY(c.confirmStructureMutation());QVERIFY(c.documentBytes()!=before);c.undo();QCOMPARE(c.documentBytes(),before);c.redo();QVERIFY(c.documentBytes()!=before);
    }
    void directGeometryEditPreviewUndoAndCancel(){
        QTemporaryDir dir;Project p;p.replace(fixture());QFile f(dir.filePath("map.json"));QVERIFY(f.open(QIODevice::WriteOnly));f.write(projectcodec::encode(p));f.close();
        EditorController c({false,dir.filePath("private.json")});QVERIFY(c.openFile(QUrl::fromLocalFile(f.fileName())));
        QVERIFY(c.selectObject({{"domain","territorial"},{"id","S"}},"replace","test"));const auto before=c.documentBytes();
        MapProjection projection;projection.rebuild(p.document());const auto vertex=projection.project({2,2});
        QVERIFY(c.beginGeometryEdit());QVERIFY(c.geometryEditState()["active"].toBool());
        QVERIFY(c.geometryInsertNearest(vertex.x+1,vertex.y,0.1));QVERIFY(c.geometryDeleteSelectedVertex());
        QVERIFY(c.geometrySelectNearest(vertex.x,vertex.y,0.1));QVERIFY(c.geometryMoveSelectedVertex(vertex.x-0.25,vertex.y+0.25));
        QCOMPARE(c.documentBytes(),before);QVERIFY2(c.requestGeometryPreview(),qPrintable(c.geometryEditState()["error"].toString()));QVERIFY(c.geometryEditState()["previewReady"].toBool());
        QVERIFY(c.confirmGeometryEdit());QVERIFY(c.documentBytes()!=before);c.undo();QCOMPARE(c.documentBytes(),before);c.redo();QVERIFY(c.documentBytes()!=before);
        QVERIFY(c.beginGeometryEdit());c.cancelGeometryEdit();QVERIFY(!c.geometryEditState()["active"].toBool());
    }
    void directGeometryDrawCreatesOneUndoableUnit(){
        QTemporaryDir dir;Project p;p.replace(fixture());QFile f(dir.filePath("map.json"));QVERIFY(f.open(QIODevice::WriteOnly));f.write(projectcodec::encode(p));f.close();
        EditorController c({false,dir.filePath("private.json")});QVERIFY(c.openFile(QUrl::fromLocalFile(f.fileName())));const auto before=c.documentBytes();
        QVERIFY(c.beginTerritorialCreate("country"));const auto id=c.structureState()["generatedId"].toString();QVERIFY(!id.isEmpty());
        QVERIFY(c.updateTerritorialCreateSetup("새 국가","","",id));QVERIFY(c.beginGeometryDraw());
        MapProjection projection;projection.rebuild(p.document());for(const auto point:std::array<Point,3>{{{12,2},{14,2},{13,4}}}) { const auto view=projection.project(point);QVERIFY(c.geometryAddPoint(view.x,view.y)); }
        QVERIFY2(c.requestGeometryPreview(),qPrintable(c.geometryEditState()["error"].toString()));QVERIFY(c.confirmGeometryEdit());
        QVERIFY(c.documentBytes()!=before);QVERIFY(c.selectObject({{"domain","territorial"},{"id",id}},"replace","test"));
        c.undo();QCOMPARE(c.documentBytes(),before);c.redo();QVERIFY(c.documentBytes()!=before);
    }
    void siblingMergeIsAtomicAndReparentsChildren(){
        auto d=fixture();
        const GeometryRef xGeometry{"X",1},yGeometry{"Y",1},zGeometry{"Z",1};
        d.geometries.insert(xGeometry,square(2,6,2));d.geometries.insert(yGeometry,square(4,6,2));d.geometries.insert(zGeometry,square(4.25,6.25,.5));
        for(const auto& row:std::array<std::pair<const char*,GeometryRef>,3>{{{"X",xGeometry},{"Y",yGeometry},{"Z",zGeometry}}}) {
            TerritorialUnit u;u.id=row.first;u.name=row.first;u.kind=UnitKind::Subunit;u.geometry=row.second;u.coverageMode="partition";d.units.push_back(u);d.presentation.objectStyles[territorialRef(row.first)]={};
        }
        d.relations.push_back({"r-X",territorialRef("X"),territorialRef("P"),territorialRef("A")});
        d.relations.push_back({"r-Y",territorialRef("Y"),territorialRef("P"),territorialRef("A")});
        d.relations.push_back({"r-Z",territorialRef("Z"),territorialRef("Y"),territorialRef("A")});
        validateDocument(d);Project p;p.replace(d);const auto before=projectcodec::encode(p);
        auto result=prepare(p,MergeTerritorialIntent{territorialRef("X"),{territorialRef("Y")}});QVERIFY2(result.ok(),result.detail.c_str());QCOMPARE(projectcodec::encode(p),before);
        QVERIFY(CommandProcessor::confirm(p,*result.preview).ok());QVERIFY(!p.index().objects.count(territorialRef("Y")));QCOMPARE(area(p,"X"),8.);
        const auto* child=effectiveRelation(p.document(),"Z",19450101);QVERIFY(child&&child->parent==territorialRef("X"));
        const auto after=projectcodec::encode(p);QVERIFY(p.undo());QCOMPARE(projectcodec::encode(p),before);QVERIFY(p.redo());QCOMPARE(projectcodec::encode(p),after);
    }
    void drawnAnnexClipsDonorAndMovesContainedChild(){
        auto d=fixture();const GeometryRef xg{"AX",1},yg{"AY",1},zg{"AZ",1};d.geometries.insert(xg,square(2,6,2));d.geometries.insert(yg,square(4,6,2));d.geometries.insert(zg,square(4.2,6.2,.4));
        for(const auto& row:std::array<std::pair<const char*,GeometryRef>,3>{{{"AX",xg},{"AY",yg},{"AZ",zg}}}){TerritorialUnit u;u.id=row.first;u.name=row.first;u.kind=UnitKind::Subunit;u.geometry=row.second;u.coverageMode="partition";d.units.push_back(u);d.presentation.objectStyles[territorialRef(row.first)]={};}
        d.relations.push_back({"r-AX",territorialRef("AX"),territorialRef("P"),territorialRef("A")});d.relations.push_back({"r-AY",territorialRef("AY"),territorialRef("P"),territorialRef("A")});d.relations.push_back({"r-AZ",territorialRef("AZ"),territorialRef("AY"),territorialRef("A")});validateDocument(d);
        Project p;p.replace(d);const auto before=projectcodec::encode(p);auto result=prepare(p,AnnexTerritoryIntent{territorialRef("AX"),{territorialRef("AY")},square(4,6,1)});QVERIFY2(result.ok(),result.detail.c_str());QVERIFY(CommandProcessor::confirm(p,*result.preview).ok());
        QCOMPARE(area(p,"AX"),5.);QCOMPARE(area(p,"AY"),3.);const auto* child=effectiveRelation(p.document(),"AZ",19450101);QVERIFY(child&&child->parent==territorialRef("AX"));QVERIFY(p.undo());QCOMPARE(projectcodec::encode(p),before);
    }
    void cutSplitCreatesOneUndoableSibling(){
        Project p;p.replace(fixture());const auto before=projectcodec::encode(p);auto result=prepare(p,SplitTerritorialIntent{territorialRef("B"),{{25,-2},{25,12}},0,"B-east","B East"});QVERIFY2(result.ok(),result.detail.c_str());QCOMPARE(projectcodec::encode(p),before);
        QVERIFY(CommandProcessor::confirm(p,*result.preview).ok());QCOMPARE(area(p,"B"),50.);QCOMPARE(area(p,"B-east"),50.);QVERIFY(p.document().units.at(p.index().objects.at(territorialRef("B-east"))).kind==UnitKind::Country);
        const auto after=projectcodec::encode(p);Project reopened;reopened.replace(projectcodec::decode(after));QCOMPARE(projectcodec::encode(reopened),after);QVERIFY(p.undo());QCOMPARE(projectcodec::encode(p),before);QVERIFY(p.redo());QCOMPARE(projectcodec::encode(p),after);
    }
    void sharedBoundaryPreservesOuterUnionAndUpdatesBothOwners(){
        auto document=fixture();auto b=std::find_if(document.units.begin(),document.units.end(),[](const auto& unit){return unit.id=="B";});QVERIFY(b!=document.units.end());GeometryRef next=b->geometry;++next.version;document.geometries.insert(next,square(10,0,10));b->geometry=next;validateDocument(document);Project project;project.replace(document);const auto before=projectcodec::encode(project);
        SharedBoundaryIntent intent{{{territorialRef("A"),rectangle(0,0,12,10)},{territorialRef("B"),rectangle(12,0,8,10)}}};auto result=prepare(project,intent);QVERIFY2(result.ok(),result.detail.c_str());QCOMPARE(projectcodec::encode(project),before);QVERIFY(CommandProcessor::confirm(project,*result.preview).ok());QCOMPARE(area(project,"A"),120.);QCOMPARE(area(project,"B"),80.);QVERIFY(project.undo());QCOMPARE(projectcodec::encode(project),before);
        auto invalid=prepare(project,SharedBoundaryIntent{{{territorialRef("A"),rectangle(0,0,13,10)},{territorialRef("B"),rectangle(12,0,8,10)}}});QVERIFY(!invalid.ok());QCOMPARE(projectcodec::encode(project),before);
    }
    void countryCoastClipsChildrenAtomically(){
        Project project;project.replace(fixture());const auto before=projectcodec::encode(project);auto result=prepare(project,CoastlineIntent{territorialRef("A"),square(0,0,7),CoastlineAuthority::Country});QVERIFY2(result.ok(),result.detail.c_str());QCOMPARE(projectcodec::encode(project),before);QVERIFY(CommandProcessor::confirm(project,*result.preview).ok());QCOMPARE(area(project,"A"),49.);QCOMPARE(area(project,"P"),36.);QVERIFY(project.undo());QCOMPARE(projectcodec::encode(project),before);
    }
};
QTEST_MAIN(TerritorialGeometryTests)
#include "territorial_geometry_tests.moc"

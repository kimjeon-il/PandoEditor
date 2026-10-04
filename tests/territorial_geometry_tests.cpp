#include "territorial_fixture.h"
#include "territorialgeometry.h"
#include "projectcodec.h"
#include "editorcontroller.h"
#include "retainedreferencerewriter.h"
#include <pandoeditor/geometrypredicates.h>
#include <pandoeditor/map/projectionengine.h>
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
        appendTerritory(d,{id,id,"",UnitKind::General,false},g);staticParentRelation(d,d.units.back().id).coverageMode="partition";
        d.presentation.objectStyles[territorialRef(id)]={};
        setFixtureParent(d,territorialRef(id),territorialRef(id==std::string("P")?"A":id==std::string("S")?"P":"S"));
    }
    // No native membership: imported web objects are valid geometry owners.
    d.presentation.membership.clear();validateDocument(d);return d;
}
double area(const Project& p,const std::string& id) {return planarArea(*p.document().geometries.get(staticGeometryBinding(p.document(),p.document().units.at(p.index().objects.at(territorialRef(id))).id).geometryRef));}
PrepareResult prepare(Project& p,const TerritorialMutationIntent& intent) {
    auto plan=CommandProcessor::planTerritorial(p,intent);
    if(!plan.ok()){PrepareResult error;error.detail=plan.detail;return error;}
    if(plan.plan->geometry.kind!=GeometryRequirementKind::WorkerPatch){CommandArguments args;args.action=ApplyTerritorialMutation{*plan.plan,{}};return CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,"territorial.relation.parent",args),[](const ProjectDocument& before,const TerritorialMutationPlan& mutation,std::vector<PreservedExtension>& candidate){auto result=retainedrefs::rewrite(before,mutation,candidate);return ExtensionRewriteResult{result.ok,result.detail,result.handledExtensionIds};});}
    JobScheduler jobs;const auto ticket=jobs.enqueue(p.snapshot(),"test");jobs.takeNext();
    return prepareTerritorialGeometry(p.snapshot(),*plan.plan,ticket.token());
}
}
class TerritorialGeometryTests:public QObject {
    Q_OBJECT
private slots:
    void fixedTargetProviderSelectionAndBack() {
        QTemporaryDir dir;Project source;source.replace(ProjectDocument({{"A","A",square(0,0,10).polygons,0xabcdef},{"B","B",square(10,0,10).polygons,0x123456}},{{"countries","Countries"}}));
        QFile file(dir.filePath("input.json"));QVERIFY(file.open(QIODevice::WriteOnly));file.write(projectcodec::encode(source));file.close();
        EditorController c({false,dir.filePath("private.json")});QVERIFY(c.openFile(QUrl::fromLocalFile(file.fileName())));c.selectCountry("A");
        const auto before=c.documentBytes();QVERIFY(c.beginMergeSelection());
        QCOMPARE(c.geometryEditState().value("target").toMap().value("id").toString(),QStringLiteral("A"));
        QVERIFY(!c.geometryToggleProvider({{"domain","territorial"},{"id","A"}}));
        QVERIFY(c.geometryToggleProvider({{"domain","territorial"},{"id","B"}}));
        QCOMPARE(c.primaryObject().value("id").toString(),QStringLiteral("A"));
        QVERIFY(c.requestGeometryPreview());QVERIFY(c.geometryBack());
        QVERIFY(!c.geometryEditState().value("calculating").toBool());QVERIFY(!c.geometryEditState().value("previewReady").toBool());QCOMPARE(c.documentBytes(),before);
        QVERIFY(c.requestGeometryPreview());QTRY_VERIFY_WITH_TIMEOUT(c.geometryEditState().value("previewReady").toBool(),5000);
        QCOMPARE(c.documentBytes(),before);QVERIFY(!c.geometryDraftPaths().empty());QVERIFY(c.geometryBack());
        QCOMPARE(c.geometryEditState().value("providers").toList().size(),1);QVERIFY(c.geometryBack());
        QCOMPARE(c.geometryEditState().value("stage").toString(),QStringLiteral("setup"));QVERIFY(c.geometryAdvanceStage());
        QVERIFY(c.requestGeometryPreview());QTRY_VERIFY_WITH_TIMEOUT(c.geometryEditState().value("previewReady").toBool(),5000);QVERIFY(c.confirmGeometryEdit());
        QCOMPARE(projectcodec::decode(c.documentBytes()).units.size(),std::size_t(1));c.undo();QCOMPARE(c.documentBytes(),before);c.redo();
    }
    void annexBackPreservesProvidersAndDrawing() {
        QTemporaryDir dir;Project source;source.replace(ProjectDocument({{"A","A",square(0,0,10).polygons,0xabcdef},{"B","B",square(10,0,10).polygons,0x123456}},{{"countries","Countries"}}));
        QFile file(dir.filePath("input.json"));QVERIFY(file.open(QIODevice::WriteOnly));file.write(projectcodec::encode(source));file.close();
        EditorController c({false,dir.filePath("private.json")});QVERIFY(c.openFile(QUrl::fromLocalFile(file.fileName())));c.selectCountry("A");const auto before=c.documentBytes();
        QVERIFY(c.beginAnnexGeometry());QVERIFY(c.geometryToggleProvider({{"domain","territorial"},{"id","B"}}));QVERIFY(c.geometryAdvanceStage());
        MapProjection projection;projection.rebuild(source.document());
        for(const auto point:{Point{10,2},Point{12,2},Point{12,4},Point{10,4}}){const auto xy=projection.project(point);QVERIFY(c.geometryAddPoint(xy.x,xy.y,0));}
        const auto drawing=c.geometryDraftPaths();QVERIFY(c.geometryBack());
        QVERIFY(c.geometryEditState().value("choosingProviders").toBool());QCOMPARE(c.geometryDraftPaths(),drawing);
        QCOMPARE(c.geometryEditState().value("providers").toList().size(),1);QCOMPARE(c.primaryObject().value("id").toString(),QStringLiteral("A"));
        QVERIFY(c.geometryAdvanceStage());QVERIFY(c.requestGeometryPreview());QTRY_VERIFY_WITH_TIMEOUT(c.geometryEditState().value("previewReady").toBool(),5000);
        QCOMPARE(c.documentBytes(),before);QVERIFY(c.geometryBack());QCOMPARE(c.geometryDraftPaths(),drawing);
        QVERIFY(c.requestGeometryPreview());QTRY_VERIFY_WITH_TIMEOUT(c.geometryEditState().value("previewReady").toBool(),5000);QVERIFY(c.confirmGeometryEdit());
        Project after;after.replace(projectcodec::decode(c.documentBytes()));QCOMPARE(area(after,"A"),104.);QCOMPARE(area(after,"B"),96.);c.undo();QCOMPARE(c.documentBytes(),before);
    }
    void oversizedDrawnAnnexPreprocessesBeforePreview_data() {
        QTest::addColumn<bool>("multiDonor");
        QTest::addColumn<bool>("remoteDonor");
        QTest::newRow("single donor") << false << false;
        QTest::newRow("multiple donors") << true << false;
        QTest::newRow("selected remote donor") << true << true;
    }
    void oversizedDrawnAnnexPreprocessesBeforePreview() {
        QFETCH(bool,multiDonor);QFETCH(bool,remoteDonor);
        QTemporaryDir dir;
        ProjectDocument document({{"A","A",square(0,0,10).polygons,0xabcdef},{"B","B",square(10,0,10).polygons,0x123456}},{{"countries","Countries"}});
        if(multiDonor) {
            const GeometryRef ref{"C",1};document.geometries.insert(ref,square(remoteDonor?40:20,0,10));
            appendTerritory(document,{"C","C","",UnitKind::General,false},ref);document.presentation.objectStyles[territorialRef("C")]={};
        }
        Project source;source.replace(document);
        QFile file(dir.filePath("input.json"));QVERIFY(file.open(QIODevice::WriteOnly));file.write(projectcodec::encode(source));file.close();
        EditorController c({false,dir.filePath("private.json")});QVERIFY(c.openFile(QUrl::fromLocalFile(file.fileName())));c.selectCountry("A");
        const auto before=c.documentBytes();
        QVERIFY(c.beginAnnexGeometry());QVERIFY(c.geometryToggleProvider({{"domain","territorial"},{"id","B"}}));
        if(multiDonor)QVERIFY(c.geometryToggleProvider({{"domain","territorial"},{"id","C"}}));
        QVERIFY(c.geometryAdvanceStage());MapProjection projection;projection.rebuild(document);
        const double right=multiDonor&&!remoteDonor?35.:25.;
        for(const auto point:{Point{5,-5},Point{right,-5},Point{right,5},Point{5,5}}) {
            const auto xy=projection.project(point);QVERIFY(c.geometryAddPoint(xy.x,xy.y,0));
        }
        QVERIFY(c.requestGeometryPreview());
        c.cancelGeometryEdit();QCOMPARE(c.documentBytes(),before);
        QVERIFY(c.beginAnnexGeometry());QVERIFY(c.geometryToggleProvider({{"domain","territorial"},{"id","B"}}));
        if(multiDonor)QVERIFY(c.geometryToggleProvider({{"domain","territorial"},{"id","C"}}));
        QVERIFY(c.geometryAdvanceStage());
        for(const auto point:{Point{5,-5},Point{right,-5},Point{right,5},Point{5,5}}) {
            const auto xy=projection.project(point);QVERIFY(c.geometryAddPoint(xy.x,xy.y,0));
        }
        QVERIFY(c.requestGeometryPreview());
        QTRY_VERIFY_WITH_TIMEOUT(!c.geometryEditState().value("calculating").toBool(),10000);
        QVERIFY2(c.geometryEditState().value("previewReady").toBool(),qPrintable(c.geometryEditState().value("error").toString()));
        QCOMPARE(c.documentBytes(),before);
        c.cancelGeometryEdit();QCOMPARE(c.documentBytes(),before);
        // The same oversized drawing is usable after cancellation; no stale job is committed.
        QVERIFY(c.beginAnnexGeometry());QVERIFY(c.geometryToggleProvider({{"domain","territorial"},{"id","B"}}));
        if(multiDonor)QVERIFY(c.geometryToggleProvider({{"domain","territorial"},{"id","C"}}));
        QVERIFY(c.geometryAdvanceStage());
        for(const auto point:{Point{5,-5},Point{right,-5},Point{right,5},Point{5,5}}) {
            const auto xy=projection.project(point);QVERIFY(c.geometryAddPoint(xy.x,xy.y,0));
        }
        QVERIFY(c.requestGeometryPreview());QTRY_VERIFY_WITH_TIMEOUT(!c.geometryEditState().value("calculating").toBool(),10000);
        QVERIFY2(c.geometryEditState().value("previewReady").toBool(),qPrintable(c.geometryEditState().value("error").toString()));
        QVERIFY(c.confirmGeometryEdit());Project after;after.replace(projectcodec::decode(c.documentBytes()));
        QCOMPARE(area(after,"A"),multiDonor&&!remoteDonor?200.:150.);QCOMPARE(area(after,"B"),50.);
        if(multiDonor)QCOMPARE(area(after,"C"),remoteDonor?100.:50.);
        const auto committed=c.documentBytes();c.undo();QCOMPARE(c.documentBytes(),before);c.redo();QCOMPARE(c.documentBytes(),committed);
    }
    void cancelledDrawnPreprocessingNeverProducesPreview() {
        Project p;p.replace(ProjectDocument({{"A","A",square(0,0,10).polygons,0xabcdef},{"B","B",square(10,0,10).polygons,0x123456}},{{"countries","Countries"}}));
        const auto before=projectcodec::encode(p);JobScheduler jobs;const auto job=jobs.enqueue(p.snapshot(),"drawn-annex");jobs.takeNext();jobs.cancel(job.id());
        const auto result=prepareDrawnTerritoryAnnex(p.snapshot(),{territorialRef("A"),{territorialRef("B")},square(5,-5,20)},job.token());
        QVERIFY(!result.ok());QVERIFY(!result.preview);QCOMPARE(result.detail,std::string("CANCELLED"));QCOMPARE(projectcodec::encode(p),before);
    }
    void rootAnnexClipsDependentsInsteadOfReparenting() {
        ProjectDocument d({{"A","A",square(0,0,10).polygons,0xabcdef},{"B","B",square(10,0,10).polygons,0x123456}},{{"countries","Countries"}});
        d.geometries.insert({"child",1},square(11,1,2));
        appendTerritory(d,{"child","child","",UnitKind::General,false},{"child",1});d.presentation.objectStyles[territorialRef("child")]={};setFixtureParent(d,territorialRef("child"),territorialRef("B"));
        Project p;p.replace(d);const auto before=projectcodec::encode(p);
        auto prepared=prepare(p,AnnexTerritoryIntent{territorialRef("A"),{territorialRef("B")},square(10,0,5)});
        QVERIFY2(prepared.ok(),prepared.detail.c_str());QCOMPARE(projectcodec::encode(p),before);
        QVERIFY(CommandProcessor::confirm(p,*prepared.preview).ok());
        QVERIFY(!p.index().objects.count(territorialRef("child")));
        QCOMPARE(area(p,"A"),125.);QCOMPARE(area(p,"B"),75.);
        const auto after=projectcodec::encode(p);QVERIFY(p.undo());QCOMPARE(projectcodec::encode(p),before);QVERIFY(p.redo());QCOMPARE(projectcodec::encode(p),after);
    }
    void rootAnnexClipsPartialAndNestedDependents() {
        ProjectDocument d({{"A","A",square(0,0,10).polygons,0xabcdef},{"B","B",square(10,0,10).polygons,0x123456}},{{"countries","Countries"}});
        const auto add=[&](const std::string& id,const std::string& parent,Geometry geometry) {
            const GeometryRef ref{id,1};d.geometries.insert(ref,geometry);appendTerritory(d,{id,id,"",UnitKind::General,false},ref);
            setFixtureParent(d,territorialRef(id),territorialRef(parent));d.presentation.objectStyles[territorialRef(id)]={};
        };
        add("child","B",square(11,1,5));add("removed-grandchild","child",square(12,2,1));add("retained-grandchild","child",square(15,5,1));
        Project p;p.replace(d);const auto before=projectcodec::encode(p);
        auto prepared=prepare(p,AnnexTerritoryIntent{territorialRef("A"),{territorialRef("B")},square(10,0,5)});
        QVERIFY2(prepared.ok(),prepared.detail.c_str());QVERIFY(CommandProcessor::confirm(p,*prepared.preview).ok());
        QCOMPARE(area(p,"child"),9.);QCOMPARE(staticParentRelation(p.document(),"child").parentId,std::string("B"));
        QVERIFY(!p.index().objects.count(territorialRef("removed-grandchild")));QCOMPARE(area(p,"retained-grandchild"),1.);
        QCOMPARE(staticParentRelation(p.document(),"retained-grandchild").parentId,std::string("child"));
        const auto after=projectcodec::encode(p);QVERIFY(p.undo());QCOMPARE(projectcodec::encode(p),before);QVERIFY(p.redo());QCOMPARE(projectcodec::encode(p),after);
    }
    void rootAnnexRejectsDanglingDistributionReference_data() {
        QTest::addColumn<QString>("referenceKind");
        QTest::newRow("distribution") << QString("distribution");
        QTest::newRow("removed-child-distribution") << QString("child-distribution");
        QTest::newRow("label") << QString("label");
        QTest::newRow("label-settings") << QString("label-settings");
    }
    void rootAnnexRejectsDanglingDistributionReference() {
        QFETCH(QString,referenceKind);
        ProjectDocument d({{"A","A",square(0,0,10).polygons,0xabcdef},{"B","B",square(10,0,10).polygons,0x123456}},{{"countries","Countries"}});
        if(referenceKind=="child-distribution") {
            d.geometries.insert({"child",1},square(11,1,2));appendTerritory(d,{"child","child","",UnitKind::General,false},{"child",1});
            setFixtureParent(d,territorialRef("child"),territorialRef("B"));d.presentation.objectStyles[territorialRef("child")]={};
        }
        if(referenceKind=="distribution"||referenceKind=="child-distribution") {
            DistributionLayer layer;layer.id="statistics";layer.name="Statistics";d.distributionLayers.push_back(layer);
            DistributionEntry entry;entry.id="entry-b";entry.layerId=layer.id;entry.territory=territorialRef(referenceKind=="child-distribution"?"child":"B");entry.value=1;d.distributionEntries.push_back(entry);
        } else if(referenceKind=="label") {
            Geometry point;point.type="Point";point.points={{15,5}};d.geometries.insert({"label-b",1},point);
            PlaceLabel label;label.id="label-b";label.name="B label";label.geometry={"label-b",1};label.territory=territorialRef("B");d.labels.push_back(label);
        } else d.presentation.webPresentation.labelSettings[territorialRef("B")]={};
        Project p;p.replace(d);const auto before=projectcodec::encode(p);
        auto prepared=prepare(p,AnnexTerritoryIntent{territorialRef("A"),{territorialRef("B")},square(10,0,10)});
        QVERIFY(!prepared.ok());QCOMPARE(projectcodec::encode(p),before);
        QVERIFY(!p.undo());
    }
    void providerPickingIgnoresCoveringContent() {
        QTemporaryDir dir;ProjectDocument document({{"A","A",square(0,0,10).polygons,0xabcdef},{"B","B",square(10,0,10).polygons,0x123456}},{{"countries","Countries"}});
        Geometry point;point.type="Point";point.points={{15,5}};document.geometries.insert({"label",1},point);
        PlaceLabel label;label.id="label";label.name="covering";label.geometry={"label",1};document.labels.push_back(label);
        DistributionLayer layer;layer.id="covering";layer.name="Covering distribution";document.distributionLayers.push_back(layer);
        DistributionEntry entry;entry.id="covering-entry";entry.layerId=layer.id;entry.geometry=staticGeometryBinding(document,document.units[1].id).geometryRef;entry.value=10;document.distributionEntries.push_back(entry);
        document.presentation.membership.clear(); // Current web documents have no legacy native-layer membership.
        Project source;source.replace(document);
        QFile file(dir.filePath("input.json"));QVERIFY(file.open(QIODevice::WriteOnly));file.write(projectcodec::encode(source));file.close();
        EditorController c({false,dir.filePath("private.json")});QVERIFY(c.openFile(QUrl::fromLocalFile(file.fileName())));QVERIFY(c.setProjectionMode("flat"));
        QVERIFY(c.publishMapView({{"viewportWidth",800.},{"viewportHeight",600.},{"scale",200.},{"translateX",400.},{"translateY",300.},{"centerLongitude",15.},{"centerLatitude",5.}}));
        const auto screen=projectPoint({15,5},static_cast<MapSceneBridge*>(c.mapSceneBridge())->viewState());QVERIFY(screen.finite);
        const auto normal=c.pickObjectScreen(screen.x,screen.y,1).value("domain").toString();
        QVERIFY2(normal=="label"||normal=="distributionLayer",qPrintable(normal));
        c.selectCountry("A");QVERIFY(c.beginMergeSelection());
        const auto provider=c.pickObjectScreen(screen.x,screen.y,1);QCOMPARE(provider.value("id").toString(),QStringLiteral("B"));QVERIFY(c.geometryToggleProvider(provider));
        QCOMPARE(c.primaryObject().value("id").toString(),QStringLiteral("A"));
    }
    void splitDefaultsToSmallerResultAndCanChooseOtherResult() {
        QTemporaryDir dir;Project source;source.replace(ProjectDocument({{"A","A",square(0,0,10).polygons,0xabcdef}},{{"countries","Countries"}}));
        QFile file(dir.filePath("input.json"));QVERIFY(file.open(QIODevice::WriteOnly));file.write(projectcodec::encode(source));file.close();
        EditorController c({false,dir.filePath("private.json")});QVERIFY(c.openFile(QUrl::fromLocalFile(file.fileName())));c.selectCountry("A");const auto before=c.documentBytes();
        MapProjection projection;projection.rebuild(source.document());QVERIFY(c.beginSplitGeometry());
        for(const auto point:{Point{2,-1},Point{2,11}}){const auto xy=projection.project(point);QVERIFY(c.geometryAddPoint(xy.x,xy.y,0));}
        QVERIFY(c.requestGeometryPreview());QTRY_VERIFY_WITH_TIMEOUT(c.geometryEditState().value("previewReady").toBool(),5000);
        QVERIFY(c.geometryChooseSplitResult(0));QTRY_VERIFY_WITH_TIMEOUT(c.geometryEditState().value("previewReady").toBool(),5000);
        QVERIFY(c.geometryChooseSplitResult(-1));QTRY_VERIFY_WITH_TIMEOUT(c.geometryEditState().value("previewReady").toBool(),5000);
        QVERIFY(c.confirmGeometryEdit());const auto doc=projectcodec::decode(c.documentBytes());
        const auto created=std::find_if(doc.units.begin(),doc.units.end(),[](const auto& u){return u.id!="A";});QVERIFY(created!=doc.units.end());
        QCOMPARE(planarArea(*doc.geometries.get(staticGeometryBinding(doc,created->id).geometryRef)),20.);c.undo();QCOMPARE(c.documentBytes(),before);
        std::array<double,2> chosenAreas{};
        for(int choice=0;choice<2;++choice) {
            c.selectCountry("A");QVERIFY(c.beginSplitGeometry());
            for(const auto point:{Point{2,-1},Point{2,11}}){const auto xy=projection.project(point);QVERIFY(c.geometryAddPoint(xy.x,xy.y,0));}
            QVERIFY(c.geometryChooseSplitResult(choice));QVERIFY(c.requestGeometryPreview());
            QTRY_VERIFY_WITH_TIMEOUT(c.geometryEditState().value("previewReady").toBool(),5000);QVERIFY(c.confirmGeometryEdit());
            const auto chosen=projectcodec::decode(c.documentBytes());
            const auto unit=std::find_if(chosen.units.begin(),chosen.units.end(),[](const auto& u){return u.id!="A";});QVERIFY(unit!=chosen.units.end());
            chosenAreas[choice]=planarArea(*chosen.geometries.get(staticGeometryBinding(chosen,unit->id).geometryRef));c.undo();QCOMPARE(c.documentBytes(),before);
        }
        QVERIFY(chosenAreas[0]!=chosenAreas[1]);QCOMPARE(chosenAreas[0]+chosenAreas[1],100.);
    }
    void metadataFieldsCommitAndUndoIndependently() {
        QTemporaryDir dir;auto d=fixture();Geometry point;point.type="Point";point.points={{1,1}};d.geometries.insert({"city",1},point);
        PlaceLabel label;label.id="city";label.name="old";label.notes="old notes";label.geometry={"city",1};d.labels.push_back(label);Project source;source.replace(d);
        QFile file(dir.filePath("input.json"));QVERIFY(file.open(QIODevice::WriteOnly));file.write(projectcodec::encode(source));file.close();
        EditorController c({false,dir.filePath("private.json")});QVERIFY(c.openFile(QUrl::fromLocalFile(file.fileName())));QVERIFY(c.selectObject({{"domain","label"},{"id","city"}},"replace","test"));
        QVERIFY(c.beginContentEdit("label","",false));QVERIFY(!c.hasPendingEdits());QSignalSpy geometryChanged(&c,&EditorController::geometryChanged);
        QVERIFY(c.updateContentField("name","new"));QVERIFY(c.updateContentField("notes","new notes"));QVERIFY(c.commitContentField("name"));
        auto doc=projectcodec::decode(c.documentBytes());QCOMPARE(doc.labels.front().name,std::string("new"));QCOMPARE(doc.labels.front().notes,std::string("old notes"));QVERIFY(c.hasPendingEdits());
        QVERIFY(c.commitContentField("notes"));QVERIFY(!c.hasPendingEdits());QCOMPARE(geometryChanged.size(),0);c.undo();
        QCOMPARE(c.contentEditState().value("notes").toString(),QStringLiteral("old notes"));QCOMPARE(c.contentEditState().value("name").toString(),QStringLiteral("new"));c.undo();
        QCOMPARE(c.contentEditState().value("name").toString(),QStringLiteral("old"));c.redo();c.redo();QCOMPARE(c.contentEditState().value("notes").toString(),QStringLiteral("new notes"));
    }
    void contentPointLineSession_data(){QTest::addColumn<bool>("mobile");QTest::newRow("desktop")<<false;QTest::newRow("mobile")<<true;}
    void contentPointLineSession(){
        QFETCH(bool,mobile);QTemporaryDir dir;Project source;source.replace(fixture());
        QFile f(dir.filePath("input.json"));QVERIFY(f.open(QIODevice::WriteOnly));f.write(projectcodec::encode(source));f.close();
        EditorController c({mobile,dir.filePath("private.json")});QVERIFY(c.openFile(QUrl::fromLocalFile(f.fileName())));
        const auto original=c.documentBytes();
        QVERIFY(c.beginContentEdit("label","city",true));QVERIFY(c.updateContentField("name","도시"));
        QVERIFY(c.hasPendingEdits());QVERIFY(c.beginContentGeometry());QVERIFY(c.geometryAddPoint(2,2,0));
        QVERIFY(c.requestGeometryPreview());QCOMPARE(c.documentBytes(),original);QVERIFY(c.confirmGeometryEdit());
        auto doc=projectcodec::decode(c.documentBytes());QCOMPARE(doc.labels.size(),std::size_t(1));QCOMPARE(doc.labels.front().name,std::string("도시"));
        const auto labelId=QString::fromStdString(doc.labels.front().id);QVERIFY(!c.hasPendingEdits());
        c.undo();QCOMPARE(c.documentBytes(),original);c.redo();
        QVERIFY(c.selectObject({{"domain","label"},{"id",labelId}},"replace","test"));
        QVERIFY(c.beginContentEdit("label","",false));QVERIFY(c.beginContentGeometry());QVERIFY(c.geometrySelectNearest(2,2,1));
        QVERIFY(c.geometryMoveSelectedVertex(3,3,0));QVERIFY(c.geometryUndoDraft());c.cancelContentEdit();
        const auto pointState=c.documentBytes();
        QVERIFY(c.beginContentEdit("hydro","river",true));QVERIFY(c.updateContentField("name","강"));QVERIFY(c.beginContentGeometry());
        QVERIFY(c.geometryAddPoint(1,1,0));QVERIFY(!c.requestGeometryPreview());QVERIFY(c.geometryAddPoint(5,1,0));
        QVERIFY(c.geometryInsertNearest(3,1,1));QVERIFY(c.geometryDeleteSelectedVertex());
        QVERIFY(c.requestGeometryPreview());QCOMPARE(c.documentBytes(),pointState);QVERIFY(c.confirmGeometryEdit());
        doc=projectcodec::decode(c.documentBytes());QCOMPARE(doc.hydro.size(),std::size_t(1));
        const auto geometry=doc.geometries.get(doc.hydro.front().geometry);QCOMPARE(geometry->type,std::string("LineString"));QCOMPARE(geometry->lines.front().size(),std::size_t(2));
        c.undo();QCOMPARE(c.documentBytes(),pointState);c.redo();
        const auto output=QUrl::fromLocalFile(dir.filePath("output.json"));QVERIFY(c.saveFile(output));QVERIFY(c.openFile(output));
        QCOMPARE(projectcodec::decode(c.documentBytes()).hydro.size(),std::size_t(1));
    }
    void retainedReferencesPresentationAndStale(){
        auto d=fixture();PreservedExtension e;e.id="generic";e.jsonPointer="/genericFeatures";
        e.payload=R"([{"properties":{"ownerId":"A","topologyGroup":"land:A"},"number":1e+09,"order":[3,1,2]}])";
        e.dependencyKnowledge="known";e.dependencies={territorialRef("A")};e.forbiddenEffects={"geometry","relation","convert"};d.extensions.push_back(e);
        auto& binding=staticGeometryBinding(d,"B");auto next=binding.geometryRef;++next.version;d.geometries.insert(next,square(-5,-5,40));binding.geometryRef=next;
        Project p;p.replace(d);auto result=prepare(p,ConvertTerritorialTypeIntent{territorialRef("A"),UnitKind::General,{},territorialRef("B"),"A"});
        QVERIFY2(result.ok(),result.detail.c_str());QVERIFY(result.preview);
        QVERIFY(PresentationCommandProcessor::apply(p,SetPresentationVisibility{"countryFlags",false})==PresentationResult::Applied);
        QVERIFY(PresentationCommandProcessor::apply(p,SetScopedVisibility{"countries",{territorialRef("A")},false})==PresentationResult::Applied);
        QVERIFY(CommandProcessor::confirm(p,*result.preview).ok());
        QVERIFY(!itemVisible(p.document().presentation.webPresentation,"subunits","A"));
        const auto saved=projectcodec::encode(p);QVERIFY(saved.contains("1e+09"));QVERIFY(saved.contains("[3,1,2]"));
        const auto payload=QByteArray::fromStdString(p.document().extensions.front().payload);QVERIFY(payload.contains("land:A"));QVERIFY(payload.contains("\"ownerId\":\"A\""));
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
        QFETCH(int,operation);Project p;auto d=fixture();if(operation==2){auto& binding=staticGeometryBinding(d,"B");auto next=binding.geometryRef;++next.version;d.geometries.insert(next,square(-5,-5,40));binding.geometryRef=next;}p.replace(d);const auto before=projectcodec::encode(p);
        TerritorialMutationIntent intent=TransferSubunitIntent{territorialRef("S"),territorialRef("B")};
        if(operation==1)intent=ConvertTerritorialTypeIntent{territorialRef("S"),UnitKind::General,{},{},{}};
        if(operation==2)intent=ConvertTerritorialTypeIntent{territorialRef("A"),UnitKind::General,{},territorialRef("B"),"A"};
        auto result=prepare(p,intent);QVERIFY2(result.ok(),result.detail.c_str());QVERIFY(result.preview);QCOMPARE(projectcodec::encode(p),before);
        QVERIFY(CommandProcessor::confirm(p,*result.preview).ok());
        if(operation==0){QCOMPARE(area(p,"A"),96.);QCOMPARE(area(p,"P"),60.);QCOMPARE(area(p,"S"),4.);QCOMPARE(area(p,"B"),104.);QCOMPARE(staticParentRelation(p.document(),"S").parentId,std::string("B"));QCOMPARE(staticParentRelation(p.document(),"C").parentId,std::string("S"));}
        if(operation==1){QCOMPARE(area(p,"A"),100.);QCOMPARE(area(p,"P"),64.);QCOMPARE(area(p,"S"),4.);QVERIFY(p.document().units.at(p.index().objects.at(territorialRef("S"))).kind==UnitKind::General);QVERIFY(staticParentRelation(p.document(),"S").parentId.empty());QCOMPARE(staticParentRelation(p.document(),"C").parentId,std::string("S"));}
        if(operation==2){QCOMPARE(area(p,"B"),1600.);QVERIFY(p.index().objects.count(territorialRef("A")));QCOMPARE(area(p,"A"),100.);QCOMPARE(staticParentRelation(p.document(),"A").parentId,std::string("B"));QCOMPARE(staticParentRelation(p.document(),"P").parentId,std::string("A"));}
        const auto after=projectcodec::encode(p);Project reopened;reopened.replace(projectcodec::decode(after));QCOMPARE(projectcodec::encode(reopened),after);
        QVERIFY(p.undo());QCOMPARE(projectcodec::encode(p),before);QVERIFY(p.redo());QCOMPARE(projectcodec::encode(p),after);
    }
    void completeCountryAndCancelledJobsAreRejected(){
        Project p;auto d=fixture();staticGeometryBinding(d,d.units[2].id).geometryRef=staticGeometryBinding(d,d.units[0].id).geometryRef;p.replace(d);const auto before=projectcodec::encode(p);
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
        QVERIFY(c.beginTerritorialCreate("general"));const auto id=c.structureState()["generatedId"].toString();QVERIFY(!id.isEmpty());
        QVERIFY(c.updateTerritorialCreateSetup("새 국가","",id));QVERIFY(c.beginGeometryDraw());
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
            TerritorialUnit u;u.id=row.first;u.name=row.first;u.kind=UnitKind::General;appendTerritory(d,u,row.second,"","partition");d.presentation.objectStyles[territorialRef(row.first)]={};
        }
        setFixtureParent(d,territorialRef("X"),territorialRef("P"));
        setFixtureParent(d,territorialRef("Y"),territorialRef("P"));
        setFixtureParent(d,territorialRef("Z"),territorialRef("Y"));
        validateDocument(d);Project p;p.replace(d);const auto before=projectcodec::encode(p);
        auto result=prepare(p,MergeTerritorialIntent{territorialRef("X"),{territorialRef("Y")}});QVERIFY2(result.ok(),result.detail.c_str());QCOMPARE(projectcodec::encode(p),before);
        QVERIFY(CommandProcessor::confirm(p,*result.preview).ok());QVERIFY(!p.index().objects.count(territorialRef("Y")));QCOMPARE(area(p,"X"),8.);
        QCOMPARE(staticParentRelation(p.document(),"Z").parentId,std::string("X"));
        const auto after=projectcodec::encode(p);QVERIFY(p.undo());QCOMPARE(projectcodec::encode(p),before);QVERIFY(p.redo());QCOMPARE(projectcodec::encode(p),after);
    }
    void drawnAnnexClipsDonorAndMovesContainedChild(){
        auto d=fixture();const GeometryRef xg{"AX",1},yg{"AY",1},zg{"AZ",1};d.geometries.insert(xg,square(2,6,2));d.geometries.insert(yg,square(4,6,2));d.geometries.insert(zg,square(4.2,6.2,.4));
        for(const auto& row:std::array<std::pair<const char*,GeometryRef>,3>{{{"AX",xg},{"AY",yg},{"AZ",zg}}}){TerritorialUnit u;u.id=row.first;u.name=row.first;u.kind=UnitKind::General;appendTerritory(d,u,row.second,"","partition");d.presentation.objectStyles[territorialRef(row.first)]={};}
        setFixtureParent(d,territorialRef("AX"),territorialRef("P"));setFixtureParent(d,territorialRef("AY"),territorialRef("P"));setFixtureParent(d,territorialRef("AZ"),territorialRef("AY"));validateDocument(d);
        Project p;p.replace(d);const auto before=projectcodec::encode(p);auto result=prepare(p,AnnexTerritoryIntent{territorialRef("AX"),{territorialRef("AY")},square(4,6,1)});QVERIFY2(result.ok(),result.detail.c_str());QVERIFY(CommandProcessor::confirm(p,*result.preview).ok());
        QCOMPARE(area(p,"AX"),5.);QCOMPARE(area(p,"AY"),3.);QCOMPARE(staticParentRelation(p.document(),"AZ").parentId,std::string("AX"));QVERIFY(p.undo());QCOMPARE(projectcodec::encode(p),before);
    }
    void cutSplitCreatesOneUndoableSibling(){
        Project p;p.replace(fixture());const auto before=projectcodec::encode(p);auto result=prepare(p,SplitTerritorialIntent{territorialRef("B"),{{25,-2},{25,12}},0,"B-east","B East"});QVERIFY2(result.ok(),result.detail.c_str());QCOMPARE(projectcodec::encode(p),before);
        QVERIFY(CommandProcessor::confirm(p,*result.preview).ok());QCOMPARE(area(p,"B"),50.);QCOMPARE(area(p,"B-east"),50.);QVERIFY(p.document().units.at(p.index().objects.at(territorialRef("B-east"))).kind==UnitKind::General);
        const auto after=projectcodec::encode(p);Project reopened;reopened.replace(projectcodec::decode(after));QCOMPARE(projectcodec::encode(reopened),after);QVERIFY(p.undo());QCOMPARE(projectcodec::encode(p),before);QVERIFY(p.redo());QCOMPARE(projectcodec::encode(p),after);
    }
    void sharedBoundaryPreservesOuterUnionAndUpdatesBothOwners(){
        auto document=fixture();auto b=std::find_if(document.units.begin(),document.units.end(),[](const auto& unit){return unit.id=="B";});QVERIFY(b!=document.units.end());auto& binding=staticGeometryBinding(document,b->id);GeometryRef next=binding.geometryRef;++next.version;document.geometries.insert(next,square(10,0,10));binding.geometryRef=next;validateDocument(document);Project project;project.replace(document);const auto before=projectcodec::encode(project);
        SharedBoundaryIntent intent{{{territorialRef("A"),rectangle(0,0,12,10)},{territorialRef("B"),rectangle(12,0,8,10)}}};auto result=prepare(project,intent);QVERIFY2(result.ok(),result.detail.c_str());QCOMPARE(projectcodec::encode(project),before);QVERIFY(CommandProcessor::confirm(project,*result.preview).ok());QCOMPARE(area(project,"A"),120.);QCOMPARE(area(project,"B"),80.);QVERIFY(project.undo());QCOMPARE(projectcodec::encode(project),before);
        auto invalid=prepare(project,SharedBoundaryIntent{{{territorialRef("A"),rectangle(0,0,13,10)},{territorialRef("B"),rectangle(12,0,8,10)}}});QVERIFY(!invalid.ok());QCOMPARE(projectcodec::encode(project),before);
    }
    void countryCoastClipsChildrenAtomically(){
        Project project;project.replace(fixture());const auto before=projectcodec::encode(project);auto result=prepare(project,CoastlineIntent{territorialRef("A"),square(0,0,7),CoastlineAuthority::Country});QVERIFY2(result.ok(),result.detail.c_str());QCOMPARE(projectcodec::encode(project),before);QVERIFY(CommandProcessor::confirm(project,*result.preview).ok());QCOMPARE(area(project,"A"),49.);QCOMPARE(area(project,"P"),36.);QVERIFY(project.undo());QCOMPARE(projectcodec::encode(project),before);
    }
};
QTEST_MAIN(TerritorialGeometryTests)
#include "territorial_geometry_tests.moc"

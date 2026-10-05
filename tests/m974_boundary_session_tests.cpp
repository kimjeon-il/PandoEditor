#include "editorcontroller.h"
#include "projectcodec.h"
#include "territorial_fixture.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <limits>
using namespace pandoeditor;
namespace {
Geometry polygon(Ring ring) { Geometry g;g.polygons={{std::move(ring)}};return g; }
Geometry box(double x,double y,double w,double h) {return polygon({{x,y},{x+w,y},{x+w,y+h},{x,y+h},{x,y}});}
ProjectDocument triple() {
    auto a=polygon({{0,0},{1,0},{1,.5},{1,1},{0,1},{0,0}});
    auto b=polygon({{1,0},{2,0},{2,1},{1,1},{1,.5},{1,0}});
    auto c=polygon({{0,1},{1,1},{2,1},{2,2},{0,2},{0,1}});
    return ProjectDocument({{"A","A",a.polygons,0xabcdef},{"B","B",b.polygons,0x123456},{"C","C",c.polygons,0x654321},{"U","Unrelated",box(5,0,1,1).polygons,0x123456}},{{"countries","Countries"}});
}
bool open(EditorController& c,const ProjectDocument& d,QTemporaryDir& dir) {
    Project p;p.replace(d);QFile f(dir.filePath("input.json"));if(!f.open(QIODevice::WriteOnly))return false;f.write(projectcodec::encode(p));f.close();return c.openFile(QUrl::fromLocalFile(f.fileName()));
}
bool select(EditorController& c,std::initializer_list<const char*> ids) {bool first=true;for(const auto id:ids){if(!c.selectObject({{"domain","territorial"},{"id",id}},first?"replace":"toggle"))return false;first=false;}return true;}
QVariantMap handle(EditorController& c,const MapProjection& projection,Point coordinate) {
    const auto p=projection.project(coordinate);
    for(const auto& value:c.geometryDraftPaths())for(const auto& vertex:value.toMap().value("vertices").toList()) {
        const auto row=vertex.toMap();if(std::abs(row.value("x").toDouble()-p.x)<1e-7&&std::abs(row.value("y").toDouble()-p.y)<1e-7)return row;
    }return {};
}
bool sameGeometry(const Geometry& a,const Geometry& b) {
    if(a.type!=b.type||a.polygons.size()!=b.polygons.size())return false;
    for(std::size_t p=0;p<a.polygons.size();++p){if(a.polygons[p].size()!=b.polygons[p].size())return false;for(std::size_t r=0;r<a.polygons[p].size();++r){if(a.polygons[p][r].size()!=b.polygons[p][r].size())return false;for(std::size_t v=0;v<a.polygons[p][r].size();++v)if(a.polygons[p][r][v].x!=b.polygons[p][r][v].x||a.polygons[p][r][v].y!=b.polygons[p][r][v].y)return false;}}return true;
}
Geometry geometry(const ProjectDocument& d,const char* id) {return *d.geometries.get(staticGeometryBinding(d,id).geometryRef);}
}
class M974BoundarySessionTests:public QObject {
    Q_OBJECT
private slots:
    void entrySelectionMatchesRootChildAndAutoChildPolicy_data(){
        QTest::addColumn<QString>("mode");for(const auto* mode:{"root","root-reordered","root-already-last","child","auto-child","rejected-root","rejected-child","root-preparation-error"})QTest::newRow(mode)<<QString(mode);
    }
    void entrySelectionMatchesRootChildAndAutoChildPolicy(){
        QFETCH(QString,mode);auto d=triple();const bool child=mode=="child"||mode=="auto-child"||mode=="rejected-child";
        if(child){d.geometries.insert({"P",1},box(0,0,2,2));appendTerritory(d,{"P","Parent","",UnitKind::General,false},{"P",1});d.presentation.objectStyles[territorialRef("P")]={};for(const auto id:{"A","B","C"})staticParentRelation(d,id).parentId="P";}
        if(mode.startsWith("rejected"))d.units[1].locked=true;
        QTemporaryDir dir;EditorController c({false,dir.filePath("private.json")});QVERIFY(open(c,d,dir));QVariantList refs;
        for(const auto id:mode=="root-reordered"?QStringList{"B","A"}:mode=="child"?QStringList{"B","A","C"}:mode=="auto-child"?QStringList{"A"}:mode=="root-preparation-error"?QStringList{"A","B","U"}:QStringList{"A","B"})refs.append(QVariantMap{{"domain","territorial"},{"id",id}});
        const QString primary=mode=="root-reordered"?"B":mode=="root-already-last"?"B":"A";QVERIFY(c.setSelection(refs,{{"domain","territorial"},{"id",primary}},"map"));const auto initial=c.selectionItems();const auto selectionRevision=c.selectionRevision();const auto before=c.documentBytes();const auto revision=c.revision();
        const bool accepted=!mode.startsWith("rejected");QCOMPARE(c.canBeginSharedBoundaryGeometry(),accepted);QCOMPARE(c.selectionItems(),initial);QCOMPARE(c.selectedId(),primary);QCOMPARE(c.selectionRevision(),selectionRevision);QCOMPARE(c.beginSharedBoundaryGeometry(),accepted);
        if(!accepted){QCOMPARE(c.selectionItems(),initial);QCOMPARE(c.selectedId(),primary);QCOMPARE(c.selectionRevision(),selectionRevision);QVERIFY(!c.geometryEditState()["active"].toBool());QCOMPARE(c.documentBytes(),before);c.cancelGeometryEdit();QCOMPARE(c.selectionItems(),initial);QCOMPARE(c.selectedId(),primary);return;}
        const QString enteredPrimary=child?primary:refs.back().toMap()["id"].toString();QCOMPARE(c.geometryEditState()["boundaryStatus"].toString(),QString("preparing"));QCOMPARE(c.selectionItems(),initial);QCOMPARE(c.selectedId(),enteredPrimary);QCOMPARE(c.selectionRevision(),selectionRevision+(enteredPrimary!=primary?1:0));
        QTRY_COMPARE_WITH_TIMEOUT(c.geometryEditState()["boundaryStatus"].toString(),mode=="root-preparation-error"?QString("error"):QString("ready"),3000);QCOMPARE(c.selectionItems(),initial);QCOMPARE(c.selectedId(),enteredPrimary);QCOMPARE(c.documentBytes(),before);QCOMPARE(c.revision(),revision);QVERIFY(!c.canUndo());
        c.cancelGeometryEdit();if(!child){QCOMPARE(c.selectionItems(),initial);QCOMPARE(c.selectedId(),enteredPrimary);}QCOMPARE(c.documentBytes(),before);
    }
    void rootPreviewBackAndToolCancelKeepObservedLastPrimary(){
        QTemporaryDir dir;const auto d=triple();EditorController c({false,dir.filePath("private.json")});QVERIFY(open(c,d,dir));QVariantList refs{QVariantMap{{"domain","territorial"},{"id","A"}},QVariantMap{{"domain","territorial"},{"id","B"}}};QVERIFY(c.setSelection(refs,refs.front().toMap(),"map"));const auto initial=c.selectionItems();QVERIFY(c.beginSharedBoundaryGeometry());QCOMPARE(c.selectedId(),QString("B"));QTRY_COMPARE_WITH_TIMEOUT(c.geometryEditState()["boundaryStatus"].toString(),QString("ready"),3000);MapProjection projection;projection.rebuild(d);const auto from=projection.project({1,.5}),to=projection.project({1.1,.5});QVERIFY(c.geometrySelectNearest(from.x,from.y,.001));QVERIFY(c.geometryBeginVertexDrag());QVERIFY(c.geometryMoveSelectedVertex(to.x,to.y,0));c.geometryEndVertexDrag(false);QTRY_VERIFY_WITH_TIMEOUT(c.geometryEditState()["previewReady"].toBool(),5000);QCOMPARE(c.selectedId(),QString("B"));QVERIFY(c.geometryBack());QCOMPARE(c.selectionItems(),initial);QCOMPARE(c.selectedId(),QString("B"));c.cancelGeometryEdit();QCOMPARE(c.selectionItems(),initial);QCOMPARE(c.selectedId(),QString("B"));QVERIFY(!c.canUndo());
    }
    void rootColdCancelKeepsObservedLastPrimaryBeforeLatePreparation(){
        QTemporaryDir dir;const auto d=triple();EditorController c({false,dir.filePath("private.json")});QVERIFY(open(c,d,dir));QVariantList refs{QVariantMap{{"domain","territorial"},{"id","A"}},QVariantMap{{"domain","territorial"},{"id","B"}}};QVERIFY(c.setSelection(refs,refs.front().toMap(),"map"));QVERIFY(c.beginSharedBoundaryGeometry());QCOMPARE(c.selectedId(),QString("B"));c.cancelGeometryEdit();QCOMPARE(c.selectedId(),QString("B"));QTest::qWait(100);QCOMPARE(c.selectedId(),QString("B"));QVERIFY(!c.geometryEditState()["active"].toBool());
    }
    void rootToolCancelRestoresOriginalItemsWithLastItemFallback_data(){QTest::addColumn<bool>("pending");QTest::newRow("pending-preparation")<<true;QTest::newRow("ready-preparation")<<false;}
    void rootToolCancelRestoresOriginalItemsWithLastItemFallback(){
        QFETCH(bool,pending);QTemporaryDir dir;const auto d=triple();EditorController c({false,dir.filePath("private.json")});QVERIFY(open(c,d,dir));QVariantList refs;for(const auto id:{"B","A","C"})refs.append(QVariantMap{{"domain","territorial"},{"id",id}});QVERIFY(c.setSelection(refs,refs.front().toMap(),"map"));QCOMPARE(c.selectedId(),QString("B"));const auto initial=c.selectionItems();const auto before=c.documentBytes();QVERIFY(c.beginSharedBoundaryGeometry());QCOMPARE(c.selectedId(),QString("C"));if(!pending)QTRY_COMPARE_WITH_TIMEOUT(c.geometryEditState()["boundaryStatus"].toString(),QString("ready"),3000);c.selectCountry("U");QCOMPARE(c.selectedId(),QString("U"));QCOMPARE(c.selectionItems().size(),1);c.cancelGeometryEdit();QCOMPARE(c.selectionItems(),initial);QCOMPARE(c.selectedId(),QString("C"));QTest::qWait(100);QVERIFY(!c.geometryEditState()["active"].toBool());QCOMPARE(c.selectionItems(),initial);QCOMPARE(c.selectedId(),QString("C"));QCOMPARE(c.documentBytes(),before);QVERIFY(!c.canUndo());
    }
    void childWholeToolCancelSelectsCurrentChildWithoutChangingEntryOrPreviewBack(){
        auto d=triple();d.geometries.insert({"P",1},box(0,0,2,2));appendTerritory(d,{"P","Parent","",UnitKind::General,false},{"P",1});d.presentation.objectStyles[territorialRef("P")]={};for(const auto id:{"A","B","C"})staticParentRelation(d,id).parentId="P";QTemporaryDir dir;EditorController c({false,dir.filePath("private.json")});QVERIFY(open(c,d,dir));QVariantList refs;for(const auto id:{"A","B","C"})refs.append(QVariantMap{{"domain","territorial"},{"id",id}});QVERIFY(c.setSelection(refs,{{"domain","territorial"},{"id","B"}},"map"));const auto initial=c.selectionItems();const auto before=c.documentBytes();QVERIFY(c.beginSharedBoundaryGeometry());QCOMPARE(c.selectedId(),QString("B"));QCOMPARE(c.selectionItems(),initial);QTRY_COMPARE_WITH_TIMEOUT(c.geometryEditState()["boundaryStatus"].toString(),QString("ready"),3000);MapProjection projection;projection.rebuild(d);const auto from=projection.project({1,1}),to=projection.project({1,1.1});QVERIFY(c.geometrySelectNearest(from.x,from.y,.001));QVERIFY(c.geometryBeginVertexDrag());QVERIFY(c.geometryMoveSelectedVertex(to.x,to.y,0));c.geometryEndVertexDrag(false);QTRY_VERIFY_WITH_TIMEOUT(c.geometryEditState()["previewReady"].toBool(),5000);QVERIFY(c.geometryBack());QCOMPARE(c.selectionItems(),initial);QCOMPARE(c.selectedId(),QString("B"));c.cancelGeometryEdit();QCOMPARE(c.selectionItems().size(),1);QCOMPARE(c.selectedId(),QString("B"));QCOMPARE(c.documentBytes(),before);QVERIFY(!c.canUndo());
    }
    void childWholeToolCancelUsesCurrentVisiblePrimary_data(){QTest::addColumn<QString>("mode");for(const auto* mode:{"changed-child-primary","auto-child","no-primary","root-primary"})QTest::newRow(mode)<<QString(mode);}
    void childWholeToolCancelUsesCurrentVisiblePrimary(){
        QFETCH(QString,mode);auto d=triple();d.geometries.insert({"P",1},box(0,0,2,2));appendTerritory(d,{"P","Parent","",UnitKind::General,false},{"P",1});d.presentation.objectStyles[territorialRef("P")]={};for(const auto id:{"A","B","C"})staticParentRelation(d,id).parentId="P";QTemporaryDir dir;EditorController c({false,dir.filePath("private.json")});QVERIFY(open(c,d,dir));QVariantList refs;for(const auto id:mode=="auto-child"?QStringList{"A"}:QStringList{"A","B","C"})refs.append(QVariantMap{{"domain","territorial"},{"id",id}});QVERIFY(c.setSelection(refs,refs.front().toMap(),"map"));const auto before=c.documentBytes();QVERIFY(c.beginSharedBoundaryGeometry());QTRY_COMPARE_WITH_TIMEOUT(c.geometryEditState()["boundaryStatus"].toString(),QString("ready"),3000);
        if(mode=="changed-child-primary")QVERIFY(c.setSelection(refs,{{"domain","territorial"},{"id","C"}},"map"));else if(mode=="no-primary")c.clearSelection();else if(mode=="root-primary")c.selectCountry("U");const auto revision=c.selectionRevision();c.cancelGeometryEdit();
        if(mode=="no-primary")QVERIFY(c.selectionItems().isEmpty());else{QCOMPARE(c.selectionItems().size(),1);QCOMPARE(c.selectedId(),mode=="changed-child-primary"?QString("C"):mode=="root-primary"?QString("U"):QString("A"));}if(mode!="changed-child-primary")QCOMPARE(c.selectionRevision(),revision);QCOMPARE(c.documentBytes(),before);QVERIFY(!c.canUndo());
    }
    void childImpactDeclinePreservesEntrySetUntilWholeToolCancel(){
        auto d=triple();d.geometries.insert({"P",1},box(0,0,2,2));appendTerritory(d,{"P","Parent","",UnitKind::General,false},{"P",1});d.presentation.objectStyles[territorialRef("P")]={};for(const auto id:{"A","B","C"})staticParentRelation(d,id).parentId="P";d.geometries.insert({"nested",1},box(.7,.2,.3,.6));appendTerritory(d,{"nested","Nested","",UnitKind::General,false},{"nested",1},"A");d.presentation.objectStyles[territorialRef("nested")]={};QTemporaryDir dir;EditorController c({false,dir.filePath("private.json")});QVERIFY(open(c,d,dir));QVariantList refs{QVariantMap{{"domain","territorial"},{"id","A"}},QVariantMap{{"domain","territorial"},{"id","B"}}};QVERIFY(c.setSelection(refs,refs.front().toMap(),"map"));const auto initial=c.selectionItems();const auto before=c.documentBytes();QVERIFY(c.beginSharedBoundaryGeometry());QTRY_COMPARE_WITH_TIMEOUT(c.geometryEditState()["boundaryStatus"].toString(),QString("ready"),3000);MapProjection projection;projection.rebuild(d);const auto from=projection.project({1,.5}),to=projection.project({.8,.5});QVERIFY(c.geometrySelectNearest(from.x,from.y,.001));QVERIFY(c.geometryBeginVertexDrag());QVERIFY(c.geometryMoveSelectedVertex(to.x,to.y,0));c.geometryEndVertexDrag(false);QTRY_VERIFY_WITH_TIMEOUT(c.geometryEditState()["previewReady"].toBool(),5000);QVERIFY(!c.confirmGeometryEdit());QVERIFY(c.geometryEditState()["boundaryImpactConfirmation"].toBool());c.geometryCancelBoundaryImpacts();QCOMPARE(c.selectionItems(),initial);QCOMPARE(c.selectedId(),QString("A"));c.cancelGeometryEdit();QCOMPARE(c.selectionItems().size(),1);QCOMPARE(c.selectedId(),QString("A"));QCOMPARE(c.documentBytes(),before);QVERIFY(!c.canUndo());
    }
    void threeOwnersColdPreparationAndAtomicLifecycle() {
        QTemporaryDir dir;const auto d=triple();EditorController c({false,dir.filePath("private.json")});QVERIFY(open(c,d,dir));QVERIFY(select(c,{"A","B","C"}));
        const auto before=c.documentBytes();const auto revision=c.revision();MapProjection projection;projection.rebuild(d);
        QVERIFY(c.beginSharedBoundaryGeometry());
        QCOMPARE(c.geometryEditState().value("boundaryStatus").toString(),QString("preparing"));
        QVERIFY(c.geometryDraftPaths().isEmpty());QVERIFY(!c.geometrySelectNearest(1,1,10));QVERIFY(!c.requestGeometryPreview());
        QTRY_COMPARE_WITH_TIMEOUT(c.geometryEditState().value("boundaryStatus").toString(),QString("ready"),3000);
        const auto node=handle(c,projection,{1,1});QVERIFY(!node.isEmpty());QVERIFY(!node.value("fixed").toBool());QCOMPARE(node.value("ownerIds").toStringList().size(),3);
        const auto p=projection.project({1,1}),next=projection.project({1,1.1});QVERIFY(c.geometrySelectNearest(p.x,p.y,.001));QVERIFY(c.geometryBeginVertexDrag());QVERIFY(c.geometryMoveSelectedVertex(next.x,next.y,0));c.geometryEndVertexDrag(false);
        QCOMPARE(c.documentBytes(),before);QVERIFY(c.geometryEditState().value("calculating").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!c.geometryEditState().value("calculating").toBool(),5000);
        QVERIFY2(c.geometryEditState().value("previewReady").toBool(),qPrintable(c.geometryEditState().value("error").toString()));
        QCOMPARE(c.geometryDraftPaths().size(),3);QCOMPARE(c.documentBytes(),before);QCOMPARE(c.revision(),revision);QVERIFY(c.confirmGeometryEdit());
        const auto after=c.documentBytes();QVERIFY(after!=before);QCOMPARE(c.revision(),revision+1);
        const auto result=projectcodec::decode(after);for(const auto id:{"A","B","C"})QVERIFY(!sameGeometry(geometry(result,id),geometry(d,id)));QVERIFY(sameGeometry(geometry(result,"U"),geometry(d,"U")));
        c.undo();QCOMPARE(c.documentBytes(),before);c.redo();QCOMPARE(c.documentBytes(),after);
    }
    void allOwnerDragCancelAndNoOp() {
        QTemporaryDir dir;const auto d=triple();EditorController c({false,dir.filePath("private.json")});QVERIFY(open(c,d,dir));QVERIFY(select(c,{"A","B","C"}));QVERIFY(c.beginSharedBoundaryGeometry());
        QTRY_COMPARE_WITH_TIMEOUT(c.geometryEditState().value("boundaryStatus").toString(),QString("ready"),3000);MapProjection projection;projection.rebuild(d);const auto before=c.documentBytes();const auto paths=c.geometryDraftPaths();
        const auto p=projection.project({1,1}),next=projection.project({1,1.1});QVERIFY(c.geometrySelectNearest(p.x,p.y,.001));QVERIFY(c.geometryBeginVertexDrag());QVERIFY(!c.geometryMoveSelectedVertex(p.x,p.y,0));c.geometryEndVertexDrag(false);QVERIFY(!c.geometryEditState().value("canUndo").toBool());QVERIFY(!c.requestGeometryPreview());
        QVERIFY(c.geometryBeginVertexDrag());QVERIFY(c.geometryMoveSelectedVertex(next.x,next.y,0));c.geometryEndVertexDrag(true);QCOMPARE(c.geometryDraftPaths(),paths);QVERIFY(!c.geometryEditState().value("canUndo").toBool());
        c.cancelGeometryEdit();QCOMPARE(c.documentBytes(),before);QVERIFY(!c.canUndo());
    }
    void cancelledAndSelectionStalePreparationCannotReopen() {
        QTemporaryDir dir;const auto d=triple();EditorController c({false,dir.filePath("private.json")});QVERIFY(open(c,d,dir));QVERIFY(select(c,{"A","B","C"}));const auto before=c.documentBytes();QVERIFY(c.beginSharedBoundaryGeometry());c.cancelGeometryEdit();QTest::qWait(100);QVERIFY(!c.geometryEditState().value("active").toBool());
        QVERIFY(c.beginSharedBoundaryGeometry());c.selectCountry("U");QTRY_COMPARE_WITH_TIMEOUT(c.geometryEditState().value("boundaryStatus").toString(),QString("error"),3000);QVERIFY(c.geometryDraftPaths().isEmpty());QVERIFY(!c.requestGeometryPreview());QCOMPARE(c.documentBytes(),before);
    }
    void disjointAdjacentPairsValidButIsolatedOwnerRejected() {
        auto d=triple();d.geometries.insert({"V",1},box(6,0,1,1));appendTerritory(d,{"V","V","",UnitKind::General,false},{"V",1});d.presentation.objectStyles[territorialRef("V")]={};
        QTemporaryDir dir;EditorController c({false,dir.filePath("private.json")});QVERIFY(open(c,d,dir));QVERIFY(select(c,{"A","B","U","V"}));QVERIFY(c.beginSharedBoundaryGeometry());QTRY_COMPARE_WITH_TIMEOUT(c.geometryEditState().value("boundaryStatus").toString(),QString("ready"),3000);c.cancelGeometryEdit();
        QVERIFY(select(c,{"A","B","U"}));QVERIFY(c.beginSharedBoundaryGeometry());QTRY_COMPARE_WITH_TIMEOUT(c.geometryEditState().value("boundaryStatus").toString(),QString("error"),3000);QVERIFY(!c.requestGeometryPreview());
    }
    void structuralLocksAndMixedKindsRejected() {
        for(const auto mode:{"selected","layer","regional","mixed-parent","ancestor"}) {
            auto d=triple();
            if(QString(mode)=="selected")d.units[1].locked=true;
            if(QString(mode)=="layer")d.presentation.userLayers[0].locked=true;
            if(QString(mode)=="regional")d.units[1].kind=UnitKind::Regional;
            if(QString(mode)=="mixed-parent"||QString(mode)=="ancestor") {
                d.geometries.insert({"P",1},box(-1,-1,4,4));appendTerritory(d,{"P","P","",UnitKind::General,QString(mode)=="ancestor"},{"P",1});d.presentation.objectStyles[territorialRef("P")]={};staticParentRelation(d,"A").parentId="P";if(QString(mode)=="ancestor")staticParentRelation(d,"B").parentId="P";
            }
            QTemporaryDir dir;EditorController c({false,dir.filePath("private.json")});QVERIFY(open(c,d,dir));QVERIFY(select(c,{"A","B"}));if(QString(mode)=="ancestor"){QVERIFY(c.beginSharedBoundaryGeometry());QTRY_COMPARE_WITH_TIMEOUT(c.geometryEditState().value("boundaryStatus").toString(),QString("error"),3000);}else QVERIFY2(!c.beginSharedBoundaryGeometry(),mode);
        }
    }
    void singleChildAutoEntryUsesOnlySeedConnectedUnlockedSiblings() {
        auto d=triple();d.geometries.insert({"P",1},box(-1,-1,9,4));appendTerritory(d,{"P","Parent","",UnitKind::General,false},{"P",1});d.presentation.objectStyles[territorialRef("P")]={};
        d.geometries.insert({"V",1},box(6,0,1,1));appendTerritory(d,{"V","V","",UnitKind::General,false},{"V",1});d.presentation.objectStyles[territorialRef("V")]={};for(const auto id:{"A","B","C","U","V"})staticParentRelation(d,id).parentId="P";
        QTemporaryDir dir;EditorController c({false,dir.filePath("private.json")});QVERIFY(open(c,d,dir));c.selectCountry("A");const auto before=c.documentBytes();QVERIFY(c.beginSharedBoundaryGeometry());QTRY_COMPARE_WITH_TIMEOUT(c.geometryEditState().value("boundaryStatus").toString(),QString("ready"),3000);
        QSet<QString> targets;for(const auto& value:c.geometryEditState().value("targets").toList())targets.insert(value.toMap().value("id").toString());QCOMPARE(targets,QSet<QString>({"A","B","C"}));c.cancelGeometryEdit();QCOMPARE(c.selectionItems().size(),1);QCOMPARE(c.selectionItems()[0].toMap().value("id").toString(),QString("A"));QCOMPARE(c.documentBytes(),before);
        QVERIFY(select(c,{"A","B","U","V"}));QVERIFY(c.beginSharedBoundaryGeometry());QTRY_COMPARE_WITH_TIMEOUT(c.geometryEditState().value("boundaryStatus").toString(),QString("ready"),3000);QCOMPARE(c.geometryEditState().value("targets").toList().size(),4);
    }
    void midpointWithThreeSelectedPreviewsOnlyTwoActualOwners() {
        QTemporaryDir dir;const auto d=triple();EditorController c({false,dir.filePath("private.json")});QVERIFY(open(c,d,dir));QVERIFY(select(c,{"A","B","C"}));QVERIFY(c.beginSharedBoundaryGeometry());QTRY_COMPARE_WITH_TIMEOUT(c.geometryEditState().value("boundaryStatus").toString(),QString("ready"),3000);
        MapProjection projection;projection.rebuild(d);const auto p=projection.project({1,.5}),next=projection.project({1.1,.5});QVERIFY(c.geometrySelectNearest(p.x,p.y,.001));QVERIFY(c.geometryMoveSelectedVertex(next.x,next.y,0));QVERIFY(c.requestGeometryPreview());QTRY_VERIFY_WITH_TIMEOUT(!c.geometryEditState().value("calculating").toBool(),5000);QVERIFY2(c.geometryEditState().value("previewReady").toBool(),qPrintable(c.geometryEditState().value("error").toString()));
        QSet<QString> owners;for(const auto& row:c.geometryDraftPaths())owners.insert(row.toMap().value("ownerId").toString());QCOMPARE(owners,QSet<QString>({"A","B"}));QVERIFY(c.confirmGeometryEdit());const auto result=projectcodec::decode(c.documentBytes());QVERIFY(sameGeometry(geometry(result,"C"),geometry(d,"C")));QVERIFY(sameGeometry(geometry(result,"U"),geometry(d,"U")));
    }
    void invalidOuterUnionRemainsRetryableAndCancelPreviewIsInert() {
        QTemporaryDir dir;const auto d=triple();EditorController c({false,dir.filePath("private.json")});QVERIFY(open(c,d,dir));QVERIFY(select(c,{"A","B","C"}));const auto before=c.documentBytes();const auto revision=c.revision();QVERIFY(c.beginSharedBoundaryGeometry());QTRY_COMPARE_WITH_TIMEOUT(c.geometryEditState().value("boundaryStatus").toString(),QString("ready"),3000);const auto originalPaths=c.geometryDraftPaths();
        MapProjection projection;projection.rebuild(d);const auto p=projection.project({1,0}),outside=projection.project({1,-.1});QVERIFY(c.geometrySelectNearest(p.x,p.y,.001));QVERIFY(c.geometryBeginVertexDrag());QVERIFY(c.geometryMoveSelectedVertex(outside.x,outside.y,0));c.geometryEndVertexDrag(false);QCOMPARE(c.geometryDraftPaths(),originalPaths);QTRY_VERIFY_WITH_TIMEOUT(!c.geometryEditState().value("calculating").toBool(),5000);QVERIFY(!c.geometryEditState().value("previewReady").toBool());QVERIFY(!c.geometryEditState().value("error").toString().isEmpty());QCOMPARE(c.documentBytes(),before);QCOMPARE(c.revision(),revision);QCOMPARE(c.geometryDraftPaths(),originalPaths);QVERIFY(!c.geometryEditState()["canUndo"].toBool());
        const auto next=projection.project({1.1,0});QVERIFY(c.geometrySelectNearest(p.x,p.y,.001));QVERIFY(c.geometryBeginVertexDrag());QVERIFY(c.geometryMoveSelectedVertex(next.x,next.y,0));c.geometryEndVertexDrag(false);QTRY_VERIFY_WITH_TIMEOUT(c.geometryEditState().value("previewReady").toBool(),5000);c.cancelGeometryEdit();QCOMPARE(c.documentBytes(),before);QCOMPARE(c.revision(),revision);QVERIFY(!c.canUndo());
    }
    void cancelledAndSelectionStalePreviewCannotPublish() {
        QTemporaryDir dir;const auto d=triple();EditorController c({false,dir.filePath("private.json")});QVERIFY(open(c,d,dir));QVERIFY(select(c,{"A","B","C"}));const auto before=c.documentBytes();MapProjection projection;projection.rebuild(d);const auto p=projection.project({1,1}),next=projection.project({1,1.1});
        for(bool cancel:{true,false}) {QVERIFY(select(c,{"A","B","C"}));QVERIFY(c.beginSharedBoundaryGeometry());QTRY_COMPARE_WITH_TIMEOUT(c.geometryEditState().value("boundaryStatus").toString(),QString("ready"),3000);QVERIFY(c.geometrySelectNearest(p.x,p.y,.001));QVERIFY(c.geometryMoveSelectedVertex(next.x,next.y,0));QVERIFY(c.requestGeometryPreview());if(cancel)c.cancelGeometryEdit();else c.selectCountry("U");QTRY_VERIFY_WITH_TIMEOUT(!c.geometryEditState().value("calculating").toBool(),5000);QTest::qWait(80);QVERIFY(!c.geometryEditState().value("previewReady").toBool());QVERIFY(!c.confirmGeometryEdit());QCOMPARE(c.documentBytes(),before);c.cancelGeometryEdit();}
    }
    void descendantImpactsRequireSeparateApprovalAndDeclineReturnsToEditing() {
        auto d=triple();d.geometries.insert({"child",1},box(.7,.2,.3,.6));appendTerritory(d,{"child","Child","",UnitKind::General,false},{"child",1},"A");d.presentation.objectStyles[territorialRef("child")]={};
        QTemporaryDir dir;EditorController c({false,dir.filePath("private.json")});QVERIFY(open(c,d,dir));QVERIFY(select(c,{"A","B"}));const auto before=c.documentBytes();QVERIFY(c.beginSharedBoundaryGeometry());QTRY_COMPARE_WITH_TIMEOUT(c.geometryEditState().value("boundaryStatus").toString(),QString("ready"),3000);
        const auto originalPaths=c.geometryDraftPaths();MapProjection projection;projection.rebuild(d);const auto p=projection.project({1,.5}),next=projection.project({.8,.5});QVERIFY(c.geometrySelectNearest(p.x,p.y,.001));QVERIFY(c.geometryBeginVertexDrag());QVERIFY(c.geometryMoveSelectedVertex(next.x,next.y,0));c.geometryEndVertexDrag(false);QTRY_VERIFY_WITH_TIMEOUT(!c.geometryEditState().value("calculating").toBool(),5000);QVERIFY2(c.geometryEditState().value("previewReady").toBool(),qPrintable(c.geometryEditState().value("error").toString()));
        bool cut=false;for(const auto& value:c.geometryEditState().value("boundaryImpacts").toList())if(value.toMap().value("kind").toString()=="clip-child")cut=true;QVERIFY(cut);
        QVERIFY(!c.confirmGeometryEdit());QCOMPARE(c.documentBytes(),before);QVERIFY(c.geometryEditState().value("boundaryImpactConfirmation").toBool());QVERIFY(!c.confirmGeometryEdit());
        QVERIFY(QMetaObject::invokeMethod(&c,"geometryCancelBoundaryImpacts"));QVERIFY(!c.geometryEditState().value("previewReady").toBool());QVERIFY(!c.geometryEditState().value("boundaryImpactConfirmation").toBool());QCOMPARE(c.geometryEditState().value("stage").toString(),QString("selection"));QCOMPARE(c.documentBytes(),before);QVERIFY(!c.canUndo());QCOMPARE(c.geometryDraftPaths(),originalPaths);
        QVERIFY(c.geometrySelectNearest(p.x,p.y,.001));QVERIFY(c.geometryBeginVertexDrag());QVERIFY(c.geometryMoveSelectedVertex(next.x,next.y,0));c.geometryEndVertexDrag(false);QTRY_VERIFY_WITH_TIMEOUT(c.geometryEditState().value("previewReady").toBool(),5000);QVERIFY(!c.confirmGeometryEdit());bool confirmed=false;QVERIFY(QMetaObject::invokeMethod(&c,"geometryConfirmBoundaryImpacts",Q_RETURN_ARG(bool,confirmed)));QVERIFY(confirmed);QVERIFY(c.documentBytes()!=before);c.undo();QCOMPARE(c.documentBytes(),before);
    }
    void previewDiscardRestoresOriginalHandlesBeforeRepeatingGesture() {
        QTemporaryDir dir;const auto d=triple();EditorController c({false,dir.filePath("private.json")});QVERIFY(open(c,d,dir));QVERIFY(select(c,{"A","B","C"}));const auto before=c.documentBytes();QVERIFY(c.beginSharedBoundaryGeometry());QTRY_COMPARE_WITH_TIMEOUT(c.geometryEditState().value("boundaryStatus").toString(),QString("ready"),3000);const auto originalPaths=c.geometryDraftPaths();MapProjection projection;projection.rebuild(d);const auto p=projection.project({1,1}),next=projection.project({1,1.1});
        for(bool discard:{true,false}){QVERIFY(c.geometrySelectNearest(p.x,p.y,.001));QVERIFY(c.geometryBeginVertexDrag());QVERIFY(c.geometryMoveSelectedVertex(next.x,next.y,0));c.geometryEndVertexDrag(false);QTRY_VERIFY_WITH_TIMEOUT(c.geometryEditState()["previewReady"].toBool(),5000);if(discard){QVERIFY(c.geometryBack());QCOMPARE(c.geometryDraftPaths(),originalPaths);QCOMPARE(c.documentBytes(),before);QVERIFY(!c.geometryEditState()["canUndo"].toBool());}else{QVERIFY(c.confirmGeometryEdit());QVERIFY(c.documentBytes()!=before);}}
    }
    void missingOwnerAndAncestorCyclePreparationRejects() {
        auto d=triple();auto index=validateDocument(d);QVERIFY(!sharedboundary::Session::eligibility(d,index,{territorialRef("A"),territorialRef("missing")}).empty());
        staticParentRelation(d,"A").parentId="missing";QVERIFY(!sharedboundary::Session::eligibility(d,index,{territorialRef("A"),territorialRef("B")}).empty());
        d=triple();staticParentRelation(d,"A").parentId="C";staticParentRelation(d,"B").parentId="C";staticParentRelation(d,"C").parentId="A";QVERIFY(!sharedboundary::Session::eligibility(d,index,{territorialRef("A"),territorialRef("B")}).empty());
    }
    void virtualMidpointMovesOnlyRealOwnersAndPreservesHolesAndComponents() {
        auto d=triple();auto a=geometry(d,"A");a.polygons[0][0].erase(a.polygons[0][0].begin()+2);a.polygons[0].push_back(box(.1,.1,.2,.2).polygons[0][0]);a.polygons.push_back(box(-2,0,1,1).polygons[0]);
        const GeometryRef ref{"A-uneven",1};d.geometries.insert(ref,a);staticGeometryBinding(d,"A").geometryRef=ref;
        const auto session=sharedboundary::Session::prepare(d,validateDocument(d),{territorialRef("A"),territorialRef("B"),territorialRef("C")});QVERIFY2(session->valid(),session->error().c_str());
        const auto found=std::find_if(session->nodes().begin(),session->nodes().end(),[](const auto& n){return n.key==sharedboundary::nodeKey({1,.5});});QVERIFY(found!=session->nodes().end());QCOMPARE(found->owners.size(),std::size_t(2));QCOMPARE(found->virtualRefs.size(),std::size_t(1));QCOMPARE(found->virtualRefs[0].owner,std::string("A"));QCOMPARE(found->virtualRefs[0].index,std::size_t(1));QCOMPARE(found->virtualRefs[0].t,.5);
        const auto node=std::size_t(found-session->nodes().begin());QVERIFY(session->move(node,{1.1,.5}));const auto changed=session->changedDrafts();QCOMPARE(changed.size(),std::size_t(2));
        const auto& moved=session->drafts().at("A");QCOMPARE(moved.polygons[0][0].size(),std::size_t(6));QCOMPARE(moved.polygons[0][0][2].x,1.1);QCOMPARE(moved.polygons[0][0][2].y,.5);
        QVERIFY(sameGeometry(polygon(moved.polygons[0][1]),polygon(a.polygons[0][1])));QVERIFY(sameGeometry(polygon(moved.polygons[1][0]),polygon(a.polygons[1][0])));QVERIFY(sameGeometry(session->drafts().at("C"),geometry(d,"C")));QVERIFY(sameGeometry(geometry(d,"A"),a));
        for(const auto& [id,g]:session->drafts())for(const auto& p:g.polygons)for(const auto& ring:p){QCOMPARE(ring.front().x,ring.back().x);QCOMPARE(ring.front().y,ring.back().y);}
    }
    void sameParentExteriorFixedAndLockedDescendantTouchBlocks() {
        auto d=triple();d.geometries.insert({"P",1},box(0,0,2,2));appendTerritory(d,{"P","P","",UnitKind::General,false},{"P",1});d.presentation.objectStyles[territorialRef("P")]={};for(const auto id:{"A","B","C"})staticParentRelation(d,id).parentId="P";
        auto session=sharedboundary::Session::prepare(d,validateDocument(d),{territorialRef("A"),territorialRef("B"),territorialRef("C")});QVERIFY(session->valid());
        const auto find=[&](Point p){return std::size_t(std::find_if(session->nodes().begin(),session->nodes().end(),[&](const auto& n){return n.key==sharedboundary::nodeKey(p);})-session->nodes().begin());};
        QVERIFY(session->nodes().at(find({1,0})).fixed);QVERIFY(!session->nodes().at(find({1,1})).fixed);QVERIFY(session->move(find({1,1}),{1,1.1}));QVERIFY(sameGeometry(geometry(d,"P"),box(0,0,2,2)));
        d.geometries.insert({"L",1},box(.8,.2,.2,.6));appendTerritory(d,{"L","Locked child","",UnitKind::General,true},{"L",1},"A");d.presentation.objectStyles[territorialRef("L")]={};
        session=sharedboundary::Session::prepare(d,validateDocument(d),{territorialRef("A"),territorialRef("B"),territorialRef("C")});QVERIFY(session->valid());const auto midpoint=find({1,.5});QVERIFY(!session->nodes().at(midpoint).fixed);QVERIFY(!session->canMove(midpoint));QVERIFY(!session->beginDrag(midpoint));QVERIFY(!session->move(midpoint,{1.1,.5}));QVERIFY(session->changedDrafts().empty());
    }
    void javascriptNegativeHalfRoundingAndSourceReferenceOrder() {
        QCOMPARE(sharedboundary::nodeKey({-.00000005,.00000005}).first,0.);QCOMPARE(sharedboundary::nodeKey({-.00000005,.00000005}).second,.0000001);
        QCOMPARE(sharedboundary::nodeKey({-.00000015,0}).first,-.0000001);QCOMPARE(sharedboundary::nodeKeyText({-.00000005,.0000001}),std::string("0,1e-7"));QCOMPARE(sharedboundary::nodeKeyText({.000001,1.1}),std::string("0.000001,1.1"));
        const auto d=triple();const auto session=sharedboundary::Session::prepare(d,validateDocument(d),{territorialRef("C"),territorialRef("B"),territorialRef("A")});QVERIFY(session->valid());
        const auto found=std::find_if(session->nodes().begin(),session->nodes().end(),[](const auto& n){return n.key==sharedboundary::nodeKey({1,1});});QVERIFY(found!=session->nodes().end());QCOMPARE(found->refs.size(),std::size_t(3));QCOMPARE(found->refs[0].owner,std::string("A"));QCOMPARE(found->refs[0].index,std::size_t(3));QCOMPARE(found->refs[1].owner,std::string("B"));QCOMPARE(found->refs[1].index,std::size_t(3));QCOMPARE(found->refs[2].owner,std::string("C"));QCOMPARE(found->refs[2].index,std::size_t(1));
    }
    void invalidLatitudeGestureRejectsOnlyAfterDetachedDrag() {
        QTemporaryDir dir;const auto d=triple();EditorController c({false,dir.filePath("private.json")});QVERIFY(open(c,d,dir));QVERIFY(select(c,{"A","B","C"}));const auto before=c.documentBytes();QVERIFY(c.beginSharedBoundaryGeometry());QTRY_COMPARE_WITH_TIMEOUT(c.geometryEditState().value("boundaryStatus").toString(),QString("ready"),3000);
        MapProjection projection;projection.rebuild(d);const auto p=projection.project({1,1}),invalid=projection.project({1,100});QVERIFY(c.geometrySelectNearest(p.x,p.y,.001));QVERIFY(c.geometryBeginVertexDrag());QVERIFY(c.geometryMoveSelectedVertex(invalid.x,invalid.y,0));QCOMPARE(c.documentBytes(),before);QCOMPARE(c.geometryEditState().value("boundaryStatus").toString(),QString("ready"));
        c.geometryEndVertexDrag(false);QCOMPARE(c.geometryEditState().value("boundaryStatus").toString(),QString("error"));QVERIFY(!c.geometryEditState().value("previewReady").toBool());QCOMPARE(c.documentBytes(),before);QVERIFY(!c.canUndo());QVERIFY(c.geometryRetryBoundaryPreparation());QTRY_COMPARE_WITH_TIMEOUT(c.geometryEditState().value("boundaryStatus").toString(),QString("ready"),3000);
    }
    void sharedGestureDisplaysEveryIncidentSegmentIncludingCoast() {
        QTemporaryDir dir;const auto d=triple();EditorController c({false,dir.filePath("private.json")});QVERIFY(open(c,d,dir));QVERIFY(select(c,{"A","B","C"}));QVERIFY(c.beginSharedBoundaryGeometry());QTRY_COMPARE_WITH_TIMEOUT(c.geometryEditState().value("boundaryStatus").toString(),QString("ready"),3000);
        MapProjection projection;projection.rebuild(d);const auto p=projection.project({1,0}),next=projection.project({1,-.1});QVERIFY(c.geometrySelectNearest(p.x,p.y,.001));QVERIFY(c.geometryBeginVertexDrag());QVERIFY(c.geometryMoveSelectedVertex(next.x,next.y,0));
        int activeSegments=0;for(const auto& row:c.geometryDraftPaths())if(row.toMap().value("activeBoundary").toBool())++activeSegments;QCOMPARE(activeSegments,3);c.geometryEndVertexDrag(true);QVERIFY(!c.geometryEditState().value("calculating").toBool());
    }
    void dragKeepsOwnerDraftsDetachedUntilRelease() {
        const auto d=triple();auto session=sharedboundary::Session::prepare(d,validateDocument(d),{territorialRef("A"),territorialRef("B"),territorialRef("C")});QVERIFY(session->valid());
        const auto found=std::find_if(session->nodes().begin(),session->nodes().end(),[](const auto& node){return node.key==sharedboundary::nodeKey({1,1});});QVERIFY(found!=session->nodes().end());const auto id=std::size_t(found-session->nodes().begin());
        QVERIFY(session->beginDrag(id));QVERIFY(session->move(id,{1,1.1}));QCOMPARE(session->nodes()[id].coordinate.y,1.1);QVERIFY(session->changedDrafts().empty());QVERIFY(sameGeometry(session->drafts().at("A"),geometry(d,"A")));
        QVERIFY(session->move(id,{1,1.2}));QVERIFY(session->changedDrafts().empty());session->endDrag(false);QCOMPARE(session->changedDrafts().size(),std::size_t(3));QVERIFY(session->canUndo());QVERIFY(session->undo());QVERIFY(session->changedDrafts().empty());
    }
    void invalidCoordinatesAndCancelledPreparationAreInert() {
        const auto d=triple();auto session=sharedboundary::Session::prepare(d,validateDocument(d),{territorialRef("A"),territorialRef("B")},[]{return true;});QVERIFY(!session->valid());QCOMPARE(session->error(),std::string("BOUNDARY_PREPARATION_CANCELLED"));
        session=sharedboundary::Session::prepare(d,validateDocument(d),{territorialRef("A"),territorialRef("B")});QVERIFY(session->valid());QVERIFY(!session->move(0,{std::numeric_limits<double>::quiet_NaN(),0}));QVERIFY(!session->move(0,{0,91}));QVERIFY(session->changedDrafts().empty());QVERIFY(!session->canUndo());
    }
    void movedOwnerPatchesPreserveObservedTopologyOrder_data(){
        QTest::addColumn<QJsonObject>("definition");QFile file(QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("fixtures/web-m974/boundary-cases.json"));QVERIFY(file.open(QIODevice::ReadOnly));for(const auto& value:QJsonDocument::fromJson(file.readAll()).object()["cases"].toArray()){const auto row=value.toObject();const auto id=row["id"].toString();if(id=="root-uneven-multipolygon"||id=="root-disjoint-pairs"||id=="child-disjoint-pairs")QTest::newRow(qPrintable(id))<<row;}
    }
    void movedOwnerPatchesPreserveObservedTopologyOrder(){
        QFETCH(QJsonObject,definition);ProjectDocument document;document.documentId="m974-observed-owner-order";
        for(const auto& value:definition["features"].toArray()) {const auto feature=value.toObject(),source=feature["geometry"].toObject(),properties=feature["properties"].toObject();Geometry shape;shape.type=source["type"].toString().toStdString();auto polygons=source["coordinates"].toArray();if(shape.type=="Polygon"){QJsonArray wrapped;wrapped.append(polygons);polygons=wrapped;}for(const auto& polygon:polygons){Polygon rings;for(const auto& ring:polygon.toArray()){Ring coordinates;for(const auto& point:ring.toArray()){const auto xy=point.toArray();coordinates.push_back({xy[0].toDouble(),xy[1].toDouble()});}rings.push_back(std::move(coordinates));}shape.polygons.push_back(std::move(rings));}const auto id=feature["id"].toString().toStdString();const GeometryRef ref{id,1};document.geometries.insert(ref,shape);appendTerritory(document,{id,id,"",UnitKind::General,false},ref,properties["parentId"].toString().toStdString(),properties["coverageMode"].toString().toStdString());document.presentation.objectStyles[territorialRef(id)]={};}
        std::vector<ObjectRef> selected;for(const auto& value:definition["selectedIds"].toArray())selected.push_back(territorialRef(value.toString().toStdString()));const auto session=sharedboundary::Session::prepare(document,validateDocument(document),selected);QVERIFY2(session->valid(),session->error().c_str());const auto move=definition["move"].toObject();const auto found=std::find_if(session->nodes().begin(),session->nodes().end(),[&](const auto& node){return QString::fromStdString(sharedboundary::nodeKeyText(node.coordinate))==move["nodeKey"].toString();});QVERIFY(found!=session->nodes().end());QCOMPARE(found->owners,std::vector<std::string>({"B","A"}));const auto node=std::size_t(found-session->nodes().begin());const auto coordinate=move["coordinate"].toArray();QVERIFY(session->move(node,{coordinate[0].toDouble(),coordinate[1].toDouble()}));const auto patches=session->changedDrafts();QCOMPARE(patches.size(),std::size_t(2));QCOMPARE(patches[0].owner.id,std::string("B"));QCOMPARE(patches[1].owner.id,std::string("A"));
    }
    void boundaryCommitSelectionAndHistoryFollowRootOrChildPolicy_data(){QTest::addColumn<bool>("child");QTest::newRow("root-first-node-owner")<<false;QTest::newRow("child-original-seed")<<true;}
    void boundaryCommitSelectionAndHistoryFollowRootOrChildPolicy(){
        QFETCH(bool,child);auto d=triple();if(child){d.geometries.insert({"P",1},box(0,0,2,2));appendTerritory(d,{"P","Parent","",UnitKind::General,false},{"P",1});d.presentation.objectStyles[territorialRef("P")]={};for(const auto id:{"A","B","C"})staticParentRelation(d,id).parentId="P";}else{auto a=geometry(d,"A");a.polygons[0][0].erase(a.polygons[0][0].begin()+2);d.geometries.insert({"uneven-A",1},a);staticGeometryBinding(d,"A").geometryRef={"uneven-A",1};}
        QTemporaryDir dir;EditorController c({false,dir.filePath("private.json")});QVERIFY(open(c,d,dir));if(child)QVERIFY(select(c,{"B","A","C"}));else QVERIFY(select(c,{"A","B","C"}));QCOMPARE(c.selectedId(),QString("C"));const auto before=c.documentBytes();QVERIFY(c.beginSharedBoundaryGeometry());QTRY_COMPARE_WITH_TIMEOUT(c.geometryEditState()["boundaryStatus"].toString(),QString("ready"),3000);MapProjection projection;projection.rebuild(d);const auto from=projection.project(child?Point{1,1}:Point{1,.5}),to=projection.project(child?Point{1,1.1}:Point{1.1,.5});QVERIFY(c.geometrySelectNearest(from.x,from.y,.001));QVERIFY(c.geometryBeginVertexDrag());QVERIFY(c.geometryMoveSelectedVertex(to.x,to.y,0));c.geometryEndVertexDrag(false);QTRY_VERIFY_WITH_TIMEOUT(c.geometryEditState()["previewReady"].toBool(),5000);QVERIFY(c.confirmGeometryEdit());const auto after=c.documentBytes();QVERIFY(after!=before);QCOMPARE(c.selectionItems().size(),1);QCOMPARE(c.selectedId(),QString("B"));
        c.undo();QCOMPARE(c.documentBytes(),before);QVERIFY(c.selectionItems().isEmpty());c.selectCountry("A");c.redo();QCOMPARE(c.documentBytes(),after);QVERIFY(c.selectionItems().isEmpty());c.selectCountry("A");c.redo();QCOMPARE(c.selectedId(),QString("A"));
    }
    void unrelatedPropertyHistoryRetainsSelectionAfterBoundaryCommit(){
        QTemporaryDir dir;const auto d=triple();EditorController c({false,dir.filePath("private.json")});QVERIFY(open(c,d,dir));QVERIFY(select(c,{"A","B","C"}));QVERIFY(c.beginSharedBoundaryGeometry());QTRY_COMPARE_WITH_TIMEOUT(c.geometryEditState()["boundaryStatus"].toString(),QString("ready"),3000);MapProjection projection;projection.rebuild(d);const auto from=projection.project({1,1}),to=projection.project({1,1.1});QVERIFY(c.geometrySelectNearest(from.x,from.y,.001));QVERIFY(c.geometryBeginVertexDrag());QVERIFY(c.geometryMoveSelectedVertex(to.x,to.y,0));c.geometryEndVertexDrag(false);QTRY_VERIFY_WITH_TIMEOUT(c.geometryEditState()["previewReady"].toBool(),5000);QVERIFY(c.confirmGeometryEdit());QCOMPARE(c.selectedId(),QString("A"));const auto boundary=c.documentBytes();c.setColor("#102030");QVERIFY(c.documentBytes()!=boundary);c.undo();QCOMPARE(c.documentBytes(),boundary);QCOMPARE(c.selectedId(),QString("A"));c.redo();QCOMPARE(c.selectedId(),QString("A"));c.undo();QCOMPARE(c.selectedId(),QString("A"));c.undo();QVERIFY(c.selectionItems().isEmpty());
    }
    void supplementalMovedNodeRetainsAllActualOwnerPatches_data(){
        QTest::addColumn<QString>("caseId");QTest::addColumn<bool>("moves");
        QTest::newRow("supp-v1-owner-already-destination-4e-10")<<QString("supp-v1-owner-already-destination-4e-10")<<false;
        QTest::newRow("supp-v1-owner-already-destination-4e-8")<<QString("supp-v1-owner-already-destination-4e-8")<<true;
    }
    void supplementalMovedNodeRetainsAllActualOwnerPatches(){
        QFETCH(QString,caseId);QFETCH(bool,moves);QFile file(QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("fixtures/web-m974/supplemental-v1-cases.json"));QVERIFY(file.open(QIODevice::ReadOnly));const auto inputs=QJsonDocument::fromJson(file.readAll()).object();QJsonObject definition;for(const auto& value:inputs["cases"].toArray())if(value.toObject()["id"].toString()==caseId)definition=value.toObject();QVERIFY(!definition.isEmpty());
        ProjectDocument document;document.documentId="m974-supplemental-owner-set";for(const auto& value:definition["features"].toArray()) {const auto feature=value.toObject(),properties=feature["properties"].toObject(),source=feature["geometry"].toObject();QCOMPARE(source["type"].toString(),QString("Polygon"));QCOMPARE(properties["entityKind"].toString(),QString("general"));QVERIFY(properties["parentId"].toString().isEmpty());Geometry shape;shape.type="Polygon";Polygon rings;for(const auto& ring:source["coordinates"].toArray()){Ring coordinates;for(const auto& point:ring.toArray()){const auto xy=point.toArray();coordinates.push_back({xy[0].toDouble(),xy[1].toDouble()});}rings.push_back(std::move(coordinates));}shape.polygons.push_back(std::move(rings));const auto id=feature["id"].toString().toStdString();const GeometryRef ref{id,1};document.geometries.insert(ref,shape);appendTerritory(document,{id,id,"",UnitKind::General,false},ref);document.presentation.objectStyles[territorialRef(id)]={};}
        std::vector<ObjectRef> selected;for(const auto& value:definition["selectedIds"].toArray())selected.push_back(territorialRef(value.toString().toStdString()));const auto session=sharedboundary::Session::prepare(document,validateDocument(document),selected);QVERIFY2(session->valid(),session->error().c_str());const auto move=definition["move"].toObject();const auto found=std::find_if(session->nodes().begin(),session->nodes().end(),[&](const auto& node){return QString::fromStdString(sharedboundary::nodeKeyText(node.coordinate))==move["nodeKey"].toString();});QVERIFY(found!=session->nodes().end());const auto node=std::size_t(found-session->nodes().begin());const auto coordinate=move["coordinate"].toArray();QVERIFY(session->beginDrag(node));QCOMPARE(session->move(node,{coordinate[0].toDouble(),coordinate[1].toDouble()}),moves);QCOMPARE(session->endDrag(false),moves);
        const auto patches=session->changedDrafts();if(!moves){QVERIFY(patches.empty());QVERIFY(!session->canUndo());return;}
        QCOMPARE(patches.size(),std::size_t(2));std::set<std::string> owners;for(const auto& patch:patches)owners.insert(patch.owner.id);QCOMPARE(owners,std::set<std::string>({"A","B"}));QVERIFY(sameGeometry(session->drafts().at("B"),geometry(document,"B")));QVERIFY(!sameGeometry(session->drafts().at("A"),geometry(document,"A")));
    }
    void unselectedThirdOwnerFixedAndBoundaryInsertDeleteDisabled() {
        QTemporaryDir dir;const auto d=triple();EditorController c({false,dir.filePath("private.json")});QVERIFY(open(c,d,dir));QVERIFY(select(c,{"A","B"}));QVERIFY(c.beginSharedBoundaryGeometry());
        QTRY_COMPARE_WITH_TIMEOUT(c.geometryEditState().value("boundaryStatus").toString(),QString("ready"),3000);MapProjection projection;projection.rebuild(d);
        const auto node=handle(c,projection,{1,1});QVERIFY(node.value("fixed").toBool());const auto p=projection.project({1,1});QVERIFY(!c.geometrySelectNearest(p.x,p.y,.001));QVERIFY(!c.geometryBeginVertexDrag());QVERIFY(!c.geometryMoveSelectedVertex(p.x+.1,p.y,0));
        const auto middle=projection.project({1,.5});QVERIFY(c.geometrySelectNearest(middle.x,middle.y,.001));QVERIFY(!c.geometryDeleteSelectedVertex());QVERIFY(!c.geometryInsertNearest(middle.x,middle.y,1));QVERIFY(!c.geometryEditState().value("canUndo").toBool());
    }
};
QTEST_MAIN(M974BoundarySessionTests)
#include "m974_boundary_session_tests.moc"

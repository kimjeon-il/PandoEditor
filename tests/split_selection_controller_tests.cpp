#include "territorial_fixture.h"
#include "editorcontroller.h"
#include "projectcodec.h"
#include "mapprojection.h"
#include "territorycutadapter.h"
#include <pandoeditor/geometrypredicates.h>
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QSemaphore>
#include <QThreadPool>
using namespace pandoeditor;
namespace {
Geometry rectangle(double x,double y,double w,double h) { Geometry g;g.type="Polygon";g.polygons={{{{x,y},{x,y+h},{x+w,y+h},{x+w,y},{x,y}}}};return g; }
QUrl writeProject(const QString& path,bool island=false,bool lockedParent=false) {
 ProjectDocument d({{"parent","Parent",rectangle(-5,-5,30,30).polygons,0xabcdef}},{{"countries","Countries"}});
 const GeometryRef ref{"source-shape",1}; auto shape=rectangle(0,0,10,10);if(island){shape.type="MultiPolygon";shape.polygons.push_back(rectangle(20,0,2,2).polygons.front());}d.geometries.insert(ref,shape);
 appendTerritory(d,{"source","Source","",UnitKind::General,false},ref,"parent");
 staticParentRelation(d,"source").coverageMode="partition";
 d.presentation.objectStyles[territorialRef("source")]={};
 d.units.front().locked=lockedParent;Project p;p.replace(std::move(d));QFile f(path);if(!f.open(QIODevice::WriteOnly))throw std::runtime_error("fixture write failed");f.write(projectcodec::encode(p));f.close();return QUrl::fromLocalFile(path);
}
// Hold the real calculation pool so a camera change happens after submission
// and before the cut worker starts, without adding a production test hook.
struct SelectionPoolGate {
 struct State {QSemaphore started,release;};
 std::shared_ptr<State> state=std::make_shared<State>();
 int previous=QThreadPool::globalInstance()->maxThreadCount();
 SelectionPoolGate(){auto* pool=QThreadPool::globalInstance();pool->waitForDone();pool->setMaxThreadCount(1);pool->start([gate=state]{gate->started.release();gate->release.acquire();});}
 ~SelectionPoolGate(){state->release.release();QThreadPool::globalInstance()->waitForDone();QThreadPool::globalInstance()->setMaxThreadCount(previous);}
 bool started(){return state->started.tryAcquire(1,5000);}
};
}
class SplitSelectionControllerTests:public QObject {
 Q_OBJECT
private slots:
 void queuedLineDraftUsesFinishTimeViewAndCoarsePointer_data() {
  QTest::addColumn<bool>("mobile");
  QTest::newRow("mouse")<<false;QTest::newRow("touch")<<true;
 }
 void queuedLineDraftUsesFinishTimeViewAndCoarsePointer() {
  QFETCH(bool,mobile);QTemporaryDir dir;EditorController c({mobile,dir.filePath("private.json")});
  QVERIFY(c.openFile(writeProject(dir.filePath("input.json"))));c.selectCountry("source");const auto before=c.documentBytes();const auto revision=c.revision();
  MapProjection projection;projection.rebuild(projectcodec::decode(before));
  QVERIFY(c.beginSplitGeometry());QVERIFY(c.geometryAdvanceStage());QVERIFY(c.geometrySelectTerritoryMethod("line"));
  QTRY_VERIFY_WITH_TIMEOUT(!c.geometryEditState()["calculating"].toBool(),10000);
  QVERIFY(c.setProjectionMode("flat"));
  QVERIFY(c.publishMapView({{"viewportWidth",1024.},{"viewportHeight",768.},{"scale",500.},{"translateX",512.},{"translateY",384.},{"centerLongitude",5.},{"centerLatitude",5.}}));
  // At scale 500 these interior endpoints snap to the source boundary. The
  // touch case needs the existing 18px coarse threshold rather than mouse 10px.
  const double inset=mobile?1.5:.5;
  TerritoryCutRequest input;input.source=rectangle(0,0,10,10);input.coarsePointer=mobile;
  input.view.scale=500;input.view.translateX=512;input.view.translateY=384;input.view.centerLongitude=5;input.view.centerLatitude=5;input.view.viewportWidth=1024;input.view.viewportHeight=768;
  for(const auto point:{Point{inset,5},Point{10-inset,5}}){const auto xy=projection.project(point);input.coordinates.push_back(projection.unproject(xy.x,xy.y));QVERIFY(c.geometryAddPoint(xy.x,xy.y,0));}
  const auto expected=prepareTerritoryLineCandidates(input);QCOMPARE(expected.status,GeometryOperationStatus::Completed);QCOMPARE(expected.candidates.size(),std::size_t(2));
  auto late=input;late.view.scale=5000;
  // This counterfactual makes a late camera read observably wrong, rather than
  // merely asserting the same geometry under two irrelevant view settings.
  QCOMPARE(prepareTerritoryLineCandidates(late).status,GeometryOperationStatus::Empty);
  if(mobile){auto mouse=input;mouse.coarsePointer=false;QCOMPARE(prepareTerritoryLineCandidates(mouse).status,GeometryOperationStatus::Empty);}
  {
   SelectionPoolGate gate;QVERIFY(gate.started());QVERIFY(c.geometryFinishTerritoryDraft());QVERIFY(c.geometryEditState()["selectionPending"].toBool());
   QVERIFY(c.publishMapView({{"scale",5000.}}));QCOMPARE(c.mapViewState()["scale"].toDouble(),5000.);QVERIFY(c.geometryEditState()["selectionPending"].toBool());
  }
  QTRY_VERIFY_WITH_TIMEOUT(!c.geometryEditState()["calculating"].toBool(),15000);
  const auto candidates=c.riverSelectionObservation()["candidates"].toList();QCOMPARE(candidates.size(),int(expected.candidates.size()));
  // setCandidates assigns its existing session-local IDs after the adapter
  // preserves the worker IDs; geometry, order and area remain unchanged.
  for(int i=0;i<candidates.size();++i){const auto actual=candidates[i].toMap();const auto& candidate=expected.candidates[std::size_t(i)];QCOMPARE(actual["id"].toString(),QStringLiteral("territory-candidates-1:%1").arg(i));QVERIFY(candidate.area);QCOMPARE(actual["area"].toDouble(),*candidate.area);
   const auto geometry=actual["geometry"].toMap();QCOMPARE(geometry["type"].toString(),QString::fromStdString(candidate.geometry.type));
   QVariantList polygons;for(const auto& polygon:candidate.geometry.polygons){QVariantList rings;for(const auto& ring:polygon){QVariantList points;for(const auto& point:ring)points.push_back(QVariantList{point.x,point.y});rings.push_back(QVariant(points));}polygons.push_back(QVariant(rings));}
   QCOMPARE(geometry["coordinates"].toList(),candidate.geometry.type=="Polygon"?polygons.front().toList():polygons);
  }
  QCOMPARE(c.documentBytes(),before);QCOMPARE(c.revision(),revision);c.cancelGeometryEdit();QCOMPARE(c.documentBytes(),before);
 }
 void multipleCrossingsUseSelectableFragmentsAndOneCreatedSibling() {
  QTemporaryDir dir;EditorController c;QVERIFY(c.openFile(writeProject(dir.filePath("input.json"),true)));c.selectCountry("source");
  const auto before=c.documentBytes();MapProjection projection;projection.rebuild(projectcodec::decode(before));
  QVERIFY(c.beginSplitGeometry());QVERIFY(c.geometryAdvanceStage());QVERIFY(c.geometrySelectTerritoryMethod("line"));
  QTRY_VERIFY_WITH_TIMEOUT(!c.geometryEditState().value("calculating").toBool(),10000);
  for(const auto point:{Point{-1,2},Point{11,2},Point{11,5},Point{-1,5},Point{-1,8},Point{11,8}}){const auto p=projection.project(point);QVERIFY(c.geometryAddPoint(p.x,p.y,0));}
  QVERIFY(c.geometryFinishTerritoryDraft());
  QTRY_VERIFY_WITH_TIMEOUT(!c.geometryEditState().value("calculating").toBool(),15000);
  QVERIFY2(c.geometryEditState().value("error").toString().isEmpty(),qPrintable(c.geometryEditState().value("error").toString()));
  QCOMPARE(c.geometryEditState().value("candidates").toList().size(),4);
  QCOMPARE(c.riverSelectionObservation().value("candidates").toList().size(),4);
  auto state=c.geometryEditState();QCOMPARE(state.value("selectedCandidateIds").toList().size(),1);
  const auto first=state.value("selectedCandidateIds").toList().front().toString();QString second;
  for(const auto item:state.value("candidates").toList())if(item.toMap().value("id").toString()!=first){second=item.toMap().value("id").toString();break;}
  QVERIFY(c.geometryToggleTerritoryCandidate(second));
  QTRY_VERIFY_WITH_TIMEOUT(c.geometryEditState().value("canAddPart").toBool(),15000);
  QVERIFY(c.geometryAddTerritoryPart());QTRY_VERIFY_WITH_TIMEOUT(c.geometryEditState().value("canAdvance").toBool(),15000);
  QCOMPARE(c.documentBytes(),before);QVERIFY(c.geometryAdvanceStage());QVERIFY(c.confirmGeometryEdit());
  QTRY_VERIFY_WITH_TIMEOUT(!c.geometryEditState().value("active").toBool(),15000);
  const auto after=c.documentBytes();const auto document=projectcodec::decode(after);QCOMPARE(document.units.size(),std::size_t(3));
  const auto source=document.geometries.get(staticGeometryBinding(document,"source").geometryRef);QVERIFY(source);QVERIFY(geometryContains(*source,rectangle(20,0,2,2)));
  c.undo();QCOMPARE(c.documentBytes(),before);c.redo();QCOMPARE(c.documentBytes(),after);
 }
 void changingChildSelectionAfterPreviewRejectsCommit() {
  QTemporaryDir dir;EditorController c;QVERIFY(c.openFile(writeProject(dir.filePath("input.json"))));c.selectCountry("source");const auto before=c.documentBytes();MapProjection projection;projection.rebuild(projectcodec::decode(before));
  QVERIFY(c.beginSplitGeometry());QVERIFY(c.geometryAdvanceStage());QVERIFY(c.geometrySelectTerritoryMethod("line"));QTRY_VERIFY_WITH_TIMEOUT(!c.geometryEditState()["calculating"].toBool(),10000);
  for(const auto point:{Point{-1,5},Point{11,5}}){const auto xy=projection.project(point);QVERIFY(c.geometryAddPoint(xy.x,xy.y,0));}
  QVERIFY(c.geometryFinishTerritoryDraft());QTRY_VERIFY_WITH_TIMEOUT(c.geometryEditState()["canAddPart"].toBool(),15000);QVERIFY(c.geometryAddTerritoryPart());QTRY_VERIFY_WITH_TIMEOUT(c.geometryEditState()["canAdvance"].toBool(),15000);QVERIFY(c.geometryAdvanceStage());
  QSignalSpy contextChanged(&c,&EditorController::geometryEditChanged);c.selectCountry("parent");QVERIFY(contextChanged.count()>0);QVERIFY(!c.confirmGeometryEdit());QTest::qWait(100);QCOMPARE(c.documentBytes(),before);c.cancelGeometryEdit();
 }
 void changingRootSelectionAfterPreviewKeepsFixedSource() {
  QTemporaryDir dir;EditorController c;QVERIFY(c.openFile(writeProject(dir.filePath("input.json"))));c.selectCountry("parent");const auto before=c.documentBytes();MapProjection projection;projection.rebuild(projectcodec::decode(before));
  QVERIFY(c.beginSplitGeometry());QVERIFY(c.geometryAdvanceStage());QVERIFY(c.geometrySelectTerritoryMethod("line"));QTRY_VERIFY_WITH_TIMEOUT(!c.geometryEditState()["calculating"].toBool(),10000);
  for(const auto point:{Point{-6,15},Point{26,15}}){const auto xy=projection.project(point);QVERIFY(c.geometryAddPoint(xy.x,xy.y,0));}
  QVERIFY(c.geometryFinishTerritoryDraft());QTRY_VERIFY_WITH_TIMEOUT(c.geometryEditState()["canAddPart"].toBool(),15000);QVERIFY(c.geometryAddTerritoryPart());QTRY_VERIFY_WITH_TIMEOUT(c.geometryEditState()["canAdvance"].toBool(),15000);QVERIFY(c.geometryAdvanceStage());
  c.selectCountry("source");QVERIFY(c.confirmGeometryEdit());QTRY_VERIFY_WITH_TIMEOUT(!c.geometryEditState()["active"].toBool(),15000);QVERIFY(c.documentBytes()!=before);c.undo();QCOMPARE(c.documentBytes(),before);
 }
 void splitPolygonCandidateUsesWebTypeAndWinding() {
  QTemporaryDir dir;EditorController c;QVERIFY(c.openFile(writeProject(dir.filePath("input.json"))));c.selectCountry("source");const auto before=c.documentBytes();MapProjection projection;projection.rebuild(projectcodec::decode(before));
  QVERIFY(c.beginSplitGeometry());QVERIFY(c.geometryAdvanceStage());QVERIFY(c.geometrySelectTerritoryMethod("polygon"));QTRY_VERIFY_WITH_TIMEOUT(!c.geometryEditState()["calculating"].toBool(),10000);
  for(const auto point:{Point{0,1},Point{3,1},Point{3,4},Point{0,4}}){const auto xy=projection.project(point);QVERIFY(c.geometryAddPoint(xy.x,xy.y,0));}
  QVERIFY(c.geometryFinishTerritoryDraft());QTRY_VERIFY_WITH_TIMEOUT(!c.geometryEditState()["calculating"].toBool(),15000);const auto candidates=c.riverSelectionObservation()["candidates"].toList();QCOMPARE(candidates.size(),1);
  const auto geometry=candidates.front().toMap()["geometry"].toMap();QCOMPARE(geometry["type"].toString(),QString("Polygon"));
  const auto ring=geometry["coordinates"].toList().front().toList();QCOMPARE(ring.front().toList(),QVariantList({0.,4.}));QCOMPARE(c.documentBytes(),before);c.cancelGeometryEdit();
 }
 void splitPolygonDraftUndoRedoIsInert() {
  QTemporaryDir dir;EditorController c;QVERIFY(c.openFile(writeProject(dir.filePath("input.json"))));c.selectCountry("source");const auto before=c.documentBytes();MapProjection projection;projection.rebuild(projectcodec::decode(before));
  QVERIFY(c.beginSplitGeometry());QVERIFY(c.geometryAdvanceStage());QVERIFY(c.geometrySelectTerritoryMethod("polygon"));QTRY_VERIFY_WITH_TIMEOUT(!c.geometryEditState()["calculating"].toBool(),10000);
  for(const auto point:{Point{1,1},Point{3,1},Point{3,3}}){const auto xy=projection.project(point);QVERIFY(c.geometryAddPoint(xy.x,xy.y,0));}
  const auto draft=c.geometryDraftPaths();QVERIFY(c.geometryEditState()["canUndoDraft"].toBool());QVERIFY(c.geometryUndoDraft());QVERIFY(c.geometryDraftPaths()!=draft);QVERIFY(c.geometryRedoDraft());QCOMPARE(c.geometryDraftPaths(),draft);QCOMPARE(c.documentBytes(),before);c.cancelGeometryEdit();QCOMPARE(c.documentBytes(),before);
 }
 void lockedParentRejectsSplitBeforeOpeningSession() {
  QTemporaryDir dir;EditorController c;QVERIFY(c.openFile(writeProject(dir.filePath("input.json"),false,true)));c.selectCountry("source");const auto before=c.documentBytes();
  QVERIFY(!c.beginSplitGeometry());QVERIFY(!c.geometryEditState().value("active").toBool());QCOMPARE(c.documentBytes(),before);
 }
 void splitEntryUsesBoundedSelectionAndCancelIsInert() {
  QTemporaryDir dir; EditorController c;QVERIFY(c.openFile(writeProject(dir.filePath("input.json"))));c.selectCountry("source");
  const auto bytes=c.documentBytes();const auto revision=c.revision();
  QVERIFY(c.beginSplitGeometry());auto state=c.geometryEditState();
  QVERIFY2(state.value("territorySelection").toBool(),"split must use the production bounded selection workflow");
  QCOMPARE(state.value("tool").toString(),QString("split"));
  QCOMPARE(state.value("providers").toList().size(),1);
  QVERIFY(c.geometryAdvanceStage());QVERIFY(c.geometrySelectTerritoryMethod("line"));
  QTRY_VERIFY_WITH_TIMEOUT(!c.geometryEditState().value("calculating").toBool(),10000);
  QCOMPARE(c.geometryEditState().value("selectionPhase").toString(),QString("drawing"));
  const auto observation=c.riverSelectionObservation();QVERIFY(observation.contains("combinedGeometry"));QVERIFY(observation["combinedGeometry"].isNull());QVERIFY(observation.contains("remainingGeometry"));QVERIFY(observation.contains("splitPreview"));QVERIFY(observation["splitPreview"].isNull());QVERIFY(observation.contains("splitPreviewPresent"));QVERIFY(!observation["splitPreviewPresent"].toBool());QVERIFY(observation.contains("splitPreviewReceiptPresent"));QVERIFY(!observation["splitPreviewReceiptPresent"].toBool());
  c.cancelGeometryEdit();QCOMPARE(c.documentBytes(),bytes);QCOMPARE(c.revision(),revision);QVERIFY(!c.geometryEditState().value("active").toBool());
 }
};
QTEST_MAIN(SplitSelectionControllerTests)
#include "split_selection_controller_tests.moc"

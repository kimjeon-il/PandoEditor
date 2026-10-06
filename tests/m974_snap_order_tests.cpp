#include "editorcontroller.h"
#include "geometrysnapprovider.h"
#include "projectcodec.h"
#include "territorial_fixture.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QThreadPool>
#include <QSemaphore>
using namespace pandoeditor;
namespace {
Geometry square(double x=0){Geometry g;g.type="Polygon";g.polygons={{{{x,0},{x,2},{x+2,2},{x+2,0},{x,0}}}};return g;}
QString write(Project& project,const QString& path){QFile file(path);if(!file.open(QIODevice::WriteOnly))return {};file.write(projectcodec::encode(project));file.close();return path;}
QJsonArray sourceGeometries(const ProjectDocument& document){
 QJsonArray result;for(const auto& unit:document.units){const auto geometry=document.geometries.get(staticGeometryBinding(document,unit.id).geometryRef);QJsonArray polygons;for(const auto& polygon:geometry->polygons){QJsonArray rings;for(const auto& ring:polygon){QJsonArray coordinates;for(const auto point:ring)coordinates.append(QJsonArray{point.x,point.y});rings.append(coordinates);}polygons.append(rings);}result.append(QJsonObject{{"id",QString::fromStdString(unit.id)},{"geometry",QJsonObject{{"type",QString::fromStdString(geometry->type)},{"coordinates",geometry->type=="Polygon"?polygons.first():QJsonValue(polygons)}}}});}return result;
}
QJsonArray historySourceGeometries(const QByteArray& bytes){
 const auto document=projectcodec::decode(bytes);auto rows=sourceGeometries(document);
 for(int i=0;i<rows.size();++i){auto row=rows[i].toObject();row["domain"]="territorial";rows[i]=row;}
 for(const auto& feature:document.genericFeatures){const auto geometry=document.geometries.get(feature.geometry);if(!geometry)continue;QJsonArray polygons;
  for(const auto& polygon:geometry->polygons){QJsonArray rings;for(const auto& ring:polygon){QJsonArray points;for(const auto point:ring)points.append(QJsonArray{point.x,point.y});rings.append(points);}polygons.append(rings);}
  rows.append(QJsonObject{{"domain","generic"},{"id",QString::fromStdString(feature.id)},{"geometry",QJsonObject{{"type",QString::fromStdString(geometry->type)},{"coordinates",geometry->type=="Polygon"?polygons.first():QJsonValue(polygons)}}}});
 }return rows;
}
bool writeHistoryEvidence(const QString& scope,const QByteArray& before,const QByteArray& after,
 const QVariantMap& warm,const QVariantMap& final,const QJsonObject& transitions){
 const auto path=qEnvironmentVariable("PANDO_M974_SOURCE_HISTORY_EVIDENCE");if(path.isEmpty())return true;
 QJsonArray cases;QFile existing(path);if(existing.exists()){if(!existing.open(QIODevice::ReadOnly))return false;QJsonParseError error;const auto document=QJsonDocument::fromJson(existing.readAll(),&error);if(error.error!=QJsonParseError::NoError||!document.isObject())return false;cases=document.object()["cases"].toArray();}
 const auto sha=[](const QByteArray& bytes){return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());};
 const auto* tag=QTest::currentDataTag();const QString id=tag&&*tag?QString::fromUtf8(tag):QString::fromUtf8(QTest::currentTestFunction());
 cases.append(QJsonObject{{"case",id},{"scope",scope},{"sourceGeometries",historySourceGeometries(before)},{"restoredSourceGeometries",historySourceGeometries(after)},
  {"warm",QJsonObject{{"indicator",QJsonObject::fromVariantMap(warm)}}},{"final",QJsonObject{{"indicator",QJsonObject::fromVariantMap(final)}}},
  {"canonicalEqualAfterUndo",semanticallyEqual(projectcodec::decode(before),projectcodec::decode(after))},
  {"beforeDocumentBytesBase64",QString::fromLatin1(before.toBase64())},{"afterDocumentBytesBase64",QString::fromLatin1(after.toBase64())},{"beforeDocumentSha256",sha(before)},{"afterDocumentSha256",sha(after)},{"transitions",transitions}});
 QSaveFile output(path);if(!output.open(QIODevice::WriteOnly))return false;const auto bytes=QJsonDocument(QJsonObject{{"schema","pando-m974-native-source-history"},{"version",1},{"cases",cases}}).toJson(QJsonDocument::Compact);return output.write(bytes)==bytes.size()&&output.commit();
}
QString snapOwner(EditorController& controller,const QString& selected,QVariantMap* observedIndicator=nullptr){
 if(!controller.selectObject({{"domain","territorial"},{"id",selected}},"replace"))return "selection-failed";
 if(!controller.beginGeometryDraw())return "draw-failed";
 MapProjection projection;projection.rebuild(projectcodec::decode(controller.documentBytes()));const auto point=projection.project({0,0});
 if(!controller.geometryAddPoint(point.x,point.y,.1,"mouse"))return "cold-input-failed";
 QElapsedTimer clock;clock.start();while(controller.geometrySnapState().value("status").toString()=="pending"&&clock.elapsed()<10000){QCoreApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}
 if(controller.geometrySnapState().value("status").toString()!="ready")return "not-ready";
 if(!controller.geometryAddPoint(point.x,point.y,.1,"mouse"))return "ready-input-failed";
 const auto indicator=controller.geometrySnapState().value("indicator").toMap();if(observedIndicator)*observedIndicator=indicator;const auto owners=indicator.value("ownerIds").toList();
 const auto result=indicator.value("kind").toString()+":"+(owners.empty()?"missing":owners.front().toString());controller.cancelGeometryEdit();return result;
}
geometrysnap::Provider* sourceProvider(EditorController& controller){
 for(auto* child:controller.children())if(auto* provider=dynamic_cast<geometrysnap::Provider*>(child))return provider;
 return nullptr;
}
Project selectionProject(bool child=false){
 ProjectDocument document(std::vector<Country>{{"root","root",square(10).polygons,0xabcdef},{"donor","donor",square(20).polygons,0xabcdef},{"other","other",square(30).polygons,0xabcdef}},{{"countries","Countries"}});
 if(child){const GeometryRef ref{"child-source",1};document.geometries.insert(ref,square(10));appendTerritory(document,{"child","child","",UnitKind::General,false},ref,"root");document.presentation.objectStyles[territorialRef("child")]={};}
 for(const auto id:{"ga","gb"}){GenericFeature feature;feature.id=id;feature.geometry={std::string("generic-")+id,1};document.geometries.insert(feature.geometry,square());document.genericFeatures.push_back(feature);}
 Project project;project.replace(document);return project;
}
// Hold the real global calculation pool, rather than racing a small polygon
// against a sleep. The selection request is submitted, but cannot settle.
struct SelectionPoolGate {
 struct State {QSemaphore started,release;};
 std::shared_ptr<State> state=std::make_shared<State>();
 int previous=QThreadPool::globalInstance()->maxThreadCount();
 SelectionPoolGate(){auto* pool=QThreadPool::globalInstance();pool->waitForDone();pool->setMaxThreadCount(1);pool->start([gate=state]{gate->started.release();gate->release.acquire();});}
 ~SelectionPoolGate(){state->release.release();QThreadPool::globalInstance()->waitForDone();QThreadPool::globalInstance()->setMaxThreadCount(previous);}
 bool started(){return state->started.tryAcquire(1,5000);}
};
}
class M974SnapOrderTests : public QObject {
 Q_OBJECT
private slots:
 void initTestCase(){
  const auto path=qEnvironmentVariable("PANDO_M974_SOURCE_HISTORY_EVIDENCE");if(path.isEmpty())return;QSaveFile output(path);QVERIFY(output.open(QIODevice::WriteOnly));const auto bytes=QJsonDocument(QJsonObject{{"schema","pando-m974-native-source-history"},{"version",1},{"cases",QJsonArray{}}}).toJson(QJsonDocument::Compact);QCOMPARE(output.write(bytes),qint64(bytes.size()));QVERIFY(output.commit());
 }
 void selectionSourceHistory_data(){
  QTest::addColumn<int>("path");QTest::addColumn<QString>("expected");
  QTest::newRow("split-setup-only")<<0<<QString("vertex:ga");
  QTest::newRow("annex-setup-only")<<1<<QString("vertex:ga");
  QTest::newRow("split-components-ready")<<2<<QString("vertex:gb");
  QTest::newRow("annex-components-ready")<<3<<QString("vertex:gb");
  QTest::newRow("component-timer-only-cancel")<<4<<QString("vertex:ga");
  QTest::newRow("component-request-pending-cancel")<<5<<QString("vertex:ga");
 }
 void selectionSourceHistory(){
  QFETCH(int,path);QFETCH(QString,expected);QTemporaryDir directory;auto project=selectionProject();
  const auto file=write(project,directory.filePath("selection.json"));EditorController controller({false,directory.filePath("private.json")});QVERIFY(controller.openFile(QUrl::fromLocalFile(file)));controller.setProjectionMode("flat");controller.resizeMapCamera(800,600);
  const auto before=controller.documentBytes();const auto revisionBefore=controller.revision();QVariantMap warm,final;QCOMPARE(snapOwner(controller,"root",&warm),QString("vertex:ga"));auto* provider=sourceProvider(controller);QVERIFY(provider);const auto initialRanks=provider->sourceRanks();const auto submittedBeforeDelete=controller.geometrySnapState().value("submitted").toULongLong();
  QVERIFY(controller.selectObject({{"domain","generic"},{"id","ga"}},"replace"));QVERIFY(controller.beginContentEdit("generic"));QVERIFY(controller.previewContentEdit(true));QVERIFY(controller.confirmContentEdit());
  const auto revisionAfterDelete=controller.revision();
  QVERIFY(controller.selectObject({{"domain","territorial"},{"id","root"}},"replace"));
  if(path==1||path==3){QVERIFY(controller.beginAnnexGeometry());QVERIFY(controller.geometryToggleProvider({{"domain","territorial"},{"id","donor"}}));}else QVERIFY(controller.beginSplitGeometry());
  QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("calculating").toBool(),5000);
  QCOMPARE(provider->sourceRanks(),initialRanks); // Native-only setup never calls the web client.
  if(path>=2){
   QVERIFY(controller.geometryAdvanceStage());
   if(path==5){
    SelectionPoolGate gate;QVERIFY(gate.started());QVERIFY(controller.geometrySelectTerritoryMethod("components"));
    QSignalSpy dispatch(&controller,&EditorController::geometryEditChanged);QTRY_VERIFY_WITH_TIMEOUT(dispatch.count()>0,5000);
    QVERIFY(controller.geometryEditState().value("selectionPending").toBool());
    QVERIFY(!provider->sourceRanks()->count({"generic","ga"})); // Actual pre-enqueue observation.
    controller.cancelGeometryEdit();
   }else {
    QVERIFY(controller.geometrySelectTerritoryMethod("components"));
    if(path==4){QCOMPARE(provider->sourceRanks(),initialRanks);controller.cancelGeometryEdit();}
    else {QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("calculating").toBool(),5000);QVERIFY(!provider->sourceRanks()->count({"generic","ga"}));}
   }
  }
  controller.cancelGeometryEdit();controller.undo();QVERIFY(controller.canRedo());const auto after=controller.documentBytes();QCOMPARE(after,before);const auto revisionAfterUndo=controller.revision(),submittedAfterUndo=controller.geometrySnapState().value("submitted").toULongLong();QCOMPARE(snapOwner(controller,"root",&final),expected);
  QVERIFY(writeHistoryEvidence("native-controller-selection-source-history",before,after,warm,final,{{"revisionBefore",qint64(revisionBefore)},{"revisionAfterDelete",qint64(revisionAfterDelete)},{"revisionAfterUndo",qint64(revisionAfterUndo)},{"submittedBeforeDelete",qint64(submittedBeforeDelete)},{"submittedAfterUndo",qint64(submittedAfterUndo)},{"submittedAfterFinal",qint64(controller.geometrySnapState().value("submitted").toULongLong())}}));
 }
 void componentMethodSwitchReusesPreparedSource(){
  QTemporaryDir directory;auto project=selectionProject();const auto file=write(project,directory.filePath("selection.json"));EditorController controller({false,directory.filePath("private.json")});QVERIFY(controller.openFile(QUrl::fromLocalFile(file)));
  QVERIFY(controller.selectObject({{"domain","territorial"},{"id","root"}},"replace"));QVERIFY(controller.beginAnnexGeometry());QVERIFY(controller.geometryToggleProvider({{"domain","territorial"},{"id","donor"}}));QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("calculating").toBool(),5000);QVERIFY(controller.geometryAdvanceStage());
  auto* provider=sourceProvider(controller);QVERIFY(provider);const auto initialRanks=provider->sourceRanks();
  // Controlled lifecycle stimulus: no native canonical edit is allowed while
  // this workflow is open. A genuine cold request must reseed at this stop.
  provider->notifyWorkerStopped();QVERIFY(controller.geometrySelectTerritoryMethod("components"));QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("calculating").toBool(),5000);QVERIFY(provider->sourceRanks()!=initialRanks);
  const auto readyRanks=provider->sourceRanks();provider->notifyWorkerStopped();
  QVERIFY(controller.geometrySelectTerritoryMethod("line"));QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("calculating").toBool(),5000);QCOMPARE(provider->sourceRanks(),readyRanks);
  QVERIFY(controller.geometrySelectTerritoryMethod("components"));QCOMPARE(controller.geometryEditState().value("confirmationKind").toString(),QString("method"));QCOMPARE(controller.geometryEditState().value("activeMethod").toString(),QString("line"));
  QVERIFY(controller.geometryConfirmTerritoryChange());QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("calculating").toBool(),5000);QCOMPARE(controller.geometryEditState().value("activeMethod").toString(),QString("components"));QCOMPARE(provider->sourceRanks(),readyRanks);
  controller.cancelGeometryEdit();
 }
 void sourceChangeWhileComponentRequestPending(){
  QTemporaryDir directory;auto project=selectionProject();const auto file=write(project,directory.filePath("selection.json"));EditorController controller({false,directory.filePath("private.json")});QVERIFY(controller.openFile(QUrl::fromLocalFile(file)));
  QVERIFY(controller.selectObject({{"domain","territorial"},{"id","root"}},"replace"));QVERIFY(controller.beginAnnexGeometry());QVERIFY(controller.geometryToggleProvider({{"domain","territorial"},{"id","donor"}}));QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("calculating").toBool(),5000);QVERIFY(controller.geometryAdvanceStage());
  auto* provider=sourceProvider(controller);QVERIFY(provider);const auto initialRanks=provider->sourceRanks();
  {SelectionPoolGate gate;QVERIFY(gate.started());QVERIFY(controller.geometrySelectTerritoryMethod("components"));QSignalSpy dispatch(&controller,&EditorController::geometryEditChanged);QTRY_VERIFY_WITH_TIMEOUT(dispatch.count()>0,5000);
   QVERIFY(controller.geometryToggleProvider({{"domain","territorial"},{"id","other"}}));QCOMPARE(controller.geometryEditState().value("stage").toString(),QString("setup"));}
  QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("calculating").toBool(),5000);QCOMPARE(provider->sourceRanks(),initialRanks);
  QVERIFY(controller.geometryAdvanceStage());QVERIFY(controller.geometrySelectTerritoryMethod("components"));QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("calculating").toBool(),5000);QVERIFY(provider->sourceRanks()!=initialRanks);
  controller.cancelGeometryEdit();
 }
 void selectionReplacementDoesNotStopWorker(){
  QTemporaryDir directory;auto project=selectionProject();const auto file=write(project,directory.filePath("selection.json"));EditorController controller({false,directory.filePath("private.json")});QVERIFY(controller.openFile(QUrl::fromLocalFile(file)));
  QVERIFY(controller.selectObject({{"domain","territorial"},{"id","root"}},"replace"));QVERIFY(controller.beginSplitGeometry());QVERIFY(controller.geometryAdvanceStage());QVERIFY(controller.geometrySelectTerritoryMethod("components"));QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("calculating").toBool(),5000);
  auto* provider=sourceProvider(controller);QVERIFY(provider);const auto ranks=provider->sourceRanks();const auto key=controller.geometryEditState().value("components").toList().front().toMap().value("key").toString();
  {SelectionPoolGate gate;QVERIFY(gate.started());QVERIFY(controller.geometryToggleTerritoryComponent(key));QSignalSpy dispatch(&controller,&EditorController::geometryEditChanged);QTRY_VERIFY_WITH_TIMEOUT(dispatch.count()>0,5000);QVERIFY(controller.geometryEditState().value("selectionPending").toBool());QVERIFY(controller.geometryToggleTerritoryComponent(key));}
  QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("calculating").toBool(),5000);QVERIFY(controller.geometryEditState().value("selectedComponentKeys").toList().empty());controller.cancelGeometryEdit();
  QCOMPARE(snapOwner(controller,"root"),QString("vertex:ga"));QCOMPARE(provider->sourceRanks(),ranks);
 }
 void clearingEmptySelectionStillStopsUnsettledRequest(){
  QTemporaryDir directory;auto project=selectionProject();const auto file=write(project,directory.filePath("selection.json"));EditorController controller({false,directory.filePath("private.json")});QVERIFY(controller.openFile(QUrl::fromLocalFile(file)));
  QVERIFY(controller.selectObject({{"domain","territorial"},{"id","root"}},"replace"));QVERIFY(controller.beginSplitGeometry());QVERIFY(controller.geometryAdvanceStage());QVERIFY(controller.geometrySelectTerritoryMethod("components"));QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("calculating").toBool(),5000);
  auto* provider=sourceProvider(controller);QVERIFY(provider);const auto ranks=provider->sourceRanks();const auto key=controller.geometryEditState().value("components").toList().front().toMap().value("key").toString();
  {SelectionPoolGate gate;QVERIFY(gate.started());QVERIFY(controller.geometryToggleTerritoryComponent(key));QSignalSpy dispatch(&controller,&EditorController::geometryEditChanged);QTRY_VERIFY_WITH_TIMEOUT(dispatch.count()>0,5000);
   QVERIFY(controller.geometryToggleTerritoryComponent(key));QSignalSpy emptyDispatch(&controller,&EditorController::geometryEditChanged);QTRY_VERIFY_WITH_TIMEOUT(emptyDispatch.count()>0,5000);
   // Drain obsolete callbacks while actual calculation remains held. Web
   // touchSelection leaves its earlier counted request unsettled in this gap.
   QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);QVERIFY(controller.geometryEditState().value("selectedComponentKeys").toList().empty());controller.cancelGeometryEdit();}
  QCOMPARE(snapOwner(controller,"root"),QString("vertex:ga"));QVERIFY(provider->sourceRanks()!=ranks);
 }
 void lateCancelledSelectionCannotSettleReplacementSession(){
  QTemporaryDir directory;auto project=selectionProject();const auto file=write(project,directory.filePath("selection.json"));EditorController controller({false,directory.filePath("private.json")});QVERIFY(controller.openFile(QUrl::fromLocalFile(file)));
  QVERIFY(controller.selectObject({{"domain","territorial"},{"id","root"}},"replace"));QVERIFY(controller.beginSplitGeometry());QVERIFY(controller.geometryAdvanceStage());QVERIFY(controller.geometrySelectTerritoryMethod("components"));QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("calculating").toBool(),5000);
  auto* provider=sourceProvider(controller);QVERIFY(provider);const auto key=controller.geometryEditState().value("components").toList().front().toMap().value("key").toString();std::shared_ptr<const geometrysnap::SourceRanks> replacementRanks;
  {SelectionPoolGate gate;QVERIFY(gate.started());QVERIFY(controller.geometryToggleTerritoryComponent(key));QSignalSpy dispatch(&controller,&EditorController::geometryEditChanged);QTRY_VERIFY_WITH_TIMEOUT(dispatch.count()>0,5000);controller.cancelGeometryEdit();
   QVERIFY(controller.beginSplitGeometry());QVERIFY(controller.geometryAdvanceStage());QVERIFY(controller.geometrySelectTerritoryMethod("components"));QSignalSpy replacementDispatch(&controller,&EditorController::geometryEditChanged);
   // Deliver the new session's enqueue before the old runner's queued reject.
   QCoreApplication::sendPostedEvents(&controller,QEvent::MetaCall);QVERIFY(replacementDispatch.count()>0);replacementRanks=provider->sourceRanks();
   QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);QVERIFY(controller.geometryEditState().value("selectionPending").toBool());controller.cancelGeometryEdit();}
  QCOMPARE(snapOwner(controller,"root"),QString("vertex:ga"));QVERIFY(provider->sourceRanks()!=replacementRanks);
 }
 void cancelMappedKeyPreservesNativeOnlyWork(){
  auto project=selectionProject();CommandJobRunner runner([&]() -> const Project& {return project;});int cancelled=0,localAccepted=0;
  const auto task=[](const ProjectSnapshot&,const JobToken&) -> GeometryJobResult {return TerritorySelectionDerivedResult{};};
  const auto mappedDone=[&](auto,JobDisposition disposition,GeometryJobResult){if(disposition==JobDisposition::Cancelled)++cancelled;};
  {SelectionPoolGate gate;QVERIFY(gate.started());const auto selection=runner.submitGeometry(project.snapshot(),"territorial:selection",task,mappedDone);
   const auto local=runner.submitGeometry(project.snapshot(),"territorial:selection-local",task,[&](auto,JobDisposition disposition,GeometryJobResult){if(disposition==JobDisposition::Accepted)++localAccepted;});
   const auto drawn=runner.submitGeometry(project.snapshot(),"territorial:selection-drawn",task,mappedDone);
   runner.cancelKey("territorial:selection");QVERIFY(selection.token().cancelled());QVERIFY(!local.token().cancelled());QVERIFY(!drawn.token().cancelled());
   runner.cancelKey("territorial:selection-drawn");QVERIFY(drawn.token().cancelled());QVERIFY(!local.token().cancelled());QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);QCOMPARE(cancelled,2);QCOMPARE(localAccepted,0);}
  QTRY_COMPARE_WITH_TIMEOUT(localAccepted,1,5000);QCOMPARE(cancelled,2);
 }
 void selectedComponentDispatchObservesSource(){
  QTemporaryDir directory;auto project=selectionProject();const auto file=write(project,directory.filePath("selection.json"));EditorController controller({false,directory.filePath("private.json")});QVERIFY(controller.openFile(QUrl::fromLocalFile(file)));
  QVERIFY(controller.selectObject({{"domain","territorial"},{"id","root"}},"replace"));QVERIFY(controller.beginSplitGeometry());QVERIFY(controller.geometryAdvanceStage());QVERIFY(controller.geometrySelectTerritoryMethod("components"));QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("calculating").toBool(),5000);
  auto* provider=sourceProvider(controller);QVERIFY(provider);const auto ranks=provider->sourceRanks();provider->notifyWorkerStopped();
  const auto key=controller.geometryEditState().value("components").toList().front().toMap().value("key").toString();QVERIFY(controller.geometryToggleTerritoryComponent(key));
  QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("selectionPending").toBool(),5000);QVERIFY(provider->sourceRanks()!=ranks);controller.cancelGeometryEdit();
 }
 void draftDispatchSourceHistory_data(){QTest::addColumn<QString>("method");QTest::addColumn<bool>("child");QTest::newRow("line-cut-does-not-stop")<<QString("line")<<false;QTest::newRow("root-polygon-preparation-is-native-only")<<QString("polygon")<<false;QTest::newRow("child-polygon-drawn-stops")<<QString("polygon")<<true;}
 void draftDispatchSourceHistory(){
  QFETCH(QString,method);QFETCH(bool,child);QTemporaryDir directory;auto project=selectionProject(child);if(child)QCOMPARE(staticParentRelation(project.document(),"child").parentId,std::string("root"));const auto file=write(project,directory.filePath("selection.json"));EditorController controller({false,directory.filePath("private.json")});QVERIFY(controller.openFile(QUrl::fromLocalFile(file)));controller.setProjectionMode("flat");controller.resizeMapCamera(800,600);
  QVERIFY(controller.selectObject({{"domain","territorial"},{"id",child?"child":"root"}},"replace"));QVERIFY(controller.beginSplitGeometry());QVERIFY(controller.geometryAdvanceStage());QVERIFY(controller.geometrySelectTerritoryMethod(method));QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("calculating").toBool(),5000);
  MapProjection projection;projection.rebuild(projectcodec::decode(controller.documentBytes()));const std::vector<Point> points=method=="line"?std::vector<Point>{{11,-1},{11,3}}:std::vector<Point>{{10,0},{11,0},{11,2},{10,2}};
  for(const auto point:points){const auto xy=projection.project(point);QVERIFY(controller.geometryAddPoint(xy.x,xy.y,0));}
  auto* provider=sourceProvider(controller);QVERIFY(provider);const auto ranks=provider->sourceRanks();provider->notifyWorkerStopped();std::shared_ptr<const geometrysnap::SourceRanks> dispatchedRanks;
  {SelectionPoolGate gate;QVERIFY(gate.started());QVERIFY(controller.geometryFinishTerritoryDraft());dispatchedRanks=provider->sourceRanks();if(method=="line"||child)QVERIFY(dispatchedRanks!=ranks);else QCOMPARE(dispatchedRanks,ranks);QVERIFY(controller.geometryEditState().value("selectionPending").toBool());controller.cancelGeometryEdit();}
  QCOMPARE(snapOwner(controller,"root"),QString("vertex:ga"));if(method=="line")QCOMPARE(provider->sourceRanks(),dispatchedRanks);else QVERIFY(provider->sourceRanks()!=dispatchedRanks);
 }
 void previewDispatchObservesSourceWithoutStoppingOnCancel_data(){QTest::addColumn<bool>("split");QTest::newRow("annex-preview")<<false;QTest::newRow("root-split-preview")<<true;}
 void previewDispatchObservesSourceWithoutStoppingOnCancel(){
  QFETCH(bool,split);QTemporaryDir directory;auto project=selectionProject();const auto file=write(project,directory.filePath("selection.json"));EditorController controller({false,directory.filePath("private.json")});QVERIFY(controller.openFile(QUrl::fromLocalFile(file)));
  QVERIFY(controller.selectObject({{"domain","territorial"},{"id","root"}},"replace"));if(split)QVERIFY(controller.beginSplitGeometry());else {QVERIFY(controller.beginAnnexGeometry());QVERIFY(controller.geometryToggleProvider({{"domain","territorial"},{"id","donor"}}));}QVERIFY(controller.geometryAdvanceStage());QVERIFY(controller.geometrySelectTerritoryMethod("components"));QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("calculating").toBool(),5000);
  const auto key=controller.geometryEditState().value("components").toList().front().toMap().value("key").toString();QVERIFY(controller.geometryToggleTerritoryComponent(key));QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("selectionPending").toBool(),5000);QVERIFY(controller.geometryEditState().value("previewPending").toBool());
  auto* provider=sourceProvider(controller);QVERIFY(provider);const auto ranks=provider->sourceRanks();provider->notifyWorkerStopped();std::shared_ptr<const geometrysnap::SourceRanks> dispatchedRanks;
  {SelectionPoolGate gate;QVERIFY(gate.started());QSignalSpy dispatch(&controller,&EditorController::geometryEditChanged);QTRY_VERIFY_WITH_TIMEOUT(dispatch.count()>0,5000);QVERIFY(controller.geometryEditState().value("previewPending").toBool());dispatchedRanks=provider->sourceRanks();QVERIFY(dispatchedRanks!=ranks);controller.cancelGeometryEdit();}
  QCOMPARE(snapOwner(controller,"root"),QString("vertex:ga"));QCOMPARE(provider->sourceRanks(),dispatchedRanks);
 }
 void rootAnnexApplyDoesNotStartWorkerRequest(){
  QTemporaryDir directory;auto project=selectionProject();const auto file=write(project,directory.filePath("selection.json"));EditorController controller({false,directory.filePath("private.json")});QVERIFY(controller.openFile(QUrl::fromLocalFile(file)));
  QVERIFY(controller.selectObject({{"domain","territorial"},{"id","root"}},"replace"));QVERIFY(controller.beginAnnexGeometry());QVERIFY(controller.geometryToggleProvider({{"domain","territorial"},{"id","donor"}}));QVERIFY(controller.geometryAdvanceStage());QVERIFY(controller.geometrySelectTerritoryMethod("components"));QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("calculating").toBool(),5000);
  const auto key=controller.geometryEditState().value("components").toList().front().toMap().value("key").toString();QVERIFY(controller.geometryToggleTerritoryComponent(key));QTRY_VERIFY_WITH_TIMEOUT(controller.geometryEditState().value("canAdvance").toBool(),5000);QVERIFY(controller.geometryAdvanceStage());
  auto* provider=sourceProvider(controller);QVERIFY(provider);const auto ranks=provider->sourceRanks();provider->notifyWorkerStopped();QVERIFY(controller.confirmGeometryEdit());QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("active").toBool(),5000);QCOMPARE(provider->sourceRanks(),ranks);
 }
 void confirmedMethodChangeStopsPendingDrawn(){
  QTemporaryDir directory;auto project=selectionProject(true);const auto file=write(project,directory.filePath("selection.json"));EditorController controller({false,directory.filePath("private.json")});QVERIFY(controller.openFile(QUrl::fromLocalFile(file)));
  QVERIFY(controller.selectObject({{"domain","territorial"},{"id","child"}},"replace"));QVERIFY(controller.beginSplitGeometry());QVERIFY(controller.geometryAdvanceStage());QVERIFY(controller.geometrySelectTerritoryMethod("polygon"));QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("calculating").toBool(),5000);
  MapProjection projection;projection.rebuild(projectcodec::decode(controller.documentBytes()));for(const auto point:{Point{10,0},Point{11,0},Point{11,2},Point{10,2}}){const auto xy=projection.project(point);QVERIFY(controller.geometryAddPoint(xy.x,xy.y,0));}
  auto* provider=sourceProvider(controller);QVERIFY(provider);const auto ranks=provider->sourceRanks();
  {SelectionPoolGate gate;QVERIFY(gate.started());QVERIFY(controller.geometryFinishTerritoryDraft());QVERIFY(controller.geometrySelectTerritoryMethod("line"));QCOMPARE(controller.geometryEditState().value("confirmationKind").toString(),QString("method"));QVERIFY(controller.geometryConfirmTerritoryChange());}
  QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("calculating").toBool(),5000);QCOMPARE(controller.geometryEditState().value("activeMethod").toString(),QString("line"));QCOMPARE(provider->sourceRanks(),ranks);controller.cancelGeometryEdit();QCOMPARE(snapOwner(controller,"root"),QString("vertex:ga"));QVERIFY(provider->sourceRanks()!=ranks);
 }
 void removingArchivedPartInvalidatesPreparedSource_data(){QTest::addColumn<bool>("undo");QTest::newRow("remove-part")<<false;QTest::newRow("undo-part")<<true;}
 void removingArchivedPartInvalidatesPreparedSource(){
  QFETCH(bool,undo);QTemporaryDir directory;auto project=selectionProject();const auto file=write(project,directory.filePath("selection.json"));EditorController controller({false,directory.filePath("private.json")});QVERIFY(controller.openFile(QUrl::fromLocalFile(file)));
  QVERIFY(controller.selectObject({{"domain","territorial"},{"id","root"}},"replace"));QVERIFY(controller.beginAnnexGeometry());QVERIFY(controller.geometryToggleProvider({{"domain","territorial"},{"id","donor"}}));QVERIFY(controller.geometryAdvanceStage());QVERIFY(controller.geometrySelectTerritoryMethod("components"));QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("calculating").toBool(),5000);
  const auto key=controller.geometryEditState().value("components").toList().front().toMap().value("key").toString();QVERIFY(controller.geometryToggleTerritoryComponent(key));QTRY_VERIFY_WITH_TIMEOUT(controller.geometryEditState().value("canAddPart").toBool(),5000);QVERIFY(controller.geometryAddTerritoryPart());QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("calculating").toBool(),5000);
  const auto parts=controller.geometryEditState().value("parts").toList();QCOMPARE(parts.size(),1);if(undo)QVERIFY(controller.geometryUndoTerritoryPart());else QVERIFY(controller.geometryRemoveTerritoryPart(parts.front().toMap().value("id").toString()));QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("calculating").toBool(),5000);
  auto* provider=sourceProvider(controller);QVERIFY(provider);const auto ranks=provider->sourceRanks();provider->notifyWorkerStopped();QVERIFY(controller.geometrySelectTerritoryMethod("components"));QTRY_VERIFY_WITH_TIMEOUT(!controller.geometryEditState().value("calculating").toBool(),5000);QVERIFY(provider->sourceRanks()!=ranks);controller.cancelGeometryEdit();
 }
 void deleteUndoWithoutPointerPreservesWorkerInsertionOrder(){
  QTemporaryDir directory;ProjectDocument input;input.documentId="source-order-controller";
  for(const auto id:{"a","b"}){const GeometryRef ref{std::string("source-")+id,1};input.geometries.insert(ref,square());TerritorialUnit unit;unit.id=id;unit.name=id;appendTerritory(input,unit,ref);input.presentation.objectStyles[territorialRef(id)]={};}
  Project project;project.replace(input);
  const auto file=write(project,directory.filePath("input.json"));QVERIFY(!file.isEmpty());EditorController controller({false,directory.filePath("private.json")});QVERIFY(controller.openFile(QUrl::fromLocalFile(file)));controller.setProjectionMode("flat");controller.resizeMapCamera(800,600);
  const auto beforeBytes=controller.documentBytes();const auto beforeDocument=projectcodec::decode(beforeBytes);const auto revisionBefore=controller.revision();QVariantMap warmIndicator,restoredIndicator;
  QCOMPARE(snapOwner(controller,"a",&warmIndicator),QString("vertex:a"));
  const auto submittedBeforeDelete=controller.geometrySnapState().value("submitted").toULongLong();
  QVERIFY(controller.selectObject({{"domain","territorial"},{"id","a"}},"replace"));const bool deleteStarted=controller.beginDeleteSelection();QVERIFY(deleteStarted);const bool deleteApplied=controller.confirmStructureMutation();QVERIFY(deleteApplied);const bool undoAvailable=controller.canUndo();QVERIFY(undoAvailable);const auto revisionAfterDelete=controller.revision();
  // No snap request is issued between these canonical transitions.
  controller.undo();const auto revisionAfterUndo=controller.revision();const bool undoApplied=controller.canRedo()&&revisionAfterUndo==revisionAfterDelete+1;QVERIFY(undoApplied);
  const auto submittedAfterUndo=controller.geometrySnapState().value("submitted").toULongLong();QCOMPARE(submittedAfterUndo,submittedBeforeDelete);
  const auto afterBytes=controller.documentBytes();const auto afterDocument=projectcodec::decode(afterBytes);const bool canonicalEqualAfterUndo=semanticallyEqual(beforeDocument,afterDocument);QVERIFY(canonicalEqualAfterUndo);
  QCOMPARE(snapOwner(controller,"a",&restoredIndicator),QString("vertex:b"));
  const auto output=qEnvironmentVariable("PANDO_M974_ORDER_EVIDENCE");if(!output.isEmpty()){
   const auto sha=[](const QByteArray& bytes){return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());};
   const QJsonObject evidence{{"schema","pando-m974-native-controller-source-order"},{"version",1},{"case","source-order-v2-coincident-remove-restore-a"},{"scope","native-controller-delete-undo"},{"sourceGeometries",sourceGeometries(beforeDocument)},{"restoredSourceGeometries",sourceGeometries(afterDocument)},
    {"warm",QJsonObject{{"indicator",QJsonObject::fromVariantMap(warmIndicator)}}},{"afterRestore",QJsonObject{{"indicator",QJsonObject::fromVariantMap(restoredIndicator)}}},
    {"transitions",QJsonObject{{"deleteStarted",deleteStarted},{"deleteApplied",deleteApplied},{"undoAvailable",undoAvailable},{"undoApplied",undoApplied},{"revisionBefore",qint64(revisionBefore)},{"revisionAfterDelete",qint64(revisionAfterDelete)},{"revisionAfterUndo",qint64(revisionAfterUndo)},{"submittedBeforeDelete",qint64(submittedBeforeDelete)},{"submittedAfterUndo",qint64(submittedAfterUndo)}}},
    {"canonicalEqualAfterUndo",canonicalEqualAfterUndo},{"beforeDocumentSha256",sha(beforeBytes)},{"afterDocumentSha256",sha(afterBytes)},{"beforeDocumentBytesBase64",QString::fromLatin1(beforeBytes.toBase64())},{"afterDocumentBytesBase64",QString::fromLatin1(afterBytes.toBase64())}};
   QSaveFile destination(output);QVERIFY2(destination.open(QIODevice::WriteOnly),qPrintable(destination.errorString()));const auto bytes=QJsonDocument(evidence).toJson(QJsonDocument::Compact);QCOMPARE(destination.write(bytes),qint64(bytes.size()));QVERIFY2(destination.commit(),qPrintable(destination.errorString()));
  }
 }
 void documentInstallationSeedsBeforeFirstPointer(){
  QTemporaryDir directory;ProjectDocument document(std::vector<Country>{{"a","a",square(10).polygons,0xabcdef}},{{"countries","Countries"}});GenericFeature generic;generic.id="g";generic.geometry={"generic-g",1};document.geometries.insert(generic.geometry,square());document.genericFeatures.push_back(generic);Project project;project.replace(document);
  const auto file=write(project,directory.filePath("input.json"));QVERIFY(!file.isEmpty());EditorController controller({false,directory.filePath("private.json")});QVERIFY(controller.openFile(QUrl::fromLocalFile(file)));controller.setProjectionMode("flat");controller.resizeMapCamera(800,600);
  CreateTerritorialIntent intent;intent.kind=UnitKind::General;intent.id="c";intent.name="c";intent.geometry=square();intent.coverageMode="explicit";QVERIFY(controller.beginTerritorialCreatePrepared(intent));QVERIFY(controller.confirmStructureMutation());
  QCOMPARE(snapOwner(controller,"c"),QString("vertex:g"));
 }
 void genericDeleteUndoDefersWorkerSynchronization_data(){
  QTest::addColumn<int>("observation");QTest::addColumn<QString>("expected");
  QTest::newRow("no-query-retains")<<0<<QString("vertex:ga");
  QTest::newRow("query-while-deleted-appends")<<1<<QString("vertex:gb");
  QTest::newRow("root-addition-syncs-generic-deletion")<<2<<QString("vertex:gb");
  QTest::newRow("replacement-resets")<<3<<QString("vertex:ga");
  QTest::newRow("child-addition-defers")<<4<<QString("vertex:ga");
  QTest::newRow("root-metadata-defers")<<5<<QString("vertex:ga");
  QTest::newRow("ready-boundary-worker-observes-deletion")<<6<<QString("vertex:gb");
  QTest::newRow("pending-boundary-cancel-rebases-on-next-query")<<7<<QString("vertex:ga");
  QTest::newRow("settled-boundary-error-retains-history")<<8<<QString("vertex:gb");
  QTest::newRow("stopped-worker-ignores-root-delete-undo")<<9<<QString("vertex:ga");
 }
 void genericDeleteUndoDefersWorkerSynchronization(){
  QFETCH(int,observation);QFETCH(QString,expected);
  QTemporaryDir directory;ProjectDocument document(std::vector<Country>{{"root","root",square(10).polygons,0xabcdef}},{{"countries","Countries"}});
  if(observation>=6){const GeometryRef ref{"boundary-neighbor",1};document.geometries.insert(ref,square(observation==8?20:12));appendTerritory(document,{"neighbor","neighbor","",UnitKind::General,false},ref);document.presentation.objectStyles[territorialRef("neighbor")]={};}
  for(const auto id:{"ga","gb"}){GenericFeature feature;feature.id=id;feature.geometry={std::string("generic-")+id,1};document.geometries.insert(feature.geometry,square());document.genericFeatures.push_back(feature);}
  Project project;project.replace(document);const auto file=write(project,directory.filePath("input.json"));QVERIFY(!file.isEmpty());EditorController controller({false,directory.filePath("private.json")});QVERIFY(controller.openFile(QUrl::fromLocalFile(file)));controller.setProjectionMode("flat");controller.resizeMapCamera(800,600);
  const auto before=controller.documentBytes();const auto revisionBefore=controller.revision();QVariantMap warm,final;QCOMPARE(snapOwner(controller,"root",&warm),QString("vertex:ga"));const auto submitted=controller.geometrySnapState().value("submitted").toULongLong();
  QVERIFY(controller.selectObject({{"domain","generic"},{"id","ga"}},"replace"));QVERIFY(controller.beginContentEdit("generic"));QVERIFY(controller.previewContentEdit(true));QVERIFY(controller.confirmContentEdit());QVERIFY(controller.canUndo());const auto revisionAfterDelete=controller.revision();
  if(observation==1||observation==3)QCOMPARE(snapOwner(controller,"root"),QString("vertex:gb"));
  if(observation==2||observation==4){CreateTerritorialIntent intent;intent.kind=UnitKind::General;intent.id="added";intent.name="added";intent.geometry=square(observation==2?20:10);intent.coverageMode="explicit";if(observation==4)intent.parent=territorialRef("root");QVERIFY(controller.beginTerritorialCreatePrepared(intent));QVERIFY(controller.confirmStructureMutation());controller.undo();}
  if(observation==5){QVERIFY(controller.selectObject({{"domain","territorial"},{"id","root"}},"replace"));controller.setNameDraft("Renamed root");QVERIFY(controller.commitCountryField("name"));controller.undo();}
  if(observation>=6){QVariantList refs{QVariantMap{{"domain","territorial"},{"id","root"}},QVariantMap{{"domain","territorial"},{"id","neighbor"}}};QVERIFY(controller.setSelection(refs,refs.front().toMap(),"map"));QVERIFY(controller.beginSharedBoundaryGeometry());if(observation==6||observation==8){QTRY_COMPARE_WITH_TIMEOUT(controller.geometryEditState().value("boundaryStatus").toString(),observation==8?QString("error"):QString("ready"),3000);}else QCOMPARE(controller.geometryEditState().value("boundaryStatus").toString(),QString("preparing"));controller.cancelGeometryEdit();QVERIFY(!controller.geometryEditState().value("active").toBool());}
  if(observation==9){QVERIFY(controller.selectObject({{"domain","territorial"},{"id","root"}},"replace"));QVERIFY(controller.beginDeleteSelection());QVERIFY(controller.confirmStructureMutation());controller.undo();}
  // The pinned web app does not sync a generic-only delete or its Undo before
  // another Worker request; neither transient membership is observed here.
  controller.undo();QVERIFY(controller.canRedo());QCOMPARE(controller.documentBytes(),before);if(observation!=1&&observation!=3)QCOMPARE(controller.geometrySnapState().value("submitted").toULongLong(),submitted);
  if(observation==3)QVERIFY(controller.openFile(QUrl::fromLocalFile(file)));
  const auto after=controller.documentBytes();const auto revisionAfterUndo=controller.revision(),submittedAfterUndo=controller.geometrySnapState().value("submitted").toULongLong();QCOMPARE(snapOwner(controller,"root",&final),expected);
  QVERIFY(writeHistoryEvidence("native-controller-source-history",before,after,warm,final,{{"revisionBefore",qint64(revisionBefore)},{"revisionAfterDelete",qint64(revisionAfterDelete)},{"revisionAfterUndo",qint64(revisionAfterUndo)},{"submittedBeforeDelete",qint64(submitted)},{"submittedAfterUndo",qint64(submittedAfterUndo)},{"submittedAfterFinal",qint64(controller.geometrySnapState().value("submitted").toULongLong())}}));
 }
 void projectReplacementResetsSourceOrder(){
  QTemporaryDir directory;Project project;project.replace(std::vector<Country>{{"a","a",square().polygons,0xabcdef},{"b","b",square().polygons,0xabcdef}});const auto file=write(project,directory.filePath("input.json"));EditorController controller({false,directory.filePath("private.json")});QVERIFY(controller.openFile(QUrl::fromLocalFile(file)));controller.setProjectionMode("flat");controller.resizeMapCamera(800,600);
  QCOMPARE(snapOwner(controller,"a"),QString("vertex:a"));QVERIFY(controller.selectObject({{"domain","territorial"},{"id","a"}},"replace"));QVERIFY(controller.beginDeleteSelection());QVERIFY(controller.confirmStructureMutation());controller.undo();
  QVERIFY(controller.openFile(QUrl::fromLocalFile(file)));QCOMPARE(snapOwner(controller,"a"),QString("vertex:a"));
 }
};
QTEST_MAIN(M974SnapOrderTests)
#include "m974_snap_order_tests.moc"

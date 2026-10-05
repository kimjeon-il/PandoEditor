#include "editorcontroller.h"
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
using namespace pandoeditor;
namespace {
Geometry square(double x=0){Geometry g;g.type="Polygon";g.polygons={{{{x,0},{x,2},{x+2,2},{x+2,0},{x,0}}}};return g;}
QString write(Project& project,const QString& path){QFile file(path);if(!file.open(QIODevice::WriteOnly))return {};file.write(projectcodec::encode(project));file.close();return path;}
QJsonArray sourceGeometries(const ProjectDocument& document){
 QJsonArray result;for(const auto& unit:document.units){const auto geometry=document.geometries.get(staticGeometryBinding(document,unit.id).geometryRef);QJsonArray polygons;for(const auto& polygon:geometry->polygons){QJsonArray rings;for(const auto& ring:polygon){QJsonArray coordinates;for(const auto point:ring)coordinates.append(QJsonArray{point.x,point.y});rings.append(coordinates);}polygons.append(rings);}result.append(QJsonObject{{"id",QString::fromStdString(unit.id)},{"geometry",QJsonObject{{"type",QString::fromStdString(geometry->type)},{"coordinates",geometry->type=="Polygon"?polygons.first():QJsonValue(polygons)}}}});}return result;
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
}
class M974SnapOrderTests : public QObject {
 Q_OBJECT
private slots:
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
 }
 void genericDeleteUndoDefersWorkerSynchronization(){
  QFETCH(int,observation);QFETCH(QString,expected);
  QTemporaryDir directory;ProjectDocument document(std::vector<Country>{{"root","root",square(10).polygons,0xabcdef}},{{"countries","Countries"}});
  if(observation==6){const GeometryRef ref{"boundary-neighbor",1};document.geometries.insert(ref,square(12));appendTerritory(document,{"neighbor","neighbor","",UnitKind::General,false},ref);document.presentation.objectStyles[territorialRef("neighbor")]={};}
  for(const auto id:{"ga","gb"}){GenericFeature feature;feature.id=id;feature.geometry={std::string("generic-")+id,1};document.geometries.insert(feature.geometry,square());document.genericFeatures.push_back(feature);}
  Project project;project.replace(document);const auto file=write(project,directory.filePath("input.json"));QVERIFY(!file.isEmpty());EditorController controller({false,directory.filePath("private.json")});QVERIFY(controller.openFile(QUrl::fromLocalFile(file)));controller.setProjectionMode("flat");controller.resizeMapCamera(800,600);
  const auto before=controller.documentBytes();QCOMPARE(snapOwner(controller,"root"),QString("vertex:ga"));const auto submitted=controller.geometrySnapState().value("submitted").toULongLong();
  QVERIFY(controller.selectObject({{"domain","generic"},{"id","ga"}},"replace"));QVERIFY(controller.beginContentEdit("generic"));QVERIFY(controller.previewContentEdit(true));QVERIFY(controller.confirmContentEdit());QVERIFY(controller.canUndo());
  if(observation==1||observation==3)QCOMPARE(snapOwner(controller,"root"),QString("vertex:gb"));
  if(observation==2||observation==4){CreateTerritorialIntent intent;intent.kind=UnitKind::General;intent.id="added";intent.name="added";intent.geometry=square(observation==2?20:10);intent.coverageMode="explicit";if(observation==4)intent.parent=territorialRef("root");QVERIFY(controller.beginTerritorialCreatePrepared(intent));QVERIFY(controller.confirmStructureMutation());controller.undo();}
  if(observation==5){QVERIFY(controller.selectObject({{"domain","territorial"},{"id","root"}},"replace"));controller.setNameDraft("Renamed root");QVERIFY(controller.commitCountryField("name"));controller.undo();}
  if(observation==6){QVariantList refs{QVariantMap{{"domain","territorial"},{"id","root"}},QVariantMap{{"domain","territorial"},{"id","neighbor"}}};QVERIFY(controller.setSelection(refs,refs.front().toMap(),"map"));QVERIFY(controller.beginSharedBoundaryGeometry());QTRY_COMPARE_WITH_TIMEOUT(controller.geometryEditState().value("boundaryStatus").toString(),QString("ready"),3000);controller.cancelGeometryEdit();QVERIFY(!controller.geometryEditState().value("active").toBool());}
  // The pinned web app does not sync a generic-only delete or its Undo before
  // another Worker request; neither transient membership is observed here.
  controller.undo();QVERIFY(controller.canRedo());QCOMPARE(controller.documentBytes(),before);if(observation!=1&&observation!=3)QCOMPARE(controller.geometrySnapState().value("submitted").toULongLong(),submitted);
  if(observation==3)QVERIFY(controller.openFile(QUrl::fromLocalFile(file)));
  QCOMPARE(snapOwner(controller,"root"),expected);
 }
 void projectReplacementResetsSourceOrder(){
  QTemporaryDir directory;Project project;project.replace(std::vector<Country>{{"a","a",square().polygons,0xabcdef},{"b","b",square().polygons,0xabcdef}});const auto file=write(project,directory.filePath("input.json"));EditorController controller({false,directory.filePath("private.json")});QVERIFY(controller.openFile(QUrl::fromLocalFile(file)));controller.setProjectionMode("flat");controller.resizeMapCamera(800,600);
  QCOMPARE(snapOwner(controller,"a"),QString("vertex:a"));QVERIFY(controller.selectObject({{"domain","territorial"},{"id","a"}},"replace"));QVERIFY(controller.beginDeleteSelection());QVERIFY(controller.confirmStructureMutation());controller.undo();
  QVERIFY(controller.openFile(QUrl::fromLocalFile(file)));QCOMPARE(snapOwner(controller,"a"),QString("vertex:a"));
 }
};
QTEST_MAIN(M974SnapOrderTests)
#include "m974_snap_order_tests.moc"

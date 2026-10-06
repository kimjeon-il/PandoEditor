#include "editorcontroller.h"
#include "projectcodec.h"
#include <QGuiApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <cstdio>
#include <stdexcept>
using namespace pandoeditor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
QByteArray read(const QString& path){QFile f(path);require(f.open(QIODevice::ReadOnly),"Input file unavailable");return f.readAll();}
QString digest(const QByteArray& bytes){return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
QJsonObject blob(const QByteArray& bytes){return {{"base64",QString::fromLatin1(bytes.toBase64())},{"sha256",digest(bytes)},{"bytes",bytes.size()}};}
void settle(EditorController& c){QElapsedTimer t;t.start();do{QCoreApplication::processEvents(QEventLoop::AllEvents,10);if(!c.geometryEditState().value("calculating").toBool()&&!c.webImportBusy())return;QThread::msleep(2);}while(t.elapsed()<30000);throw std::runtime_error("Native lifecycle did not settle");}
void configure(EditorController& c,const QJsonObject& view){
 const auto tr=view["translate"].toArray(),ro=view["rotate"].toArray(),ce=view["center"].toArray();const auto size=view["size"].toObject();
 require(c.setProjectionMode(view["kind"].toString())&&c.publishMapView({{"scale",view["scale"].toDouble()},{"translateX",tr[0].toDouble()},{"translateY",tr[1].toDouble()},{"rotationLongitude",ro[0].toDouble()},{"rotationLatitude",ro[1].toDouble()},{"rotationRoll",ro[2].toDouble()},{"centerLongitude",ce[0].toDouble()},{"centerLatitude",ce[1].toDouble()},{"viewportWidth",size["width"].toDouble()},{"viewportHeight",size["height"].toDouble()}}),"Explicit view rejected");
}
QJsonObject run(const QJsonObject& corpus,const QJsonObject& def,const QString& fixture){
 QTemporaryDir directory;require(directory.isValid(),"Private directory unavailable");const auto profile=def["profile"].toObject();
 EditorController c({profile["pointerType"]=="touch",directory.filePath("private.json")});QJsonArray errors,events,inputs,order;QJsonObject stages;QString phase="setup";
 QObject::connect(&c,&EditorController::errorOccurred,&c,[&](const QString& e){errors.append(e);});
 require(c.prepareWebImport(QUrl::fromLocalFile(fixture)),"Actual web import dispatch rejected");settle(c);require(c.hasWebImportPreview(),"Actual web import preview unavailable");require(c.confirmWebImport(c.webImportHash(),"discard"),"Actual import confirmation rejected");
 c.selectCountry("target");configure(c,def["view"].toObject());MapProjection projection;projection.rebuild(projectcodec::decode(c.documentBytes()));require(projection.hydroParameters()==c.hydroProjection(),"Probe public projection differs from controller");
 QObject::connect(&c,&EditorController::geometryEditChanged,&c,[&]{events.append(QJsonObject{{"sequence",events.size()},{"phase",phase},{"state",QJsonObject::fromVariantMap(c.geometryEditState())}});});
 const auto record=[&](const QString& name,const QJsonValue& outcome=QJsonValue::Null){
  require(def["stages"].toArray().contains(name)&&!stages.contains(name),"Undeclared or duplicate stage");phase=name;const auto bytes=c.documentBytes();Project p;p.replace(projectcodec::decode(bytes));const auto state=c.geometryEditState();const auto paths=c.geometryDraftPaths();QJsonArray coordinates,vertices;
  for(const auto path:paths)for(const auto item:path.toMap().value("vertices").toList()){const auto v=item.toMap();const Point mapped{v.value("x").toDouble(),v.value("y").toDouble()},point=projection.unproject(mapped.x,mapped.y);vertices.append(QJsonArray{mapped.x,mapped.y});coordinates.append(QJsonArray{point.x,point.y});}
  stages[name]=QJsonObject{{"observed",true},{"eventCount",events.size()},{"outcome",outcome},{"document",blob(bytes)},{"web",blob(projectcodec::encodeWeb(p.snapshot()))},{"revision",double(c.revision())},{"history",QJsonObject{{"canUndo",c.canUndo()},{"canRedo",c.canRedo()}}},{"state",QJsonObject::fromVariantMap(state)},{"coordinates",coordinates},{"projectedVertices",vertices},{"draftPaths",QJsonArray::fromVariantList(paths)},{"selection",QJsonObject::fromVariantMap(c.riverSelectionObservation())},{"mapViewState",QJsonObject::fromVariantMap(c.mapViewState())},{"snapState",QJsonObject::fromVariantMap(c.geometrySnapState())}};order.append(name);
 };
 const auto tap=[&](const QString& name,const QJsonArray& point){phase=name;const auto projected=projection.project({point[0].toDouble(),point[1].toDouble()}),inverse=projection.unproject(projected.x,projected.y);const auto screen=c.editMapPointToScreen(projected.x,projected.y,c.mapViewState());const bool pending=c.geometryEditState().value("selectionPending").toBool();const bool accepted=c.geometryAddPoint(projected.x,projected.y,0,profile["pointerType"].toString());inputs.append(QJsonObject{{"stage",name},{"coordinate",point},{"mapCoordinate",QJsonArray{projected.x,projected.y}},{"screen",QJsonArray{screen.x(),screen.y()}},{"roundTrip",QJsonArray{inverse.x,inverse.y}},{"pointerType",profile["pointerType"]},{"prePending",pending},{"accepted",accepted}});record(name,accepted);};
 const auto cycle=[&](const QString& prefix){
  const auto name=[&](const QString& value){return prefix.isEmpty()?value:prefix+value.left(1).toUpper()+value.mid(1);};
  phase=name("activation");c.selectCountry("target");require(c.beginAnnexGeometry(),"Annex start rejected");require(c.geometryToggleProvider({{"domain","territorial"},{"id","donor"}}),"Source provider rejected");settle(c);require(c.geometryAdvanceStage(),"Source advance rejected");settle(c);require(c.geometrySelectTerritoryMethod("polygon"),"Polygon method rejected");record(name("activation"));
  const auto pending=corpus["pendingPoints"].toArray();tap(name("firstPending"),pending[0].toArray());tap(name("secondPending"),pending[1].toArray());
  phase=name("held");require(c.geometryEditState().value("selectionPending").toBool(),"Preparation not pending before barrier");const int before=events.size();QCoreApplication::sendPostedEvents(&c,QEvent::MetaCall);const int emitted=events.size()-before;require(emitted==1,"Selection timer dispatch not observed once");require(QThreadPool::globalInstance()->waitForDone(30000),"Actual selection worker did not finish");require(c.geometryEditState().value("selectionPending").toBool(),"Owner result adopted inside barrier");record(name("held"),QJsonObject{{"timerTailSignals",emitted},{"workerComplete",true},{"ownerCompletionEventsProcessed",false}});
  settle(c);require(!c.geometryEditState().value("selectionPending").toBool(),"Selection remained pending");record(name("ready"));
  const auto ready=corpus["readyPoints"].toArray();const QStringList names{"firstReady","secondReady","thirdReady","fourthReady"};for(int i=0;i<ready.size();++i)tap(name(names[i]),ready[i].toArray());
  phase=name("finished");require(c.geometryFinishTerritoryDraft(),"Actual Finish rejected");settle(c);record(name("finished"),true);require(c.geometryAddTerritoryPart(),"Actual archive rejected");settle(c);require(c.geometryEditState().value("previewReady").toBool(),"Actual preview not ready");record(name("preview"),true);require(c.geometryAdvanceStage(),"Actual review rejected");record(name("review"),true);
 };
 record("before");cycle("");phase="cancel";c.cancelGeometryEdit();settle(c);record("cancel");cycle("retry");phase="applied";require(c.confirmGeometryEdit(),"Actual Apply rejected");settle(c);require(!c.geometryEditState().value("active").toBool(),"Apply did not finish");record("applied",true);
 phase="undo";require(c.canUndo(),"Project Undo unavailable");c.undo();settle(c);record("undo",true);phase="redo";require(c.canRedo(),"Project Redo unavailable");c.redo();settle(c);record("redo",true);
 require(errors.isEmpty(),"Controller emitted operation errors");return {{"case",def["id"]},{"input",def},{"stages",stages},{"stageOrder",order},{"inputs",inputs},{"events",events},{"errors",errors},{"projection",QJsonObject::fromVariantMap(projection.hydroParameters())}};
}
}
int main(int argc,char** argv){QGuiApplication app(argc,argv);try{
 require(argc==3,"Usage: probe corpus.json fixture.json");const auto bytes=read(QString::fromLocal8Bit(argv[1])),fixture=read(QString::fromLocal8Bit(argv[2]));const auto corpus=QJsonDocument::fromJson(bytes).object();require(corpus["schema"]=="pando-m977-pending-lifecycle-corpus"&&corpus["version"]==2,"Unsupported lifecycle corpus");require(digest(fixture)==corpus["fixture"].toObject()["sha256"],"Fixture bytes mismatch");QJsonArray cases;for(const auto d:corpus["cases"].toArray())cases.append(run(corpus,d.toObject(),QString::fromLocal8Bit(argv[2])));
 const auto out=QJsonDocument(QJsonObject{{"schema","pando-m977-native-pending-lifecycle"},{"version",1},{"runtime",QJsonObject{{"compiledQt",QT_VERSION_STR},{"qt",qVersion()}}},{"corpusSha256",digest(bytes)},{"fixtureSha256",digest(fixture)},{"cases",cases}}).toJson(QJsonDocument::Compact);std::fwrite(out.constData(),1,out.size(),stdout);return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}

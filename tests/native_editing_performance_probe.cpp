// Independent M98.5 diagnostic. Fixture helpers retain their original contracts.
// This entry point measures actual controller calls and production job events;
// it never substitutes a timer for an unobserved backend sub-stage.
#define main m974_boundary_reference_main
#include "m974_boundary_probe.cpp"
#undef main
#include "geometrysnapprovider.h"
#include "commandjobrunner.h"
#include "m974_snap_fixture.h"
#include <pandoeditor/map/projectionengine.h>
#include <QGuiApplication>
#include <QDir>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>
#include <functional>

namespace editingdiagnostic {
QByteArray read(const QString& path) { QFile f(path);require(f.open(QIODevice::ReadOnly),"INPUT_MISSING");return f.readAll(); }
QString sha(const QByteArray& b) {return QString::fromLatin1(QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex());}
QJsonObject parse(const QByteArray& b) {QJsonParseError e;auto d=QJsonDocument::fromJson(b,&e);require(e.error==QJsonParseError::NoError&&d.isObject(),"INPUT_INVALID_JSON");return d.object();}
void wait(const std::function<bool()>& ready) {QElapsedTimer t;t.start();while(!ready()){require(t.elapsed()<30000,"ACTUAL_JOB_TIMEOUT");QCoreApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}}
QJsonObject geometry(double left,double bottom,double width=2,double height=2) {
 return {{"type","Polygon"},{"coordinates",QJsonArray{QJsonArray{QJsonArray{left,bottom},QJsonArray{left,bottom+height},QJsonArray{left+width,bottom+height},QJsonArray{left+width,bottom},QJsonArray{left,bottom}}}}};
}
QJsonObject feature(const QString& id,const QJsonObject& g) {return {{"id",id},{"geometry",g},{"properties",QJsonObject{{"name",id},{"entityKind","general"},{"parentId",""},{"coverageMode","explicit"},{"validFrom",QJsonValue::Null},{"validTo",QJsonValue::Null},{"style",QJsonObject{}}}}};}
struct Session {
 QTemporaryDir dir;Project project;MapProjection projection;std::unique_ptr<EditorController> c;
 QJsonArray stages;QByteArray original;
 explicit Session(const QJsonObject& input,const QString& hydro={}) {
  require(dir.isValid(),"TEMP_DIRECTORY_FAILED");auto document=fixture(input);if(!hydro.isEmpty())document.physicalData.source=hydro.toStdString();project.replace(std::move(document));projection.rebuild(project.document());
  QFile f(dir.filePath("input.json"));require(f.open(QIODevice::WriteOnly),"TEMP_INPUT_WRITE_FAILED");const auto bytes=projectcodec::encode(project);require(f.write(bytes)==bytes.size(),"TEMP_INPUT_WRITE_FAILED");f.close();
  EditorControllerConfig config;config.bootstrapWorld=false;config.autosaveEnabled=false;config.privateProjectPath=dir.filePath("private.json");c=std::make_unique<EditorController>(config);
  require(c->openFile(QUrl::fromLocalFile(f.fileName())),"CONTROLLER_OPEN_FAILED");original=c->documentBytes();
 }
 bool stage(const QString& name,const std::function<bool()>& operation,bool required=true) {
  const auto before=c->editingPerformanceStats();const auto serial=before["eventSerial"].toULongLong();QElapsedTimer clock;clock.start();bool accepted=operation();settle(*c);const double ms=clock.nsecsElapsed()/1e6;
  const auto after=c->editingPerformanceStats();QJsonArray events;for(const auto& event:after["events"].toList())if(event.toMap()["eventSerial"].toULongLong()>serial)events.append(QJsonObject::fromVariantMap(event.toMap()));
  stages.append(QJsonObject{{"stage",name},{"accepted",accepted},{"controllerElapsedMs",ms},{"elapsedScope","public call plus queue, worker and owner-thread delivery; excludes frame presentation"},{"eventSerialBefore",qint64(serial)},{"eventSerialAfter",qint64(after["eventSerial"].toULongLong())},{"eventsDroppedBefore",QJsonValue::fromVariant(before["eventsDropped"])},{"eventsDroppedAfter",QJsonValue::fromVariant(after["eventsDropped"])},{"productionEvents",events},{"state",QJsonObject::fromVariantMap(c->geometryEditState())},{"snap",QJsonObject::fromVariantMap(c->geometrySnapState())},{"territory",QJsonObject::fromVariantMap(c->riverSelectionObservation())},{"resourceCaches",QJsonValue::fromVariant(c->renderQuality().value("resourceCaches"))},{"canonicalSha256",sha(c->documentBytes())}});
  // Preserve completed stage evidence even if a subsequent assertion fails.
  auto raw=stages.last().toObject();raw["schema"]="pando-m98-editing-raw-stage-v1";raw["session"]=dir.path();const auto bytes=QJsonDocument(raw).toJson(QJsonDocument::Compact);std::fwrite(bytes.constData(),1,bytes.size(),stdout);std::putchar('\n');std::fflush(stdout);
  if(required)require(accepted,"CONTROLLER_STAGE_REJECTED");return accepted;
 }
 void point(Point p){auto exact=exactScreen(projection,p);require(bool(exact),"NO_EXACT_PUBLIC_INPUT_PREIMAGE");require(c->geometryAddPoint(exact->chosen.x,exact->chosen.y,0),"DRAW_POINT_REJECTED");}
 void applyUndo() {
  stage("confirm",[&]{bool ok=c->confirmGeometryEdit();if(c->geometryEditState()["boundaryImpactConfirmation"].toBool())ok=c->geometryConfirmBoundaryImpacts();return ok;});
  require(!c->geometryEditState()["active"].toBool()&&c->canUndo(),"COMMIT_NOT_OBSERVED");
  require(!c->geometryPresentationPaths().isEmpty(),"COMMITTED_PREVIEW_NOT_HELD_UNTIL_PRESENTATION");
  const auto changed=c->documentBytes();require(changed!=original,"COMMIT_DID_NOT_CHANGE_DOCUMENT");
  stage("undo",[&]{c->undo();return true;});require(semanticallyEqual(projectcodec::decode(original),projectcodec::decode(c->documentBytes())),"UNDO_SEMANTICS_DIFFER");
  stage("redo",[&]{require(c->canRedo(),"REDO_NOT_AVAILABLE");c->redo();return true;});require(semanticallyEqual(projectcodec::decode(changed),projectcodec::decode(c->documentBytes())),"REDO_SEMANTICS_DIFFER");
  stage("undo-after-redo",[&]{c->undo();return true;});
 }
};
QJsonObject runBoundary(int count) {
 // Partition one fixed square into N disjoint sectors. The central node has N
 // owners; moving it preserves the outer union and introduces no overlap.
 // Coincident copies would be invalid: moving their outer vertex changes union.
 const Ring perimeter{{-4,-4},{-2,-4},{0,-4},{2,-4},{4,-4},{4,-2},{4,0},{4,2},{4,4},{2,4},{0,4},{-2,4},{-4,4},{-4,2},{-4,0},{-4,-2}};
 QJsonArray features;QVariantList refs;for(int i=0;i<count;++i){auto id=QString("owner-%1").arg(i);QJsonArray ring{QJsonArray{0,0}};for(int j=0;j<=16/count;++j){auto p=perimeter[(i*(16/count)+j)%16];ring.append(QJsonArray{p.x,p.y});}ring.append(QJsonArray{0,0});features.append(feature(id,{{"type","Polygon"},{"coordinates",QJsonArray{ring}}}));refs.append(QVariantMap{{"domain","territorial"},{"id",id}});}
 Session s({{"features",features}});auto& c=*s.c;require(c.setSelection(refs,refs.first().toMap()),"OWNER_SELECTION_REJECTED");
 auto gesture=[&](bool cancel){s.stage("prepare",[&]{return c.beginSharedBoundaryGeometry();});require(c.geometryEditState()["boundaryStatus"]=="ready","BOUNDARY_NOT_READY");require(c.geometryEditState()["targets"].toList().size()==count,"ACTUAL_BOUNDARY_OWNER_COUNT_DIFFERS");
  QVariantMap node;for(auto p:c.geometryDraftPaths())for(auto v:p.toMap()["vertices"].toList())if(v.toMap()["nodeKey"].toString()==QString::fromStdString(sharedboundary::nodeKeyText({0,0})))node=v.toMap();
  require(!node.empty(),"SHARED_HANDLE_MISSING");s.stage("query-begin-drag",[&]{return c.geometrySelectNearest(node["x"].toDouble(),node["y"].toDouble(),.001)&&c.geometryBeginVertexDrag();});
  auto target=exactScreen(s.projection,{1,0});require(bool(target),"BOUNDARY_INPUT_PREIMAGE_MISSING");s.stage("drag",[&]{return c.geometryMoveSelectedVertex(target->chosen.x,target->chosen.y,0);});
  s.stage(cancel?"release-cancel":"release-auto-preview",[&]{c.geometryEndVertexDrag(cancel);return true;});
  if(cancel){s.stage("cancel",[&]{c.cancelGeometryEdit();return true;});require(c.documentBytes()==s.original,"CANCEL_CHANGED_DOCUMENT");}
  else {require(c.geometryEditState()["previewReady"].toBool(),"BOUNDARY_PREVIEW_MISSING");s.applyUndo();}
 };
 gesture(false);require(c.setSelection(refs,refs.first().toMap()),"REPEAT_SELECTION_REJECTED");gesture(true);require(c.setSelection(refs,refs.first().toMap()),"REPEAT_SELECTION_REJECTED");gesture(false);
 return {{"case",QString("shared-boundary-%1").arg(count)},{"fixtureClass","synthetic mechanism: disjoint square sectors sharing one central node; fixed outer union"},{"requestedOwners",count},{"sourceInput",QJsonObject{{"features",features}}},{"stages",s.stages},{"events",QJsonObject::fromVariantMap(c.editingPerformanceStats())}};
}
QJsonObject runTerritory(bool river,const QString& hydro) {
 QJsonArray features{feature("target",geometry(30,0,10,10)),feature("donor0",geometry(40,0,10,10))};Session s({{"features",features}},river?hydro:QString{});auto& c=*s.c;c.selectCountry("target");
 const auto start=[&](QString method){s.stage("begin-annex",[&]{return c.beginAnnexGeometry();});s.stage("provider",[&]{return c.geometryToggleProvider({{"domain","territorial"},{"id","donor0"}});});s.stage("advance",[&]{return c.geometryAdvanceStage();});s.stage("method",[&]{return c.geometrySelectTerritoryMethod(method);});};
 start(river?"components":"polygon");
 auto choose=[&]{auto parts=c.geometryEditState()["components"].toList();require(!parts.empty(),"RIVER_COMPONENTS_MISSING");s.stage("component-selection",[&]{return c.geometryToggleTerritoryComponent(parts.first().toMap()["key"].toString());});};
 auto partition=[&]{s.stage("river-provider-and-partition",[&]{if(!c.geometryToggleRiverBoundaries(true))return false;wait([&]{return c.geometryEditState()["riverStatus"]!="pending"&&!c.geometryEditState()["calculating"].toBool();});require(c.geometryEditState()["riverStatus"]=="ready","REAL_HYDRO_NOT_READY");require(c.riverSelectionObservation()["sourceDiagnostics"].toMap()["loadedRivers"].toInt()>0,"ACTUAL_RIVER_INPUT_NOT_LOADED");return true;});};
 if(river){partition();choose();}else{s.stage("draw",[&]{for(auto p:Ring{{40,0},{40,10},{45,10},{45,0}})s.point(p);return true;});s.stage("draft-candidates",[&]{return c.geometryFinishTerritoryDraft();});}
 s.stage("archive",[&]{return c.geometryAddTerritoryPart();});
 if(river){auto parts=c.geometryEditState()["parts"].toList();require(!parts.empty(),"ARCHIVED_PART_MISSING");s.stage("delete-part-and-invalidate",[&]{return c.geometryRemoveTerritoryPart(parts.first().toMap()["id"].toString());});require(c.geometryEditState()["parts"].toList().empty(),"DELETED_PART_REMAINED");s.stage("components-after-delete",[&]{return c.geometrySelectTerritoryMethod("components");});partition();choose();s.stage("archive-after-repartition",[&]{return c.geometryAddTerritoryPart();});}
 s.stage("review-preview",[&]{return c.geometryAdvanceStage();});require(c.geometryEditState()["previewReady"].toBool(),"TERRITORY_PREVIEW_MISSING");s.applyUndo();
 start(river?"components":"polygon");s.stage("cancel-repeat",[&]{c.cancelGeometryEdit();return true;});require(semanticallyEqual(projectcodec::decode(s.original),projectcodec::decode(c.documentBytes())),"CANCEL_REPEAT_CHANGED_DOCUMENT");
 return {{"case",river?"river-delete-repartition":"annex-draw-archive-apply"},{"fixtureClass","synthetic editing geometry; river uses immutable existing miniature production-format hydro"},{"sourceInput",QJsonObject{{"features",features}}},{"stages",s.stages},{"events",QJsonObject::fromVariantMap(c.editingPerformanceStats())}};
}
QJsonObject runSplit(const QJsonObject& d) {
 Session s({{"features",QJsonArray{feature("source",d["source"].toObject())}}});auto& c=*s.c;c.selectCountry("source");const auto view=d["view"].toObject();const auto t=view["translate"].toArray(),r=view["rotate"].toArray(),center=view["center"].toArray();const auto size=view["size"].toObject();
 require(c.setProjectionMode(view["kind"].toString()),"SPLIT_VIEW_REJECTED");require(c.publishMapView({{"scale",view["scale"].toDouble()},{"translateX",t[0].toDouble()},{"translateY",t[1].toDouble()},{"rotationLongitude",r[0].toDouble()},{"rotationLatitude",r[1].toDouble()},{"rotationRoll",r[2].toDouble()},{"centerLongitude",center[0].toDouble()},{"centerLatitude",center[1].toDouble()},{"viewportWidth",size["width"].toDouble()},{"viewportHeight",size["height"].toDouble()}}),"SPLIT_VIEW_REJECTED");
 auto start=[&]{c.selectCountry("source");s.stage("begin-split",[&]{return c.beginSplitGeometry();});s.stage("advance",[&]{return c.geometryAdvanceStage();});s.stage("line-method",[&]{return c.geometrySelectTerritoryMethod("line");});s.stage("line-input",[&]{for(auto p:d["coords"].toArray()){auto xy=p.toArray();s.point({xy[0].toDouble(),xy[1].toDouble()});}return true;});require(QJsonObject::fromVariantMap(c.riverSelectionObservation())["inputLine"]==d["coords"],"SPLIT_INPUT_DRIFT");s.stage("cut-and-candidates",[&]{return c.geometryFinishTerritoryDraft();});};
 start();bool archive=s.stage("archive",[&]{return c.geometryAddTerritoryPart();},false);bool review=archive&&s.stage("review-preview",[&]{return c.geometryAdvanceStage();},false);
 require(archive&&review&&c.geometryEditState()["previewReady"].toBool(),"SPLIT_PREVIEW_NOT_REACHED");s.applyUndo();
 s.stage("cancel",[&]{c.cancelGeometryEdit();return true;});require(semanticallyEqual(projectcodec::decode(s.original),projectcodec::decode(c.documentBytes())),"SPLIT_CANCEL_CHANGED_DOCUMENT");start();s.stage("cancel-repeat",[&]{c.cancelGeometryEdit();return true;});
 return {{"case",d["id"]},{"fixtureClass","exact existing pinned split input; no regenerated oracle"},{"sourceInput",d},{"archiveAccepted",archive},{"previewAccepted",review},{"stages",s.stages},{"events",QJsonObject::fromVariantMap(c.editingPerformanceStats())},{"backendInitializationCount",QJsonValue::Null},{"backendInitializationMs",QJsonValue::Null},{"scriptLoadMs",QJsonValue::Null},{"qjsConversionMs",QJsonValue::Null},{"internalCutMs",QJsonValue::Null},{"unobservedReason","public production events time the whole worker operation; engine initialization, script loading, conversion and cut do not have separate hooks"}};
}
QJsonObject runSnap(int distant,const QString& pointer) {
 ProjectDocument d;d.documentId="m98-editing-snap-diagnostic";auto add=[&](QString id,QJsonObject g){auto ref=GeometryRef{id.toStdString(),1};d.geometries.insert(ref,decodeGeometry(g));GenericFeature f;f.id=id.toStdString();f.geometry=ref;d.genericFeatures.push_back(f);};
 add("local",geometry(0,0));for(int i=0;i<distant;++i)add(QString("distant-%1").arg(i),geometry(40+(i%50)*2,30+((i/50)%20)*2,1,1));Project p;p.replace(std::move(d));CommandJobRunner runner([&]()->const Project&{return p;});geometrysnap::Provider provider(runner);QJsonArray samples,events;
 QObject::connect(&runner,&CommandJobRunner::operationMeasured,&runner,[&](qulonglong id,QString key,double ms,int disposition){events.append(QJsonObject{{"jobId",qint64(id)},{"flow",key},{"durationMs",ms},{"disposition",disposition}});});
 MapViewState view;view.viewportWidth=1920;view.viewportHeight=929;view.translateX=960;view.translateY=464.5;view.scale=100*180/3.14159265358979323846;
 const double margin=geometrysnap::marginForScale(view.scale);
 const geometrysnap::ProjectPoint project=[view](Point xy)->std::optional<Point>{const auto point=projectPoint(xy,view);if(!point.finite)return {};return Point{point.x,point.y};};
 std::vector<double> resolvedTimes,elapsedTimes;for(int i=0;i<300;++i){geometrysnap::Request request;request.coordinate={double(i%60)/30.0,double(i/60)/2.0};request.margin=margin*2;QElapsedTimer elapsed;elapsed.start();provider.candidates(p.snapshot(),request,"draw",margin,1);wait([&]{return provider.status()!="pending";});require(provider.status()=="ready","SNAP_PROVIDER_NOT_READY");const auto& candidates=provider.candidates(p.snapshot(),request,"draw",margin,1);QElapsedTimer resolve;resolve.start();const auto result=geometrysnap::resolveSnap(request.coordinate,*project(request.coordinate),candidates,project,pointer.toStdString());const double resolveMs=resolve.nsecsElapsed()/1e6,elapsedMs=elapsed.nsecsElapsed()/1e6;resolvedTimes.push_back(resolveMs);elapsedTimes.push_back(elapsedMs);const auto diagnostics=provider.diagnostics();QJsonArray returned;for(const auto& candidate:candidates)returned.append(m974snapfixture::json(candidate));samples.append(QJsonObject{{"sequence",i},{"coordinate",QJsonArray{request.coordinate.x,request.coordinate.y}},{"candidateCount",int(candidates.size())},{"candidates",returned},{"resolved",m974snapfixture::json(result)},{"resolveMs",resolveMs},{"providerAndResolveElapsedMs",elapsedMs},{"submittedCount",qint64(provider.submittedCount())},{"diagnostics",QJsonObject{{"nearbyObjects",qint64(diagnostics.nearbyObjects)},{"segmentEntriesExamined",qint64(diagnostics.segmentEntriesExamined)},{"visitedSegments",qint64(diagnostics.visitedSegments)},{"intersectionTests",qint64(diagnostics.intersectionTests)},{"geometryIndexBuilds",qint64(diagnostics.geometryIndexBuilds)},{"preparedObjects",qint64(diagnostics.preparedObjects)}}}});}
 std::sort(resolvedTimes.begin(),resolvedTimes.end());std::sort(elapsedTimes.begin(),elapsedTimes.end());return {{"case",QString("snap-%1-%2").arg(pointer).arg(distant)},{"fixtureClass","synthetic mechanism: original m974 diagnostic distant-grid pattern, actual production Provider and resolveSnap"},{"pointerType",pointer},{"radiusPixels",geometrysnap::snapThreshold(pointer.toStdString())},{"totalObjects",distant+1},{"screenViewport",QJsonArray{1920,929}},{"samples",samples},{"workerEvents",events},{"p95ResolveMs",resolvedTimes[284]},{"p95ProviderAndResolveElapsedMs",elapsedTimes[284]},{"workerScope","actual submitGeometry calculation, excludes queue and GUI delivery"}};
}
}
int main(int argc,char** argv) {
 QGuiApplication app(argc,argv);QStandardPaths::setTestModeEnabled(true);QCoreApplication::setApplicationName("m98-editing-diagnostic-"+QUuid::createUuid().toString(QUuid::WithoutBraces));QJsonArray results;int failures=0;
 try {require(argc==2||(argc==3&&QString::fromLocal8Bit(argv[2])=="--validate-only"),"USAGE: native_editing_performance_probe diagnostic-manifest.json [--validate-only]");auto inputPath=QString::fromLocal8Bit(argv[1]);auto manifest=editingdiagnostic::parse(editingdiagnostic::read(inputPath));require(manifest["schema"]=="pando-m98-editing-diagnostic-input-v1","MANIFEST_SCHEMA_INVALID");require(manifest["fixedWebSha"]=="ebcfae4d27b29cbbea6416a7045a4806930204be","SOURCE_PIN_INVALID");require(manifest["caseCount"].toInt()==14&&manifest["splitInputs"].toArray().size()==4&&!manifest["pins"].toArray().empty(),"MANIFEST_CASE_CONTRACT_INVALID");auto root=QFileInfo(inputPath).absoluteDir();
  for(auto item:manifest["pins"].toArray()){auto pin=item.toObject();auto bytes=editingdiagnostic::read(root.absoluteFilePath(pin["path"].toString()));require(bytes.size()==pin["bytes"].toInteger()&&editingdiagnostic::sha(bytes)==pin["sha256"].toString(),"FIXTURE_HASH_MISMATCH");}
  auto place=editingdiagnostic::parse(editingdiagnostic::read(root.absoluteFilePath(manifest["productionPlaceManifest"].toString())));require(place["revision"]=="empty-v1"&&place["tiles"].toObject().empty()&&place["stages"].toArray().empty(),"DIAGNOSTIC_PLACE_CONTRACT_CHANGED");
  if(argc==3){std::puts("{\"schema\":\"pando-m98-editing-input-preflight-v1\",\"validated\":true,\"caseCount\":14,\"acceptanceStatus\":\"BLOCKED\",\"acceptedBy\":null}");return 0;}
  auto run=[&](QString id,std::function<QJsonObject()> execute){try{auto result=execute();result["executionStatus"]="MEASURED_DIAGNOSTIC";results.append(result);}catch(const std::exception& e){++failures;results.append(QJsonObject{{"case",id},{"executionStatus","FAIL"},{"error",e.what()}});}const auto bytes=QJsonDocument(results.last().toObject()).toJson(QJsonDocument::Compact);std::fwrite(bytes.constData(),1,bytes.size(),stdout);std::putchar('\n');std::fflush(stdout);};
  for(int count:{500,5000})for(QString pointer:{QStringLiteral("mouse"),QStringLiteral("touch")})run(QString("snap-%1-%2").arg(pointer).arg(count),[=]{return editingdiagnostic::runSnap(count,pointer);});
  run("annex-draw-archive-apply",[]{return editingdiagnostic::runTerritory(false,{});});const auto hydro=root.absoluteFilePath(manifest["hydroManifest"].toString());run("river-delete-repartition",[&]{return editingdiagnostic::runTerritory(true,hydro);});
  for(auto row:manifest["splitInputs"].toArray()){auto definition=row.toObject();run(definition["id"].toString(),[=]{return editingdiagnostic::runSplit(definition);});}for(int count:{2,4,8,16})run(QString("shared-boundary-%1").arg(count),[=]{return editingdiagnostic::runBoundary(count);});
  require(results.size()==14,"CASE_COUNT_INVALID");QJsonObject summary{{"schema","pando-m98-editing-diagnostic-result-v1"},{"durationUnit","ms"},{"fixedWebSha",manifest["fixedWebSha"]},{"inputManifestSha256",editingdiagnostic::sha(editingdiagnostic::read(inputPath))},{"cases",results},{"caseCount",results.size()},{"failures",failures},{"qtRuntime",qVersion()},{"status",failures?"FAIL":"DIAGNOSTIC"},{"acceptanceStatus","BLOCKED: immutable production place dataset is empty-v1; full data acceptance has not executed"},{"acceptedBy",QJsonValue::Null},{"editingBudgets",QJsonValue::Null},{"framePresentationObserved",false}};const auto bytes=QJsonDocument(summary).toJson(QJsonDocument::Compact);std::fwrite(bytes.constData(),1,bytes.size(),stdout);std::putchar('\n');return failures?1:0;
 }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 2;}
}

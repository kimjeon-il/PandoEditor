#include "territorial_fixture.h"
#include "editorcontroller.h"
#include "projectcodec.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <cmath>
#include <limits>
#include <stdexcept>
using namespace pandoeditor;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
Geometry decodeGeometry(const QJsonObject& json){
 Geometry g;g.type=json["type"].toString().toStdString();require(g.type=="Polygon"||g.type=="MultiPolygon","invalid fixture geometry");auto ps=json["coordinates"].toArray();if(g.type=="Polygon")ps=QJsonArray{ps};
 for(const auto& p:ps){Polygon polygon;for(const auto& r:p.toArray()){Ring ring;for(const auto& c:r.toArray()){const auto xy=c.toArray();require(xy.size()==2&&xy[0].isDouble()&&xy[1].isDouble(),"invalid fixture coordinate");ring.push_back({xy[0].toDouble(),xy[1].toDouble()});}polygon.push_back(ring);}g.polygons.push_back(polygon);}return g;
}
QJsonObject encodeGeometry(const Geometry& g){QJsonArray ps;for(const auto& p:g.polygons){QJsonArray rs;for(const auto& r:p){QJsonArray cs;for(const auto xy:r)cs.append(QJsonArray{xy.x,xy.y});rs.append(cs);}ps.append(rs);}return {{"type",QString::fromStdString(g.type)},{"coordinates",g.type=="Polygon"?ps[0].toArray():ps}};}
// A fixture-input bridge only: select a nearby representable screen value
// whose PUBLIC inverse projection produces the intended geographic double
// exactly. Never alter source geometry or comparison results. Remove with the
// stage-6 screen interaction parity work; a missing exact preimage is a failure.
struct ExactScreenInput {Point naive,chosen;int xSteps=0,ySteps=0;};
std::optional<ExactScreenInput> exactScreenInput(const MapProjection& projection,Point intended){
 if(!std::isfinite(intended.x)||!std::isfinite(intended.y))return {};
 ExactScreenInput result;result.naive=projection.project(intended);result.chosen=result.naive;
 if(!std::isfinite(result.naive.x)||!std::isfinite(result.naive.y))return {};
 auto solve=[&](bool x,double start,double target,double& value,int& steps){
  auto inverse=[&](double candidate){return x?projection.unproject(candidate,result.naive.y).x:projection.unproject(result.naive.x,candidate).y;};
  if(inverse(start)==target){value=start;steps=0;return true;}
  double below=start,above=start;
  for(int radius=1;radius<=4;++radius){below=std::nextafter(below,-std::numeric_limits<double>::infinity());above=std::nextafter(above,std::numeric_limits<double>::infinity());for(const auto candidate:{std::pair<double,int>{below,-radius},{above,radius}})if(inverse(candidate.first)==target){value=candidate.first;steps=candidate.second;return true;}}
  return false;
 };
 if(!solve(true,result.naive.x,intended.x,result.chosen.x,result.xSteps)||!solve(false,result.naive.y,intended.y,result.chosen.y,result.ySteps))return {};
 const auto observed=projection.unproject(result.chosen.x,result.chosen.y);if(observed.x!=intended.x||observed.y!=intended.y)return {};return result;
}
QJsonObject inputContractTests(){
 ProjectDocument d;d.documentId="m973-input-contract";Geometry shape;shape.type="Polygon";shape.polygons={{{{0,0},{0,10},{10,10},{10,0},{0,0}}}};d.geometries.insert({"source",1},shape);appendTerritory(d,{"source","Source","",UnitKind::General,false},{"source",1});d.presentation.objectStyles[territorialRef("source")]={};MapProjection projection;projection.rebuild(d);
 require(exactScreenInput(projection,{-1,5}).has_value(),"control coordinate lacks exact preimage");
 require(exactScreenInput(projection,{0,0}).has_value(),"zero control lacks exact preimage");
 require(!exactScreenInput(projection,{std::numeric_limits<double>::infinity(),0}),"nonfinite input accepted");
 require(!exactScreenInput(projection,{0,std::numeric_limits<double>::quiet_NaN()}),"NaN input accepted");
 projection.setWorldExtent();require(!exactScreenInput(projection,{0.1,0}),"known absent exact preimage must fail closed");require(exactScreenInput(projection,{180,0}).has_value(),"dateline lacks exact preimage");require(exactScreenInput(projection,{-180,90}).has_value(),"pole lacks exact preimage");
 return {{"schema","m973-exact-screen-preimage-tests"},{"passed",7},{"maximumUlpRadius",4}};
}
ProjectDocument fixture(const QJsonObject& row){
 require(row["definition"].isObject()&&row["features"].isArray()&&!row["features"].toArray().empty(),"missing fixture definition or features");ProjectDocument d;d.documentId="m973-split-controller";
 for(const auto& f:row["features"].toArray()){const auto feature=f.toObject(),properties=feature["properties"].toObject();const auto id=feature["id"].toString().toStdString();require(!id.empty(),"missing source identity");const GeometryRef ref{id,1};d.geometries.insert(ref,decodeGeometry(feature["geometry"].toObject()));appendTerritory(d,{id,properties["name"].toString().toStdString(),"",UnitKind::General,false},ref,properties["parentId"].toString().toStdString(),properties["coverageMode"].toString().toStdString());d.presentation.objectStyles[territorialRef(id)]={};}
 const auto def=row["definition"].toObject();
 if(def["dependentReference"].toBool()){DistributionLayer layer;layer.id="distribution";layer.name="Fixture";d.distributionLayers.push_back(layer);DistributionEntry e;e.id="child-reference";e.layerId=layer.id;e.territory=territorialRef("child-moved");e.value=1;d.distributionEntries.push_back(e);}
 if(def["dependentLabel"].toBool()){Geometry g;g.type="Point";g.points={{1.5,7.5}};d.geometries.insert({"child-label",1},g);PlaceLabel l;l.id="child-label";l.name="Source";l.geometry={"child-label",1};l.territory=territorialRef("source");d.labels.push_back(l);}
 if(def["dependentLabelSettings"].toBool())d.presentation.webPresentation.labelSettings[territorialRef("child-moved")]={};
 if(def["dependentPresentation"].toBool()){d.presentation.webPresentation.hiddenItems["subunits"].insert("child-moved");d.presentation.webPresentation.objectStyles["territorial:entity:child-moved"].opacity=0.5;}
 if(def["dependentGeneric"].toBool()){require(row["genericFeatures"].isArray(),"missing generic fixture observations");for(const auto& item:row["genericFeatures"].toArray()){const auto f=item.toObject(),p=f["properties"].toObject(),source=p["source"].toObject();GenericFeature g;g.id=f["id"].toString().toStdString();g.name=p["name"].toString().toStdString();g.geometry={g.id,1};Geometry point;point.type="Point";const auto xy=f["geometry"].toObject()["coordinates"].toArray();point.points={{xy[0].toDouble(),xy[1].toDouble()}};d.geometries.insert(g.geometry,point);g.source.kind=source["kind"].toString().toStdString();g.source.details=QJsonDocument(source["details"].toObject()).toJson(QJsonDocument::Compact).toStdString();d.genericFeatures.push_back(g);}}
 validateDocument(d);return d;
}
void settle(EditorController& c){QElapsedTimer t;t.start();do{QCoreApplication::processEvents(QEventLoop::AllEvents,10);if(!c.geometryEditState().value("calculating").toBool())return;QThread::msleep(2);}while(t.elapsed()<20000);throw std::runtime_error("split controller job did not settle");}
QJsonObject snapshot(EditorController& c,const QByteArray& before,QJsonValue outcome=QJsonValue::Null){
 const auto bytes=c.documentBytes();const auto d=projectcodec::decode(bytes);const auto raw=QJsonObject::fromVariantMap(c.riverSelectionObservation());QJsonArray features,references,preview,hidden,styles,generic;
 for(const auto& u:d.units){const auto& relation=staticParentRelation(d,u.id);features.append(QJsonObject{{"id",QString::fromStdString(u.id)},{"name",QString::fromStdString(u.name)},{"parentId",QString::fromStdString(relation.parentId)},{"coverageMode",QString::fromStdString(relation.coverageMode)},{"geometry",encodeGeometry(*d.geometries.get(staticGeometryBinding(d,u.id).geometryRef))}});}
 for(const auto& e:d.distributionEntries)if(e.territory)references.append(QJsonObject{{"kind","distribution"},{"id",QString::fromStdString(e.id)},{"target",QString::fromStdString(e.territory->id)}});
 for(const auto& l:d.labels)if(l.territory)references.append(QJsonObject{{"kind","label"},{"id",QString::fromStdString(l.id)},{"target",QString::fromStdString(l.territory->id)}});
 for(const auto& [ref,settings]:d.presentation.webPresentation.labelSettings)references.append(QJsonObject{{"kind","label-settings"},{"id",QString::fromStdString(ref.id)},{"target",QString::fromStdString(ref.id)}});
 const auto state=QJsonObject::fromVariantMap(c.geometryEditState());if(raw["splitPreviewPresent"].toBool())for(const auto& item:raw["splitPreview"].toObject()["rows"].toArray()){const auto r=item.toObject();if(r["after"].isObject()){QJsonObject feature{{"id",r["owner"].toObject()["id"]},{"geometry",r["after"]}};if(r.contains("parentId"))feature["parentId"]=r["parentId"];preview.append(feature);}}
 for(const auto& [group,ids]:d.presentation.webPresentation.hiddenItems)for(const auto& id:ids)hidden.append(QJsonObject{{"group",QString::fromStdString(group)},{"id",QString::fromStdString(id)}});
 for(const auto& [key,style]:d.presentation.webPresentation.objectStyles){QJsonObject s{{"key",QString::fromStdString(key)}};if(style.opacity)s["opacity"]=*style.opacity;styles.append(s);}
 for(const auto& f:d.genericFeatures)generic.append(QJsonObject{{"id",QString::fromStdString(f.id)},{"sourceDetails",QJsonDocument::fromJson(QByteArray::fromStdString(f.source.details)).object()}});
 return {{"outcome",outcome},{"state",state},{"raw",raw},{"features",features},{"references",references},{"previewFeatures",preview},{"presentation",QJsonObject{{"hiddenItems",hidden},{"objectStyles",styles}}},{"genericMetadata",generic},{"primaryObject",QJsonObject::fromVariantMap(c.primaryObject())},{"history",QJsonObject{{"canUndo",c.canUndo()},{"canRedo",c.canRedo()}}},{"documentSha256",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())},{"unchangedFromBefore",bytes==before}};
}
QJsonObject run(const QJsonObject& row){
 QTemporaryDir dir;require(dir.isValid(),"fixture directory unavailable");Project p;p.replace(fixture(row));MapProjection projection;projection.rebuild(p.document());QFile file(dir.filePath("input.json"));require(file.open(QIODevice::WriteOnly),"fixture write failed");file.write(projectcodec::encode(p));file.close();
 EditorController c({false,dir.filePath("private.json")});require(c.openFile(QUrl::fromLocalFile(file.fileName())),"controller fixture open failed");c.selectCountry("source");const auto def=row["definition"].toObject(),view=def["view"].toObject();const auto translate=view["translate"].toArray(),rotate=view["rotate"].toArray(),center=view["center"].toArray();const auto size=view["size"].toObject();
 require(c.setProjectionMode(view["kind"].toString()),"fixture view mode failed");require(c.publishMapView({{"scale",view["scale"].toDouble()},{"translateX",translate[0].toDouble()},{"translateY",translate[1].toDouble()},{"rotationLongitude",rotate[0].toDouble()},{"rotationLatitude",rotate[1].toDouble()},{"rotationRoll",rotate[2].toDouble()},{"centerLongitude",center[0].toDouble()},{"centerLatitude",center[1].toDouble()},{"viewportWidth",size["width"].toDouble()},{"viewportHeight",size["height"].toDouble()}}),"fixture view rejected");
 const auto before=c.documentBytes();QJsonObject stages;QJsonArray order,events,inputObservations,reactivationEvidence;
 QObject::connect(&c,&EditorController::geometryEditChanged,&c,[&]{const auto s=c.geometryEditState();events.append(QJsonObject{{"selectionPending",s.value("selectionPending").toBool()},{"previewPending",s.value("previewPending").toBool()},{"applying",s.value("applying").toBool()},{"active",s.value("active").toBool()}});});
 auto observe=[&](const QString& name,QJsonValue outcome=QJsonValue::Null){order.append(name);stages[name]=snapshot(c,before,outcome);};
 auto start=[&](){c.selectCountry("source");require(c.beginSplitGeometry(),"begin actual split failed");require(c.geometryAdvanceStage(),"advance setup failed");require(c.geometrySelectTerritoryMethod("line"),"select line failed");settle(c);QJsonArray projected;
  for(const auto& value:def["coords"].toArray()){const auto xy=value.toArray();const Point intended{xy[0].toDouble(),xy[1].toDouble()};const auto exact=exactScreenInput(projection,intended);require(exact.has_value(),"No exact public screen preimage within 4 ULPs; fixture input unobserved");projected.append(QJsonObject{{"naive",QJsonArray{exact->naive.x,exact->naive.y}},{"chosen",QJsonArray{exact->chosen.x,exact->chosen.y}},{"xUlpSteps",exact->xSteps},{"yUlpSteps",exact->ySteps},{"maximumUlpRadius",4}});require(c.geometryAddPoint(exact->chosen.x,exact->chosen.y,0),"actual line input failed");}
  require(QJsonObject::fromVariantMap(c.riverSelectionObservation())["inputLine"]==def["coords"],"actual controller input differs from exact intended coordinates");inputObservations.append(QJsonObject{{"intended",def["coords"]},{"projected",projected},{"observed",QJsonObject::fromVariantMap(c.riverSelectionObservation())["inputLine"]},{"view",QJsonObject::fromVariantMap(c.mapViewState())}});
  const bool accepted=c.geometryFinishTerritoryDraft();settle(c);auto input=inputObservations.last().toObject();input["finishDispatchAccepted"]=accepted;input["settledError"]=c.geometryEditState().value("error").toString();inputObservations.replace(inputObservations.size()-1,input);return accepted&&!c.geometryEditState().value("candidates").toList().empty();};
 auto select=[&](bool record=false){const auto wanted=def["select"];if(wanted.isString()&&wanted=="default")return;const auto state=c.geometryEditState();const auto candidates=state.value("candidates").toList(),selected=state.value("selectedCandidateIds").toList();for(int i=0;i<candidates.size();++i){const auto id=candidates[i].toMap().value("id").toString();bool choose=wanted=="all";for(const auto& v:wanted.toArray())if(v.toInt(-1)==i)choose=true;if(selected.contains(id)!=choose)require(c.geometryToggleTerritoryCandidate(id),"candidate toggle failed");}settle(c);if(def["reactivateAfterEmpty"].toBool()){require(selected.size()==1,"reactivation requires one originally selected candidate");const bool advance=c.geometryAdvanceStage();settle(c);if(record)observe("emptied",advance);require(!advance,"empty selection unexpectedly advanced");const auto eventStart=events.size();const bool picked=c.geometryToggleTerritoryCandidate(selected[0].toString());settle(c);QJsonArray observedEvents;for(auto i=eventStart;i<events.size();++i)observedEvents.append(events[i]);reactivationEvidence.append(QJsonObject{{"candidateId",selected[0].toString()},{"emptiedAdvanceAccepted",advance},{"reactivatedSelectionAccepted",picked},{"events",observedEvents}});if(record)observe("reactivated",picked);require(picked,"actual reactivation failed");}};
 observe("before");const bool finished=start();observe("candidates",finished);select(true);observe("selected");const bool archived=c.geometryAddTerritoryPart();settle(c);observe("archive",archived);const bool reviewed=c.geometryAdvanceStage();settle(c);observe("review",reviewed);c.cancelGeometryEdit();observe("cancel");
 if(archived&&reviewed){start();select();require(c.geometryAddTerritoryPart(),"repeat archive failed");settle(c);require(c.geometryAdvanceStage(),"repeat review failed");settle(c);const bool requested=c.confirmGeometryEdit();settle(c);const bool committed=requested&&!c.geometryEditState().value("active").toBool();observe("confirm",committed);if(committed){const bool undo=c.canUndo();c.undo();observe("undo",undo);const bool redo=c.canRedo();c.redo();observe("redo",redo);}}
 c.cancelGeometryEdit();return {{"case",row["case"]},{"stageOrder",order},{"stages",stages},{"events",events},{"inputObservations",inputObservations},{"reactivationEvidence",reactivationEvidence}};
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{if(argc==2&&QString::fromLocal8Bit(argv[1])=="--input-contract-self-test"){const auto bytes=QJsonDocument(inputContractTests()).toJson(QJsonDocument::Compact);std::fwrite(bytes.constData(),1,bytes.size(),stdout);return 0;}require(argc==1,"unexpected probe argument");QFile in;require(in.open(stdin,QIODevice::ReadOnly),"stdin unavailable");QJsonParseError error;const auto input=QJsonDocument::fromJson(in.readAll(),&error);require(error.error==QJsonParseError::NoError&&input.isArray(),"invalid fixture corpus");QJsonArray results;for(const auto& row:input.array())results.append(run(row.toObject()));const auto bytes=QJsonDocument(results).toJson(QJsonDocument::Compact);std::fwrite(bytes.constData(),1,bytes.size(),stdout);return 0;}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}

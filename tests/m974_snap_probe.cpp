#include <pandoeditor/map/geometrysnap.h>
#include "m974_snap_fixture.h"
#include "territorial_fixture.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <algorithm>
#include <map>
#include <stdexcept>
using namespace pandoeditor;
namespace {
using namespace m974snapfixture;
QJsonObject run(const QJsonObject& input){
 if(input.contains("nodeKeyValues")){QJsonArray keys;for(const auto v:input["nodeKeyValues"].toArray())keys.append(QString::fromStdString(geometrysnap::nodeKey(point(v))));return {{"nodeKeys",keys}};}
 ProjectDocument document;document.documentId="m974-snap-probe";
 const auto add=[&](QJsonObject feature,bool generic){const auto id=feature["id"].toString().toStdString();if(id.empty())throw std::runtime_error("missing feature ID");const GeometryRef ref{(generic?"generic:":"territorial:")+id,1};document.geometries.insert(ref,geometry(feature["geometry"].toObject()));if(generic){GenericFeature f;f.id=id;f.geometry=ref;f.locked=feature["properties"].toObject()["locked"].toBool();document.genericFeatures.push_back(f);}else{TerritorialUnit u;u.id=id;u.name=id;u.locked=feature["properties"].toObject()["locked"].toBool();appendTerritory(document,u,ref);document.presentation.objectStyles[territorialRef(id)]={};}};
 std::vector<QJsonObject> featureRows,genericRows;for(const auto f:input["features"].toArray())featureRows.push_back(f.toObject());for(const auto f:input["genericFeatures"].toArray())genericRows.push_back(f.toObject());
 const auto sourceOrder=strings(input["synchronizedSourceOrder"]);
 if(!sourceOrder.empty()){
  const auto rank=[&](const std::string& key){const auto found=std::find(sourceOrder.begin(),sourceOrder.end(),key);if(found==sourceOrder.end())throw std::runtime_error("source missing from synchronized order");return found-sourceOrder.begin();};
  std::stable_sort(featureRows.begin(),featureRows.end(),[&](const auto& a,const auto& b){return rank("territorial:"+a["id"].toString().toStdString())<rank("territorial:"+b["id"].toString().toStdString());});
  std::stable_sort(genericRows.begin(),genericRows.end(),[&](const auto& a,const auto& b){return rank("generic:"+a["id"].toString().toStdString())<rank("generic:"+b["id"].toString().toStdString());});
  if(!featureRows.empty()&&!genericRows.empty()&&rank("generic:"+genericRows.front()["id"].toString().toStdString())<rank("territorial:"+featureRows.back()["id"].toString().toStdString()))throw std::runtime_error("cross-domain insertion history requires sequenced replay");
 }
 for(const auto& f:featureRows)add(f,f["kind"]=="generic");for(const auto& f:genericRows)add(f,true);
 Project project;project.replace(std::move(document));geometrysnap::Index index;index.prepare(project.snapshot());
 auto requestJson=input["payload"].toObject();if(requestJson.empty())requestJson=input["request"].toObject();if(requestJson.empty())requestJson=input;
 geometrysnap::Request request;request.coordinate=requestJson.contains("coordinate")?point(requestJson["coordinate"]):Point{};request.margin=requestJson["margin"].toDouble();request.activeOwnerIds=strings(requestJson["activeOwnerIds"]);request.sourceKey=requestJson["sourceKey"].toString().toStdString();request.sourceRevision=input["sourceRevision"].toInteger();auto source=requestJson["source"].toObject();if(source.empty())source=requestJson["sourceGeometry"].toObject();if(source.empty())source=input["sourceGeometry"].toObject();if(!source.empty())request.sourceGeometry=std::make_shared<Geometry>(geometry(source));
 auto batch=index.collect(request);QJsonArray candidates;for(const auto& c:batch.candidates)candidates.append(json(c));
 QJsonObject output{{"case",input["case"]},{"queryIndex",input["queryIndex"]},{"scope","native-app-snap-helper-only"},{"candidates",candidates},{"diagnostics",QJsonObject{{"nearbyObjects",qint64(batch.diagnostics.nearbyObjects)},{"visitedSegments",qint64(batch.diagnostics.visitedSegments)},{"segmentEntriesExamined",qint64(batch.diagnostics.segmentEntriesExamined)},{"intersectionTests",qint64(batch.diagnostics.intersectionTests)},{"geometryIndexBuilds",qint64(batch.diagnostics.geometryIndexBuilds)}}}};
 auto resolve=input["resolve"].toObject();if(resolve.empty()&&input.contains("screenPoint"))resolve=input;if(!resolve.empty()){
  if(input.contains("resolve")&&resolve["candidates"].isArray()){batch.candidates.clear();for(const auto c:resolve["candidates"].toArray())batch.candidates.push_back(candidate(c.toObject()));}
  const auto projection=resolve["projection"].toObject();const Point scale=projection["scale"].isArray()?point(projection["scale"]):Point{1,1},translate=projection["translate"].isArray()?point(projection["translate"]):Point{};
  std::map<std::pair<double,double>,std::optional<Point>> observedProjection;
  for(const auto row:input["projectedPoints"].toArray()){const auto object=row.toObject();const auto geographic=point(object["coordinate"]);observedProjection[{geographic.x,geographic.y}]=object["screen"].isNull()?std::nullopt:std::optional<Point>{point(object["screen"])};}
  const bool browserObserved=input.contains("projectedPoints");if(browserObserved)output["projection"]="browser-observed";
  const geometrysnap::ProjectPoint projectPoint=[scale,translate,observedProjection,browserObserved](Point p)->std::optional<Point>{if(browserObserved){const auto found=observedProjection.find({p.x,p.y});if(found==observedProjection.end())throw std::runtime_error("candidate projection was not observed in browser");return found->second;}return Point{p.x*scale.x+translate.x,p.y*scale.y+translate.y};};
  const auto resolved=geometrysnap::resolveSnap(resolve.contains("coordinate")?point(resolve["coordinate"]):request.coordinate,point(resolve["screenPoint"]),batch.candidates,projectPoint,resolve["pointerType"].toString("mouse").toStdString(),resolve["excludeNodeKey"].toString().toStdString());output["result"]=json(resolved);
  if(resolved){QJsonObject indicator{{"kind",QString::fromStdString(resolved->candidate.kind)},{"coordinate",json(resolved->coordinate)},{"ownerIds",json(resolved->candidate.ownerIds)},{"nodeKey",resolved->candidate.nodeKey.empty()?QJsonValue::Null:QJsonValue{QString::fromStdString(resolved->candidate.nodeKey)}},{"segmentKey",resolved->candidate.segmentKey.empty()?QJsonValue::Null:QJsonValue{QString::fromStdString(resolved->candidate.segmentKey)}},{"segmentEndpoints",resolved->segmentEndpoints?QJsonValue{QJsonArray{json((*resolved->segmentEndpoints)[0]),json((*resolved->segmentEndpoints)[1])}}:QJsonValue::Null}};output["indicator"]=indicator;}else output["indicator"]=QJsonValue::Null;
 }
 return output;
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{QFile input;if(argc>1)input.setFileName(QString::fromLocal8Bit(argv[1]));else input.open(stdin,QIODevice::ReadOnly);if(argc>1&&!input.open(QIODevice::ReadOnly))throw std::runtime_error("input open failed");QJsonParseError error;const auto parsed=QJsonDocument::fromJson(input.readAll(),&error);if(error.error!=QJsonParseError::NoError||!parsed.isObject())throw std::runtime_error("invalid input JSON");const auto bytes=QJsonDocument(run(parsed.object())).toJson(QJsonDocument::Compact);std::fwrite(bytes.data(),1,bytes.size(),stdout);std::putchar('\n');return 0;}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}

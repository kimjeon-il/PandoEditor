#include "m974_snap_fixture.h"
#include "geometrysnapprovider.h"
#include "commandjobrunner.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QThread>
#include <cstdio>
#include <map>
using namespace pandoeditor;
using namespace m974snapfixture;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
QJsonArray candidates(const std::vector<geometrysnap::Candidate>& values){QJsonArray result;for(const auto& value:values)result.append(json(value));return result;}
ProjectDocument document(const QJsonObject& input){
 ProjectDocument result;result.documentId="m974-provider";
 for(const auto value:input["features"].toArray()){const auto f=value.toObject(),p=f["properties"].toObject();TerritorialUnit u;u.id=f["id"].toString().toStdString();u.name=p["name"].toString(QString::fromStdString(u.id)).toStdString();u.locked=p["locked"].toBool();const GeometryRef ref{"territorial:"+u.id,1};result.geometries.insert(ref,geometry(f["geometry"].toObject()));appendTerritory(result,u,ref,p["parentId"].toString().toStdString());result.presentation.objectStyles[territorialRef(u.id)]={};}
 for(const auto value:input["genericFeatures"].toArray()){const auto f=value.toObject(),p=f["properties"].toObject();GenericFeature generic;generic.id=f["id"].toString().toStdString();generic.name=p["name"].toString().toStdString();generic.locked=p["locked"].toBool();generic.geometry={"generic:"+generic.id,1};result.geometries.insert(generic.geometry,geometry(f["geometry"].toObject()));result.genericFeatures.push_back(generic);}
 return result;
}
void command(Project& project,CommandAction action){CommandArguments args;args.action=std::move(action);std::string territorialCommand; if(const auto* mutation=std::get_if<ApplyTerritorialMutation>(&args.action)){const auto kind=mutation->plan.kind;territorialCommand=(kind==TerritorialMutationKind::CreateCountry||kind==TerritorialMutationKind::CreateSubunit||kind==TerritorialMutationKind::CreateRegion)?"territorial.create":(kind==TerritorialMutationKind::DeleteCountry||kind==TerritorialMutationKind::DeleteUnits)?"territorial.delete":"territorial.geometry.commit";}const bool content=std::holds_alternative<ContentEdit>(args.action);auto prepared=CommandProcessor::prepare(project,CommandProcessor::makeRequest(project,!territorialCommand.empty()?territorialCommand:content?"content.edit":"territorial.field",std::move(args)));if(prepared.status==CommandStatus::NoOp)return;if(!prepared.preview)throw std::runtime_error("fixture command: "+prepared.detail);const auto result=CommandProcessor::confirm(project,*prepared.preview);if(!result.ok())throw std::runtime_error("fixture commit: "+result.detail);}
void territorial(Project& project,TerritorialMutationIntent intent,std::optional<GeometryPatch> patch={}){const auto plan=CommandProcessor::planTerritorial(project,std::move(intent));if(!plan.plan)throw std::runtime_error("fixture planning: "+plan.detail);command(project,ApplyTerritorialMutation{*plan.plan,std::move(patch)});}
void sync(Project& project,const QJsonObject& previous,const QJsonObject& next){
 const auto features=[](const QJsonObject& value,const char* field){std::map<std::string,QJsonObject> rows;for(const auto item:value[field].toArray()){const auto row=item.toObject();rows[row["id"].toString().toStdString()]=row;}return rows;};
 const auto before=features(previous,"features"),after=features(next,"features");
 for(const auto& old:before)if(!after.count(old.first))territorial(project,DeleteTerritorialIntent{{territorialRef(old.first)}});
 // Iterate source arrays, retaining their append order rather than ID sorting.
 for(const auto item:next["features"].toArray()){const auto f=item.toObject(),p=f["properties"].toObject();const auto id=f["id"].toString().toStdString();const auto old=before.find(id);
  if(old==before.end()){CreateTerritorialIntent intent;intent.kind=UnitKind::General;intent.id=id;intent.name=p["name"].toString(QString::fromStdString(id)).toStdString();intent.geometry=geometry(f["geometry"].toObject());intent.coverageMode="explicit";const auto parent=p["parentId"].toString();if(!parent.isEmpty())intent.parent=territorialRef(parent.toStdString());territorial(project,intent);}
  else{if(old->second["geometry"]!=f["geometry"]){GeometryPatch patch;patch.sourceRevision=project.revision();patch.replacements.push_back({territorialRef(id),geometry(f["geometry"].toObject())});territorial(project,ReplaceGeometryIntent{territorialRef(id)},patch);}if(old->second["properties"].toObject()["name"]!=p["name"]){CommandArguments args;args.action=TerritorialFieldEdit{territorialRef(id),TerritorialField::Name,p["name"].toString().toStdString()};auto prepared=CommandProcessor::prepare(project,CommandProcessor::makeRequest(project,"territorial.field",args));if(!prepared.preview)throw std::runtime_error("fixture metadata command: "+prepared.detail);require(CommandProcessor::confirm(project,*prepared.preview).ok(),"fixture metadata commit");}}
 }
 const auto oldGeneric=features(previous,"genericFeatures"),newGeneric=features(next,"genericFeatures");
 for(const auto& old:oldGeneric)if(!newGeneric.count(old.first))command(project,ContentEdit{{"generic",old.first},{},{},false});
 for(const auto item:next["genericFeatures"].toArray()){const auto f=item.toObject(),p=f["properties"].toObject();const auto id=f["id"].toString().toStdString();const auto old=oldGeneric.find(id);if(old!=oldGeneric.end()&&old->second==f)continue;GenericFeature value;value.id=id;value.name=p["name"].toString().toStdString();value.locked=p["locked"].toBool();std::optional<std::pair<GeometryRef,Geometry>> replacement;if(old==oldGeneric.end())value.geometry={"generic:"+id,1};else value.geometry=project.document().genericFeatures.at(project.index().objects.at({"generic",id})).geometry;if(old==oldGeneric.end()||old->second["geometry"]!=f["geometry"]){if(old!=oldGeneric.end())++value.geometry.version;replacement=std::make_pair(value.geometry,geometry(f["geometry"].toObject()));}command(project,ContentEdit{{"generic",id},value,replacement,old==oldGeneric.end()});}
}
void settle(CommandJobRunner& runner,geometrysnap::Provider& provider){QElapsedTimer timer;timer.start();while(provider.status()=="pending"||runner.runningCount()||runner.queueDepth()){QCoreApplication::processEvents(QEventLoop::AllEvents,10);if(timer.elapsed()>20000)throw std::runtime_error("native provider did not settle");QThread::msleep(1);}QCoreApplication::processEvents(QEventLoop::AllEvents,10);}
QJsonObject run(const QJsonObject& input){
 const auto definition=input["definition"].toObject();require(!definition.empty(),"missing provider definition");const auto observation=input["observation"].toObject();const auto observedQueries=observation["queries"].toArray();const bool synthetic=input["synthetic"].toBool();if(!synthetic){require(!observation.empty()&&observation["id"]==definition["id"],"missing or mismatched browser observation");require(observedQueries.size()==definition["queries"].toArray().size(),"browser query rows missing");}
 Project project;project.replace(document(definition));CommandJobRunner runner([&]()->const Project&{return project;});geometrysnap::Provider provider(runner);
 QJsonObject previous=definition;std::map<std::string,std::shared_ptr<const Geometry>> sources;QJsonArray rows;std::uint64_t stateRevision=0;QJsonObject lifecycle;
 const auto definitionQueries=definition["queries"].toArray();const auto count=observedQueries.empty()?definitionQueries.size():observedQueries.size();
 for(qsizetype i=0;i<count;++i){
  const auto observed=observedQueries.empty()?QJsonObject{}:observedQueries[i].toObject();auto event=observed["input"].toObject();if(event.empty())event=definitionQueries[i].toObject();const auto mutation=event["mutation"].toObject();if(!mutation.empty())++stateRevision;
  auto snapshot=observed["sourceSnapshot"].toObject();if(snapshot.empty())snapshot=previous;
  const bool expectedError=event["expectWorkerError"].toBool();if(!expectedError){sync(project,previous,snapshot);previous=snapshot;}
  auto view=event["view"].toObject();if(view.empty())view=definition["view"].toObject();const double baseMargin=geometrysnap::marginForScale(view["scale"].toDouble(1000));
  const auto payload=observed["candidateRequest"].toObject()["message"].toObject()["payload"].toObject();geometrysnap::Request request;request.coordinate=point(event["coordinate"]);request.margin=expectedError?-1:baseMargin*2;request.activeOwnerIds=payload.contains("activeOwnerIds")?strings(payload["activeOwnerIds"]):strings(definition["activeOwnerIds"]);request.sourceKey=payload["sourceKey"].toString().toStdString();request.sourceRevision=snapshot["sourceRevision"].toInteger();const auto source=snapshot["sourceGeometry"].toObject();if(!request.sourceKey.empty()&&!source.empty()){auto& retained=sources[request.sourceKey];if(!retained)retained=std::make_shared<Geometry>(geometry(source));request.sourceGeometry=retained;}
  const auto submitted=provider.submittedCount();const auto& cold=provider.candidates(project.snapshot(),request,definition["tool"].toString(),baseMargin,stateRevision);const auto* coldAddress=&cold;const auto coldValues=candidates(cold);const auto coldStatus=provider.status();
  const auto& pending=provider.candidates(project.snapshot(),request,definition["tool"].toString(),baseMargin,stateRevision);QJsonObject row{{"queryIndex",i},{"inputEquivalent",true},{"cold",QJsonObject{{"candidates",coldValues},{"status",coldStatus}}},{"pending",QJsonObject{{"sameArray",coldAddress==&pending},{"requestCount",qint64(provider.submittedCount()-submitted)},{"candidates",candidates(pending)}}}};
  const auto scenario=definition["scenario"].toString();
  if(!scenario.isEmpty()){
   const auto before=provider.submittedCount();
   if(scenario=="superseded"){++stateRevision;request.coordinate.x+=1;request.coordinate.y+=1;provider.candidates(project.snapshot(),request,definition["tool"].toString(),baseMargin,stateRevision);}
   else if(scenario=="cancel")provider.reset();
   else if(scenario=="rebase"){provider.reset();++stateRevision;provider.candidates(project.snapshot(),request,definition["tool"].toString(),baseMargin,stateRevision);}
   else if(scenario=="source-stale"){const auto id=project.document().units.front().id;const auto revisionBefore=project.revision();GeometryPatch patch;patch.sourceRevision=project.revision();patch.replacements.push_back({territorialRef(id),*project.document().geometries.get(staticGeometryBinding(project.document(),id).geometryRef)});territorial(project,ReplaceGeometryIntent{territorialRef(id)},patch);if(project.revision()==revisionBefore){row["inputEquivalent"]=false;row["nativeFailureStimulus"]="actual native metadata revision change; identical web geometry reallocation is a native canonical NoOp";require(project.renameCountry(id,"Native stale revision diagnostic"),"native stale diagnostic mutation failed");}}
   else if(scenario=="project-replacement"){project.replace(document(definition));provider.reset();}
   settle(runner,provider);row["ready"]=QJsonObject{{"observed",false}};lifecycle={{"inputEquivalent",row["inputEquivalent"]},{"scenario",scenario},{"requestsAdded",qint64(provider.submittedCount()-before)},{"settledStatus",provider.status()}};
  }else{
   settle(runner,provider);
   if(expectedError){row["ready"]=QJsonObject{{"observed",false}};row["inputEquivalent"]=false;row["nativeFailureStimulus"]="invalid worker margin; canonical Project rejects the web malformed-ring fixture";}
   else{const auto& ready=provider.candidates(project.snapshot(),request,definition["tool"].toString(),baseMargin,stateRevision);row["ready"]=QJsonObject{{"candidates",candidates(ready)},{"status",provider.status()}};require(provider.status()=="ready","ready input unexpectedly missed cache");}
  }
  row["afterSettlement"]=QJsonObject{{"status",provider.status()},{"submitted",qint64(provider.submittedCount())}};rows.append(row);
 }
 QJsonArray failures,excluded;int compared=0;
 if(!synthetic){
  for(qsizetype i=0;i<rows.size();++i){const auto native=rows[i].toObject(),web=observedQueries[i].toObject();
   if(!native["inputEquivalent"].toBool()){excluded.append(QJsonObject{{"queryIndex",i},{"reason",native["nativeFailureStimulus"]}});continue;}++compared;
   const auto compare=[&](const char* section,const char* field){const auto a=native[section].toObject(),b=web[section].toObject();if(!a.contains(field)||!b.contains(field)||a[field]!=b[field])failures.append(QString::number(i)+":"+section+"."+field);};
   compare("cold","candidates");for(const auto field:{"sameArray","requestCount","candidates"})compare("pending",field);
   if(web["ready"].toObject()["observed"]==false)compare("ready","observed");else compare("ready","candidates");
  }
  if(!lifecycle.empty()&&lifecycle["inputEquivalent"].toBool()){
   const auto requests=observation["requests"].toArray();if(requests.empty())failures.append("lifecycle:missing browser requests");else{const auto expected=requests.last().toObject()["status"]=="resolved"?QString("ready"):QString("empty");if(lifecycle["settledStatus"]!=expected)failures.append("lifecycle:settledStatus");}
   if(!observation["lifecycle"].toObject().contains("requestsAdded")||lifecycle["requestsAdded"]!=observation["lifecycle"].toObject()["requestsAdded"])failures.append("lifecycle:requestsAdded");
  }
 }
 return {{"case",definition["id"]},{"scope","native-provider-only"},{"rawParity",false},{"queries",rows},{"lifecycle",lifecycle},{"comparison",QJsonObject{{"equivalentRowsPassed",failures.empty()},{"allRowsProven",!synthetic&&failures.empty()&&excluded.empty()},{"expectedQueries",observedQueries.size()},{"comparedQueries",compared},{"excludedRows",excluded},{"failures",failures}}}};
}
void selfTest(){
 QJsonObject g{{"type","Polygon"},{"coordinates",QJsonArray{QJsonArray{QJsonArray{0,0},QJsonArray{0,2},QJsonArray{2,2},QJsonArray{2,0},QJsonArray{0,0}}}}};QJsonObject feature{{"id","a"},{"geometry",g},{"properties",QJsonObject{{"name","a"}}}};
 QJsonObject definition{{"id","native-provider-self-test"},{"features",QJsonArray{feature}},{"genericFeatures",QJsonArray{}},{"tool","draw"},{"activeOwnerIds",QJsonArray{"a"}},{"view",QJsonObject{{"scale",1000}}},{"queries",QJsonArray{QJsonObject{{"coordinate",QJsonArray{.1,.1}}},QJsonObject{{"coordinate",QJsonArray{.11,.11}}},QJsonObject{{"coordinate",QJsonArray{30,30}}}}}};
 const auto result=run({{"definition",definition},{"synthetic",true}});const auto rows=result["queries"].toArray();const auto first=rows[0].toObject(),second=rows[1].toObject(),third=rows[2].toObject();require(first["cold"].toObject()["candidates"].toArray().empty(),"provider cold must return empty");require(first["pending"].toObject()["requestCount"]==1,"pending lookup resubmitted");require(!first["ready"].toObject()["candidates"].toArray().empty(),"ready candidates missing");require(second["pending"].toObject()["requestCount"]==0,"same cell failed to reuse cache");require(third["cold"].toObject()["candidates"].toArray().empty(),"new cell reused old candidates");require(third["ready"].toObject()["candidates"].toArray().empty(),"distant ready candidates unexpected");
 Project errorProject;errorProject.replace(document(definition));CommandJobRunner errorRunner([&]()->const Project&{return errorProject;});geometrysnap::Provider errorProvider(errorRunner);geometrysnap::Request request;request.coordinate={.1,.1};request.margin=-1;errorProvider.candidates(errorProject.snapshot(),request,"draw",.1,1);request.margin=.2;errorProvider.candidates(errorProject.snapshot(),request,"draw",.1,2);settle(errorRunner,errorProvider);require(errorProvider.status()=="ready"&&!errorProvider.candidates(errorProject.snapshot(),request,"draw",.1,2).empty(),"stale worker failure cleared newer ready candidates");

}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{if(argc>1&&QString::fromLocal8Bit(argv[1])=="--self-test"){selfTest();std::puts("PASS real provider cold/pending/ready/cell lifecycle");return 0;}QFile input;if(argc>1)input.setFileName(QString::fromLocal8Bit(argv[1]));else input.open(stdin,QIODevice::ReadOnly);if(argc>1&&!input.open(QIODevice::ReadOnly))throw std::runtime_error("provider input open failed");QJsonParseError error;const auto parsed=QJsonDocument::fromJson(input.readAll(),&error);if(error.error!=QJsonParseError::NoError||!parsed.isObject())throw std::runtime_error("invalid provider JSON");const auto result=run(parsed.object());const auto bytes=QJsonDocument(result).toJson(QJsonDocument::Compact);std::fwrite(bytes.data(),1,bytes.size(),stdout);std::putchar('\n');return result["comparison"].toObject()["equivalentRowsPassed"].toBool()?0:2;}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}

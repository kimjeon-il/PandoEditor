#include "editorcontroller.h"
#include "projectcodec.h"
#include "webimport.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QRegularExpression>
#include <QSet>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <cstdio>
#include <stdexcept>

using namespace pandoeditor;
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
QString sha256(const QByteArray& bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QByteArray readFile(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "File read failed");
    return file.readAll();
}
void writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "File write failed");
}

void protocol(bool ok,const QString& message) {
    if(!ok) throw std::runtime_error(("PROTOCOL: "+message).toStdString());
}
void fields(const QJsonObject& object,const QSet<QString>& allowed) {
    for(auto it=object.begin();it!=object.end();++it) protocol(allowed.contains(it.key()),"unknown field "+it.key());
}
bool finite(const QJsonValue& value) { return value.isDouble()&&std::isfinite(value.toDouble()); }
bool nonempty(const QJsonValue& value) { return value.isString()&&!value.toString().isEmpty(); }
void vector(const QJsonValue& value,int size) {
    protocol(value.isArray()&&value.toArray().size()==size,"invalid vector size");
    for(const auto number:value.toArray()) protocol(finite(number),"invalid vector number");
}
void validateView(const QJsonObject& value) {
    fields(value,{"kind","scale","translate","rotate","center","size","snapDistance","coarsePointer"});
    protocol(value["kind"]=="flat"||value["kind"]=="globe","invalid projection kind");
    protocol(finite(value["scale"])&&value["scale"].toDouble()>0,"invalid view scale");
    vector(value["translate"],2);vector(value["rotate"],3);vector(value["center"],2);
    protocol(value["size"].isObject(),"missing viewport size");const auto size=value["size"].toObject();fields(size,{"width","height"});
    for(const auto* key:{"width","height"})protocol(finite(size[key])&&size[key].toDouble()>0,"invalid viewport extent");
    if(value.contains("coarsePointer"))protocol(value["coarsePointer"].isBool()&&!value["coarsePointer"].toBool(),"unsupported pointer mode");
    if(value.contains("snapDistance")) {
        protocol(value["snapDistance"].isObject(),"invalid snap policy");const auto snap=value["snapDistance"].toObject();fields(snap,{"mouse","touch"});
        protocol(snap["mouse"]==10&&snap["touch"]==18,"unsupported snap policy");
    }
}
void validateRequest(const QJsonObject& value,const QByteArray& input) {
    fields(value,{"schema","version","id","mode","format","inputSha256","expectedStages","actions","view","expectWebExport","expectedError","expectedCommitError"});
    protocol(value["schema"]=="pando-m975-native-exchange-request"&&value["version"]==1,"schema/version mismatch");
    protocol(nonempty(value["id"]),"missing case id");
    const auto mode=value["mode"].toString();protocol(QSet<QString>{"codec","timeline","edit","reject"}.contains(mode),"unknown mode");
    protocol(value["format"]=="web"||value["format"]=="native","unknown input format");
    protocol(value["inputSha256"].isString()&&value["inputSha256"]==sha256(input),"input SHA256 mismatch");
    protocol(!input.isEmpty(),"empty input bytes");
    if(value.contains("expectWebExport"))protocol(value["expectWebExport"].isBool()&&mode=="edit","invalid export expectation");
    if(value.contains("expectedError"))protocol(nonempty(value["expectedError"])&&mode=="reject","invalid error expectation");
    if(value.contains("expectedCommitError"))protocol(nonempty(value["expectedCommitError"])&&mode=="edit"&&QRegularExpression("^[A-Z][A-Z0-9_]*$").match(value["expectedCommitError"].toString()).hasMatch(),"invalid commit error expectation");
    protocol(value["expectedStages"].isArray()&&!value["expectedStages"].toArray().empty(),"empty stage inventory");
    QSet<QString> names;const QRegularExpression safe("^[a-z][a-z0-9-]*$");
    for(const auto stage:value["expectedStages"].toArray()) {
        protocol(stage.isString()&&safe.match(stage.toString()).hasMatch()&&!names.contains(stage.toString()),"unsafe or duplicate stage");names.insert(stage.toString());
    }
    protocol(value["actions"].isArray(),"missing action list");const auto actions=value["actions"].toArray();
    if(mode!="edit") {
        protocol(actions.empty(),"non-edit mode contains actions");
        const auto wanted=mode=="codec"?QJsonArray{"storage"}:mode=="timeline"?QJsonArray{"storage","activation-rejected"}:QJsonArray{"rejected"};
        protocol(value["expectedStages"]==wanted,"wrong automatic stage inventory");return;
    }
    protocol(value["view"].isObject(),"missing explicit edit view");validateView(value["view"].toObject());
    protocol(!actions.empty(),"empty edit actions");QJsonArray observedStages;bool expectedCommitRejection=false;
    for(const auto item:actions) {
        protocol(item.isObject(),"nonobject action");const auto action=item.toObject();const auto op=action["op"].toString();
        QSet<QString> allowed{"op","stage","expectAccepted"};
        if(op=="select")allowed.unite({"id","domain","additive"});
        else if(op=="begin")allowed.insert("tool");
        else if(op=="provider")allowed.insert("id");
        else if(op=="method")allowed.insert("method");
        else if(op=="draft")allowed.insert("points");
        else if(op=="candidate")allowed.unite({"index","indices"});
        else if(op=="component")allowed.unite({"key","all"});
        else if(op=="move-node")allowed.unite({"nodeKey","coordinate"});
        else protocol(QSet<QString>{"observe","advance","finish","archive","back","cancel","confirm","undo","redo"}.contains(op),"unknown action "+op);
        fields(action,allowed);
        if(action.contains("expectAccepted"))protocol(action["expectAccepted"].isBool(),"untyped action expectation");
        if(op=="confirm"&&action.contains("expectAccepted")&&!action["expectAccepted"].toBool())expectedCommitRejection=true;
        if(action.contains("stage")) {protocol(nonempty(action["stage"]),"empty stage name");observedStages.append(action["stage"]);}
        if(op=="select"||op=="provider")protocol(nonempty(action["id"]),"missing object id");
        if(op=="select") {
            if(action.contains("domain"))protocol(QSet<QString>{"territorial","generic","label","hydro","distributionLayer","distributionEntry"}.contains(action["domain"].toString()),"unsupported selection domain");
            if(action.contains("additive"))protocol(action["additive"].isBool(),"invalid selection mode");
        }
        if(op=="begin")protocol(QSet<QString>{"annex","split","shared-boundary","delete"}.contains(action["tool"].toString()),"unknown edit tool");
        if(op=="method")protocol(QSet<QString>{"line","polygon","components"}.contains(action["method"].toString()),"unknown territory method");
        if(op=="draft") {
            protocol(action["points"].isArray()&&!action["points"].toArray().empty(),"empty draft");
            for(const auto point:action["points"].toArray())vector(point,2);
        }
        if(op=="candidate") {
            protocol(action.contains("index")!=action.contains("indices"),"ambiguous candidate selection");
            QJsonArray indices;if(action.contains("index"))indices.append(action["index"]);else{protocol(action["indices"].isArray(),"invalid candidate indices");indices=action["indices"].toArray();}
            QSet<int> seen;for(const auto index:indices){protocol(finite(index)&&index.toDouble()>=0&&index.toDouble()==index.toInt(-1)&&!seen.contains(index.toInt()),"invalid candidate index");seen.insert(index.toInt());}
        }
        if(op=="component") {protocol(action.contains("key")!=action.contains("all"),"ambiguous component selection");if(action.contains("key"))protocol(nonempty(action["key"]),"missing component key");else protocol(action["all"].isBool()&&action["all"].toBool(),"invalid all-components selection");}
        if(op=="move-node") {protocol(nonempty(action["nodeKey"]),"missing boundary node key");vector(action["coordinate"],2);}
    }
    protocol(observedStages==value["expectedStages"],"action/stage inventory mismatch");
    protocol(observedStages.first()=="before","edit stages must start before");
    protocol(actions.first().toObject()["op"]=="observe"&&actions.first().toObject()["stage"]=="before","before must precede all edits");
    protocol(expectedCommitRejection==value.contains("expectedCommitError"),"failed confirm requires a specific expected commit error");
}
struct ExactScreenInput { Point naive, chosen; int xSteps=0, ySteps=0; };
std::optional<ExactScreenInput> exactScreenInput(const MapProjection& projection,Point target) {
    if(!std::isfinite(target.x)||!std::isfinite(target.y))return {};
    ExactScreenInput result;result.naive=projection.project(target);result.chosen=result.naive;
    if(!std::isfinite(result.naive.x)||!std::isfinite(result.naive.y))return {};
    const auto solve=[&](bool x,double base,double wanted,double& chosen,int& steps) {
        const auto inverse=[&](double v){const auto p=x?projection.unproject(v,result.naive.y):projection.unproject(result.naive.x,v);return x?p.x:p.y;};
        if(inverse(base)==wanted)return true;double below=base,above=base;
        for(int radius=1;radius<=4;++radius){below=std::nextafter(below,-std::numeric_limits<double>::infinity());above=std::nextafter(above,std::numeric_limits<double>::infinity());for(const auto candidate:{std::pair<double,int>{below,-radius},{above,radius}})if(inverse(candidate.first)==wanted){chosen=candidate.first;steps=candidate.second;return true;}}
        return false;
    };
    if(!solve(true,result.naive.x,target.x,result.chosen.x,result.xSteps)||!solve(false,result.naive.y,target.y,result.chosen.y,result.ySteps))return {};
    const auto inverse=projection.unproject(result.chosen.x,result.chosen.y);if(inverse.x!=target.x||inverse.y!=target.y)return {};return result;
}
QJsonObject blob(const QByteArray& bytes,bool raw=false) {
    QJsonObject value{{"base64",QString::fromLatin1(bytes.toBase64())},{"sha256",sha256(bytes)},{"size",qint64(bytes.size())}};
    if(raw){const auto text=QString::fromUtf8(bytes);require(text.toUtf8()==bytes,"Output is not exact UTF8");value["raw"]=text;}return value;
}
void settle(EditorController& controller) {
    QElapsedTimer timer;timer.start();
    do {QCoreApplication::processEvents(QEventLoop::AllEvents,10);if(!controller.geometryEditState().value("calculating").toBool()&&!controller.structureState().value("calculating").toBool()&&!controller.webImportBusy())return;QThread::msleep(2);}while(timer.elapsed()<30000);
    throw std::runtime_error("Controller operation did not settle");
}
QJsonObject history(const EditorController& controller) {return {{"canUndo",controller.canUndo()},{"canRedo",controller.canRedo()}};}
void configureView(EditorController& controller,const QJsonObject& view) {
    const auto translate=view["translate"].toArray(),rotate=view["rotate"].toArray(),center=view["center"].toArray();const auto size=view["size"].toObject();
    const QVariantMap values{{"scale",view["scale"].toDouble()},{"translateX",translate[0].toDouble()},{"translateY",translate[1].toDouble()},
        {"rotationLongitude",rotate[0].toDouble()},{"rotationLatitude",rotate[1].toDouble()},{"rotationRoll",rotate[2].toDouble()},
        {"centerLongitude",center[0].toDouble()},{"centerLatitude",center[1].toDouble()},{"viewportWidth",size["width"].toDouble()},{"viewportHeight",size["height"].toDouble()}};
    require(controller.setProjectionMode(view["kind"].toString())&&controller.publishMapView(values),"Public supplied view rejected");
    const auto observed=controller.mapViewState();require(observed.value("projection").toString()==view["kind"].toString(),"Supplied projection differs from public view");
    for(auto it=values.begin();it!=values.end();++it)require(observed.value(it.key()).toDouble()==it.value().toDouble(),"Supplied numeric view differs from public view");
}
QJsonObject codecSave(const QByteArray& bytes,const QString& path) {
    writeFile(path,bytes);const auto saved=readFile(path);Project reopened;reopened.replace(projectcodec::decode(saved));
    require(saved==bytes&&projectcodec::encode(reopened)==bytes,"Native QFile codec roundtrip changed bytes");
    auto result=blob(saved);result["saveMethod"]="QFile-production-codec";result["codecFileRoundtrip"]=true;result["reopenEqual"]=true;return result;
}
QJsonObject appSave(EditorController& controller,const QString& path,const QString& directory) {
    const auto before=controller.documentBytes();const auto historyBefore=history(controller);const bool active=controller.geometryEditState().value("active").toBool();
    const QByteArray sentinel("m975-existing-native-destination\n");const auto attempt=active?path+".blocked":path;
    if(active)writeFile(attempt,sentinel);
    const bool saved=controller.saveFile(QUrl::fromLocalFile(attempt));
    QJsonObject result;
    if(active) {
        require(!saved&&readFile(attempt)==sentinel,"Active geometry save was not atomically refused");result=codecSave(before,path);
        result["saveMethod"]="QFile-production-codec-after-controller-save-refusal";result["activeSaveRefused"]=true;result["destinationUnchanged"]=true;
    } else {
        require(saved,"Actual EditorController saveFile failed");const auto bytes=readFile(path);require(bytes==before,"Actual save changed canonical bytes");
        EditorController reopened({false,QDir(directory).filePath("reopen-private.json")});require(reopened.openFile(QUrl::fromLocalFile(path)),"Actual EditorController saved-file reopen failed");
        require(reopened.documentBytes()==before,"Actual EditorController reopen changed canonical bytes");result=blob(bytes);result["saveMethod"]="EditorController.saveFile/openFile";result["reopenEqual"]=true;
    }
    require(controller.documentBytes()==before&&history(controller)==historyBefore,"Stage save altered canonical state or history");
    result["saveAccepted"]=saved;result["canonicalUnchanged"]=true;result["historyUnchanged"]=true;return result;
}
QJsonObject webOutput(const QByteArray& bytes,const QString& path,bool expected,const QString& expectedError={}) {
    Project project;project.replace(projectcodec::decode(bytes));
    if(expected) {const auto output=projectcodec::encodeWeb(project.snapshot());writeFile(path,output);require(readFile(path)==output,"Web QFile write changed bytes");return blob(output,true);}
    const QByteArray sentinel("m975-existing-web-destination\n");const auto fresh=path+".new";require(!QFileInfo::exists(fresh),"Fresh rejection destination already exists");writeFile(path,sentinel);QString error;
    for(const auto& destination:{path,fresh}) {
        bool refused=false;try{const auto output=projectcodec::encodeWeb(project.snapshot());writeFile(destination,output);}catch(const std::exception& failure){refused=true;error=QString::fromUtf8(failure.what());}
        require(refused,"Unsupported web export was accepted");
    }
    require(readFile(path)==sentinel&&!QFileInfo::exists(fresh),"Rejected export published or overwrote a destination");
    require(projectcodec::encode(project)==bytes,"Rejected export changed native canonical bytes");
    if(!expectedError.isEmpty())require(error.contains(expectedError),"Unexpected export rejection error");
    return {{"exportRejected",true},{"accepted",false},{"refusalPhase","export"},{"error",error},{"destinationUnchanged",true},{"sentinelUnchanged",true},{"newDestinationAbsent",true}};
}
QJsonObject snapshot(EditorController& controller,const QString& stage,const QString& directory,bool exportExpected) {
    const auto bytes=controller.documentBytes();const auto prefix=QDir(directory).filePath(stage);
    QJsonObject result{{"observed",true},{"document",blob(bytes)},{"state",QJsonObject::fromVariantMap(controller.geometryEditState())},
        {"structure",QJsonObject::fromVariantMap(controller.structureState())},{"content",QJsonObject::fromVariantMap(controller.contentEditState())},
        {"history",history(controller)},{"mapViewState",QJsonObject::fromVariantMap(controller.mapViewState())},{"controllerProjection",QJsonObject::fromVariantMap(controller.hydroProjection())},{"primaryObject",QJsonObject::fromVariantMap(controller.primaryObject())}};
    result["nativeFile"]=appSave(controller,prefix+".native.json",directory);
    result[exportExpected?"webFile":"webRefusal"]=webOutput(bytes,prefix+".web.json",exportExpected);return result;
}
QJsonObject run(const QByteArray& input,const QJsonObject& request,const QString& directory,const QString& suppliedInputPath={}) {
    validateRequest(request,input);require(QDir().mkpath(directory),"Output directory unavailable");const QDir out(directory);
    const auto mode=request["mode"].toString();const bool web=request["format"]=="web";
    const QString inputPath=suppliedInputPath.isEmpty()?out.filePath(web?"input.web.json":"input.native.json"):suppliedInputPath;if(suppliedInputPath.isEmpty())writeFile(inputPath,input);require(readFile(inputPath)==input,"Input QFile bytes changed");
    QJsonObject result{{"schema","pando-m975-native-exchange-result"},{"version",1},{"id",request["id"]},{"mode",mode},{"input",blob(input)},
        {"runtime",QJsonObject{{"compiledQt",QT_VERSION_STR},{"qt",qVersion()}}},{"documentIdByteEqualityClaimed",false},
        {"normalizations",QJsonArray{"web-import-generates-native-documentId"}}};
    QJsonObject stages;QJsonArray order,actions,inputObservations;
    const auto record=[&](const QString& name,const QJsonObject& value){require(!stages.contains(name),"Duplicate observed stage");order.append(name);stages[name]=value;};
    if(mode=="reject"&&web) {
        EditorController controller({false,out.filePath("private.json")});const auto before=controller.documentBytes();const auto historyBefore=history(controller);
        require(controller.prepareWebImport(QUrl::fromLocalFile(inputPath)),"Import refusal dispatch failed");settle(controller);
        const auto error=controller.webImportError();require(!controller.hasWebImportPreview()&&!error.isEmpty(),"Invalid web import was accepted");
        if(request.contains("expectedError"))require(error.contains(request["expectedError"].toString()),"Unexpected import rejection error");
        require(controller.documentBytes()==before&&history(controller)==historyBefore,"Rejected import changed canonical/history state");
        record("rejected",{{"observed",true},{"accepted",false},{"refusalPhase","import"},{"error",error},{"importOnly",true},{"publicationAttempted",false},{"projectOutputPublished",false},{"canonicalUnchanged",true},{"historyUnchanged",true}});
    } else if(mode=="codec"||mode=="timeline") {
        Project project;project.replace(web?webimport::prepare(readFile(inputPath)).document:projectcodec::decode(readFile(inputPath)));const auto bytes=projectcodec::encode(project);
        QJsonObject storage{{"observed",true},{"document",blob(bytes)},{"nativeFile",codecSave(bytes,out.filePath("storage.native.json"))},{"webFile",webOutput(bytes,out.filePath("storage.web.json"),true)}};record("storage",storage);
        if(mode=="timeline") {
            require(!isStaticTimeline(project.document()),"Timeline rejection case is actually static");EditorController controller({false,out.filePath("private.json")});const auto before=controller.documentBytes();const auto historyBefore=history(controller);QString error;
            QObject::connect(&controller,&EditorController::errorOccurred,&controller,[&](const QString& value){error=value;});const bool accepted=controller.openFile(QUrl::fromLocalFile(out.filePath("storage.native.json")));
            require(!accepted&&!error.isEmpty(),"Rich timeline unexpectedly activated");require(controller.documentBytes()==before&&history(controller)==historyBefore,"Rejected activation changed controller");
            record("activation-rejected",{{"observed",true},{"accepted",false},{"activationAccepted",false},{"error",error},{"canonicalUnchanged",true},{"historyUnchanged",true}});
        }
    } else {
        EditorController controller({false,out.filePath("private.json")});QJsonArray errors;
        QObject::connect(&controller,&EditorController::errorOccurred,&controller,[&](const QString& error){errors.append(error);});
        if(web) {require(controller.prepareWebImport(QUrl::fromLocalFile(inputPath)),"Actual web import dispatch failed");settle(controller);require(controller.hasWebImportPreview(),("Web input has no import preview: "+controller.webImportError()).toStdString().c_str());require(controller.confirmWebImport(controller.webImportHash(),"discard"),"Actual web import confirmation failed");}
        else require(controller.openFile(QUrl::fromLocalFile(inputPath)),"Actual native input open failed");
        const auto before=controller.documentBytes();
        if(mode=="reject") {
            auto rejected=snapshot(controller,"rejected",directory,false);const auto refusal=rejected["webRefusal"].toObject();for(auto it=refusal.begin();it!=refusal.end();++it)rejected[it.key()]=it.value();
            if(request.contains("expectedError"))require(rejected["error"].toString().contains(request["expectedError"].toString()),"Unexpected rejection error");record("rejected",rejected);
        } else {
            configureView(controller,request["view"].toObject());const bool exportExpected=!request.contains("expectWebExport")||request["expectWebExport"].toBool();QByteArray confirmedBytes;
            for(const auto value:request["actions"].toArray()) {
                const auto action=value.toObject();const auto op=action["op"].toString();const auto prior=controller.documentBytes();const auto historyBefore=history(controller);const auto errorStart=errors.size();bool dispatch=true,accepted=true;
                if(op=="select") {const auto domain=action.contains("domain")?action["domain"].toString():QString("territorial");const QVariantMap ref{{"domain",domain},{"id",action["id"].toString()}};if(action["additive"].toBool()){auto refs=controller.selectionItems();refs.append(ref);dispatch=controller.setSelection(refs,ref);}else dispatch=controller.selectObject(ref,"replace");}
                else if(op=="begin") {
                    const auto tool=action["tool"].toString();
                    if(tool=="annex")dispatch=controller.beginAnnexGeometry();else if(tool=="split")dispatch=controller.beginSplitGeometry();else if(tool=="shared-boundary")dispatch=controller.beginSharedBoundaryGeometry();
                    else {const auto domain=controller.primaryObject().value("domain").toString();if(domain=="territorial")dispatch=controller.beginDeleteSelection();else dispatch=controller.beginContentEdit(domain)&&controller.previewContentEdit(true);}
                } else if(op=="provider")dispatch=controller.geometryToggleProvider({{"domain","territorial"},{"id",action["id"].toString()}});
                else if(op=="advance")dispatch=controller.geometryAdvanceStage();
                else if(op=="method")dispatch=controller.geometrySelectTerritoryMethod(action["method"].toString());
                else if(op=="draft"||op=="move-node") {
                    MapProjection projection;projection.rebuild(projectcodec::decode(controller.documentBytes()));
                    require(projection.hydroParameters()==controller.hydroProjection(),"Exact-input projection differs from actual public controller projection");
                    if(op=="move-node") {
                        QVariantMap handle;for(const auto& path:controller.geometryDraftPaths())for(const auto& node:path.toMap().value("vertices").toList())if(node.toMap().value("nodeKey").toString()==action["nodeKey"].toString())handle=node.toMap();
                        require(!handle.empty(),"Requested public boundary node was not observed");require(controller.geometrySelectNearest(handle.value("x").toDouble(),handle.value("y").toDouble(),.001)&&controller.geometryBeginVertexDrag(),"Actual boundary drag did not begin");
                    }
                    const auto points=op=="draft"?action["points"].toArray():QJsonArray{action["coordinate"]};
                    for(const auto coordinate:points) {
                        const auto xy=coordinate.toArray();const Point target{xy[0].toDouble(),xy[1].toDouble()};const auto screen=exactScreenInput(projection,target);
                        if(!screen){const auto naive=projection.project(target),inverse=projection.unproject(naive.x,naive.y);const QJsonObject evidence{{"op",op},{"intended",coordinate},{"naive",QJsonArray{naive.x,naive.y}},{"inverse",QJsonArray{inverse.x,inverse.y}},{"projection",QJsonObject::fromVariantMap(projection.hydroParameters())},{"controllerProjection",QJsonObject::fromVariantMap(controller.hydroProjection())},{"maximumUlpRadius",4}};throw std::runtime_error(("No exact public projection preimage within four ULPs: "+QJsonDocument(evidence).toJson(QJsonDocument::Compact)).toStdString());}const auto inverse=projection.unproject(screen->chosen.x,screen->chosen.y);
                        inputObservations.append(QJsonObject{{"op",op},{"intended",coordinate},{"naive",QJsonArray{screen->naive.x,screen->naive.y}},{"chosen",QJsonArray{screen->chosen.x,screen->chosen.y}},{"inverse",QJsonArray{inverse.x,inverse.y}},{"xUlpSteps",screen->xSteps},{"yUlpSteps",screen->ySteps},{"maximumUlpRadius",4},{"exact",inverse.x==target.x&&inverse.y==target.y},{"controllerProjection",QJsonObject::fromVariantMap(controller.hydroProjection())},{"view",QJsonObject::fromVariantMap(controller.mapViewState())}});
                        const bool moved=op=="draft"?controller.geometryAddPoint(screen->chosen.x,screen->chosen.y,0):controller.geometryMoveSelectedVertex(screen->chosen.x,screen->chosen.y,0,QString());dispatch=moved&&dispatch;
                    }
                    if(op=="move-node")controller.geometryEndVertexDrag(false);
                    else if(controller.geometryEditState().value("activeMethod").toString()=="line")require(QJsonObject::fromVariantMap(controller.riverSelectionObservation())["inputLine"]==action["points"],"Observed draft differs from exact input line");
                } else if(op=="finish")dispatch=controller.geometryFinishTerritoryDraft();
                else if(op=="candidate") {
                    const auto candidates=controller.geometryEditState().value("candidates").toList();require(!candidates.empty(),"No observed candidates");
                    if(action.contains("index")) {const auto index=action["index"].toInt();require(index<candidates.size(),"Candidate index not observed");dispatch=controller.geometryToggleTerritoryCandidate(candidates[index].toMap().value("id").toString());}
                    else {QSet<int> selected;for(const auto index:action["indices"].toArray()){require(index.toInt()<candidates.size(),"Candidate index not observed");selected.insert(index.toInt());}for(int i=0;i<candidates.size();++i){const auto candidate=candidates[i].toMap();if(candidate.value("selected").toBool()!=selected.contains(i)){dispatch=controller.geometryToggleTerritoryCandidate(candidate.value("id").toString())&&dispatch;settle(controller);}}}
                } else if(op=="component") {
                    if(action.contains("key"))dispatch=controller.geometryToggleTerritoryComponent(action["key"].toString());
                    else {const auto components=controller.geometryEditState().value("components").toList();require(!components.empty(),"No observed components");for(const auto& value:components){const auto component=value.toMap();if(!component.value("selected").toBool()){dispatch=controller.geometryToggleTerritoryComponent(component.value("key").toString())&&dispatch;settle(controller);}}}
                } else if(op=="archive")dispatch=controller.geometryAddTerritoryPart();
                else if(op=="back")dispatch=controller.geometryBack();
                else if(op=="cancel") {controller.cancelGeometryEdit();controller.cancelStructureMutation();controller.cancelContentEdit();}
                else if(op=="confirm") {
                    if(controller.geometryEditState().value("active").toBool()){dispatch=controller.confirmGeometryEdit();if(controller.geometryEditState().value("boundaryImpactConfirmation").toBool())dispatch=controller.geometryConfirmBoundaryImpacts();}
                    else if(controller.structureDialogOpen())dispatch=controller.confirmStructureMutation();else dispatch=controller.confirmContentEdit();
                } else if(op=="undo") {dispatch=controller.canUndo();if(dispatch)controller.undo();}
                else if(op=="redo") {dispatch=controller.canRedo();if(dispatch)controller.redo();}
                settle(controller);accepted=dispatch;
                QJsonArray actionErrors;for(auto i=errorStart;i<errors.size();++i)actionErrors.append(errors[i]);
                if(op=="confirm")accepted=dispatch&&!controller.geometryEditState().value("active").toBool()&&!controller.structureDialogOpen()&&!controller.contentEditState().value("active").toBool()&&controller.documentBytes()!=prior;
                const bool expected=!action.contains("expectAccepted")||action["expectAccepted"].toBool();
                if(accepted!=expected)throw std::runtime_error(("Action "+op+" acceptance differs from request; errors="+QString::fromUtf8(QJsonDocument(errors).toJson(QJsonDocument::Compact))).toStdString());
                if(!accepted)require(controller.documentBytes()==prior&&history(controller)==historyBefore,"Rejected action changed canonical bytes/history");
                if(op=="confirm"&&!accepted) {
                    const QRegularExpression token("(?:^|\\W)"+QRegularExpression::escape(request["expectedCommitError"].toString())+"(?:\\W|$)");bool observed=false;
                    for(const auto error:actionErrors)observed=observed||(error.toString().contains("VALIDATION_FAILED:")&&token.match(error.toString()).hasMatch());
                    require(observed,"Expected commit error was not observed on current failed confirm");
                }
                if(op=="cancel")require(controller.documentBytes()==before&&!controller.geometryEditState().value("active").toBool(),"Cancel left committed content or active edit");
                if(op=="confirm"&&accepted){require(controller.canUndo()&&!controller.canRedo(),"Confirmed edit history invalid");confirmedBytes=controller.documentBytes();require(confirmedBytes!=before,"Successful confirm did not change document");}
                if(op=="undo"&&accepted)require(controller.documentBytes()!=prior&&!controller.canUndo()&&controller.canRedo(),"Undo did not change canonical bytes/history");
                if(op=="redo"&&accepted)require(!confirmedBytes.isEmpty()&&controller.documentBytes()==confirmedBytes&&controller.canUndo()&&!controller.canRedo(),"Redo did not restore committed canonical bytes/history");
                QJsonObject observation{{"op",op},{"accepted",accepted},{"dispatchAccepted",dispatch},{"errors",actionErrors}};if(action.contains("stage"))observation["stage"]=action["stage"];actions.append(observation);
                if(action.contains("stage")) {const auto name=action["stage"].toString();auto stage=snapshot(controller,name,directory,exportExpected);stage["accepted"]=accepted;stage["errors"]=actionErrors;if(name=="preview")require(controller.documentBytes()==before&&controller.geometryEditState().value("active").toBool()&&controller.geometryEditState().value("previewReady").toBool(),"Preview is not an actual uncommitted ready preview");record(name,stage);}
            }
            controller.cancelGeometryEdit();controller.cancelStructureMutation();controller.cancelContentEdit();
        }
    }
    const auto inputAfter=readFile(inputPath);require(inputAfter==input,"Input source file changed after operations");result["inputAfter"]=blob(inputAfter);result["sourcePreserved"]=true;
    if(mode=="reject"&&web){auto rejection=stages["rejected"].toObject();rejection["sourcePreserved"]=true;stages["rejected"]=rejection;}
    require(order==request["expectedStages"]&&stages.size()==order.size(),"Observed stage inventory differs from request");result["stageOrder"]=order;result["stages"]=stages;result["actions"]=actions;result["inputObservations"]=inputObservations;
    writeFile(out.filePath("result.json"),QJsonDocument(result).toJson(QJsonDocument::Compact));return result;
}

QJsonObject request(const QByteArray& input, const QString& mode = "codec", const QString& format = "web") {
    QJsonArray stages;
    if (mode == "codec") stages = QJsonArray{"storage"};
    else if (mode == "timeline") stages = QJsonArray{"storage", "activation-rejected"};
    else if (mode == "reject") stages = QJsonArray{"rejected"};
    return {{"schema", "pando-m975-native-exchange-request"}, {"version", 1},
        {"id", "m975-self-test"}, {"mode", mode}, {"format", format},
        {"inputSha256", sha256(input)}, {"expectedStages", stages}, {"actions", QJsonArray{}}};
}
QJsonObject view() {
    return {{"kind", "flat"}, {"scale", 100}, {"translate", QJsonArray{400, 200}},
        {"rotate", QJsonArray{0, 0, 0}}, {"center", QJsonArray{0, 0}},
        {"size", QJsonObject{{"width", 800}, {"height", 400}}}};
}
QJsonObject editRequest(const QByteArray& input) {
    auto value = request(input, "edit", "native");
    value["view"] = view();
    value["expectedStages"] = QJsonArray{"before", "confirm", "undo", "redo"};
    value["actions"] = QJsonArray{
        QJsonObject{{"op", "observe"}, {"stage", "before"}},
        QJsonObject{{"op", "select"}, {"domain", "generic"}, {"id", "22000000-0000-4000-8000-000000000002"}},
        QJsonObject{{"op", "begin"}, {"tool", "delete"}},
        QJsonObject{{"op", "confirm"}, {"stage", "confirm"}},
        QJsonObject{{"op", "undo"}, {"stage", "undo"}},
        QJsonObject{{"op", "redo"}, {"stage", "redo"}}};
    return value;
}
void expectProtocolRejection(const QJsonObject& value, const QByteArray& input) {
    bool refused = false;
    try { validateRequest(value, input); }
    catch (const std::exception& error) {
        require(QString::fromUtf8(error.what()).startsWith("PROTOCOL:"), "Protocol rejection has no specific reason");
        refused = true;
    }
    require(refused, "Invalid protocol was accepted");
}
QByteArray verifiedBlob(const QJsonObject& value) {
    require(value["base64"].isString() && value["sha256"].isString() && value["size"].isDouble(), "Missing raw-byte observation");
    const auto bytes = QByteArray::fromBase64(value["base64"].toString().toLatin1());
    require(QString::fromLatin1(bytes.toBase64()) == value["base64"].toString(), "Noncanonical base64 observation");
    require(sha256(bytes) == value["sha256"] && bytes.size() == value["size"].toInteger(), "Observed byte identity mismatch");
    if (value.contains("raw")) require(value["raw"].toString().toUtf8() == bytes, "Web raw string differs from bytes");
    return bytes;
}
void verifyResult(const QJsonObject& result, const QJsonObject& expected, const QByteArray& input) {
    require(result["schema"] == "pando-m975-native-exchange-result" && result["version"] == 1, "Missing production exchange result");
    const auto runtime=result["runtime"].toObject();
    require(runtime["compiledQt"]==QString::fromLatin1(QT_VERSION_STR)&&runtime["qt"]==QString::fromLatin1(qVersion()),"Probe Qt identity differs from its actual runtime");
    require(runtime["compiledQt"]=="6.8.3"&&runtime["qt"]=="6.8.3","Probe requires approved compiled/runtime Qt 6.8.3");
    require(result["id"] == expected["id"] && result["mode"] == expected["mode"], "Exchange result identity mismatch");
    require(verifiedBlob(result["input"].toObject()) == input, "Input file bytes were respelled");
    require(result["sourcePreserved"].toBool()&&verifiedBlob(result["inputAfter"].toObject())==input,"Input source was not reread unchanged after operations");
    require(result["stageOrder"] == expected["expectedStages"], "Exchange stage inventory mismatch");
    require(result["stages"].isObject() && result["stages"].toObject().size() == expected["expectedStages"].toArray().size(), "Extra or missing stage");
    const auto stages = result["stages"].toObject();
    for (const auto name : result["stageOrder"].toArray()) {
        const auto stage = stages[name.toString()].toObject();
        require(stage["observed"].toBool(), "Unobserved exchange stage");
        if (!stage.contains("document")) continue;
        const auto canonical = verifiedBlob(stage["document"].toObject());
        const auto nativeFile=stage["nativeFile"].toObject();
        const auto saved = verifiedBlob(nativeFile);
        require(canonical == saved, "Native save differs from canonical bytes");
        require(nativeFile["reopenEqual"].toBool(), "Native save was not reopened losslessly");
        if(expected["mode"]=="codec"||(expected["mode"]=="timeline"&&name=="storage")) {
            require(nativeFile["saveMethod"]=="QFile-production-codec"&&nativeFile["codecFileRoundtrip"].toBool(),"Codec stage did not identify its QFile-only seam");
            require(!nativeFile["saveAccepted"].toBool(),"Codec-only stage incorrectly claims application save");
        } else if(expected["mode"]=="edit") {
            const auto state=stage["state"].toObject();
            require(state["active"].isBool(),"Edit stage omits observed tool activity");
            const auto projection=stage["controllerProjection"].toObject();
            for(const auto* key:{"cosLatitude","minX","maxLatitude"})require(finite(projection[key]),"Edit stage omits actual public projection");
            if(state["active"].toBool()) {
                require(nativeFile["saveMethod"]=="QFile-production-codec-after-controller-save-refusal","Active edit skipped actual application save refusal");
                require(nativeFile["saveAccepted"].isBool()&&!nativeFile["saveAccepted"].toBool()&&nativeFile["activeSaveRefused"].toBool(),"Active edit save refusal was not observed");
                require(nativeFile["destinationUnchanged"].toBool()&&nativeFile["codecFileRoundtrip"].toBool(),"Active edit save modified destination or skipped separate canonical roundtrip");
            } else {
                require(nativeFile["saveMethod"]=="EditorController.saveFile/openFile"&&nativeFile["saveAccepted"].toBool(),"Inactive edit stage did not use actual application save/open");
                require(!nativeFile["activeSaveRefused"].toBool()&&!nativeFile["codecFileRoundtrip"].toBool(),"Inactive edit stage silently used codec fallback");
            }
            require(nativeFile["canonicalUnchanged"].toBool()&&nativeFile["historyUnchanged"].toBool(),"Stage persistence altered canonical state/history");
        }
        if (stage.contains("webFile")) {
            const auto exported = verifiedBlob(stage["webFile"].toObject());
            require(!exported.isEmpty(), "Web export is empty");
            Project reopened;
            reopened.replace(webimport::prepare(exported).document);
            require(!projectcodec::encode(reopened).isEmpty(), "Web output cannot reimport");
        }
    }
}

int selfTest() {
    const auto content = readFile(QStringLiteral(M975_EXCHANGE_FIXTURES "/../lineage-v10-content/content.json"));
    const auto complex = readFile(QStringLiteral(M975_EXCHANGE_FIXTURES "/complex.json"));
    Project source;
    source.replace(webimport::prepare(content).document);
    const auto native = projectcodec::encode(source);
    QJsonArray passed, failed;
    const auto test = [&](const QString& name, const std::function<void()>& body) {
        try { body(); passed.append(name); }
        catch (const std::exception& error) { failed.append(QJsonObject{{"name", name}, {"error", QString::fromUtf8(error.what())}}); }
    };
    const auto invalid = [&](const QString& name, const std::function<void(QJsonObject&)>& mutate) {
        test(name, [&] { auto value = editRequest(native); mutate(value); expectProtocolRejection(value, native); });
    };
    test("approved-qt-runtime", [&] {
        require(QString::fromLatin1(QT_VERSION_STR)=="6.8.3"&&QString::fromLatin1(qVersion())=="6.8.3","Compiled and runtime Qt must both be approved 6.8.3");
    });
    test("valid-protocol", [&] { validateRequest(request(content), content); validateRequest(editRequest(native), native); });
    invalid("schema-required", [](auto& v) { v.remove("schema"); });
    invalid("wrong-version", [](auto& v) { v["version"] = 2; });
    invalid("empty-id", [](auto& v) { v["id"] = ""; });
    invalid("unknown-mode", [](auto& v) { v["mode"] = "assume-success"; });
    invalid("unknown-format", [](auto& v) { v["format"] = "legacy"; });
    invalid("wrong-input-hash", [](auto& v) { v["inputSha256"] = QString(64, '0'); });
    invalid("unknown-request-field", [](auto& v) { v["ignoreFailures"] = true; });
    invalid("empty-edit-actions", [](auto& v) { v["actions"] = QJsonArray{}; });
    invalid("nonobject-action", [](auto& v) { v["actions"] = QJsonArray{false}; });
    invalid("unknown-action", [](auto& v) { v["actions"] = QJsonArray{QJsonObject{{"op", "mutate-private-state"}, {"stage", "before"}}}; });
    invalid("untyped-expectation", [](auto& v) { auto a=v["actions"].toArray(); auto x=a[0].toObject(); x["expectAccepted"]="false";a[0]=x;v["actions"]=a; });
    invalid("empty-stage-inventory", [](auto& v) { v["expectedStages"] = QJsonArray{}; });
    invalid("missing-observed-stage", [](auto& v) { v["expectedStages"] = QJsonArray{"before", "confirm"}; });
    invalid("duplicate-stage", [](auto& v) { auto a=v["actions"].toArray();a.append(QJsonObject{{"op","observe"},{"stage","before"}});v["actions"]=a; });
    invalid("unsafe-stage-path", [](auto& v) { v["expectedStages"]=QJsonArray{"../before"};v["actions"]=QJsonArray{QJsonObject{{"op","observe"},{"stage","../before"}}}; });
    invalid("missing-view", [](auto& v) { v.remove("view"); });
    invalid("malformed-view-vector", [](auto& v) { auto x=v["view"].toObject();x["rotate"]=QJsonArray{0,0};v["view"]=x; });
    invalid("unknown-action-field", [](auto& v) { auto a=v["actions"].toArray();auto x=a[0].toObject();x["roundGeometry"]=true;a[0]=x;v["actions"]=a; });
    invalid("missing-select-id", [](auto& v) { auto a=v["actions"].toArray();auto x=a[1].toObject();x.remove("id");a[1]=x;v["actions"]=a; });
    invalid("unknown-edit-tool", [](auto& v) { auto a=v["actions"].toArray();auto x=a[2].toObject();x["tool"]="private-delete";a[2]=x;v["actions"]=a; });
    invalid("malformed-draft-coordinate", [](auto& v) { auto a=v["actions"].toArray();a.insert(2,QJsonObject{{"op","draft"},{"points",QJsonArray{QJsonArray{0,"1"}}}});v["actions"]=a; });
    invalid("invalid-candidate-index", [](auto& v) { auto a=v["actions"].toArray();a.insert(2,QJsonObject{{"op","candidate"},{"index",-1}});v["actions"]=a; });
    test("invalid-import-error-expectation", [&] {
        auto value=request(content,"reject");value["expectedError"]=17;expectProtocolRejection(value,content);
    });
    test("exact-screen-zero-control", [&] {
        MapProjection projection;projection.setWorldExtent();const auto screen=exactScreenInput(projection,{180,0});
        require(screen.has_value(),"Exact control input has no observed preimage");
        const auto inverse=projection.unproject(screen->chosen.x,screen->chosen.y);
        require(inverse.x==180&&inverse.y==0,"Exact-screen input changed coordinate");
        require(std::abs(screen->xSteps)<=4&&std::abs(screen->ySteps)<=4,"Exact-screen search exceeded four ULPs");
    });
    test("exact-screen-no-preimage-fails", [&] {
        MapProjection projection;projection.setWorldExtent();
        require(!exactScreenInput(projection,{0.1,0}),"Missing preimage was fabricated");
    });
    test("exact-screen-nonfinite-fails", [&] {
        MapProjection projection;projection.setWorldExtent();
        require(!exactScreenInput(projection,{std::numeric_limits<double>::infinity(),0}),"Infinity input accepted");
        require(!exactScreenInput(projection,{0,std::numeric_limits<double>::quiet_NaN()}),"NaN input accepted");
    });
    test("web-codec-file-roundtrip", [&] {
        QTemporaryDir directory; const auto expected=request(content); const auto result=run(content,expected,directory.path());
        verifyResult(result,expected,content);
        require(result["normalizations"].toArray().contains("web-import-generates-native-documentId"), "documentId normalization is undeclared");
        require(!result["documentIdByteEqualityClaimed"].toBool(), "Web reimport documentId must not be claimed byte-equal");
    });
    test("native-codec-file-roundtrip", [&] {
        QTemporaryDir directory; const auto expected=request(native,"codec","native"); const auto result=run(native,expected,directory.path());
        verifyResult(result,expected,native);
        require(verifiedBlob(result["stages"].toObject()["storage"].toObject()["document"].toObject())==native,"Native codec changed bytes");
    });
    test("generic-delete-save-history", [&] {
        QTemporaryDir directory; const auto expected=editRequest(native); const auto result=run(native,expected,directory.path());
        verifyResult(result,expected,native);
        const auto stages=result["stages"].toObject();
        const auto bytes=[&](const char* name){return verifiedBlob(stages[name].toObject()["document"].toObject());};
        require(bytes("before")==bytes("undo"),"Undo did not restore complete native bytes");
        require(bytes("confirm")==bytes("redo"),"Redo did not restore complete native bytes");
        require(bytes("before")!=bytes("confirm"),"Delete did not edit native bytes");
        require(projectcodec::decode(bytes("confirm")).genericFeatures.empty(),"Generic delete did not remove object");
        require(stages["undo"].toObject()["history"].toObject()["canRedo"].toBool(),"Snapshot save erased redo history");
    });
    test("rich-timeline-storage-only", [&] {
        QTemporaryDir directory; const auto expected=request(complex,"timeline"); const auto result=run(complex,expected,directory.path());
        verifyResult(result,expected,complex);
        const auto rejected=result["stages"].toObject()["activation-rejected"].toObject();
        require(rejected["activationAccepted"].isBool()&&!rejected["activationAccepted"].toBool(),"Rich timeline activation was accepted");
        require(rejected["canonicalUnchanged"].toBool()&&rejected["historyUnchanged"].toBool(),"Rejected activation changed controller");
    });
    test("raw-delta-import-refusal", [&] {
        const QByteArray input="{\n  \"format\": \"pandolab-autosave-delta\",\n  \"schemaVersion\": 10,\n  \"changedEntities\": [],\n  \"removedEntityIds\": [\"donor\"]\n}\n";
        auto expected=request(input,"reject");expected["expectedError"]="BASE_DATA_REQUIRED";
        validateRequest(expected,input);
        QTemporaryDir directory;const auto result=run(input,expected,directory.path());verifyResult(result,expected,input);
        const auto rejected=result["stages"].toObject()["rejected"].toObject();
        require(rejected["accepted"].isBool()&&!rejected["accepted"].toBool(),"Raw delta import was accepted");
        require(rejected["refusalPhase"]=="import"&&rejected["error"].toString().contains("BASE_DATA_REQUIRED"),"Raw delta rejection lost its production error");
        require(rejected["importOnly"].toBool()&&rejected["publicationAttempted"].isBool()&&!rejected["publicationAttempted"].toBool()&&rejected["projectOutputPublished"].isBool()&&!rejected["projectOutputPublished"].toBool(),"Import refusal misstates its nonpublication seam");
        require(rejected["sourcePreserved"].toBool(),"Rejected delta source bytes were not reread");
        require(!rejected.contains("sentinelUnchanged")&&!rejected.contains("destinationUnchanged"),"Import-only refusal claims an unexercised publication destination");
        require(rejected["canonicalUnchanged"].toBool()&&rejected["historyUnchanged"].toBool(),"Rejected delta changed canonical state/history");
        require(!rejected.contains("webFile"),"Rejected delta produced a web file");
    });
    const auto rejectedAnnexRequest=[&](const QByteArray& bytes) {
        auto value=request(bytes,"edit");value["view"]=view();value["expectedCommitError"]="DANGLING_REF";value["expectedStages"]=QJsonArray{"before","rejected"};
        value["actions"]=QJsonArray{QJsonObject{{"op","observe"},{"stage","before"}},QJsonObject{{"op","select"},{"id","target"}},QJsonObject{{"op","begin"},{"tool","annex"}},QJsonObject{{"op","provider"},{"id","donor"}},QJsonObject{{"op","advance"}},QJsonObject{{"op","method"},{"method","components"}},QJsonObject{{"op","component"},{"all",true}},QJsonObject{{"op","advance"}},QJsonObject{{"op","confirm"},{"expectAccepted",false},{"stage","rejected"}}};return value;
    };
    test("commit-refusal-current-error", [&] {
        const auto bytes=readFile(QStringLiteral(M975_EXCHANGE_FIXTURES "/../lineage-v10-content/annex-rejected-deleted-reference.web10.json"));const auto expected=rejectedAnnexRequest(bytes);
        QTemporaryDir directory;const auto result=run(bytes,expected,directory.path());verifyResult(result,expected,bytes);
        const auto errors=result["stages"].toObject()["rejected"].toObject()["errors"].toArray();
        require(!errors.empty(),"Expected commit rejection lacks current action error");bool token=false;
        for(const auto error:errors){require(!error.toString().contains("GEOMETRY_EDIT_ACTIVE"),"Checkpoint save-refusal noise contaminated commit errors");token=token||error.toString().contains("DANGLING_REF");}
        require(token,"Expected DANGLING_REF was not observed");require(result["actions"].toArray().last().toObject()["errors"].toArray()==errors,"Stage refusal is not bound to current failed action");
    });
    test("unrelated-false-confirm-is-not-dangling-ref", [&] {
        auto expected=editRequest(native);expected["expectedCommitError"]="DANGLING_REF";expected["expectedStages"]=QJsonArray{"before","rejected"};expected["actions"]=QJsonArray{QJsonObject{{"op","observe"},{"stage","before"}},QJsonObject{{"op","confirm"},{"expectAccepted",false},{"stage","rejected"}}};
        validateRequest(expected,native);QTemporaryDir directory;bool rejected=false;try{(void)run(native,expected,directory.path());}catch(const std::exception& error){rejected=QString::fromUtf8(error.what()).contains("Expected commit error was not observed");}
        require(rejected,"Unrelated disabled confirm satisfied an intended dangling-ref rejection");
    });
    test("untyped-commit-error-expectation", [&] {auto expected=editRequest(native);expected["expectedCommitError"]=false;expectProtocolRejection(expected,native);});
    test("input-source-rewrite-detected", [&] {
        QTemporaryDir directory;QObject timerOwner;const auto path=directory.filePath("input.native.json");bool changed=false;
        QTimer::singleShot(0,&timerOwner,[&]{writeFile(path,"tampered source\n");changed=true;});bool rejected=false;
        try{(void)run(native,editRequest(native),directory.path());}catch(const std::exception& error){rejected=QString::fromUtf8(error.what()).contains("Input source file changed after operations");}
        require(changed&&rejected,"Actual source QFile rewrite was not detected");
    });
    test("lossless-metadata-edit-save-history", [&] {
        auto document=source.document();document.units.front().metadata="{\"integer\":9007199254740993,\"decimal\":0.123456789012345678901}";
        Project lossless;lossless.replace(document);const auto input=projectcodec::encode(lossless);
        QTemporaryDir directory;auto expected=editRequest(input);expected["expectWebExport"]=false;
        const auto result=run(input,expected,directory.path());verifyResult(result,expected,input);
        const auto stages=result["stages"].toObject();
        for(const auto name:expected["expectedStages"].toArray()) {
            const auto stage=stages[name.toString()].toObject();const auto bytes=verifiedBlob(stage["document"].toObject());
            require(bytes.contains("9007199254740993")&&bytes.contains("0.123456789012345678901"),"Edit/history lost native metadata tokens");
            require(stage["webRefusal"].toObject()["destinationUnchanged"].toBool()&&stage["webRefusal"].toObject()["newDestinationAbsent"].toBool(),"Unsupported stage export was published");
        }
        require(stages["before"].toObject()["document"]==stages["undo"].toObject()["document"],"Lossless Undo snapshot changed");
        require(stages["confirm"].toObject()["document"]==stages["redo"].toObject()["document"],"Lossless Redo snapshot changed");
    });
    for(const auto token : {"9007199254740993","1e400","0.123456789012345678901"}) test(QString("lossless-refusal-")+token,[&] {
        auto document=source.document();document.units.front().metadata=std::string("{\"number\":")+token+"}";
        Project lossless;lossless.replace(document);const auto input=projectcodec::encode(lossless);
        QTemporaryDir directory;const auto expected=request(input,"reject","native");const auto result=run(input,expected,directory.path());
        verifyResult(result,expected,input);
        const auto rejected=result["stages"].toObject()["rejected"].toObject();
        require(rejected["exportRejected"].toBool()&&rejected["destinationUnchanged"].toBool()&&rejected["newDestinationAbsent"].toBool(),"Unsupported export was not atomically refused");
        require(verifiedBlob(rejected["document"].toObject())==input,"Refusal changed lossless metadata");
        require(readFile(directory.filePath("rejected.native.json")).contains(token),"Native file lost original number token");
    });
    const auto output=QJsonDocument(QJsonObject{{"schema","pando-m975-native-exchange-self-test"},{"passed",passed},{"failed",failed},{"skipped",0}}).toJson(QJsonDocument::Compact);
    std::fwrite(output.constData(),1,output.size(),stdout);
    return failed.empty()?0:1;
}
}
int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    try {
        if(argc==2&&QString::fromLocal8Bit(argv[1])=="--self-test") return selfTest();
        require(argc==4,"Usage: m975_model_exchange_probe <input.json> <request.json> <output-dir>");
        const auto input=readFile(QString::fromLocal8Bit(argv[1]));
        QJsonParseError error;const auto parsed=QJsonDocument::fromJson(readFile(QString::fromLocal8Bit(argv[2])),&error);
        require(error.error==QJsonParseError::NoError&&parsed.isObject(),"PROTOCOL: invalid request JSON");
        const auto result=run(input,parsed.object(),QString::fromLocal8Bit(argv[3]),QString::fromLocal8Bit(argv[1]));
        const auto bytes=QJsonDocument(result).toJson(QJsonDocument::Compact);
        std::fwrite(bytes.constData(),1,bytes.size(),stdout);return 0;
    } catch(const std::exception& error) { std::fprintf(stderr,"%s\n",error.what());return 1; }
}

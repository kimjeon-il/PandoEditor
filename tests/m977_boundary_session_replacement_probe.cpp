// Reuse fixture decoding, exact public projection and raw controller observation.
// No helper topology is substituted for the actual replacement controller paths.
#define main m974_original_boundary_probe_main
#include "m974_boundary_probe.cpp"
#undef main

namespace {
QJsonObject replacementFixture(const QJsonObject& definition) {
    QJsonArray features;
    for(const auto& value:definition["features"].toArray()) {
        const auto f=value.toObject();features.append(QJsonObject{{"id",f["id"]},{"geometry",QJsonObject{{"type","Polygon"},{"coordinates",QJsonArray{f["ring"]}}}},
            {"properties",QJsonObject{{"name",f["id"]},{"entityKind","general"},{"coverageMode","explicit"},{"parentId",""},{"validFrom",QJsonValue::Null},{"validTo",QJsonValue::Null},{"style",QJsonObject{}}}}});
    }
    return {{"features",features}};
}
QJsonObject runReplacement(const QJsonObject& definition) {
    QJsonObject output{{"case",definition["id"]},{"input",definition}},stages;QJsonArray events,stageOrder;
    try {
        QTemporaryDir directory;require(directory.isValid(),"Temporary directory unavailable");Project source;source.replace(fixture(replacementFixture(definition)));
        MapProjection projection;projection.rebuild(source.document());QFile file(directory.filePath("input.json"));require(file.open(QIODevice::WriteOnly),"Fixture unavailable");file.write(projectcodec::encode(source));file.close();
        EditorControllerConfig config;config.bootstrapWorld=false;config.autosaveEnabled=false;config.privateProjectPath=directory.filePath("private.json");EditorController controller(config);
        require(controller.openFile(QUrl::fromLocalFile(file.fileName())),"Native fixture failed to open");
        const auto select=[&](const QJsonArray& ids,const QString& primary){QVariantList refs;for(const auto& id:ids)refs.append(QVariantMap{{"domain","territorial"},{"id",id.toString()}});return controller.setSelection(refs,{{"domain","territorial"},{"id",primary}},"map");};
        require(select(definition["selectedIds"].toArray(),definition["seedId"].toString()),"Original public selection rejected");const auto before=controller.documentBytes();QString phase="before";
        const auto event=[&](const QString& kind,QJsonObject detail=QJsonObject{}){detail["sequence"]=events.size();detail["phase"]=phase;detail["kind"]=kind;events.append(detail);};
        QObject::connect(&controller,&EditorController::geometryEditChanged,&controller,[&]{event("geometry-signal",{{"edit",QJsonObject::fromVariantMap(controller.geometryEditState())}});});
        const auto record=[&](const QString& name){phase=name;auto snapshot=observe(controller,before,{});snapshot["eventSequence"]=events.size();stages[name]=snapshot;stageOrder.append(name);};
        const auto action=[&](const QString& name,QJsonObject detail=QJsonObject{}){phase=name;detail["action"]=name;event("public-action",detail);};
        record("before");action("enter-original");require(controller.beginSharedBoundaryGeometry(),"Original public boundary entry rejected");record("entered");
        const auto preview=definition["phase"]=="preview";
        if(preview) {
            settle(controller);require(controller.geometryEditState().value("boundaryStatus").toString()=="ready","Original preparation not READY");record("prepared");
            QVariantMap node;for(const auto& path:controller.geometryDraftPaths())for(const auto& vertex:path.toMap().value("vertices").toList())if(vertex.toMap().value("nodeKey").toString()==definition["move"].toObject()["nodeKey"].toString())node=vertex.toMap();
            require(!node.empty()&&!node.value("fixed").toBool(),"Original movable junction unavailable");
            const auto coordinate=definition["move"].toObject()["coordinate"].toArray();action("begin-move",{{"nodeKey",definition["move"].toObject()["nodeKey"]},{"coordinate",coordinate}});
            require(controller.geometrySelectNearest(node.value("x").toDouble(),node.value("y").toDouble(),.001),"Public original handle selection rejected");require(controller.geometryBeginVertexDrag(),"Public original drag rejected");
            const auto screen=exactScreen(projection,{coordinate[0].toDouble(),coordinate[1].toDouble()});require(bool(screen),"No exact public projection preimage");
            const auto inverse=projection.unproject(screen->chosen.x,screen->chosen.y);
            output["moveInput"]=QJsonObject{{"intended",coordinate},{"chosen",QJsonArray{screen->chosen.x,screen->chosen.y}},{"inverse",QJsonArray{inverse.x,inverse.y}},{"maximumUlpRadius",4},{"exact",inverse.x==coordinate[0].toDouble()&&inverse.y==coordinate[1].toDouble()}};
            require(controller.geometryMoveSelectedVertex(screen->chosen.x,screen->chosen.y,0,QString()),"Public original move rejected");record("drag");phase="commit-old";controller.geometryEndVertexDrag(false);
        }else output["moveInput"]=QJsonValue::Null;
        require(controller.geometryEditState().value("calculating").toBool(),"Old real job is not pending at completion barrier");
        const auto eventCount=events.size();
        // Wait for actual CPU completion without processing any owner events.
        // This is a completed-result boundary, never a CPU-in-flight interval.
        require(QThreadPool::globalInstance()->waitForDone(30000),"Old actual pool job failed to finish");require(events.size()==eventCount,"Owner signal was delivered during completion hold");
        require(controller.geometryEditState().value("calculating").toBool(),"Old owner completion already adopted");require(controller.documentBytes()==before,"Old worker changed canonical bytes");
        phase="old-completed";event("old-worker-completed",{{"operation",preview?"downstream-canonical-preview":"boundary-preparation"},{"ownerEventsProcessed",false},{"timeoutMs",30000}});record("oldCompleted");
        action("cancel");controller.cancelGeometryEdit();record("afterCancel");
        action("replace-selection",{{"ids",definition["replacementIds"]},{"primary",definition["replacementPrimary"]}});require(select(definition["replacementIds"].toArray(),definition["replacementPrimary"].toString()),"Replacement public selection rejected");record("replacementSelected");
        action("enter-replacement");require(controller.beginSharedBoundaryGeometry(),"Replacement public boundary entry rejected");record("replacementEntered");
        // Snapshot above happens synchronously. No old or new owner completion
        // is pumped until that distinct public session and primary are recorded.
        phase="drain-old-and-new";event("owner-completion-drain");
        require(QThreadPool::globalInstance()->waitForDone(30000),"Replacement pool failed to complete");QCoreApplication::sendPostedEvents();settle(controller);
        require(QThreadPool::globalInstance()->waitForDone(30000),"Final actual pool failed to drain");QCoreApplication::sendPostedEvents();QCoreApplication::processEvents(QEventLoop::AllEvents,10);settle(controller);
        event("owner-completion-drained",{{"poolCompleted",true},{"calculating",controller.geometryEditState().value("calculating").toBool()}});record("settled");
        output["completionBarrier"]=QJsonObject{{"mechanism","actual-old-worker-completed-owner-delivery-withheld"},{"timeoutMs",30000},{"oldPoolCompleted",true},{"ownerEventsProcessedBeforeReplacement",false},{"replacementEntryCapturedBeforeDrain",true},{"finalPoolCompleted",true},{"ownerEventsProcessedAfterReplacement",true}};
        output["limits"]=QJsonObject{{"rawParity",false},{"workerCPUExecutionPhase",false},{"boundaryMoveWorkerInterval",false},{"privateRequestIds",false},{"privateResultPayload",false},{"nativeRawPreviewGeometry",false},{"historyDepth",false},{"fullCanonicalDocumentBytes",true},{"pointerPixels",false},{"gpuRendering",false},{"projectGenerationChange",false},{"ownerLockCoverage",false}};
        QObject::disconnect(&controller,nullptr,&controller,nullptr);controller.cancelGeometryEdit();drain();
    }catch(const std::exception& error){output["error"]=QString::fromUtf8(error.what());}
    output["stages"]=stages;output["stageOrder"]=stageOrder;output["events"]=events;return output;
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
    require(argc==1,"Unexpected native replacement probe argument");QFile input;require(input.open(stdin,QIODevice::ReadOnly),"Probe stdin unavailable");QJsonParseError error;const auto payload=QJsonDocument::fromJson(input.readAll(),&error);require(error.error==QJsonParseError::NoError&&payload.isArray(),"Expected replacement case array");
    const auto cases=payload.array();require(cases.size()==2,"Exactly two additive cases required");require(cases[0].toObject()["id"]=="preparation-completed-reenter"&&cases[1].toObject()["id"]=="preview-completed-reenter","Exact ordered replacement identities required");QJsonArray rows;for(const auto& definition:cases)rows.append(runReplacement(definition.toObject()));
    const auto bytes=QJsonDocument(QJsonObject{{"schema","pando-m977-boundary-session-replacement-native"},{"version",1},{"rows",rows}}).toJson(QJsonDocument::Compact);std::fwrite(bytes.constData(),1,bytes.size(),stdout);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}

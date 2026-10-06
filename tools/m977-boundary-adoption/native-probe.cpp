// Reuse only the established fixture/projection/public-observation helpers.
// This translation unit supplies its own entry point and independent scenarios.
#define main m974_original_boundary_probe_main
#include "../../tests/m974_boundary_probe.cpp"
#undef main
#include <QSemaphore>
#include <QtConcurrent/QtConcurrentRun>

namespace {
struct QueuedWorkerBarrier {
    QSemaphore entered, release;
    int previous = QThreadPool::globalInstance()->maxThreadCount();
    QFuture<void> blocker;
    bool active = true;
    QueuedWorkerBarrier() {
        require(QThreadPool::globalInstance()->waitForDone(30000), "Prior actual pool work did not settle");
        QThreadPool::globalInstance()->setMaxThreadCount(1);
        blocker = QtConcurrent::run([this] { entered.release(); release.acquire(); });
        require(entered.tryAcquire(1,30000), "Pool blocker did not start");
    }
    void unblock() { if(active){active=false;release.release();blocker.waitForFinished();} }
    ~QueuedWorkerBarrier(){unblock();QThreadPool::globalInstance()->waitForDone(30000);QThreadPool::globalInstance()->setMaxThreadCount(previous);}
};
QJsonObject inputFixture(const QJsonObject& definition) {
    QJsonArray features;
    for(const auto& value:definition["features"].toArray()){
        const auto f=value.toObject();features.append(QJsonObject{{"id",f["id"]},{"geometry",QJsonObject{{"type","Polygon"},{"coordinates",QJsonArray{f["ring"]}}}},
            {"properties",QJsonObject{{"name",f["id"]},{"entityKind","general"},{"coverageMode","explicit"},{"parentId",""},{"validFrom",QJsonValue::Null},{"validTo",QJsonValue::Null},{"style",QJsonObject{}}}}});
    }
    return {{"features",features}};
}
QJsonObject runTiming(const QJsonObject& definition) {
    QJsonObject output{{"case",definition["id"]},{"input",definition}},stages;
    try {
        QTemporaryDir directory;require(directory.isValid(),"Temporary directory unavailable");Project source;source.replace(fixture(inputFixture(definition)));
        MapProjection projection;projection.rebuild(source.document());QFile file(directory.filePath("input.json"));require(file.open(QIODevice::WriteOnly),"Input file unavailable");file.write(projectcodec::encode(source));file.close();
        EditorControllerConfig config;config.bootstrapWorld=false;config.autosaveEnabled=false;config.privateProjectPath=directory.filePath("private.json");EditorController controller(config);
        require(controller.openFile(QUrl::fromLocalFile(file.fileName())),"Native fixture failed to open");
        QVariantList selected;for(const auto& id:definition["selectedIds"].toArray())selected.append(QVariantMap{{"domain","territorial"},{"id",id.toString()}});
        const QVariantMap primary{{"domain","territorial"},{"id",definition["seedId"].toString()}};require(controller.setSelection(selected,primary,"map"),"Native selection failed");
        const QByteArray before=controller.documentBytes();
        const auto record=[&](const QString& name,const QJsonObject& outcome=QJsonObject{}){stages[name]=observe(controller,before,outcome);};
        const auto phase=definition["phase"].toString(),action=definition["action"].toString();
        std::unique_ptr<QueuedWorkerBarrier> barrier;
        record("before");if(phase=="prepare-queued")barrier=std::make_unique<QueuedWorkerBarrier>();
        require(controller.beginSharedBoundaryGeometry(),"Actual native boundary entry failed");record("entered");
        if(phase=="prepare-queued"){
            require(controller.geometryEditState().value("calculating").toBool(),"Expected queued real preparation");
            record("interval",{{"workerStarted",false},{"mechanism","single-pool-thread-occupied-before-public-entry"}});
        } else {
            settle(controller);require(controller.geometryEditState().value("boundaryStatus").toString()=="ready","Native preparation not READY");record("prepared");
            if(phase=="render-adoption")stages["interval"]=unobserved("Native boundary preparation result and session are adopted atomically in the owner completion callback; no public client-settled render-adoption interval exists.");
            else {
                QVariantMap node;for(const auto& path:controller.geometryDraftPaths())for(const auto& value:path.toMap().value("vertices").toList())if(value.toMap().value("nodeKey").toString()==definition["move"].toObject()["nodeKey"].toString())node=value.toMap();
                require(!node.empty(),"Original handle not found");require(controller.geometrySelectNearest(node.value("x").toDouble(),node.value("y").toDouble(),.001),"Public handle selection failed");require(controller.geometryBeginVertexDrag(),"Public drag begin failed");
                const auto xy=definition["move"].toObject()["coordinate"].toArray();Point desired{xy[0].toDouble(),xy[1].toDouble()};const auto screen=exactScreen(projection,desired);require(bool(screen),"No exact public projection input for timing corpus");
                require(controller.geometryMoveSelectedVertex(screen->chosen.x,screen->chosen.y,0,QString()),"Public boundary move failed");record("drag",{{"coordinate",xy},{"exactPublicProjection",true}});
                barrier=std::make_unique<QueuedWorkerBarrier>();controller.geometryEndVertexDrag(false);
                require(controller.geometryEditState().value("calculating").toBool(),"Actual downstream preview should be queued");
                stages["interval"]=unobserved("Native boundary node fan-out is synchronous. After public release, the separate downstream canonical preview is queued; this is not a boundary-move worker interval.");
            }
        }
        record("actionBoundary",{{"timingEquivalent",phase=="prepare-queued"},{"workerQueued",bool(barrier)}});
        if(action=="cancel")controller.cancelGeometryEdit();
        else if(action=="select"){const auto id=definition["selectionAfter"].toArray().last().toString();require(controller.selectObject({{"domain","territorial"},{"id",id}},"replace","countries"),"Public object selection failed");}
        record("afterAction",{{"action",action}});if(barrier)barrier->unblock();
        require(QThreadPool::globalInstance()->waitForDone(30000),"Actual worker pool did not finish before final owner delivery");
        QCoreApplication::sendPostedEvents();QCoreApplication::processEvents();settle(controller);drain();
        output["completionBarrier"]=QJsonObject{{"workerPoolCompletedBeforeFinalSnapshot",true},{"ownerEventsProcessedAfterCompletion",true},{"timeoutMs",30000}};
        record("settled");
        output["limits"]=QJsonObject{{"rawParity",false},{"workerCPUExecutionPhase",false},{"renderAdoptionPending",false},{"boundaryMoveWorkerInterval",false},{"timingEquivalent",phase=="prepare-queued"},{"nativeRawPreviewGeometry",false},{"historyDepth",false},{"fullCanonicalDocumentBytes",true},{"pointerPixels",false},{"gpuRendering",false}};
        controller.cancelGeometryEdit();drain();
    }catch(const std::exception& error){output["error"]=QString::fromUtf8(error.what());}
    output["stages"]=stages;return output;
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{require(argc==1,"Unexpected probe argument");QFile input;require(input.open(stdin,QIODevice::ReadOnly),"stdin unavailable");QJsonParseError error;const auto payload=QJsonDocument::fromJson(input.readAll(),&error);require(error.error==QJsonParseError::NoError&&payload.isArray(),"Expected timing case array");QJsonArray rows;for(const auto& definition:payload.array())rows.append(runTiming(definition.toObject()));const auto bytes=QJsonDocument(QJsonObject{{"schema","pando-m977-native-boundary-timing"},{"version",1},{"rows",rows}}).toJson(QJsonDocument::Compact);std::fwrite(bytes.constData(),1,bytes.size(),stdout);return 0;}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}

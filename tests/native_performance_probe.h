#pragma once
#include "gpumapitem.h"
#include "maprenderitem.h"
#include "terrainimageprovider.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QThread>
#include <QStandardPaths>
#include <atomic>
#include <mutex>
#include <QWheelEvent>
#include <QEventLoop>
#include <QScopeGuard>
#include <QCryptographicHash>
#include <limits>
#include <QMouseEvent>
#include <QKeyEvent>

class NativePerfEventFilter final : public QObject {
public:
    std::function<void(QObject*,QEvent*)> observe;
protected:
    bool eventFilter(QObject* target,QEvent* event) override {if(observe)observe(target,event);return false;}
};

static QJsonObject nativePerfJsonFile(const QString& path)
{
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
static QString nativePerfFileHash(const QString& path)
{
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!hash.addData(&file))return {};
    return QString::fromLatin1(hash.result().toHex());
}

// Opt-in native-device measurement. It uses the production QML and controller,
// isolated persistence, and never changes the supplied recovery files.
static void runNativePerformanceProbe()
{
    const auto reportPath=qEnvironmentVariable("PANDOEDITOR_NATIVE_PERF_REPORT");
    if(reportPath.isEmpty())QSKIP("Native GPU performance requires an explicit isolated measurement run");
    const auto manifestPath=qEnvironmentVariable("PANDOEDITOR_NATIVE_PERF_FIXTURES");
    const auto manifest=nativePerfJsonFile(manifestPath);
    QCOMPARE(manifest.value("schema").toString(),QString("pandoeditor-native-performance-fixtures"));
    QCOMPARE(manifest.value("version").toInt(),2);
    const QStringList ids={"idle","flat-pan","flat-zoom","globe-rotation","globe-zoom","projection-switch","hover","selection","dense-labels","hydro-dense","dem-zoom-in","dem-zoom-out","combined-pan","menu-panel","large-navigation"};
    const QStringList names={"Idle","Flat pan","Flat zoom","Globe rotation","Globe zoom",QString::fromUtf8("Flat ↔ Globe switch"),"Hover","Selection","Dense labels pan/zoom","Hydro dense viewport","DEM LOD zoom-in","DEM LOD zoom-out",QString::fromUtf8("Terrain + labels + hydro 동시 pan"),"Menu/panel UI","Large project navigation"};
    QHash<QString,QJsonObject> fixtureContracts;
    QHash<QString,QJsonObject> inputContracts;
    for(const auto& value:manifest.value("fixtures").toArray()) {
        const auto fixture=value.toObject();fixtureContracts.insert(fixture.value("id").toString(),fixture);
        for(const auto& pair:{qMakePair("projectPath","projectSha256"),qMakePair("viewPath","viewSha256")}) {
            const auto path=QFileInfo(manifestPath).dir().filePath(fixture.value(pair.first).toString());
            QCOMPARE(nativePerfFileHash(path),fixture.value(pair.second).toString().toLower());
        }
        for(const auto& asset:fixture.value("assets").toArray()) {
            const auto record=asset.toObject();QCOMPARE(nativePerfFileHash(QFileInfo(manifestPath).dir().filePath(record.value("path").toString())),record.value("sha256").toString().toLower());
        }
        const auto inputPath=QFileInfo(manifestPath).dir().filePath(fixture.value("inputContractPath").toString());
        QCOMPARE(nativePerfFileHash(inputPath),fixture.value("inputContractSha256").toString().toLower());
        const auto contract=nativePerfJsonFile(inputPath);
        QCOMPARE(contract.value("schema").toString(),QString("pandoeditor-native-performance-inputs"));
        QCOMPARE(contract.value("version").toInt(),2);
        QCOMPARE(contract.value("sequenceId").toString(),QString("m98-native-v2"));
        inputContracts.insert(fixture.value("id").toString(),contract);
    }
    for(const auto* id:{"world-standard","dense-view","editing-heavy","large-project"})QVERIFY(fixtureContracts.contains(id));
    const auto mode=qEnvironmentVariable("PANDOEDITOR_NATIVE_PERF_MODE");
    const auto preflight=nativePerfJsonFile(qEnvironmentVariable("PANDOEDITOR_NATIVE_PERF_PREFLIGHT"));
    QVERIFY2(mode=="diagnostic"||preflight.value("status").toString()=="PASS","Production full-stack acceptance preflight blocked");
    QTemporaryDir privateData;QVERIFY(privateData.isValid());
    QHash<QString,QString> fixtureProjects;
    for(auto it=fixtureContracts.begin();it!=fixtureContracts.end();++it) {
        const auto original=QFileInfo(manifestPath).dir().filePath(it.value().value("projectPath").toString());
        const auto copy=privateData.filePath(it.key()+".json");QVERIFY(QFile::copy(original,copy));fixtureProjects.insert(it.key(),copy);
    }
    EditorControllerConfig config;
    config.bootstrapWorld=true;config.autosaveEnabled=true;config.projectPreviewEnabled=true;
    config.worldDataRoot=QStringLiteral(PANDOEDITOR_WORLD_ASSET_DIR);
    config.privateProjectPath=privateData.filePath("private.json");
    config.autosaveProjectPath=privateData.filePath("autosave-project.json");
    config.autosaveViewPath=privateData.filePath("autosave-view.json");
    config.appearancePath=privateData.filePath("appearance.json");
    config.projectPreviewCachePath=privateData.filePath("preview");
    const auto recovery=fixtureProjects.value("world-standard");
    QElapsedTimer clock;clock.start();
    EditorController editor(config);
    const auto seedViewPath=qEnvironmentVariable("PANDOEDITOR_NATIVE_PERF_VIEW");
    if(recovery.isEmpty()&&!seedViewPath.isEmpty()) {
        QFile seed(seedViewPath);QVERIFY(seed.open(QIODevice::ReadOnly));
        const auto envelope=QJsonDocument::fromJson(seed.readAll()).object();
        QCOMPARE(envelope.value("format").toString(),QString("pandoeditor-view-state"));
        const auto view=envelope.value("view").toObject().toVariantMap();
        QVERIFY(!view.isEmpty());
        auto applied=std::make_shared<bool>(false);
        const auto apply=[&,view,applied] {
            if(*applied||editor.worldStatus()!="canonical")return;
            *applied=true;
            QVERIFY(editor.setProjectionMode(view.value("projection").toString()));
            QVERIFY(editor.publishMapView(view));
        };
        QObject::connect(&editor,&EditorController::worldStatusChanged,&editor,apply);
        apply();
    }
    QQmlApplicationEngine engine;
    auto* terrain=new TerrainImageProvider;
    terrain->setSource(editor.terrainProviderSnapshot());
    engine.addImageProvider("terrain",terrain);
    QObject::connect(&editor,&EditorController::terrainChanged,&engine,[&]{terrain->setSource(editor.terrainProviderSnapshot());});
    engine.rootContext()->setContextProperty("editor",&editor);
    engine.load(QUrl("qrc:/common/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());QVERIFY(window);
    window->setFlag(Qt::WindowStaysOnTopHint,true);
    window->setTitle(QStringLiteral("Pandoeditor 성능 측정 — 자동 종료까지 조작하지 마세요"));
    window->setPosition(0,0);
    window->resize(1920,977);
    window->hide();window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* map=navigationItem(window->contentItem(),"mapView");QVERIFY(map);
    QTest::qWait(500);
    window->resize(1920,window->height()+929-int(map->height()));
    QTest::qWait(100);
    QCOMPARE(map->width(),1920.);QCOMPARE(map->height(),929.);
    auto* gpu=window->findChild<GpuMapItem*>("gpuMapRenderer");QVERIFY(gpu);
    auto* cpuRenderer=window->findChild<MapRenderItem*>("canonicalMapRenderer");QVERIFY(cpuRenderer);
    if(qEnvironmentVariable("PANDOEDITOR_NATIVE_PERF_ABLATION")=="no-cull") {
        auto* source=qobject_cast<MapSceneBridge*>(editor.mapSceneBridge());
        auto* diagnostic=new MapSceneBridge(&engine,false);
        const auto copy=[source,diagnostic] {
            if(const auto original=source->sceneSnapshot()) {
                diagnostic->publishScene(original);
            }
            diagnostic->publishView(source->viewState());
        };
        QObject::connect(source,&MapSceneBridge::sceneChanged,diagnostic,copy);
        QObject::connect(source,&MapSceneBridge::viewChanged,diagnostic,[source,diagnostic] {
            diagnostic->publishView(source->viewState());
        });
        copy();gpu->setSceneBridge(diagnostic);
    }
    std::atomic<quint64> presents{0};
    std::atomic<int> phase{0},requested{0},synchronized{0},presented{0};
    std::atomic<bool> repeatPhase{false};
    std::atomic<qint64> synchronizedNs{0},presentedNs{0};
    std::atomic<quint64> presentedFrame{0};
    std::mutex frameMutex;
    auto* sceneBridge=qobject_cast<MapSceneBridge*>(gpu->sceneBridge());QVERIFY(sceneBridge);
    const auto frameState=[](const std::shared_ptr<const MapFrame>& frame) {
        QJsonObject state;
        if(!frame||!frame->scene)return state;
        state["viewRevision"]=double(frame->view.revision);
        state["sceneRevision"]=double(frame->scene->revision);
        state["selectionRevision"]=double(frame->scene->revisions.selection);
        state["projection"]=frame->view.mode==ProjectionMode::Globe?"globe":"flat";
        state["selectedId"]=frame->scene->interaction.primary?QString::fromStdString(frame->scene->interaction.primary->id):QString{};
        state["hoveredId"]=frame->scene->interaction.hover?QString::fromStdString(frame->scene->interaction.hover->id):QString{};
        state["editTargetId"]=frame->scene->interaction.editTarget?QString::fromStdString(frame->scene->interaction.editTarget->id):QString{};
        QJsonArray selected;for(const auto& ref:frame->scene->interaction.selected)selected.append(QJsonObject{{"domain",QString::fromStdString(ref.domain)},{"id",QString::fromStdString(ref.id)}});state["selectedObjects"]=selected;
        state["centerLongitude"]=frame->view.centerLongitude;state["centerLatitude"]=frame->view.centerLatitude;
        state["rotationLongitude"]=frame->view.rotationLongitude;state["rotationLatitude"]=frame->view.rotationLatitude;
        state["rotationRoll"]=frame->view.rotationRoll;
        state["scale"]=frame->view.scale;state["translateX"]=frame->view.translateX;state["translateY"]=frame->view.translateY;
        return state;
    };
    QJsonObject expectedState,synchronizedState,presentedState,guiState;
    std::shared_ptr<const MapFrame> expectedRenderFrame;
    QJsonObject synchronizedOwner,presentedOwner;
    QJsonArray presentationFrames;
    qint64 lastPresent=0;int lastPhase=-1;
    const auto syncConnection=QObject::connect(window,&QQuickWindow::afterSynchronizing,window,[&]{
        // The bridge publishes immutable render frames. GUI-only properties are
        // copied after the input completes, protected by this same mutex; this
        // render-thread callback never reads the editor or QML GUI objects.
        const auto observation=gpu->renderObservation();
        if(!observation||!observation->rendererReady||observation->stats.uploadsPending)return;
        auto actual=frameState(observation->frame);
        std::lock_guard<std::mutex> lock(frameMutex);
        for(auto it=guiState.begin();it!=guiState.end();++it)actual[it.key()]=it.value();
        if(requested.load()>synchronized.load()&&observation->frame==expectedRenderFrame&&actual==expectedState){
            synchronizedOwner=QJsonObject{{"windowGeneration",double(observation->windowGeneration)},{"resourceGeneration",double(observation->resourceGeneration)},{"bridgeGeneration",double(observation->bridgeGeneration)}};
            synchronizedState=actual;synchronizedNs.store(clock.nsecsElapsed());synchronized.store(requested.load());
        }
    },Qt::DirectConnection);
    const auto presentConnection=QObject::connect(window,&QQuickWindow::frameSwapped,window,[&]{
        ++presents;const auto now=clock.nsecsElapsed();const int current=phase.load();
        const auto rendered=gpu->renderObservation();
        std::lock_guard<std::mutex> lock(frameMutex);
        const bool currentOwner=rendered&&rendered->rendererReady&&!rendered->stats.uploadsPending&&rendered->frame==expectedRenderFrame&&
            double(rendered->windowGeneration)==synchronizedOwner.value("windowGeneration").toDouble()&&
            double(rendered->resourceGeneration)==synchronizedOwner.value("resourceGeneration").toDouble()&&
            double(rendered->bridgeGeneration)==synchronizedOwner.value("bridgeGeneration").toDouble();
        if(currentOwner&&synchronized.load()>presented.load()){presentedNs.store(now);presentedFrame.store(presents.load());presented.store(synchronized.load());presentedState=synchronizedState;presentedOwner=synchronizedOwner;}
        if(lastPhase==current&&current>=0)presentationFrames.append(QJsonObject{
            {"scenarioId",repeatPhase.load()?QString("long-run"):ids.value(current)},{"frameSequence",double(presents.load())},{"elapsedMs",now/1.e6},{"ms",(now-lastPresent)/1.e6}});
        lastPresent=now;lastPhase=current;
    },Qt::DirectConnection);
    const auto stopRenderObservation=qScopeGuard([&]{
        QObject::disconnect(syncConnection);QObject::disconnect(presentConnection);
        window->hide();window->setProperty("allowClose",true);window->close();
    });
    QJsonArray rows,heartbeats,frames,inputs,heartbeatEvents,editingEvents,rawInputEvents;
    qulonglong lastEditingEventSerial=0;
    quint64 projectGeneration=1;
    const QStringList cumulativeMetrics={"sceneFullBuildCount","scenePreparationCount","sceneDeltaUpdateCount","scenePresentationUpdateCount","sceneTransientUpdateCount","sceneGraphRebuildCount","geometryUploads","uploadedBytes","uploadContinuationCount","viewUniformUpdateCount","labelLayouts","labelReprojects","labelCandidatesExamined"};
    const QStringList editingCounts={"computeCount","previewCount","prepareCommitCount","commitCount","undoCount","redoCount","snapQueryCount","snapCandidatesExamined","selectionPreparationCount","riverPartitionCount","splitPreparationCount","annexPreparationCount","sharedBoundaryPreparationCount"};
    const QStringList editingTimes={"computeMs","previewMs","prepareCommitMs","commitMs","undoMs","redoMs","computeTotalMs","previewTotalMs","prepareCommitTotalMs","commitTotalMs","undoTotalMs","redoTotalMs","snapQueryMs","selectionPreparationMs","riverPartitionMs","splitPreparationMs","annexPreparationMs","sharedBoundaryPreparationMs","previewLatencyMs","commitLatencyMs","undoLatencyMs"};
    const auto disconnectFrames=qScopeGuard([&]{
        QObject::disconnect(syncConnection);QObject::disconnect(presentConnection);
        QObject::disconnect(gpu,nullptr,&engine,nullptr);
    });
    QObject::connect(gpu,&GpuMapItem::frameSampled,&engine,[&](double ms){frames.append(ms);});
    QFile journal(reportPath+".jsonl");QVERIFY(journal.open(QIODevice::WriteOnly));
    int rawInputSequence=0;
    NativePerfEventFilter rawInputFilter;
    rawInputFilter.observe=[&](QObject* target,QEvent* event){
        if(target!=window)return;
        QJsonObject record{{"event","raw-input"},{"inputSequence",rawInputSequence},{"startedNs",double(clock.nsecsElapsed())},
            {"scenarioId",repeatPhase.load()?QString("long-run"):ids.value(phase.load())},{"qtEventType",int(event->type())}};
        switch(event->type()) {
            case QEvent::MouseButtonPress:case QEvent::MouseButtonRelease:case QEvent::MouseMove:{
                const auto* mouse=static_cast<QMouseEvent*>(event);record["type"]="pointer";record["x"]=mouse->position().x();record["y"]=mouse->position().y();record["buttons"]=int(mouse->buttons());record["button"]=int(mouse->button());break;}
            case QEvent::Wheel:{const auto* wheel=static_cast<QWheelEvent*>(event);record["type"]="wheel";record["x"]=wheel->position().x();record["y"]=wheel->position().y();record["angleDeltaX"]=wheel->angleDelta().x();record["angleDeltaY"]=wheel->angleDelta().y();break;}
            case QEvent::KeyPress:case QEvent::KeyRelease:{const auto* key=static_cast<QKeyEvent*>(event);record["type"]="key";record["key"]=key->key();record["modifiers"]=int(key->modifiers());break;}
            default:return;
        }
        rawInputEvents.append(record);journal.write(QJsonDocument(record).toJson(QJsonDocument::Compact)+'\n');journal.flush();
    };
    qApp->installEventFilter(&rawInputFilter);
    const auto removeInputFilter=qScopeGuard([&]{qApp->removeEventFilter(&rawInputFilter);});
    auto cpuMilliseconds=[]() -> double {
#ifdef Q_OS_WIN
        FILETIME created,exit,kernel,user;
        if(!GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&user))return std::numeric_limits<double>::quiet_NaN();
        ULARGE_INTEGER k,u;k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
        u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;
        return double(k.QuadPart+u.QuadPart)/10000.;
#else
        return std::numeric_limits<double>::quiet_NaN();
#endif
    };
    const int logicalProcessors=
#ifdef Q_OS_WIN
        int(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
#else
        std::max(1,QThread::idealThreadCount());
#endif
    QVERIFY(logicalProcessors>0);
    double lastCpu=cpuMilliseconds();qint64 lastSample=clock.elapsed(),lastBeat=lastSample;
    QTimer pulse; pulse.setInterval(20);
    QObject::connect(&pulse,&QTimer::timeout,&engine,[&]{const auto now=clock.elapsed();heartbeats.append(double(now-lastBeat));heartbeatEvents.append(QJsonObject{{"elapsedMs",double(now)},{"phase",phase.load()},{"ms",double(now-lastBeat)}});lastBeat=now;});
    QTimer sample; sample.setInterval(1000);
    const auto ablation=qEnvironmentVariable("PANDOEDITOR_NATIVE_PERF_ABLATION");
    bool ablationApplied=false;
    QObject::connect(&sample,&QTimer::timeout,&engine,[&]{
        const auto now=clock.elapsed();const auto cpu=cpuMilliseconds();
        QJsonObject row=QJsonObject::fromVariantMap(editor.renderQuality());
        row["elapsedMs"]=double(now);row["worldStatus"]=editor.worldStatus();
        row["scenarioId"]=repeatPhase.load()?QString("long-run"):ids.value(phase.load());
        row["viewRevision"]=double(qobject_cast<MapSceneBridge*>(editor.mapSceneBridge())->viewState().revision);
        row["startupBusy"]=editor.startupBusy();row["gpuReady"]=gpu->rendererReady();
        row["backendDiagnostic"]=gpu->diagnostic();row["graphicsApi"]=int(window->rendererInterface()->graphicsApi());
        row["mapWidth"]=map->width();row["mapHeight"]=map->height();row["exposed"]=window->isExposed();
        row["windowDevicePixelRatio"]=window->devicePixelRatio();
        row["windowVisibility"]=int(window->visibility());row["windowState"]=int(window->windowState());
        row["windowActive"]=window->isActive();
        row["presents"]=double(presents.load());row["geometryUploads"]=double(gpu->geometryUploadCount());
        row["geometryBytes"]=double(gpu->geometryBytes());row["uploadsPending"]=gpu->uploadsPending();
        row["uploadBytesThisFrame"]=double(gpu->uploadBytesThisFrame());
        const auto& stats=gpu->gpuStats();
        row["sceneGraphSyncCount"]=double(stats.syncCount);row["sceneGraphRebuildCount"]=double(stats.treeRebuildCount);
        row["nodeAttachmentCount"]=double(stats.nodeAttachmentCount);
        row["drawNodes"]=double(stats.drawNodes);row["strokeBytes"]=double(stats.strokeBytes);
        row["syncMs"]=stats.syncMilliseconds;row["uploadMs"]=stats.uploadMilliseconds;
        row["strokeUploadMs"]=stats.strokeUploadMilliseconds;row["uploadedBytes"]=double(stats.uploadedBytes);
        row["uploadContinuationCount"]=double(gpu->uploadContinuationCount());
        row["cpuRendererVisible"]=cpuRenderer->isVisible();row["cpuPaintCount"]=double(cpuRenderer->paintCount());
        row["cpuPaintMs"]=cpuRenderer->paintMilliseconds();
        const auto resources=editor.terrainDataStatus();
        row["activeDownloads"]=resources.contains("activeDownloads")?QJsonValue::fromVariant(resources.value("activeDownloads")):QJsonValue(QJsonValue::Null);
        row["queuedDownloads"]=resources.contains("queuedDownloads")?QJsonValue::fromVariant(resources.value("queuedDownloads")):QJsonValue(QJsonValue::Null);
        row["cpuRawPercent"]=100*(cpu-lastCpu)/std::max<qint64>(1,now-lastSample);
        row["cpuPercent"]=row["cpuRawPercent"].toDouble()/logicalProcessors;
        row["logicalProcessors"]=logicalProcessors;
        row["terrainPendingJobs"]=resources.contains("pendingJobs")?QJsonValue::fromVariant(resources.value("pendingJobs")):QJsonValue(QJsonValue::Null);
        row["pendingJobs"]=row.contains("pendingJobs")?row.value("pendingJobs"):QJsonValue(QJsonValue::Null);
        row["generations"]=QJsonObject{{"process",1},{"controller",1},{"project",double(projectGeneration)},{"labels",double(editor.renderQuality().value("labelCounterGeneration").toULongLong())},{"window",double(gpu->resourceGeneration())}};
        QJsonObject values;
        for(const auto& key:cumulativeMetrics)values[key]=row.contains(key)?row.value(key):QJsonValue(QJsonValue::Null);
        values["viewUniformUpdateCount"]=double(gpu->viewUniformUpdateCount());
        const auto editing=editor.property("editingPerformanceStats").toMap();
        for(const auto& key:editingCounts+editingTimes)values["editing."+key]=editing.contains(key)?QJsonValue::fromVariant(editing.value(key)):QJsonValue(QJsonValue::Null);
        row["editingEventSerial"]=editing.contains("eventSerial")?QJsonValue::fromVariant(editing.value("eventSerial")):QJsonValue(QJsonValue::Null);
        row["editingEventsDropped"]=editing.contains("eventsDropped")?QJsonValue::fromVariant(editing.value("eventsDropped")):QJsonValue(QJsonValue::Null);
        for(const auto& event:editing.value("events").toList()){
            const auto value=event.toMap();const auto serial=value.value("eventSerial").toULongLong();
            if(serial<=lastEditingEventSerial)continue;
            auto observed=QJsonObject::fromVariantMap(value);observed["sampledAtMs"]=double(now);observed["scenarioId"]=repeatPhase.load()?QString("long-run"):ids.value(phase.load());editingEvents.append(observed);
            journal.write(QJsonDocument(observed).toJson(QJsonDocument::Compact)+'\n');lastEditingEventSerial=serial;
        }
        values["viewportResourceGeneration"]=row.contains("viewportResourceGeneration")?row.value("viewportResourceGeneration"):QJsonValue(QJsonValue::Null);
        row["values"]=values;
        row["terrainResources"]=QJsonObject::fromVariantMap(resources);
        row["placeResources"]=QJsonObject::fromVariantMap(editor.placeDataStatus());
        row["qsgResources"]=QJsonObject::fromVariantMap(gpu->resourceCacheStats());
        rows.append(row);journal.write(QJsonDocument(row).toJson(QJsonDocument::Compact)+'\n');journal.flush();
        lastCpu=cpu;lastSample=now;
        if(!ablationApplied&&!ablation.isEmpty()&&!editor.startupBusy()&&gpu->geometryUploadCount()>0&&!gpu->uploadsPending()) {
            ablationApplied=true;
            if(ablation=="no-labels")for(const auto* key:{"basemapLabels","countryFlags","subunitLabels","subunitFlags","regionLabels","regionFlags","labels"})
                editor.setPresentationVisibility(QString::fromLatin1(key),false);
            if(ablation=="no-map")gpu->setVisible(false);
        }
    });
    pulse.start();sample.start();
    // Finish immutable world/provider bootstrap, then use the normal project
    // import path. Raw project files are not autosave envelopes.
    QTRY_VERIFY_WITH_TIMEOUT(!editor.startupBusy()&&editor.worldStatus()!="loading-preview"&&editor.worldStatus()!="preview"&&editor.worldStatus()!="canonical-pending-mesh",90000);
    QVERIFY(editor.openFile(QUrl::fromLocalFile(recovery)));
    QTRY_VERIFY_WITH_TIMEOUT(!editor.startupBusy(),90000);
    const auto initialEnvelope=nativePerfJsonFile(QFileInfo(manifestPath).dir().filePath(fixtureContracts.value("world-standard").value("viewPath").toString()));
    const auto fixtureView=initialEnvelope.value("view").toObject().toVariantMap();
    QVERIFY(!fixtureView.isEmpty());QVERIFY(editor.setProjectionMode(fixtureView.value("projection").toString()));QVERIFY(editor.publishMapView(fixtureView));
    bool customWarmup=false;
    int warmup=qEnvironmentVariableIntValue("PANDOEDITOR_NATIVE_PERF_WARMUP_MS",&customWarmup);
    warmup=customWarmup?std::max(60000,warmup):60000;
    QTest::qWait(warmup);
    QVERIFY(window->isExposed());QVERIFY(gpu->rendererReady());
    QVERIFY(presents.load()>0);QVERIFY(gpu->geometryUploadCount()>0);
    QVERIFY(!gpu->uploadsPending());QVERIFY(!editor.startupBusy());
    const auto initialView=QJsonObject::fromVariantMap(editor.mapViewState());
    const auto currentState=[&] {
        auto state=frameState(sceneBridge->frameSnapshot());
        if(auto* popup=window->findChild<QObject*>("fileMenu"))state["menuVisible"]=popup->property("visible").toBool();
        else state["menuVisible"]=false;
        state["hasPreparedPreview"]=editor.hasPreparedPreview();
        state["geometryEdit"]=QJsonObject::fromVariantMap(editor.geometryEditState());
        state["contentEdit"]=QJsonObject::fromVariantMap(editor.contentEditState());
        return state;
    };
    const auto returnState=[&] {
        const auto state=currentState();QJsonObject result;
        for(const auto* key:{"projection","centerLongitude","centerLatitude","rotationLongitude","rotationLatitude","rotationRoll","scale","translateX","translateY","selectedId","selectedObjects","editTargetId","menuVisible","hasPreparedPreview"})result[key]=state.value(key);
        result["geometryEditActive"]=editor.geometryEditState().value("active").toBool();
        result["contentEditActive"]=editor.contentEditState().value("active").toBool();
        return result;
    };
    const auto input=[&](int kind,const std::function<void()>& action,const std::function<bool()>& ready=std::function<bool()>{}) {
        phase.store(kind);
        const auto before=currentState();
        const auto start=clock.nsecsElapsed();
        rawInputSequence=requested.load()+1;
        QEventLoop presentedLoop;
        QTimer deadline;deadline.setSingleShot(true);deadline.setInterval(5000);
        int sequence=0;
        const auto wake=QObject::connect(window,&QQuickWindow::frameSwapped,&presentedLoop,[&]{
            if(presented.load()>=sequence)presentedLoop.quit();
        },Qt::QueuedConnection);
        QObject::connect(&deadline,&QTimer::timeout,&presentedLoop,&QEventLoop::quit);
        deadline.start();
        action();const auto actionNs=clock.nsecsElapsed()-start;
        QCoreApplication::processEvents();
        const auto frameMatchesGui=[&]{const auto frame=frameState(sceneBridge->frameSnapshot());
            return frame.value("selectedId").toString()==editor.selectedId()&&frame.value("hoveredId").toString()==editor.hoverObject().value("id").toString()&&frame.value("viewRevision").toDouble()==editor.mapViewState().value("revision").toDouble();};
        while(!frameMatchesGui()&&clock.nsecsElapsed()-start<5'000'000'000LL)QTest::qWait(5);
        while(ready&&!ready()&&clock.nsecsElapsed()-start<5'000'000'000LL)QTest::qWait(5);
        const bool completionReady=frameMatchesGui()&&(!ready||ready());
        const auto expected=currentState();
        {std::lock_guard<std::mutex> lock(frameMutex);
            expectedState=expected;expectedRenderFrame=sceneBridge->frameSnapshot();guiState=QJsonObject{{"menuVisible",expected.value("menuVisible")},{"hasPreparedPreview",expected.value("hasPreparedPreview")},{"geometryEdit",expected.value("geometryEdit")},{"contentEdit",expected.value("contentEdit")}};
            sequence=++requested;}
        window->update();
        if(presented.load()<sequence&&clock.nsecsElapsed()-start<5'000'000'000LL)presentedLoop.exec();
        QObject::disconnect(wake);
        const bool delivered=presented.load()>=sequence;
        QJsonObject actual,owner;{std::lock_guard<std::mutex> lock(frameMutex);actual=presentedState;owner=presentedOwner;}
        QJsonObject observation{{"event","input"},{"scenarioId",repeatPhase.load()?QString("long-run"):ids.value(kind)},{"elapsedMs",start/1.e6},
            {"latencyMs",((delivered?presentedNs.load():clock.nsecsElapsed())-start)/1.e6},
            {"startedNs",double(start)},{"synchronizedNs",double(synchronizedNs.load())},{"presentedNs",double(presentedNs.load())},
            {"inputSequence",sequence},{"frameSequence",double(presentedFrame.load())},
            {"renderOwner",owner},{"renderSnapshotMatched",delivered},{"renderUploadsPending",delivered?QJsonValue(false):QJsonValue(QJsonValue::Null)},
            {"delivered",delivered&&completionReady},{"stateChanged",before!=expected},{"matched",delivered&&completionReady&&actual==expected},
            {"expectedState",expected},{"renderState",actual}};
        observation["actionMs"]=actionNs/1.e6;
        observation["selectedId"]=editor.selectedId();
        if(auto* popup=window->findChild<QObject*>("fileMenu"))observation["menuVisible"]=popup->property("visible").toBool();
        inputs.append(observation);
        journal.write(QJsonDocument(observation).toJson(QJsonDocument::Compact)+'\n');journal.flush();
        rawInputSequence=0;
    };
    const QPoint panStart=map->mapToScene({960,464.5}).toPoint();
    QJsonArray scenarios,cycles;
    QString activeFixture="world-standard";
    const auto loadFixture=[&](const QString& id) {
        phase.store(-1);
        const auto fixture=fixtureContracts.value(id);
        if(id!=activeFixture){
            QVERIFY(editor.openFile(QUrl::fromLocalFile(fixtureProjects.value(id))));
            ++projectGeneration;
            activeFixture=id;
        }
        const auto envelope=nativePerfJsonFile(QFileInfo(manifestPath).dir().filePath(fixture.value("viewPath").toString()));
        const auto view=envelope.value("view").toObject().toVariantMap();
        QVERIFY(!view.isEmpty());QVERIFY(editor.setProjectionMode(view.value("projection").toString()));QVERIFY(editor.publishMapView(view));
        QTest::qWait(1000);
    };
    const auto pan=[&](int kind,int count) {
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,panStart,0);
        for(int i=0;i<count;++i)input(kind,[&]{QTest::mouseMove(window,panStart+QPoint(20+int(120*std::sin(i*.045)),int(25*std::sin(i*.07))),0);});
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,panStart,0);
    };
    const auto zoom=[&](int kind,int count,bool reverse=false,bool oneDirection=false) {
        for(int i=0;i<count;++i)input(kind,[&]{
            const int direction=oneDirection?1:i<count/2?1:-1;
            QWheelEvent wheel(panStart,window->mapToGlobal(panStart),{},QPoint(0,(reverse?-1:1)*direction*5),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QCoreApplication::sendEvent(window,&wheel);
        });
    };
    const auto clickItem=[&](const QString& name) {
        auto* item=navigationItem(window->contentItem(),name);QVERIFY2(item,qPrintable("Missing actual QML input target: "+name));
        QVERIFY(item->isVisible());QVERIFY(item->isEnabled());
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,item->mapToScene({item->width()/2,item->height()/2}).toPoint(),0);
    };
    const auto projectionInput=[&](int kind,const QString& mode) {
        clickItem("mapDisplayButton");QCoreApplication::processEvents();clickItem("viewProjectionMenu");QCoreApplication::processEvents();
        input(kind,[&]{clickItem(mode=="globe"?"projectionGlobeButton":"projectionFlatButton");});
        QTest::keyClick(window,Qt::Key_Escape,Qt::NoModifier,0);QTest::keyClick(window,Qt::Key_Escape,Qt::NoModifier,0);
    };
    const auto selectAt=[&](int kind,const QJsonArray& point) {
        QCOMPARE(point.size(),2);const auto p=map->mapToScene(QPointF(point[0].toDouble(),point[1].toDouble())).toPoint();
        input(kind,[&]{QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,p,0);});
        QVERIFY(!editor.selectedId().isEmpty());
    };
    const auto previewCancel=[&] {
        const auto contract=inputContracts.value("editing-heavy").value("editingPreviewCancel").toObject();
        QVERIFY2(!contract.isEmpty(),"Missing immutable editing preview/cancel pointer contract");
        const auto before=editor.documentBytes();
        selectAt(12,contract.value("selectionScreen").toArray());
        clickItem("objectActionsTab");QCoreApplication::processEvents();
        input(12,[&]{clickItem("editorGeometryAction");});
        QVERIFY(editor.geometryEditState().value("active").toBool());
        const auto from=contract.value("dragFrom").toArray(),to=contract.value("dragTo").toArray();
        QCOMPARE(from.size(),2);QCOMPARE(to.size(),2);
        const auto a=map->mapToScene(QPointF(from[0].toDouble(),from[1].toDouble())).toPoint();
        const auto b=map->mapToScene(QPointF(to[0].toDouble(),to[1].toDouble())).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,a,0);
        for(int step=1;step<=8;++step)input(12,[&]{QTest::mouseMove(window,a+(b-a)*step/8,0);});
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,b,0);
        input(12,[&]{clickItem("geometryPreview");},[&]{return editor.geometryEditState().value("previewReady").toBool();});
        QVERIFY(editor.geometryEditState().value("previewReady").toBool());
        input(12,[&]{clickItem("geometryCancel");});
        QVERIFY(!editor.geometryEditState().value("active").toBool());QCOMPARE(editor.documentBytes(),before);
    };
    for(int kind=0;kind<ids.size();++kind){
        const QString fixtureId=kind==14?"large-project":kind==8||kind==9||kind==12?"dense-view":"world-standard";
        loadFixture(fixtureId);
        if(kind==3||kind==4)QVERIFY(editor.setProjectionMode("globe"));
        if(kind==10||kind==11)QVERIFY(editor.setTerrainMode("color"));
        phase.store(kind);const auto started=clock.elapsed();
        if(kind==0)QTest::qWait(30000);
        else if(kind==1||kind==3||kind==9||kind==12||kind==14)pan(kind,120);
        else if(kind==2||kind==4)zoom(kind,90);
        else if(kind==10||kind==11)zoom(kind,90,kind==11,true);
        else if(kind==5)for(int i=0;i<20;++i)projectionInput(kind,i%2?"flat":"globe");
        else if(kind==6){
            QVector<QPair<QPoint,QString>> targets;QSet<QString> found;
            for(int y=140;y<800;y+=90)for(int x=500;x<1450;x+=90){const auto hit=editor.pickObjectScreen(x,y,map->property("zoom").toDouble());const auto id=hit.value("id").toString();if(hit.value("domain").toString()=="territorial"&&!found.contains(id)){found.insert(id);targets.append({map->mapToScene(QPointF(x,y)).toPoint(),id});}}
            QVERIFY2(targets.size()>=2,"Hover fixture requires distinct visible objects");
            for(int i=0;i<40;++i){const auto target=std::find_if(targets.begin(),targets.end(),[&](const auto& t){return t.second!=editor.hoverObject().value("id").toString();});QVERIFY(target!=targets.end());input(kind,[&]{QTest::mouseMove(window,target->first,0);});}
        }
        else if(kind==7){
            QVector<QPair<QPoint,QString>> targets;
            for(int y=140;y<800;y+=90)for(int x=500;x<1450;x+=90){const auto hit=editor.pickObjectScreen(x,y,map->property("zoom").toDouble());if(hit.value("domain").toString()=="territorial")targets.append({map->mapToScene(QPointF(x,y)).toPoint(),hit.value("id").toString()});}
            for(int i=0;i<18;++i){auto* toolbar=navigationItem(window->contentItem(),"territorialToolbar");const auto blocked=toolbar&&toolbar->isVisible()?toolbar->mapRectToScene(toolbar->boundingRect()):QRectF{};
                auto target=std::find_if(targets.begin(),targets.end(),[&](const auto& t){return t.second!=editor.selectedId()&&!blocked.adjusted(-12,-12,12,12).contains(t.first);});
                QVERIFY2(target!=targets.end(),"No unobstructed fixture selection target");input(kind,[&]{QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,target->first,0);});QCOMPARE(editor.selectedId(),target->second);}
        }
        else if(kind==8){pan(kind,120);zoom(kind,90);}
        else if(kind==13){auto* menu=navigationItem(window->contentItem(),"fileMenuButton");QVERIFY(menu);for(int i=0;i<20;++i)input(kind,[&]{if(i%2)QTest::keyClick(window,Qt::Key_Escape,Qt::NoModifier,0);else QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,menu->mapToScene({menu->width()/2,menu->height()/2}).toPoint(),0);});}
        QTest::qWait(400);
        const auto expected=fixtureContracts.value(fixtureId).value("expectedFinalStates").toObject().value(ids[kind]).toObject();
        const auto state=currentState();QJsonObject actual;
        for(auto it=expected.begin();it!=expected.end();++it)actual[it.key()]=state.value(it.key());
        const QJsonObject scenario{{"id",ids[kind]},{"name",names[kind]},{"fixtureId",fixtureId},{"status","completed"},{"startMs",double(started)},{"endMs",double(clock.elapsed())},{"expectedFinalState",expected},{"actualFinalState",actual}};
        scenarios.append(scenario);journal.write(QJsonDocument(scenario).toJson(QJsonDocument::Compact)+'\n');journal.flush();
    }
    bool customRepeat=false;int repeatMs=qEnvironmentVariableIntValue("PANDOEDITOR_NATIVE_PERF_REPEAT_MS",&customRepeat);repeatMs=customRepeat?std::max(720000,repeatMs):720000;
    const auto repeatStart=clock.elapsed();int cycle=0;
    repeatPhase.store(true);
    while(clock.elapsed()-repeatStart<repeatMs){
        const auto started=clock.elapsed();loadFixture("dense-view");phase.store(12);pan(12,120);zoom(12,90);
        projectionInput(12,"globe");pan(12,120);zoom(12,90);projectionInput(12,"flat");
        loadFixture("editing-heavy");phase.store(12);previewCancel();loadFixture("dense-view");phase.store(12);
        while(clock.elapsed()-started<60000)QTest::qWait(100);
        const auto resource=editor.terrainDataStatus(),quality=editor.renderQuality();
        const bool settled=quality.contains("pendingJobs")&&quality.value("pendingJobs").isValid()&&quality.value("pendingJobs").toInt()==0&&
            !editor.startupBusy()&&!gpu->uploadsPending()&&!resource.value("activeDownloads").toInt()&&!resource.value("queuedDownloads").toInt()&&
            !resource.value("cpuPreparationPending").toBool()&&!resource.value("cpuPreparationScheduled").toBool()&&
            !resource.value("uploadWorkPending").toBool()&&resource.value("pendingDisplayAdoptions").toULongLong()==0;
        // Missing stale-publication instrumentation remains null, never zero.
        const auto cache=[&](const char* domain){const auto domains=quality.value("resourceCaches").toMap();
            if(domains.contains(domain))return QJsonObject::fromVariantMap(domains.value(domain).toMap());
            return QJsonObject{{"residentBytes",QJsonValue(QJsonValue::Null)},{"budgetBytes",QJsonValue(QJsonValue::Null)}};};
        const QJsonObject caches{{"terrain",cache("terrain")},{"geometry",cache("geometry")},{"hydro",cache("hydro")},{"place",cache("place")},{"labels",cache("label")}};
        const QJsonObject cycleRecord{{"index",cycle++},{"startMs",double(started)},{"endMs",double(clock.elapsed())},{"settled",settled},{"pendingJobs",quality.contains("pendingJobs")?QJsonValue::fromVariant(quality.value("pendingJobs")):QJsonValue(QJsonValue::Null)},
            {"stalePublications",quality.contains("stalePublications")?QJsonValue::fromVariant(quality.value("stalePublications")):QJsonValue(QJsonValue::Null)},
            {"returnState",returnState()},{"resourceCaches",caches}};
        cycles.append(cycleRecord);journal.write(QJsonDocument(cycleRecord).toJson(QJsonDocument::Compact)+'\n');journal.flush();
    }
    phase.store(0);QTest::qWait(1000);
    pulse.stop();sample.stop();
    QObject::disconnect(syncConnection);QObject::disconnect(presentConnection);
    QJsonArray submittedFrames;{std::lock_guard<std::mutex> lock(frameMutex);submittedFrames=presentationFrames;}
    QJsonObject metrics;
    for(const auto& key:cumulativeMetrics) {
        bool supported=true;for(const auto& value:rows)if(value.toObject().value("values").toObject().value(key).isNull())supported=false;
        const auto domain=QString(key).startsWith("sceneGraph")||QString(key).startsWith("geometry")||QString(key).startsWith("upload")||QString(key).startsWith("viewUniform")?"window":QString(key).startsWith("label")?"labels":"controller";
        metrics[key]=QJsonObject{{"unit",QString(key)=="uploadedBytes"?"bytes":"count"},{"kind","cumulative"},{"resetDomain",domain},{"supported",supported}};
    }
    for(const auto& key:editingCounts+editingTimes){const bool supported=editor.property("editingPerformanceStats").toMap().contains(key);
        const bool cumulative=!editingTimes.contains(key)||key.endsWith("TotalMs");
        metrics["editing."+key]=QJsonObject{{"unit",editingTimes.contains(key)?"ms":"count"},{"kind",cumulative?"cumulative":"gauge"},{"resetDomain","controller"},{"supported",supported},{"nullable",true},{"sampleMeaning",cumulative?"production cumulative work in this controller lifetime":"latest production operation duration; periodic snapshots are not event counts"}};}
    metrics["viewportResourceGeneration"]=QJsonObject{{"unit","revision"},{"kind","gauge"},{"resetDomain","project"},{"supported",true}};
    auto provenance=nativePerfJsonFile(qEnvironmentVariable("PANDOEDITOR_NATIVE_PERF_PROVENANCE"));
    provenance["qtVersion"]=qVersion();
    switch(window->rendererInterface()->graphicsApi()) {
        case QSGRendererInterface::Direct3D11:provenance["graphicsApi"]="Direct3D11";break;
        case QSGRendererInterface::Direct3D12:provenance["graphicsApi"]="Direct3D12";break;
        case QSGRendererInterface::OpenGL:provenance["graphicsApi"]="OpenGL";break;
        case QSGRendererInterface::Vulkan:provenance["graphicsApi"]="Vulkan";break;
        case QSGRendererInterface::Metal:provenance["graphicsApi"]="Metal";break;
        case QSGRendererInterface::Software:provenance["graphicsApi"]="Software";break;
        default:provenance["graphicsApi"]="Unknown";break;
    }
    provenance["runId"]=qEnvironmentVariable("PANDOEDITOR_NATIVE_PERF_RUN_ID");
#ifdef QT_NO_DEBUG
    provenance["buildType"]="Release";
#else
    provenance["buildType"]="Debug";
#endif
    QJsonArray fixtureReceipts;
    for(auto it=fixtureContracts.begin();it!=fixtureContracts.end();++it)fixtureReceipts.append(QJsonObject{{"id",it.key()},{"manifestSha256",nativePerfFileHash(manifestPath)},{"fullStack",it.value().value("fullStack")}});
    QJsonObject result{{"schema","pandoeditor-native-performance"},{"version",2},
        {"completed",true},{"measurementMode",mode},{"diagnosticReason",qEnvironmentVariable("PANDOEDITOR_NATIVE_PERF_REASON")},
        {"warmupMs",warmup},{"repeatMs",repeatMs},{"ablation",ablation},
        {"logicalProcessors",logicalProcessors},{"cpuNormalization","process-time/all-active-logical-processors"},
        {"provenance",provenance},{"preflight",preflight},{"fixtures",fixtureReceipts},{"metrics",metrics},{"scenarios",scenarios},{"cycles",cycles},
        {"initialView",initialView},
        {"recovery",!recovery.isEmpty()},{"elapsedMs",double(clock.elapsed())},
        {"samples",rows},{"heartbeatIntervalsMs",heartbeats},{"gpuFrameSamplesMs",frames},
        {"heartbeatEvents",heartbeatEvents},{"inputs",inputs},{"presentationFrames",submittedFrames},{"editingEvents",editingEvents},{"rawInputEvents",rawInputEvents}};
    QSaveFile output(reportPath);QVERIFY(output.open(QIODevice::WriteOnly));
    output.write(QJsonDocument(result).toJson());QVERIFY(output.commit());
    window->grabWindow().save(reportPath+".png");
    window->setProperty("allowClose",true);window->close();
}

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

// Opt-in native-device measurement. It uses the production QML and controller,
// isolated persistence, and never changes the supplied recovery files.
static void runNativePerformanceProbe()
{
    const auto reportPath=qEnvironmentVariable("PANDOEDITOR_NATIVE_PERF_REPORT");
    if(reportPath.isEmpty())QSKIP("Native GPU performance requires an explicit isolated measurement run");
    QTemporaryDir privateData;QVERIFY(privateData.isValid());
    EditorControllerConfig config;
    config.bootstrapWorld=true;config.autosaveEnabled=true;config.projectPreviewEnabled=true;
    config.worldDataRoot=QStringLiteral(PANDOEDITOR_WORLD_ASSET_DIR);
    config.privateProjectPath=privateData.filePath("private.json");
    config.autosaveProjectPath=privateData.filePath("autosave-project.json");
    config.autosaveViewPath=privateData.filePath("autosave-view.json");
    config.appearancePath=privateData.filePath("appearance.json");
    config.projectPreviewCachePath=privateData.filePath("preview");
    const auto recovery=qEnvironmentVariable("PANDOEDITOR_NATIVE_PERF_RECOVERY");
    if(!recovery.isEmpty()) {
        QVERIFY(QFile::copy(recovery,config.autosaveProjectPath));
        const auto view=QFileInfo(recovery).dir().filePath("autosave-view.json");
        if(QFile::exists(view))QVERIFY(QFile::copy(view,config.autosaveViewPath));
    }
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
    std::atomic<qint64> presentedNs{0};
    std::mutex frameMutex;
    QJsonArray presentationFrames;
    qint64 lastPresent=0;int lastPhase=-1;
    const auto syncConnection=QObject::connect(window,&QQuickWindow::afterSynchronizing,window,[&]{
        synchronized.store(requested.load());
    },Qt::DirectConnection);
    const auto presentConnection=QObject::connect(window,&QQuickWindow::frameSwapped,window,[&]{
        ++presents;const auto now=clock.nsecsElapsed();const int current=phase.load();
        presentedNs.store(now);presented.store(synchronized.load());
        std::lock_guard<std::mutex> lock(frameMutex);
        if(lastPhase==current&&current>0)presentationFrames.append(QJsonObject{
            {"phase",current},{"elapsedMs",now/1.e6},{"ms",(now-lastPresent)/1.e6}});
        lastPresent=now;lastPhase=current;
    },Qt::DirectConnection);
    QJsonArray rows,heartbeats,frames,inputs,heartbeatEvents;
    const auto disconnectFrames=qScopeGuard([&]{
        QObject::disconnect(syncConnection);QObject::disconnect(presentConnection);
        QObject::disconnect(gpu,nullptr,&engine,nullptr);
    });
    QObject::connect(gpu,&GpuMapItem::frameSampled,&engine,[&](double ms){frames.append(ms);});
    QFile journal(reportPath+".jsonl");QVERIFY(journal.open(QIODevice::WriteOnly));
    auto cpuMilliseconds=[]() -> double {
#ifdef Q_OS_WIN
        FILETIME created,exit,kernel,user;
        if(!GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&user))return 0;
        ULARGE_INTEGER k,u;k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
        u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;
        return double(k.QuadPart+u.QuadPart)/10000.;
#else
        return 0;
#endif
    };
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
        row["phase"]=phase.load();
        row["viewRevision"]=double(qobject_cast<MapSceneBridge*>(editor.mapSceneBridge())->viewState().revision);
        row["startupBusy"]=editor.startupBusy();row["gpuReady"]=gpu->rendererReady();
        row["backendDiagnostic"]=gpu->diagnostic();row["graphicsApi"]=int(window->rendererInterface()->graphicsApi());
        row["mapWidth"]=map->width();row["mapHeight"]=map->height();row["exposed"]=window->isExposed();
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
        row["activeDownloads"]=resources.value("activeDownloads").toInt();
        row["queuedDownloads"]=resources.value("queuedDownloads").toInt();
        row["cpuPercent"]=100*(cpu-lastCpu)/std::max<qint64>(1,now-lastSample)/std::max(1,QThread::idealThreadCount());
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
    bool customWarmup=false;
    int warmup=qEnvironmentVariableIntValue("PANDOEDITOR_NATIVE_PERF_WARMUP_MS",&customWarmup);
    warmup=customWarmup?std::clamp(warmup,5000,60000):60000;
    QTest::qWait(warmup);
    QVERIFY(window->isExposed());QVERIFY(gpu->rendererReady());
    QVERIFY(presents.load()>0);QVERIFY(gpu->geometryUploadCount()>0);
    QVERIFY(!gpu->uploadsPending());QVERIFY(!editor.startupBusy());
    const auto initialView=QJsonObject::fromVariantMap(editor.mapViewState());
    const auto initialRevision=editor.revision();
    const auto input=[&](int kind,const std::function<void()>& action) {
        phase.store(kind);
        const auto start=clock.nsecsElapsed();const int sequence=++requested;
        QEventLoop presentedLoop;
        QTimer deadline;deadline.setSingleShot(true);deadline.setInterval(5000);
        const auto wake=QObject::connect(window,&QQuickWindow::frameSwapped,&presentedLoop,[&]{
            if(presented.load()>=sequence)presentedLoop.quit();
        },Qt::QueuedConnection);
        QObject::connect(&deadline,&QTimer::timeout,&presentedLoop,&QEventLoop::quit);
        deadline.start();
        action();const auto actionNs=clock.nsecsElapsed()-start;window->update();
        if(presented.load()<sequence&&clock.nsecsElapsed()-start<5'000'000'000LL)presentedLoop.exec();
        QObject::disconnect(wake);
        const bool delivered=presented.load()>=sequence;
        QJsonObject observation{{"event","input"},{"phase",kind},{"elapsedMs",start/1.e6},
            {"ms",((delivered?presentedNs.load():clock.nsecsElapsed())-start)/1.e6},{"presented",delivered}};
        observation["viewRevision"]=double(qobject_cast<MapSceneBridge*>(editor.mapSceneBridge())->viewState().revision);
        observation["actionMs"]=actionNs/1.e6;
        observation["selectedId"]=editor.selectedId();
        if(auto* popup=window->findChild<QObject*>("fileMenu"))observation["menuVisible"]=popup->property("visible").toBool();
        inputs.append(observation);
        journal.write(QJsonDocument(observation).toJson(QJsonDocument::Compact)+'\n');journal.flush();
    };
    const QPoint panStart=map->mapToScene({960,464.5}).toPoint();
    QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,panStart,0);
    const bool brief=qEnvironmentVariableIsSet("PANDOEDITOR_NATIVE_PERF_BRIEF");
    for(int i=0;i<(brief?6:120);++i)input(1,[&]{QTest::mouseMove(window,panStart+QPoint(20+int(120*std::sin(i*.045)),int(25*std::sin(i*.07))),0);});
    QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,panStart,0);
    phase.store(0);QTest::qWait(400);
    for(int i=0;i<(brief?6:90);++i)input(2,[&]{
        QWheelEvent wheel(panStart,window->mapToGlobal(panStart),{},QPoint(0,i<45?5:-5),
                          Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QCoreApplication::sendEvent(window,&wheel);
    });
    phase.store(0);QTest::qWait(400);
    for(int i=0;i<(brief?6:40);++i)input(3,[&]{QTest::mouseMove(window,panStart+QPoint((i%8)*30-120,(i%3)*30-90),0);});
    QVector<QPair<QPoint,QString>> selectionTargets;
    for(int y=140;y<800;y+=90)for(int x=500;x<1450;x+=90) {
        const auto hit=editor.pickObjectScreen(x,y,map->property("globeMode").toBool()?
            map->property("globeZoom").toDouble():map->property("zoom").toDouble());
        if(hit.value("domain").toString()=="territorial")
            selectionTargets.append({map->mapToScene(QPointF(x,y)).toPoint(),hit.value("id").toString()});
    }
    for(int i=0;i<(brief?6:18);++i) {
        auto* toolbar=navigationItem(window->contentItem(),"territorialToolbar");
        const auto blocked=toolbar&&toolbar->isVisible()?toolbar->mapRectToScene(toolbar->boundingRect()):QRectF{};
        auto target=std::find_if(selectionTargets.begin(),selectionTargets.end(),[&](const auto& t){
            return t.second!=editor.selectedId()&&!blocked.adjusted(-12,-12,12,12).contains(t.first);
        });
        QVERIFY2(target!=selectionTargets.end(),"No unobstructed map selection target");
        input(4,[&]{QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,target->first,0);});
        QCOMPARE(editor.selectedId(),target->second);
    }
    auto* menu=navigationItem(window->contentItem(),"fileMenuButton");QVERIFY(menu);
    for(int i=0;i<(brief?6:20);++i)input(5,[&]{
        if(i%2)QTest::keyClick(window,Qt::Key_Escape,Qt::NoModifier,0);
        else QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,menu->mapToScene({menu->width()/2,menu->height()/2}).toPoint(),0);
    });
    phase.store(0);QTest::qWait(1000);
    QCOMPARE(editor.revision(),initialRevision); // Input probe must never edit the copied document.
    pulse.stop();sample.stop();
    QObject::disconnect(syncConnection);QObject::disconnect(presentConnection);
    QJsonObject result{{"schema","pandoeditor-native-performance"},{"version",1},
        {"brief",brief},
        {"warmupMs",warmup},{"ablation",ablation},
        {"initialView",initialView},
        {"recovery",!recovery.isEmpty()},{"elapsedMs",double(clock.elapsed())},
        {"samples",rows},{"heartbeatIntervalsMs",heartbeats},{"gpuFrameSamplesMs",frames},
        {"heartbeatEvents",heartbeatEvents},{"inputs",inputs},{"presentationFrames",presentationFrames},
        {"phaseNames",QJsonArray{"idle","pan","zoom","hover","selection","menu"}}};
    QSaveFile output(reportPath);QVERIFY(output.open(QIODevice::WriteOnly));
    output.write(QJsonDocument(result).toJson());QVERIFY(output.commit());
    window->grabWindow().save(reportPath+".png");
    window->setProperty("allowClose",true);window->close();
}

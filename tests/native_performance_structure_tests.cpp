#include "editorcontroller.h"
#include "commandjobrunner.h"
#include "mapscenebridge.h"
#include "nativeperformancemetrics.h"
#include <QElapsedTimer>
#include <QFile>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <atomic>
#include <cmath>

using namespace pandoeditor;
namespace {
Project sample() {
    Project p;
    ProjectDocument document({
        {"A","Alpha",{{{{0,0},{10,0},{10,10},{0,10},{0,0}}}},0x123456},
        {"B","Beta",{{{{20,0},{30,0},{30,10},{20,10},{20,0}}}},0x654321}},
        {{"countries","Countries"}});
    for(int i=0;i<2;++i) {
        GeometryRef geometry{"point-"+std::to_string(i),1};
        document.geometries.insert(geometry,Geometry{"Point",{{2.+20*i,2}},{},{}});
        PlaceLabel label;label.id="L"+std::to_string(i);label.name="Label "+std::to_string(i);
        label.geometry=geometry;document.labels.push_back(std::move(label));
    }
    p.replace(std::move(document));return p;
}
struct EditorFixture {
    QTemporaryDir directory;
    EditorController editor;
    EditorFixture():editor([&] {
        EditorControllerConfig config;
        config.appearancePath=directory.filePath("appearance.json");return config;
    }()) {}
    QString path() const {return directory.filePath("project.pando.json");}
    bool open() {
        QFile file(path());if(!file.open(QIODevice::WriteOnly))return false;
        const auto bytes=projectcodec::encode(sample());
        if(file.write(bytes)!=bytes.size())return false;file.close();
        if(!editor.openFile(QUrl::fromLocalFile(path())))return false;
        editor.resizeMapCamera(800,600,1);
        editor.setProjectionMode("flat");
        return editor.publishMapView({{"viewportWidth",800},{"viewportHeight",600},
            {"centerLongitude",0},{"centerLatitude",0},{"scale",100},
            {"translateX",400},{"translateY",300}});
    }
    MapSceneBridge* bridge() {return qobject_cast<MapSceneBridge*>(editor.mapSceneBridge());}
};
qulonglong number(const QVariantMap& values,const char* key) {return values.value(key).toULongLong();}
const PolygonDrawPacket* polygon(const RenderScene& scene,const std::string& id) {
    for(const auto& packet:scene.polygons)if(packet.object.id==id)return &packet;return nullptr;
}
const PointDrawPacket* point(const RenderScene& scene,const std::string& id) {
    for(const auto& packet:scene.points)if(packet.object.id==id)return &packet;return nullptr;
}
CommandRequest request(const Project& project) {
    CommandArguments args;args.action=SetCountryColor{"A",0x102030};
    return CommandProcessor::makeRequest(project,"country.color",args);
}
struct Gate {QSemaphore started,release,finished;};
struct Release {std::shared_ptr<Gate> gate;~Release(){gate->release.release();}};
CommandJobRunner::Task blocked(std::shared_ptr<Gate> gate,CommandRequest request) {
    return [gate,request=std::move(request)](const ProjectSnapshot& snapshot,const JobToken&) {
        gate->started.release();gate->release.acquire();
        auto result=CommandProcessor::prepare(snapshot,request);gate->finished.release();return result;
    };
}
void recordMeasured(NativePerformanceMetrics& metrics,const QList<QVariant>& signal) {
    metrics.record("compute",signal[1].toString(),signal[2].toDouble(),
        signal[3].toInt()==int(JobDisposition::Accepted)?"accepted":"discarded",{},signal[0].toULongLong());
}
}

class NativePerformanceStructureTests final:public QObject {
    Q_OBJECT
private slots:
    void cameraChangeRetainsActualSceneAndGeometry() {
        EditorFixture fixture;QVERIFY(fixture.open());auto& editor=fixture.editor;
        auto* bridge=fixture.bridge();QVERIFY(bridge);
        const auto scene=bridge->sceneSnapshot();QVERIFY(scene);QVERIFY(polygon(*scene,"A"));
        const auto stats=editor.renderQuality();const auto bytes=editor.documentBytes();
        const auto publications=bridge->scenePublicationCount(),views=bridge->viewFrameCount();
        QVERIFY(editor.publishMapView({{"translateX",427},{"scale",125}}));
        const auto frame=bridge->frameSnapshot();QVERIFY(frame);
        QVERIFY(frame->scene==scene);QCOMPARE(frame->path,FrameUpdatePath::ViewOnly);
        QCOMPARE(bridge->scenePublicationCount(),publications);QCOMPARE(bridge->viewFrameCount(),views+1);
        for(const char* key:{"scenePreparationCount","sceneDeltaUpdateCount","packetCacheBuilds","packetCacheHits"})
            QCOMPARE(number(editor.renderQuality(),key),number(stats,key));
        QCOMPARE(editor.documentBytes(),bytes);
    }
    void selectionAndHoverPublishInteractionWithoutGeometryPreparation() {
        EditorFixture fixture;QVERIFY(fixture.open());auto& editor=fixture.editor;auto* bridge=fixture.bridge();
        const auto before=bridge->sceneSnapshot();QVERIFY(before);QVERIFY(polygon(*before,"A"));
        const auto stats=editor.renderQuality();const auto frames=bridge->interactionFrameCount();
        const auto bytes=editor.documentBytes();editor.selectCountry("A");
        QVERIFY(editor.setHoverObject({{"domain","territorial"},{"id","B"}},"structure-test"));
        const auto after=bridge->sceneSnapshot();QVERIFY(after!=before);
        QVERIFY(after->preparationIdentity==before->preparationIdentity);
        QVERIFY(polygon(*after,"A"));QVERIFY(polygon(*after,"B"));
        QVERIFY(polygon(*after,"A")->geometryPacket.positions==polygon(*before,"A")->geometryPacket.positions);
        QVERIFY(polygon(*after,"B")->geometryPacket.indices==polygon(*before,"B")->geometryPacket.indices);
        QCOMPARE(after->revisions.geometry,before->revisions.geometry);
        QCOMPARE(bridge->interactionFrameCount(),frames+2);
        for(const char* key:{"scenePreparationCount","packetCacheBuilds","packetCacheHits"})
            QCOMPARE(number(editor.renderQuality(),key),number(stats,key));
        QCOMPARE(editor.documentBytes(),bytes);
    }
    void presentationStyleReusesGeometryStorage() {
        EditorFixture fixture;QVERIFY(fixture.open());auto& editor=fixture.editor;
        const auto before=fixture.bridge()->sceneSnapshot();QVERIFY(before);QVERIFY(polygon(*before,"A"));
        const auto stats=editor.renderQuality();QVERIFY(editor.setPresentationOpacity("countries",.375));
        QCOMPARE(number(editor.renderQuality(),"labelCounterGeneration"),number(stats,"labelCounterGeneration")+1);
        const auto after=fixture.bridge()->sceneSnapshot();QVERIFY(after);QVERIFY(polygon(*after,"A"));
        QVERIFY(after!=before);QVERIFY(after->presentationSignature!=before->presentationSignature);
        QVERIFY(after->revisions.presentation>before->revisions.presentation);
        QCOMPARE(after->revisions.geometry,before->revisions.geometry);
        for(const auto& id:{"A","B"}) {
            QVERIFY(polygon(*after,id));QVERIFY(polygon(*before,id));
            QVERIFY(polygon(*after,id)->geometryPacket.positions==polygon(*before,id)->geometryPacket.positions);
            QVERIFY(polygon(*after,id)->geometryPacket.unitSpherePositions==polygon(*before,id)->geometryPacket.unitSpherePositions);
        }
        QCOMPARE(number(editor.renderQuality(),"packetCacheBuilds"),number(stats,"packetCacheBuilds"));
        QCOMPARE(number(editor.renderQuality(),"scenePreparationCount"),number(stats,"scenePreparationCount"));
        QCOMPARE(number(editor.renderQuality(),"scenePresentationUpdateCount"),number(stats,"scenePresentationUpdateCount")+1);
    }
    void unrelatedLabelTextEditRetainsAllGeometryBuffers() {
        EditorFixture fixture;QVERIFY(fixture.open());auto& editor=fixture.editor;
        QVERIFY(editor.selectObject({{"domain","label"},{"id","L0"}}));
        const auto before=fixture.bridge()->sceneSnapshot();QVERIFY(point(*before,"L0"));QVERIFY(point(*before,"L1"));
        const auto stats=editor.renderQuality();QVERIFY(editor.beginContentEdit("label"));
        QVERIFY(editor.updateContentField("name","Changed label"));
        QVERIFY(editor.previewContentEdit());QVERIFY(editor.confirmContentEdit());
        const auto after=fixture.bridge()->sceneSnapshot();QVERIFY(point(*after,"L0"));QVERIFY(point(*after,"L1"));
        for(const auto& id:{"L0","L1"})
            QVERIFY(point(*after,id)->geometryPacket.positions==point(*before,id)->geometryPacket.positions);
        for(const auto& id:{"A","B"})
            QVERIFY(polygon(*after,id)->geometryPacket.indices==polygon(*before,id)->geometryPacket.indices);
        const auto document=projectcodec::decode(editor.documentBytes());
        QCOMPARE(document.labels[0].name,std::string("Changed label"));QCOMPARE(document.labels[1].name,std::string("Label 1"));
        QCOMPARE(number(editor.renderQuality(),"packetCacheBuilds"),number(stats,"packetCacheBuilds"));
        QCOMPARE(number(editor.renderQuality(),"scenePreparationCount"),number(stats,"scenePreparationCount"));
    }
    void onePointGeometryEditRebuildsOnlyAffectedPacket() {
        EditorFixture fixture;QVERIFY(fixture.open());auto& editor=fixture.editor;
        // The production picker is lazy. Establish its actual spatial index
        // before asserting that an edit updates that existing index locally.
        editor.pickObjectScreen(400,300,1);
        QVERIFY(editor.selectObject({{"domain","label"},{"id","L0"}}));
        QVERIFY(editor.beginContentEdit("label"));QVERIFY(editor.beginContentGeometry());
        const auto vertices=editor.geometryDraftPaths().first().toMap().value("vertices").toList();QVERIFY(!vertices.isEmpty());
        const auto vertex=vertices.first().toMap();const double x=vertex.value("x").toDouble(),y=vertex.value("y").toDouble();
        QVERIFY(editor.geometrySelectNearest(x,y,0));QVERIFY(editor.geometryMoveSelectedVertex(x+15,y+9,0));
        const auto before=fixture.bridge()->sceneSnapshot();QVERIFY(point(*before,"L0"));QVERIFY(point(*before,"L1"));
        const auto stats=editor.renderQuality();QVERIFY(editor.requestGeometryPreview());QVERIFY(editor.confirmGeometryEdit());
        const auto after=fixture.bridge()->sceneSnapshot();QVERIFY(point(*after,"L0"));QVERIFY(point(*after,"L1"));
        QVERIFY(point(*after,"L0")->geometryPacket.positions!=point(*before,"L0")->geometryPacket.positions);
        QVERIFY(*point(*after,"L0")->geometryPacket.positions!=*point(*before,"L0")->geometryPacket.positions);
        QVERIFY(point(*after,"L1")->geometryPacket.positions==point(*before,"L1")->geometryPacket.positions);
        for(const auto& id:{"A","B"})
            QVERIFY(polygon(*after,id)->geometryPacket.positions==polygon(*before,id)->geometryPacket.positions);
        QCOMPARE(number(editor.renderQuality(),"packetCacheBuilds"),number(stats,"packetCacheBuilds")+1);
        QCOMPARE(number(editor.renderQuality(),"scenePreparationCount"),number(stats,"scenePreparationCount")+1);
        QCOMPARE(number(editor.renderQuality(),"lastEditAffectedObjects"),qulonglong(1));
        QCOMPARE(number(editor.renderQuality(),"spatialIncrementalUpdateCount"),number(stats,"spatialIncrementalUpdateCount")+1);
    }
    void controllerActualWorkerRecordsOnceAndReadsDoNotCreateEvents() {
        EditorFixture fixture;QVERIFY(fixture.open());auto& editor=fixture.editor;editor.selectCountry("A");
        editor.setNameDraft("New Alpha");const auto document=editor.documentBytes();
        const auto before=editor.editingPerformanceStats();bool notifiedAfterMeasurement=false;
        connect(&editor,&EditorController::renderQualityChanged,this,[&] {
            notifiedAfterMeasurement|=number(editor.editingPerformanceStats(),"eventSerial")>number(before,"eventSerial");
        });
        QVERIFY(editor.preparePendingEditsAsync());
        QTRY_VERIFY(editor.hasPreparedPreview());QTRY_VERIFY(!editor.jobBusy());
        const auto stats=editor.editingPerformanceStats();
        QCOMPARE(number(stats,"computeCount"),number(before,"computeCount")+1);
        QCOMPARE(number(stats,"eventSerial"),number(before,"eventSerial")+1);
        QCOMPARE(editor.documentBytes(),document);
        const auto event=stats.value("events").toList().last().toMap();
        QCOMPARE(event.value("flow").toString(),QString("editor:properties"));
        QCOMPARE(event.value("stage").toString(),QString("compute"));
        QCOMPARE(event.value("disposition").toString(),QString("accepted"));
        QVERIFY(number(event,"jobId")>0);QVERIFY(event.value("owners").isNull());
        QVERIFY(std::isfinite(event.value("durationMs").toDouble()));QVERIFY(event.value("durationMs").toDouble()>=0);
        QVERIFY2(notifiedAfterMeasurement,"A metrics-bound UI must be notified after the actual worker event is recorded");
        QTRY_COMPARE(number(editor.renderQuality(),"pendingJobs"),qulonglong(0));
        for(int i=0;i<8;++i){editor.renderQuality();QCOMPARE(editor.editingPerformanceStats(),stats);}
        QVERIFY(editor.confirmPreview());QVERIFY(editor.documentBytes()!=document);
    }
    void metricsFollowControllerOwnerLifetimeAndUnobservedRemainNull() {
        EditorFixture fixture;QVERIFY(fixture.open());auto& editor=fixture.editor;editor.selectCountry("A");
        editor.setNameDraft("New Alpha");QVERIFY(editor.preparePendingEdits());QVERIFY(editor.confirmPreview());
        bool undoNotified=false,redoNotified=false;
        connect(&editor,&EditorController::renderQualityChanged,this,[&] {
            const auto stats=editor.editingPerformanceStats();
            undoNotified|=number(stats,"undoCount")==1;redoNotified|=number(stats,"redoCount")==1;
        });
        editor.undo();QVERIFY2(undoNotified,"Undo metrics must be recorded before their notification");
        editor.redo();QVERIFY2(redoNotified,"Redo metrics must be recorded before their notification");
        const auto before=editor.editingPerformanceStats();
        QCOMPARE(number(before,"undoCount"),qulonglong(1));QCOMPARE(number(before,"redoCount"),qulonglong(1));
        const auto sceneCount=fixture.bridge()->scenePublicationCount();const auto oldInstance=editor.projectInstanceId();
        QVERIFY(editor.openFile(QUrl::fromLocalFile(fixture.path())));QVERIFY(editor.projectInstanceId()!=oldInstance);
        QCOMPARE(editor.editingPerformanceStats(),before);QVERIFY(fixture.bridge()->scenePublicationCount()>sceneCount);
        EditorFixture fresh;QVERIFY(fresh.open());const auto empty=fresh.editor.editingPerformanceStats();
        QCOMPARE(empty.value("resetDomain").toString(),QString("controller lifetime; counters cumulative, latest durations gauges"));
        QCOMPARE(number(empty,"eventSerial"),qulonglong(0));QCOMPARE(number(empty,"undoCount"),qulonglong(0));
        for(const char* key:{"computeMs","computeTotalMs","undoMs","redoMs","snapCandidatesExamined","previewLatencyMs","commitLatencyMs","undoLatencyMs"}) {
            QVERIFY(empty.contains(key));QVERIFY(empty.value(key).isNull());
        }
        const auto caches=fresh.editor.renderQuality().value("resourceCaches").toMap();
        QVERIFY(!caches.value("qsg").toMap().value("available").toBool());
        QVERIFY(caches.value("qsg").toMap().value("driverGpuBytes").isNull());
        const auto quality=fresh.editor.renderQuality();QVERIFY(quality.contains("stalePublications"));
        QVERIFY(quality.value("stalePublications").isNull());
    }
    void acceptedWorkerDurationUsesActualMillisecondsAndOwnerSignal() {
        auto project=sample();CommandJobRunner runner([&]()->const Project&{return project;});
        QSignalSpy measured(&runner,&CommandJobRunner::operationMeasured);
        auto inside=std::make_shared<double>(0);auto worker=std::make_shared<std::atomic<bool>>(false);
        QElapsedTimer outside;outside.start();const auto* owner=QThread::currentThread();bool completed=false;
        runner.submit(project.snapshot(),"units",[req=request(project),inside,worker,owner](const ProjectSnapshot& snapshot,const JobToken&) {
            worker->store(QThread::currentThread()!=owner);QElapsedTimer clock;clock.start();PrepareResult result;
            for(int i=0;i<160;++i)result=CommandProcessor::prepare(snapshot,req);
            *inside=double(clock.nsecsElapsed())/1e6;return result;
        },[&](auto,auto disposition,PrepareResult result){QCOMPARE(QThread::currentThread(),runner.thread());QCOMPARE(disposition,JobDisposition::Accepted);QVERIFY(result.preview);completed=true;});
        QTRY_VERIFY(completed);QCOMPARE(measured.size(),1);QVERIFY(worker->load());
        const double milliseconds=measured.first()[2].toDouble();
        QVERIFY(std::isfinite(milliseconds));QVERIFY(*inside>0);QVERIFY(milliseconds>=*inside);
        QVERIFY(milliseconds<=double(outside.nsecsElapsed())/1e6);
        QCOMPARE(measured.first()[1].toString(),QString("units"));QCOMPARE(measured.first()[3].toInt(),int(JobDisposition::Accepted));
        QVERIFY(!project.canUndo());
    }
    void runningCancellationMeasuresOnlyAfterActualWorkerCompletion() {
        auto project=sample();CommandJobRunner runner([&]()->const Project&{return project;});QSignalSpy measured(&runner,&CommandJobRunner::operationMeasured);
        auto gate=std::make_shared<Gate>();Release release{gate};int callbacks=0;
        const auto ticket=runner.submit(project.snapshot(),"cancel",blocked(gate,request(project)),[&](auto,auto disposition,PrepareResult result){++callbacks;QCOMPARE(disposition,JobDisposition::Cancelled);QVERIFY(!result.preview);});
        QTRY_VERIFY(gate->started.available());runner.cancel(ticket.id());QTRY_COMPARE(callbacks,1);QCOMPARE(measured.size(),0);
        gate->release.release();QTRY_COMPARE(measured.size(),1);QCOMPARE(callbacks,1);
        QCOMPARE(measured.first()[3].toInt(),int(JobDisposition::Cancelled));QVERIFY(measured.first()[2].toDouble()>0);
        QVERIFY(!project.canUndo());QCOMPARE(runner.runningCount(),std::size_t(0));
    }
    void staleWorkerIsMeasuredAndNeverPublishesPreparedPayload() {
        auto project=sample();CommandJobRunner runner([&]()->const Project&{return project;});QSignalSpy measured(&runner,&CommandJobRunner::operationMeasured);
        auto gate=std::make_shared<Gate>();Release release{gate};bool completed=false;
        runner.submit(project.snapshot(),"stale",blocked(gate,request(project)),[&](auto,auto disposition,PrepareResult result){QCOMPARE(disposition,JobDisposition::Stale);QVERIFY(!result.preview);completed=true;});
        QTRY_VERIFY(gate->started.available());QVERIFY(project.setMemo("A","Unrelated later edit"));
        gate->release.release();QTRY_VERIFY(completed);QCOMPARE(measured.size(),1);
        QCOMPARE(measured.first()[3].toInt(),int(JobDisposition::Stale));QVERIFY(measured.first()[2].toDouble()>0);
        QVERIFY(project.country("A"));QCOMPARE(project.country("A")->color,std::uint32_t(0x123456));
    }
    void cancelledQueuedTaskHasNoInventedDurationEvent() {
        auto project=sample();CommandJobRunner runner([&]()->const Project&{return project;});QSignalSpy measured(&runner,&CommandJobRunner::operationMeasured);
        auto gate=std::make_shared<Gate>();Release release{gate};int callbacks=0;auto executed=std::make_shared<std::atomic<int>>(0);
        runner.submit(project.snapshot(),"held",blocked(gate,request(project)),[](auto,auto,PrepareResult){});QTRY_VERIFY(gate->started.available());
        const auto queued=runner.submit(project.snapshot(),"queued",[req=request(project),executed](const ProjectSnapshot& snapshot,const JobToken&){++*executed;return CommandProcessor::prepare(snapshot,req);},[&](auto,auto disposition,PrepareResult){QCOMPARE(disposition,JobDisposition::Cancelled);++callbacks;});
        QCOMPARE(runner.queueDepth(),std::size_t(1));runner.cancel(queued.id());QTRY_COMPARE(callbacks,1);
        QCOMPARE(measured.size(),0);QCOMPARE(executed->load(),0);gate->release.release();QTRY_COMPARE(measured.size(),1);
        QCOMPARE(measured.first()[1].toString(),QString("held"));QCOMPARE(executed->load(),0);
    }
    void realWorkerEventsKeepCumulativeTotalsAndBoundedHistory() {
        auto project=sample();CommandJobRunner runner([&]()->const Project&{return project;});QSignalSpy measured(&runner,&CommandJobRunner::operationMeasured);NativePerformanceMetrics metrics;
        connect(&runner,&CommandJobRunner::operationMeasured,this,[&](qulonglong job,QString operation,double milliseconds,int disposition){recordMeasured(metrics,{QVariant::fromValue(job),operation,milliseconds,disposition});});
        int completed=0;
        for(int i=0;i<260;++i) {
            runner.submit(project.snapshot(),"ring:"+std::to_string(i),[req=request(project)](const ProjectSnapshot& snapshot,const JobToken&){return CommandProcessor::prepare(snapshot,req);},[&](auto,auto disposition,PrepareResult result){QCOMPARE(disposition,JobDisposition::Accepted);QVERIFY(result.preview);++completed;});
        }
        QTRY_COMPARE_WITH_TIMEOUT(completed,260,10000);
        QCOMPARE(measured.size(),260);const auto stats=metrics.snapshot();const auto events=stats.value("events").toList();
        QCOMPARE(number(stats,"eventSerial"),qulonglong(260));QCOMPARE(number(stats,"computeCount"),qulonglong(260));
        QCOMPARE(number(stats,"eventsDropped"),qulonglong(4));QCOMPARE(events.size(),256);
        QCOMPARE(number(events.first().toMap(),"eventSerial"),qulonglong(5));QCOMPARE(number(events.last().toMap(),"eventSerial"),qulonglong(260));
        double total=0;for(const auto& signal:measured)total+=signal[2].toDouble();
        QCOMPARE(stats.value("computeTotalMs").toDouble(),total);QCOMPARE(stats.value("computeMs").toDouble(),measured.last()[2].toDouble());
        QCOMPARE(stats.value("computeMs"),events.last().toMap().value("durationMs"));
        for(int i=0;i<8;++i)QCOMPARE(metrics.snapshot(),stats);
        NativePerformanceMetrics fresh;QCOMPARE(number(fresh.snapshot(),"eventSerial"),qulonglong(0));QVERIFY(fresh.snapshot().value("computeMs").isNull());
    }
};
QTEST_MAIN(NativePerformanceStructureTests)
#include "native_performance_structure_tests.moc"

#include "../app/terrainimageprovider.h"
#include "../renderer/terrainrenderowner.h"
#include "../renderer/terrainlayeritem.h"
#include "../renderer/terrainlandmaskitem.h"
#include <pandoeditor/map/mapscenebuilder.h>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QtTest>

// Mechanism fixtures use explicit synthetic image bytes; production Qt/RHI
// observations are exercised separately by the registered scene-graph harness.
class TerrainRenderOwnerTests final : public QObject {
    Q_OBJECT
private slots:
    void immutablePixelIdentityIgnoresAllocationAndIncludesSource() {
        QImage a(4,3,QImage::Format_RGBA8888);a.fill(QColor(12,34,56,255));
        auto b=a.copy();QVERIFY(a.cacheKey()!=b.cacheKey());
        QCOMPARE(TerrainImageBridge::imageContentKey(a,7),TerrainImageBridge::imageContentKey(b,7));
        QVERIFY(TerrainImageBridge::imageContentKey(a,7)!=TerrainImageBridge::imageContentKey(a,8));
        b.setPixelColor(0,0,QColor(13,34,56,255));
        QVERIFY(TerrainImageBridge::imageContentKey(a,7)!=TerrainImageBridge::imageContentKey(b,7));
    }
    void indivisibleUploadStopsRemainingWork() {
        TerrainUploadSchedule schedule; schedule.beginFrame(2*1024*1024,2);
        QVERIFY(schedule.reserve(18*1024*1024));schedule.committed(18*1024*1024,0.25);
        QVERIFY(!schedule.reserve(32));QCOMPARE(schedule.oversizedOperations(),quint64(1));
        schedule.beginFrame(2*1024*1024,2);QVERIFY(schedule.reserve(32));schedule.committed(32,2.1);
        QVERIFY(!schedule.reserve(32));
    }
    void lateInsertRejectsEightFieldsButRetirementUsesOwner() {
        TerrainImageBridge bridge;TerrainRenderInput input;
        input.scope={1,2,3,4,5,6,7,8};bridge.setRenderInput(input);
        QSignalSpy spy(&bridge,&TerrainImageBridge::renderObserved);
        std::uint64_t TerrainDisplayScope::* fields[]={&TerrainDisplayScope::sourceEpoch,&TerrainDisplayScope::windowEpoch,
            &TerrainDisplayScope::contextEpoch,&TerrainDisplayScope::projectGeneration,&TerrainDisplayScope::viewGeneration,
            &TerrainDisplayScope::maskGeneration,&TerrainDisplayScope::candidateSequence,&TerrainDisplayScope::requestSequence};
        for(const auto field:fields) {TerrainRenderObservation o;o.kind=TerrainRenderObservation::UploadSubmitted;
            o.scope=input.scope;--(o.scope.*field);bridge.observeRender(o);}
        QCoreApplication::processEvents();QCOMPARE(spy.size(),0);
        TerrainRenderObservation retired;retired.kind=TerrainRenderObservation::ResourceRetired;
        retired.scope=input.scope;--retired.scope.viewGeneration;bridge.observeRender(retired);
        QTRY_COMPARE(spy.size(),1);
        --retired.scope.contextEpoch;bridge.observeRender(retired);QCoreApplication::processEvents();QCOMPARE(spy.size(),1);
    }
    void adoptionRequiresExactObservedReceipt() {
        TerrainImageBridge bridge;TerrainRenderInput input;input.scope={1,2,3,4,5,6,7,8};bridge.setRenderInput(input);
        TerrainDisplayReceipt receipt{input.scope,17,29,{}};
        bridge.acknowledgeDisplay(receipt,{});QVERIFY(!bridge.renderAdoption());
        TerrainRenderObservation o;o.kind=TerrainRenderObservation::DisplayReceipt;o.scope=input.scope;o.receipt=receipt;
        bridge.observeRender(o);QCoreApplication::processEvents();
        auto wrong=receipt;++wrong.frameSequence;bridge.acknowledgeDisplay(wrong,{});QVERIFY(!bridge.renderAdoption());
        bridge.acknowledgeDisplay(receipt,{});QVERIFY(bridge.renderAdoption());
        ++input.scope.requestSequence;bridge.setRenderInput(input);bridge.acknowledgeDisplay(receipt,{});
        QCOMPARE(bridge.renderAdoption()->receipt.frameSequence,quint64(29));
    }
    void actualQsgBootstrapAndFrameReceipt() {
        // Real Qt scene graph with synthetic pixels: this is execution of the
        // production owner/material path, not production terrain acceptance.
        QQuickWindow window;window.resize(320,240);
        TerrainImageBridge bridge;MapSceneBridge scene;
        auto published=std::make_shared<RenderScene>();published->revision=1;scene.publishScene(published);
        MapViewState view;view.viewportWidth=320;view.viewportHeight=240;
        view.translateX=160;view.translateY=120;view.scale=50;view.revision=1;scene.publishView(view);
        auto* layer=new TerrainLayerItem(window.contentItem());layer->setWidth(320);layer->setHeight(240);
        layer->setSceneBridge(&scene);layer->setTerrainBridge(&bridge);
        std::vector<TerrainRenderObservation> observations;
        connect(&bridge,&TerrainImageBridge::renderObserved,&bridge,[&](TerrainRenderObservation o){observations.push_back(std::move(o));});
        window.show();
        // Windows STARTUPINFO's hidden startup flag applies to the first ShowWindow.
        // A second explicit show exposes the real native test window.
        window.hide();window.show();
        QTRY_VERIFY_WITH_TIMEOUT(bridge.ownerSnapshot().alive,10000);
        const auto owner=bridge.ownerSnapshot();TerrainRenderInput input;
        input.scope={1,owner.windowEpoch,owner.contextEpoch,4,view.revision,6,7,8};input.view=view;input.sceneRevision=1;
        input.tint=QImage(4,2,QImage::Format_RGBA8888);input.tint.fill(QColor(1,2,3,255));
        input.tintContentKey=TerrainImageBridge::imageContentKey(input.tint,1);
        for(int half=0;half<2;++half) {
            TerrainRenderResource r;r.frame.image=QImage(4,4,QImage::Format_RGBA8888);
            r.frame.image.fill(QColor(10,20,30,255));r.frame.levelSize={8,4};r.frame.sourceEpoch=1;
            r.resource={QStringLiteral("synthetic/%1/raster-color").arg(half),
                TerrainImageBridge::imageContentKey(r.frame.image,1),0,{half?0.:-180.,90,half?180.:0.,-90}};
            input.baseResources.push_back({r.resource.key,r.resource.contentKey});input.resources.push_back(r);
        }
        TerrainDisplayState state;TerrainDisplayDemand demand;demand.scope=input.scope;
        for(const auto& r:input.resources){demand.base.push_back(r.resource);demand.target.push_back(r.resource);}
        demand.domains={{-180,90,180,-90}};state.beginDemand(demand);
        for(const auto& r:input.resources)QVERIFY(state.cpuReady(input.scope,r.resource));
        bridge.setRenderInput(input);
        const auto uploaded=[&] {
            std::set<QString> ids;bool tint=false;
            for(const auto& o:observations)if(o.kind==TerrainRenderObservation::UploadSubmitted) {
                for(const auto& r:o.resources)ids.insert(r.key);tint=tint||o.tintContentKey==input.tintContentKey;
            }
            return ids.size()==2&&tint;
        };
        QTRY_VERIFY_WITH_TIMEOUT(uploaded(),10000);
        for(const auto& o:observations) {
            QVERIFY(o.kind!=TerrainRenderObservation::DisplayReceipt);
            if(o.kind==TerrainRenderObservation::UploadSubmitted)for(const auto& r:o.resources)QVERIFY(state.uploadSubmitted(o.scope,r));
        }
        input.candidate=state.buildCandidate();QVERIFY(input.candidate);
        input.protectedResources=state.snapshot().protectedResources;bridge.setRenderInput(input);
        const auto displayed=[&] {return std::any_of(observations.begin(),observations.end(),[](const auto& o){return o.kind==TerrainRenderObservation::DisplayReceipt;});};
        QTRY_VERIFY_WITH_TIMEOUT(displayed(),10000);
        bool submitted=false;
        for(const auto& o:observations) {
            if(o.kind==TerrainRenderObservation::CandidateSubmitted) {
                QVERIFY(o.receipt);QVERIFY(state.submitCandidate(*input.candidate,o.receipt->frameSequence,o.receipt->draws));submitted=true;
            }
            if(o.kind==TerrainRenderObservation::DisplayReceipt) {
                QVERIFY(submitted&&o.receipt);QVERIFY(state.acceptDisplayReceipt(*o.receipt));
                bridge.acknowledgeDisplay(*o.receipt,state.snapshot().protectedResources);
            }
        }
        QVERIFY(submitted);QCOMPARE(bridge.renderStats().uploadOperations,quint64(2));
        QCOMPARE(bridge.renderStats().allocationCount,quint64(2));
        QCOMPARE(bridge.renderStats().logicalResourceCount,quint64(3));
        QCOMPARE(bridge.renderStats().uploadBytes,quint64(4*4*4+4*2*4));
        const auto image=window.grabWindow();QVERIFY(!image.isNull());
        QCOMPARE(image.pixelColor(image.width()/2,image.height()/2),QColor(10,20,30,255));
        // Completed submissions superseded before GUI adoption must not pin a
        // history of obsolete frames. Exercise the actual threaded QSG owner.
        for(int request=0;request<8;++request) {
            ++input.scope.requestSequence;++input.scope.candidateSequence;
            demand.scope=input.scope;state.beginDemand(demand);
            input.candidate=state.buildCandidate();QVERIFY(input.candidate);
            bridge.setRenderInput(input);
            const auto currentDisplayed=[&]{return std::any_of(observations.begin(),observations.end(),[&](const auto& o){
                return o.kind==TerrainRenderObservation::DisplayReceipt&&o.scope==input.scope;});};
            QTRY_VERIFY_WITH_TIMEOUT(currentDisplayed(),10000);
        }
        QVERIFY2(bridge.renderStats().pendingSubmittedFrames<=1,"completed obsolete submissions retain viewport history");
        input.enabled=false;input.releaseResources=true;++input.scope.requestSequence;++input.scope.candidateSequence;
        bridge.setRenderInput(input);
        QTRY_COMPARE_WITH_TIMEOUT(bridge.renderStats().textureNominalBytes,quint64(0),10000);
        QCOMPARE(bridge.renderStats().logicalResourceCount,quint64(0));
        QCOMPARE(bridge.renderStats().cpuImageBytes,quint64(0));
        QCOMPARE(bridge.renderStats().stagingNominalBytes,quint64(0));
        QCOMPARE(bridge.renderStats().meshBytes,quint64(0));
        window.hide();window.releaseResources();
    }
    void actualPressureSourceMaskAndWindowHandoff() {
        // Synthetic pixels exercise the production retained owner, material,
        // physical mask and real QSG receipts; this is not official DEM parity.
        qmlRegisterType<TerrainLandMaskItem>("Pandoeditor.RetainedTerrainTest",1,0,"TerrainLandMaskItem");
        qmlRegisterType<TerrainLayerItem>("Pandoeditor.RetainedTerrainTest",1,0,"TerrainLayerItem");
        TerrainImageBridge bridge;MapSceneBridge scene;GeometryPacketCache cache;MapSceneBuilder builder(cache);
        pandoeditor::ProjectDocument document({{"LAND","Land",{{{{-80,-40},{-20,-40},{-20,20},{-80,20},{-80,-40}}}},0x112233}},{{"countries","Countries"}});
        MapViewState view;view.mode=ProjectionMode::Flat;view.viewportWidth=320;view.viewportHeight=240;
        view.translateX=160;view.translateY=120;view.scale=50;scene.publishView(view);view=scene.viewState();
        scene.publishScene(builder.buildDocument(document,1,view,{},{}));
        QQmlEngine engine;engine.rootContext()->setContextProperty("fixtureScene",&scene);
        engine.rootContext()->setContextProperty("fixtureTerrain",&bridge);
        QQmlComponent component(&engine);
        component.setData(R"qml(import QtQuick
import Pandoeditor.RetainedTerrainTest 1.0
Item {
    width: 320; height: 240
    property bool maskEnabled: false
    TerrainLandMaskItem {
        id: land; width: 320; height: 240; sceneBridge: fixtureScene
        originX: 160; originY: 120; mapScale: 50*Math.PI/180
        mapCosLatitude: 1; mapMinX: 0; mapMaxLatitude: 0
        textureSource: parent.maskEnabled ? maskTexture : null
    }
    ShaderEffectSource {
        id: maskTexture; width: 0; height: 0; sourceItem: land
        sourceRect: Qt.rect(0,0,320,240); textureSize: Qt.size(320,240)
        hideSource: true; live: true; smooth: false; mipmap: false; recursive: false
    }
    TerrainLayerItem {
        width: 320; height: 240; sceneBridge: fixtureScene
        terrainBridge: fixtureTerrain; landMaskSource: land
    }
})qml",QUrl());
        QVERIFY2(component.isReady(),qPrintable(component.errorString()));
        QQuickWindow window;window.resize(320,240);window.setColor(Qt::black);
        std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem*>(component.create()));QVERIFY(root);
        root->setParentItem(window.contentItem());
        std::vector<TerrainRenderObservation> observations;
        connect(&bridge,&TerrainImageBridge::renderObserved,&bridge,[&](TerrainRenderObservation o){observations.push_back(std::move(o));});
        window.show();window.hide();window.show();
        QTRY_VERIFY_WITH_TIMEOUT(bridge.ownerSnapshot().alive,10000);
#ifdef Q_OS_WIN
        QCOMPARE(window.rendererInterface()->graphicsApi(),QSGRendererInterface::Direct3D11);
#endif
        TerrainRenderInput input;const auto owner=bridge.ownerSnapshot();
        input.scope={1,owner.windowEpoch,owner.contextEpoch,4,view.revision,1,1,1};input.view=view;input.sceneRevision=1;
        input.tint=QImage(4,2,QImage::Format_RGBA8888);input.tint.fill(QColor(1,2,3,255));
        input.tintContentKey=TerrainImageBridge::imageContentKey(input.tint,1);input.budgetBytes=96;
        const auto makeResource=[&](int half,quint64 epoch,bool dem) {
            TerrainRenderResource r;r.frame.image=QImage(4,4,QImage::Format_RGBA8888);
            r.frame.image.fill(dem?QColor(50,200,212,255):QColor(10,20,30,255));
            r.frame.levelSize={8,4};r.frame.sourceEpoch=epoch;r.frame.dem=dem;
            r.resource={QString("synthetic/%1/%2").arg(epoch).arg(half),TerrainImageBridge::imageContentKey(r.frame.image,epoch),0,{half?0.:-180.,90,half?180.:0.,-90}};
            if(dem){r.frame.tint=input.tint;r.tintContentKey=input.tintContentKey;}
            return r;
        };
        for(int half=0;half<2;++half){const auto r=makeResource(half,1,false);input.resources.push_back(r);input.baseResources.push_back({r.resource.key,r.resource.contentKey});}
        TerrainDisplayState state;TerrainDisplayDemand demand;demand.scope=input.scope;demand.domains={{-180,90,180,-90}};
        for(const auto& r:input.resources){demand.base.push_back(r.resource);demand.target.push_back(r.resource);}
        state.beginDemand(demand);for(const auto& r:input.resources)QVERIFY(state.cpuReady(input.scope,r.resource));
        std::size_t observed=0;bool validObservations=true,adoptReceipts=true;
        const auto drive=[&] {
            while(observed<observations.size()) {
                const auto o=observations[observed++];
                if(o.scope!=input.scope)continue;
                if(o.kind==TerrainRenderObservation::UploadSubmitted)for(const auto& r:o.resources)validObservations=state.uploadSubmitted(o.scope,r)&&validObservations;
                if(o.kind==TerrainRenderObservation::CandidateSubmitted&&o.receipt) {
                    validObservations=input.candidate&&state.submitCandidate(*input.candidate,o.receipt->frameSequence,o.receipt->draws)&&validObservations;
                }
                if(o.kind==TerrainRenderObservation::DisplayReceipt&&o.receipt) {
                    validObservations=state.acceptDisplayReceipt(*o.receipt)&&validObservations;
                    if(adoptReceipts)bridge.acknowledgeDisplay(*o.receipt,state.snapshot().protectedResources);
                }
            }
            if(!input.candidate)if(const auto candidate=state.buildCandidate()) {
                input.candidate=candidate;input.protectedResources=state.snapshot().protectedResources;bridge.setRenderInput(input);
            }
            return validObservations;
        };
        const auto displayed=[&] {const auto valid=drive();const auto snapshot=state.snapshot();return valid&&snapshot.receiptAccepted&&snapshot.submitted&&snapshot.submitted->scope==input.scope;};
        const auto pixel=[&](QQuickWindow& w,int x,int y) {const auto image=w.grabWindow();return image.isNull()?QColor{}:image.pixelColor(x,y);};
        bridge.setRenderInput(input);QTRY_VERIFY_WITH_TIMEOUT(displayed(),10000);
        QCOMPARE(pixel(window,204,129),QColor(10,20,30,255));QCOMPARE(bridge.renderStats().textureNominalBytes,quint64(96));
        int processed=1;
        auto detail=makeResource(0,1,false);detail.resource.key="synthetic/detail";detail.resource.level=1;
        detail.frame.image=QImage(16,16,QImage::Format_RGBA8888);detail.frame.image.fill(QColor(200,40,30,255));
        detail.resource.contentKey=TerrainImageBridge::imageContentKey(detail.frame.image,1);input.resources.push_back(detail);
        auto prefetch=detail;prefetch.resource.key="synthetic/prefetch";prefetch.prefetch=true;
        prefetch.frame.image.fill(QColor(30,40,200,255));prefetch.resource.contentKey=TerrainImageBridge::imageContentKey(prefetch.frame.image,1);
        input.resources.push_back(prefetch);demand.prefetch={prefetch.resource};
        ++input.scope.requestSequence;++input.scope.candidateSequence;demand.scope=input.scope;demand.target={detail.resource};
        state.beginDemand(demand);QVERIFY(state.cpuReady(input.scope,detail.resource));QVERIFY(state.cpuReady(input.scope,prefetch.resource));input.candidate.reset();
        const auto allocations=bridge.renderStats().allocationCount;bridge.setRenderInput(input);
        QTRY_VERIFY_WITH_TIMEOUT(drive()&&bridge.renderStats().deferredResources>0,10000);
        QCOMPARE(bridge.renderStats().allocationCount,allocations);QCOMPARE(pixel(window,204,129),QColor(10,20,30,255));++processed;
        demand.prefetch.clear();
        // A different source has only half its world reserve ready. Actual old
        // display must survive new allocations and nominal budget overflow.
        ++input.scope.sourceEpoch;++input.scope.requestSequence;++input.scope.candidateSequence;
        input.gray=true;input.requiresTint=true;input.tint.fill(QColor(140,170,120,255));
        input.tintContentKey=TerrainImageBridge::imageContentKey(input.tint,2);
        const auto west=makeResource(0,2,true),east=makeResource(1,2,true);
        input.resources={west};input.baseResources={{west.resource.key,west.resource.contentKey},{east.resource.key,east.resource.contentKey}};
        demand.scope=input.scope;demand.base={west.resource,east.resource};demand.target=demand.base;
        state.beginDemand(demand);QVERIFY(state.cpuReady(input.scope,west.resource));input.candidate.reset();
        input.protectedResources=state.snapshot().protectedResources;bridge.setRenderInput(input);
        QTRY_VERIFY_WITH_TIMEOUT(drive()&&bridge.renderStats().mandatoryOverflowBytes>0,10000);
        QVERIFY(!state.snapshot().receiptAccepted);
        QCOMPARE(pixel(window,204,129),QColor(10,20,30,255));++processed;
        input.resources.push_back(east);QVERIFY(state.cpuReady(input.scope,east.resource));bridge.setRenderInput(input);
        QTRY_VERIFY_WITH_TIMEOUT(drive()&&input.candidate&&bridge.renderStats().uploadWorkPending==false,10000);
        QVERIFY(!state.snapshot().receiptAccepted);
        QCOMPARE(pixel(window,204,129),QColor(10,20,30,255));++processed;
        adoptReceipts=false;
        root->setProperty("maskEnabled",true);window.update();QTRY_VERIFY_WITH_TIMEOUT(displayed(),10000);
        // On the very first authenticated gray display, old sea must be absent.
        // Hold GUI adoption so readback cannot retire the old source first and
        // accidentally conceal a broken same-frame suppression path.
        QVERIFY(bridge.renderStats().textureNominalBytes>input.budgetBytes);
        QCOMPARE(pixel(window,204,129),QColor(Qt::black));QVERIFY(pixel(window,116,129)!=QColor(Qt::black));++processed;
        adoptReceipts=true;QVERIFY(state.snapshot().submitted);
        bridge.acknowledgeDisplay(*state.snapshot().submitted,state.snapshot().protectedResources);
        const auto settledBudget=[&]{const auto valid=drive();const auto stats=bridge.renderStats();return valid&&stats.textureNominalBytes<=input.budgetBytes&&stats.mandatoryOverflowBytes==0&&stats.stagingNominalBytes==0;};
        QTRY_VERIFY_WITH_TIMEOUT(settledBudget(),10000);
        QCOMPARE(bridge.renderStats().mandatoryOverflowBytes,quint64(0));++processed;
        const auto oldReceipt=state.snapshot().submitted;QVERIFY(oldReceipt);
        window.setPersistentSceneGraph(false);window.setPersistentGraphics(false);window.hide();
        root->setParentItem(nullptr);window.releaseResources();
        QTRY_VERIFY_WITH_TIMEOUT(!bridge.ownerSnapshot().alive,10000);
        QQuickWindow replacement;replacement.resize(320,240);replacement.setColor(Qt::black);root->setParentItem(replacement.contentItem());
        replacement.show();replacement.hide();replacement.show();
        QTRY_VERIFY_WITH_TIMEOUT(bridge.ownerSnapshot().alive&&bridge.ownerSnapshot().windowEpoch!=owner.windowEpoch,10000);
        const auto newOwner=bridge.ownerSnapshot();input.scope.windowEpoch=newOwner.windowEpoch;input.scope.contextEpoch=newOwner.contextEpoch;
        ++input.scope.requestSequence;++input.scope.candidateSequence;demand.scope=input.scope;state.beginDemand(demand);
        QVERIFY(!state.acceptDisplayReceipt(*oldReceipt));
        for(const auto& r:input.resources)QVERIFY(state.cpuReady(input.scope,r.resource));
        input.candidate.reset();input.protectedResources=state.snapshot().protectedResources;bridge.setRenderInput(input);
        QTRY_VERIFY_WITH_TIMEOUT(displayed(),10000);
        QVERIFY(bridge.renderStats().uploadOperations>=2);QCOMPARE(pixel(replacement,204,129),QColor(Qt::black));++processed;
        input.enabled=false;input.releaseResources=true;++input.scope.requestSequence;++input.scope.candidateSequence;bridge.setRenderInput(input);
        QTRY_COMPARE_WITH_TIMEOUT(bridge.renderStats().textureNominalBytes,quint64(0),10000);
        QCOMPARE(bridge.renderStats().cpuImageBytes,quint64(0));QCOMPARE(bridge.renderStats().meshBytes,quint64(0));++processed;
        qInfo().noquote()<<QString("RETAINED_TERRAIN_HANDOFF pid=%1 expected=8 processed=%2 fail=0 skip=0 syntheticPixels=true").arg(QCoreApplication::applicationPid()).arg(processed);
        QCOMPARE(processed,8);root->setParentItem(nullptr);replacement.hide();replacement.releaseResources();
    }
};
QTEST_MAIN(TerrainRenderOwnerTests)
#include "terrain_render_owner_tests.moc"

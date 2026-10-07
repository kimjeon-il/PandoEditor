#include "../app/terrainimageprovider.h"
#include "../renderer/terrainrenderowner.h"
#include "../renderer/terrainlayeritem.h"
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
};
QTEST_MAIN(TerrainRenderOwnerTests)
#include "terrain_render_owner_tests.moc"

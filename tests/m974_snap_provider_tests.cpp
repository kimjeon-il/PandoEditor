#include "geometrysnapprovider.h"
#include "commandjobrunner.h"
#include <QtTest>
using namespace pandoeditor;
class M974SnapProviderTests : public QObject {
    Q_OBJECT
    static Geometry square(double x=0,double y=0){Geometry g;g.type="Polygon";g.polygons={{{{x,y},{x+2,y},{x+2,y+2},{x,y+2},{x,y}}}};return g;}
    static void pair(Project& p){p.replace(std::vector<Country>{{"a","a",square().polygons,0xabcdef},{"b","b",square().polygons,0xabcdef}});}
    static bool apply(Project& p,const char* id,CommandAction action){CommandArguments args;args.action=std::move(action);auto prepared=CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,id,args));return prepared.preview&&CommandProcessor::confirm(p,*prepared.preview).ok();}
    static bool removeA(Project& p){const auto planned=CommandProcessor::planTerritorial(p,DeleteTerritorialIntent{{territorialRef("a")}});return planned.plan&&apply(p,"territorial.delete",ApplyTerritorialMutation{*planned.plan,{}});}
    static void populate(Project& p){p.replace(std::vector<Country>{{"A","A",{{{{0,0},{2,0},{2,2},{0,2},{0,0}}}},0xabcdef}});}
private slots:
    void stoppedWorkerDefersRebaseAndIgnoresRootPatches(){
        Project p;pair(p);CommandJobRunner runner([&]()->const Project&{return p;});geometrysnap::Provider provider(runner);
        provider.synchronizeInstallation(p.snapshot());QVERIFY(removeA(p));provider.synchronizeSources(p.snapshot());QVERIFY(p.undo());provider.synchronizeSources(p.snapshot());
        const auto reordered=provider.sourceRanks();QVERIFY(reordered->at(territorialRef("b"))<reordered->at(territorialRef("a")));
        const auto oldEpoch=provider.beginWorkerOperation(p.snapshot());provider.notifyWorkerStopped();QCOMPARE(provider.sourceRanks().get(),reordered.get());
        const auto beforeDelete=p.snapshot();QVERIFY(removeA(p));
        QVERIFY(!provider.requiresImmediateSynchronization(p.snapshot(),calculateChangeImpact(beforeDelete.document(),p.document())));
        provider.synchronizeSources(p.snapshot());provider.synchronizeInstallation(p.snapshot());QCOMPARE(provider.sourceRanks().get(),reordered.get());
        QVERIFY(p.undo());provider.synchronizeSources(p.snapshot());QCOMPARE(provider.sourceRanks().get(),reordered.get());
        QVERIFY(!provider.completeWorkerOperation(p.snapshot(),oldEpoch));QCOMPARE(provider.sourceRanks().get(),reordered.get());
        const auto newEpoch=provider.beginWorkerOperation(p.snapshot());QVERIFY(newEpoch!=oldEpoch);const auto rebased=provider.sourceRanks();
        QVERIFY(rebased.get()!=reordered.get());QVERIFY(rebased->at(territorialRef("a"))<rebased->at(territorialRef("b")));
        QVERIFY(reordered->at(territorialRef("b"))<reordered->at(territorialRef("a")));
        QVERIFY(!provider.completeWorkerOperation(p.snapshot(),oldEpoch));QCOMPARE(provider.sourceRanks().get(),rebased.get());
        QVERIFY(provider.completeWorkerOperation(p.snapshot(),newEpoch));QCOMPARE(provider.sourceRanks().get(),rebased.get());
    }
    void stoppedWorkerKeepsReadyCacheUntilGenuineRequest(){
        Project p;pair(p);CommandJobRunner runner([&]()->const Project&{return p;});geometrysnap::Provider provider(runner);
        provider.synchronizeSources(p.snapshot());QVERIFY(removeA(p));provider.synchronizeSources(p.snapshot());QVERIFY(p.undo());provider.synchronizeSources(p.snapshot());
        geometrysnap::Request q;q.coordinate={.05,.05};q.margin=.2;provider.candidates(p.snapshot(),q,"draw",.1);QTRY_COMPARE(provider.status(),QString("ready"));
        QCOMPARE(provider.candidates(p.snapshot(),q,"draw",.1).front().ownerIds.front(),std::string("b"));const auto reordered=provider.sourceRanks();
        const auto submitted=provider.submittedCount();const auto diagnostics=provider.diagnostics();provider.notifyWorkerStopped();
        for(int i=0;i<50;++i){QCOMPARE(provider.status(),QString("ready"));QCOMPARE(provider.candidates(p.snapshot(),q,"draw",.1).front().ownerIds.front(),std::string("b"));QCOMPARE(provider.sourceRanks().get(),reordered.get());}
        QCOMPARE(provider.submittedCount(),submitted);QCOMPARE(provider.diagnostics().geometryIndexBuilds,diagnostics.geometryIndexBuilds);QCOMPARE(provider.diagnostics().segmentEntriesExamined,diagnostics.segmentEntriesExamined);
        q.coordinate={.15,.05};QVERIFY(provider.candidates(p.snapshot(),q,"draw",.1).empty());QTRY_COMPARE(provider.status(),QString("ready"));
        QCOMPARE(provider.submittedCount(),submitted+1);QCOMPARE(provider.candidates(p.snapshot(),q,"draw",.1).front().ownerIds.front(),std::string("a"));
        QVERIFY(provider.sourceRanks().get()!=reordered.get());QCOMPARE(provider.diagnostics().geometryIndexBuilds,std::size_t(0));
    }
    void stoppedPendingSnapCannotInstallAndReplacementOverridesStop(){
        Project p;pair(p);CommandJobRunner runner([&]()->const Project&{return p;});geometrysnap::Provider provider(runner);
        const auto oldSnapshot=p.snapshot();const auto oldEpoch=provider.beginWorkerOperation(oldSnapshot);const auto oldRanks=provider.sourceRanks();
        geometrysnap::Request q;q.coordinate={.05,.05};q.margin=.2;provider.candidates(p.snapshot(),q,"draw",.1);QCOMPARE(provider.status(),QString("pending"));
        provider.notifyWorkerStopped();QCOMPARE(provider.status(),QString("empty"));QCOMPARE(provider.sourceRanks().get(),oldRanks.get());
        QVERIFY(removeA(p));provider.synchronizeSources(p.snapshot());QCOMPARE(provider.sourceRanks().get(),oldRanks.get());
        pair(p);provider.synchronizeInstallation(p.snapshot());const auto replacement=provider.sourceRanks();QVERIFY(replacement.get()!=oldRanks.get());
        QVERIFY(!provider.completeWorkerOperation(oldSnapshot,oldEpoch));QCOMPARE(provider.sourceRanks().get(),replacement.get());
        const auto before=p.snapshot();QVERIFY(removeA(p));QVERIFY(provider.requiresImmediateSynchronization(p.snapshot(),calculateChangeImpact(before.document(),p.document())));
        provider.synchronizeSources(p.snapshot());QCOMPARE(provider.sourceRanks()->count(territorialRef("a")),std::size_t(0));QVERIFY(p.undo());provider.synchronizeSources(p.snapshot());
        provider.candidates(p.snapshot(),q,"draw",.1);QTRY_COMPARE(provider.status(),QString("ready"));QCOMPARE(provider.candidates(p.snapshot(),q,"draw",.1).front().ownerIds.front(),std::string("b"));
    }
    void fulfilledOperationObservesCurrentSourcesWithoutAdoptingOldRanks(){
        Project p;pair(p);CommandJobRunner runner([&]()->const Project&{return p;});geometrysnap::Provider provider(runner);
        const auto initial=p.snapshot();const auto epoch=provider.beginWorkerOperation(initial);const auto oldRanks=provider.sourceRanks();QVERIFY(removeA(p));
        QVERIFY(provider.completeWorkerOperation(p.snapshot(),epoch));const auto removed=provider.sourceRanks();QCOMPARE(removed->count(territorialRef("a")),std::size_t(0));QVERIFY(oldRanks->count(territorialRef("a")));
        QVERIFY(!provider.completeWorkerOperation(initial,epoch));QCOMPARE(provider.sourceRanks().get(),removed.get());
        QVERIFY(p.undo());QVERIFY(provider.completeWorkerOperation(p.snapshot(),epoch));const auto restored=provider.sourceRanks();QVERIFY(restored->at(territorialRef("b"))<restored->at(territorialRef("a")));
        QVERIFY(p.renameCountry("a","Renamed"));QVERIFY(provider.completeWorkerOperation(p.snapshot(),epoch));QCOMPARE(provider.sourceRanks().get(),restored.get());
    }
    void installationDefersGenericTransitionsUntilQueryOrRootGeometrySync(){
        ProjectDocument document(std::vector<Country>{{"root","root",square(10).polygons,0xabcdef}},{{"countries","Countries"}});
        for(const auto id:{"ga","gb"}){GenericFeature f;f.id=id;f.geometry={id,1};document.geometries.insert(f.geometry,square());document.genericFeatures.push_back(f);}
        Project p;p.replace(document);CommandJobRunner runner([&]()->const Project&{return p;});geometrysnap::Provider provider(runner);provider.synchronizeInstallation(p.snapshot());const auto initial=provider.sourceRanks();QVERIFY(initial);
        const auto beforeDelete=p.snapshot();QVERIFY(apply(p,"content.edit",ContentEdit{{"generic","ga"},{},{},false}));QVERIFY(!provider.requiresImmediateSynchronization(p.snapshot(),calculateChangeImpact(beforeDelete.document(),p.document())));provider.synchronizeInstallation(p.snapshot());QCOMPARE(provider.sourceRanks().get(),initial.get());QVERIFY(provider.sourceRanks()->count({"generic","ga"}));
        const auto beforeRename=p.snapshot();QVERIFY(p.renameCountry("root","Renamed"));QVERIFY(!provider.requiresImmediateSynchronization(p.snapshot(),calculateChangeImpact(beforeRename.document(),p.document())));provider.synchronizeInstallation(p.snapshot());QCOMPARE(provider.sourceRanks().get(),initial.get());
        const auto beforeGeometry=p.snapshot();const auto plan=CommandProcessor::planTerritorial(p,ReplaceGeometryIntent{territorialRef("root")});QVERIFY(plan.plan);GeometryPatch patch;patch.sourceRevision=p.revision();patch.replacements.push_back({territorialRef("root"),square(20)});QVERIFY(apply(p,"territorial.geometry.commit",ApplyTerritorialMutation{*plan.plan,patch}));QVERIFY(provider.requiresImmediateSynchronization(p.snapshot(),calculateChangeImpact(beforeGeometry.document(),p.document())));provider.synchronizeSources(p.snapshot());const auto deleted=provider.sourceRanks();QVERIFY(deleted.get()!=initial.get());QCOMPARE(deleted->count({"generic","ga"}),std::size_t(0));QVERIFY(initial->count({"generic","ga"}));
        // Root Undo synchronizes the still-deleted generic membership. Generic
        // restoration alone waits for the next query, preserving old requests.
        auto before=p.snapshot();QVERIFY(p.undo());QVERIFY(provider.requiresImmediateSynchronization(p.snapshot(),calculateChangeImpact(before.document(),p.document())));provider.synchronizeSources(p.snapshot());QVERIFY(p.undo());provider.synchronizeInstallation(p.snapshot());before=p.snapshot();QVERIFY(p.undo());QVERIFY(!provider.requiresImmediateSynchronization(p.snapshot(),calculateChangeImpact(before.document(),p.document())));provider.synchronizeInstallation(p.snapshot());QCOMPARE(provider.sourceRanks().get(),deleted.get());
        geometrysnap::Request q;q.coordinate={0,0};q.margin=.2;provider.candidates(p.snapshot(),q,"draw",.1);QTRY_COMPARE(provider.status(),QString("ready"));QCOMPARE(provider.candidates(p.snapshot(),q,"draw",.1).front().ownerIds.front(),std::string("gb"));const auto queried=provider.sourceRanks();for(int i=0;i<50;++i){provider.synchronizeInstallation(p.snapshot());provider.candidates(p.snapshot(),q,"draw",.1);QCOMPARE(provider.sourceRanks().get(),queried.get());}QCOMPARE(provider.submittedCount(),std::uint64_t(1));
        p.replace(document);provider.synchronizeInstallation(p.snapshot());QVERIFY(provider.sourceRanks().get()!=queried.get());QVERIFY(provider.sourceRanks()->at({"generic","ga"})<provider.sourceRanks()->at({"generic","gb"}));QCOMPARE(provider.status(),QString("empty"));
    }
    void sourceTransitionsWithoutQueriesRetainImmutableRanks(){
        Project p;pair(p);CommandJobRunner runner([&]()->const Project&{return p;});geometrysnap::Provider provider(runner);provider.synchronizeSources(p.snapshot());const auto initial=p.snapshot();const auto oldRanks=provider.sourceRanks();QVERIFY(oldRanks);
        geometrysnap::Request q;q.coordinate={0,0};q.margin=.2;provider.candidates(p.snapshot(),q,"draw",.1);QTRY_COMPARE(provider.status(),QString("ready"));QCOMPARE(provider.candidates(p.snapshot(),q,"draw",.1).front().ownerIds.front(),std::string("a"));
        QVERIFY(removeA(p));provider.synchronizeSources(p.snapshot());QCOMPARE(provider.sourceRanks()->count(territorialRef("a")),std::size_t(0));QVERIFY(p.undo());provider.synchronizeSources(p.snapshot());const auto latest=provider.sourceRanks();QVERIFY(latest.get()!=oldRanks.get());QVERIFY(latest->at(territorialRef("b"))<latest->at(territorialRef("a")));QVERIFY(oldRanks->at(territorialRef("a"))<oldRanks->at(territorialRef("b")));QVERIFY_EXCEPTION_THROWN(provider.synchronizeSources(initial),std::invalid_argument);QCOMPARE(provider.sourceRanks().get(),latest.get());
        provider.candidates(p.snapshot(),q,"draw",.1);QTRY_COMPARE(provider.status(),QString("ready"));QCOMPARE(provider.candidates(p.snapshot(),q,"draw",.1).front().ownerIds.front(),std::string("b"));
        geometrysnap::Index index;q.sourceRanks=oldRanks;q.sourceRanksInstance=initial.instanceId();q.sourceRanksRevision=initial.revision();QCOMPARE(index.prepareAndCollect(initial,q).candidates.front().ownerIds.front(),std::string("a"));QCOMPARE(provider.sourceRanks().get(),latest.get());
        QVERIFY_EXCEPTION_THROWN(index.prepareAndCollect(p.snapshot(),q),std::invalid_argument);
        q.sourceRanks=latest;q.sourceRanksInstance=p.instanceId();q.sourceRanksRevision=p.revision();QCOMPARE(index.prepareAndCollect(p.snapshot(),q).candidates.front().ownerIds.front(),std::string("b"));
        auto missing=std::make_shared<geometrysnap::SourceRanks>(*latest);missing->erase(territorialRef("a"));q.sourceRanks=missing;QVERIFY_EXCEPTION_THROWN(index.prepareAndCollect(p.snapshot(),q),std::invalid_argument);
        auto duplicate=std::make_shared<geometrysnap::SourceRanks>(*latest);(*duplicate)[territorialRef("a")]=duplicate->at(territorialRef("b"));q.sourceRanks=duplicate;QVERIFY_EXCEPTION_THROWN(index.prepareAndCollect(p.snapshot(),q),std::invalid_argument);
        auto high=std::make_shared<geometrysnap::SourceRanks>(*latest);(*high)[territorialRef("a")]=std::numeric_limits<std::uint64_t>::max();(*high)[territorialRef("b")]=std::numeric_limits<std::uint64_t>::max()-1;q.sourceRanks=high;QCOMPARE(index.prepareAndCollect(p.snapshot(),q).candidates.front().ownerIds.front(),std::string("b"));
    }
    void interleavedAdditionsAndContentUpdatesDoNotCopyWarmRanks(){
        ProjectDocument d(std::vector<Country>{{"t1","t1",square().polygons,0xabcdef}},{{"countries","Countries"}});
        const auto add=[&](std::string id,Geometry geometry){GenericFeature f;f.id=id;f.geometry={id,1};d.geometries.insert(f.geometry,std::move(geometry));d.genericFeatures.push_back(f);};add("g1",square());for(int i=0;i<500;++i)add("far"+std::to_string(i),square(40+(i%40)*2,20+((i/40)%20)*2));
        Project p;p.replace(d);CommandJobRunner runner([&]()->const Project&{return p;});geometrysnap::Provider provider(runner);provider.synchronizeSources(p.snapshot());const auto initial=provider.sourceRanks();QVERIFY(initial);QVERIFY(p.renameCountry("t1","Renamed"));provider.synchronizeSources(p.snapshot());QCOMPARE(provider.sourceRanks().get(),initial.get());
        auto updated=p.document().genericFeatures.front();updated.geometry={"g1",2};QVERIFY(apply(p,"content.edit",ContentEdit{{"generic","g1"},updated,std::make_pair(updated.geometry,square(.01)),false}));provider.synchronizeSources(p.snapshot());QCOMPARE(provider.sourceRanks().get(),initial.get());
        CreateTerritorialIntent intent;intent.kind=UnitKind::General;intent.id="t2";intent.name="t2";intent.geometry=square();intent.coverageMode="explicit";const auto plan=CommandProcessor::planTerritorial(p,intent);QVERIFY(plan.plan);QVERIFY(apply(p,"territorial.create",ApplyTerritorialMutation{*plan.plan,{}}));provider.synchronizeSources(p.snapshot());
        GisGenericInput g;g.id="g2";g.name="g2";g.geometry=square();const auto importPlan=planGenericGisImport(p.snapshot(),"order-import",{"order.geojson","geojson"},{g});QVERIFY(apply(p,"gis.import.generic",importPlan));provider.synchronizeSources(p.snapshot());const auto ranks=provider.sourceRanks();QVERIFY(ranks->at(territorialRef("t1"))<ranks->at({"generic","g1"}));QVERIFY(ranks->at({"generic","g1"})<ranks->at(territorialRef("t2")));QVERIFY(ranks->at(territorialRef("t2"))<ranks->at({"generic","g2"}));
        geometrysnap::Request q;q.coordinate={.05,.05};q.margin=.2;for(int i=0;i<50;++i){provider.synchronizeSources(p.snapshot());provider.candidates(p.snapshot(),q,"draw",.1);QCOMPARE(provider.sourceRanks().get(),ranks.get());}QCOMPARE(provider.submittedCount(),std::uint64_t(1));QTRY_COMPARE(provider.status(),QString("ready"));q.coordinate={.15,.05};provider.candidates(p.snapshot(),q,"draw",.1);QTRY_COMPARE(provider.status(),QString("ready"));QCOMPARE(provider.sourceRanks().get(),ranks.get());QCOMPARE(provider.diagnostics().geometryIndexBuilds,std::size_t(0));QCOMPARE(provider.diagnostics().nearbyObjects,std::size_t(4));QVERIFY(provider.diagnostics().segmentEntriesExamined<=8);
    }
    void pendingOldWorkAndReplacementCannotRollBackSourceRanks(){
        Project p;pair(p);CommandJobRunner runner([&]()->const Project&{return p;});geometrysnap::Provider provider(runner);provider.synchronizeSources(p.snapshot());const auto initial=provider.sourceRanks();QVERIFY(initial);geometrysnap::Request q;q.coordinate={0,0};q.margin=.2;provider.candidates(p.snapshot(),q,"draw",.1);
        QVERIFY(removeA(p));provider.synchronizeSources(p.snapshot());QVERIFY(p.undo());provider.synchronizeSources(p.snapshot());const auto restored=provider.sourceRanks();provider.candidates(p.snapshot(),q,"draw",.1);QTRY_COMPARE(provider.status(),QString("ready"));QCOMPARE(provider.sourceRanks().get(),restored.get());QCOMPARE(provider.candidates(p.snapshot(),q,"draw",.1).front().ownerIds.front(),std::string("b"));
        q.coordinate={.15,.05};provider.candidates(p.snapshot(),q,"draw",.1);pair(p);provider.synchronizeSources(p.snapshot());const auto replacement=provider.sourceRanks();QVERIFY(replacement.get()!=restored.get());QVERIFY(replacement->at(territorialRef("a"))<replacement->at(territorialRef("b")));q.coordinate={0,0};provider.candidates(p.snapshot(),q,"draw",.1);QTRY_COMPARE(provider.status(),QString("ready"));QCOMPARE(provider.sourceRanks().get(),replacement.get());QCOMPARE(provider.candidates(p.snapshot(),q,"draw",.1).front().ownerIds.front(),std::string("a"));
    }
    void coldPendingReadyAndCellChange(){
        Project p;populate(p);CommandJobRunner runner([&]()->const Project&{return p;});geometrysnap::Provider provider(runner);
        geometrysnap::Request request;request.coordinate={.05,.05};request.margin=.2;
        QVERIFY(provider.candidates(p.snapshot(),request,"draw",.1).empty());QCOMPARE(provider.status(),QString("pending"));QCOMPARE(provider.submittedCount(),1u);
        QVERIFY(provider.candidates(p.snapshot(),request,"draw",.1).empty());QCOMPARE(provider.submittedCount(),1u);
        QTRY_COMPARE(provider.status(),QString("ready"));QVERIFY(!provider.candidates(p.snapshot(),request,"draw",.1).empty());QCOMPARE(provider.submittedCount(),1u);
        request.coordinate={50,50};QVERIFY(provider.candidates(p.snapshot(),request,"draw",.1).empty());QCOMPARE(provider.status(),QString("pending"));
        QTRY_COMPARE(provider.status(),QString("ready"));QVERIFY(provider.candidates(p.snapshot(),request,"draw",.1).empty());QCOMPARE(provider.submittedCount(),2u);
    }
    void supersededAndResetCannotInstallOldCandidates(){
        Project p;populate(p);CommandJobRunner runner([&]()->const Project&{return p;});geometrysnap::Provider provider(runner);
        geometrysnap::Request request;request.coordinate={.05,.05};request.margin=.2;provider.candidates(p.snapshot(),request,"draw",.1);
        request.coordinate={50,50};provider.candidates(p.snapshot(),request,"draw",.1);QTRY_COMPARE(provider.status(),QString("ready"));
        QVERIFY(provider.candidates(p.snapshot(),request,"draw",.1).empty());
        request.coordinate={.05,.05};provider.candidates(p.snapshot(),request,"draw",.1);provider.reset();QTest::qWait(30);QCOMPARE(provider.status(),QString("empty"));
        QVERIFY(provider.candidates(p.snapshot(),request,"draw",.1).empty());QTRY_COMPARE(provider.status(),QString("ready"));QVERIFY(!provider.candidates(p.snapshot(),request,"draw",.1).empty());
    }
    void supersededFailureCannotClearNewReadyEntry(){
        Project p;populate(p);CommandJobRunner runner([&]()->const Project&{return p;});geometrysnap::Provider provider(runner);
        geometrysnap::Request bad;bad.coordinate={.05,.05};bad.margin=-1;provider.candidates(p.snapshot(),bad,"draw",.1);
        auto next=bad;next.coordinate={.15,.05};next.margin=.2;provider.candidates(p.snapshot(),next,"draw",.1);
        QTRY_COMPARE(provider.status(),QString("ready"));QVERIFY(!provider.candidates(p.snapshot(),next,"draw",.1).empty());QCOMPARE(provider.submittedCount(),2u);
    }
    void immutableSourceRetentionAndDestruction(){
        Project p;populate(p);CommandJobRunner runner([&]()->const Project&{return p;});auto provider=new geometrysnap::Provider(runner);
        const auto geometry=p.document().geometries.get(staticGeometryBinding(p.document(),"A").geometryRef);
        const auto first=provider->retainSource(*geometry,"first"),again=provider->retainSource(*geometry,"first");QCOMPARE(first.get(),again.get());const auto firstKey=provider->sourceKey();
        auto changed=*geometry;changed.polygons[0][0][1].x=3;const auto second=provider->retainSource(changed,"second");QVERIFY(second.get()!=first.get());QVERIFY(provider->sourceKey()!=firstKey);QCOMPARE(first->polygons[0][0][1].x,2.);
        geometrysnap::Request request;request.coordinate={.05,.05};request.margin=.2;provider->candidates(p.snapshot(),request,"draw",.1);delete provider;QTest::qWait(40);
        QVERIFY(p.document().geometries.get(staticGeometryBinding(p.document(),"A").geometryRef)==geometry);
    }
    void currentFailureCanRetryAndProjectReplacementCannotInstall(){
        Project p;populate(p);CommandJobRunner runner([&]()->const Project&{return p;});geometrysnap::Provider provider(runner);
        geometrysnap::Request request;request.coordinate={.05,.05};request.margin=-1;provider.candidates(p.snapshot(),request,"draw",.1);
        QTRY_COMPARE(provider.status(),QString("empty"));const auto count=provider.submittedCount();request.margin=.2;
        QVERIFY(provider.candidates(p.snapshot(),request,"draw",.1).empty());QCOMPARE(provider.submittedCount(),count+1);populate(p);
        QTRY_COMPARE(provider.status(),QString("empty"));QVERIFY(provider.candidates(p.snapshot(),request,"draw",.1).empty());QTRY_COMPARE(provider.status(),QString("ready"));
    }
};
QTEST_GUILESS_MAIN(M974SnapProviderTests)
#include "m974_snap_provider_tests.moc"

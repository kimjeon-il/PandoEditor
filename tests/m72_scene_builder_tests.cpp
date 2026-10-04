#include "territorial_fixture.h"
#include <pandoeditor/map/mapscenebuilder.h>
#include <pandoeditor/commands.h>
#include <pandoeditor/presentationcommands.h>
#include <algorithm>
#include <stdexcept>
#include <cmath>
#include <iostream>

namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
pandoeditor::Geometry square(double west) {
    return {"Polygon",{},{},{{{{west,0},{west+1,0},{west+1,1},{west,1},{west,0}}}}};
}
pandoeditor::ProjectDocument sample() {
    pandoeditor::ProjectDocument doc;
    appendTerritory(doc,{"C","Country",{},pandoeditor::UnitKind::General,false},{"C",1});
    appendTerritory(doc,{"S","Subunit",{},pandoeditor::UnitKind::General,false},{"S",1});
    appendTerritory(doc,{"R","Region",{},pandoeditor::UnitKind::Regional,false},{"R",1});
    for(const auto& id:{"C","S","R"})doc.geometries.insert({id,1},square(0));
    doc.presentation.webPresentation.objectOrder={"territorial:entity:R","territorial:entity:S"};
    return doc;
}
void builderPreservesM5DrawOrderAndCache() {
    auto doc=sample();MapViewState view;GeometryPacketCache cache;MapSceneBuilder builder(cache);
    auto first=builder.buildDocument(doc,3,view,{},{});
    require(first->polygons.size()==3&&first->drawSequence.size()>=3,"country subunit region packets");
    require(cache.resourceCacheSnapshot().activeBytes>0,"published resident geometry activity is measured");
    require(first->drawSequence.front().order.pass==0,"country fill behind overlays");
    for(std::size_t i=1;i<first->drawSequence.size();++i)
        require(!(first->drawSequence[i].order<first->drawSequence[i-1].order),"M5 order sorted");
    const auto firstPositions=first->polygons.front().geometryPacket.positions;
    doc.presentation.objectStyles[{"territorial","C"}].color=0xaabbcc;
    auto restyled=builder.buildDocument(doc,3,view,{},first);
    require(restyled->polygons.front().geometryPacket.positions==firstPositions,"presentation reuses geometry");
    require(restyled->revision==first->revision+1,"scene changes after presentation");
    view.revision=1;view.centerLongitude=10;
    auto panned=builder.buildDocument(doc,3,view,{},restyled);
    require(panned->polygons.front().geometryPacket.positions==firstPositions,"view does not retriangulate");
    require(panned->revisions.view==1,"view revision carried");
}
void disabledTerritorialColorPreservesFillAndBoundary() {
    auto doc=sample();doc.presentation.objectStyles[{"territorial","C"}].color=0x112233;
    doc.presentation.webPresentation.styles["countries"].colorVisible=false;
    MapViewState view;GeometryPacketCache cache;MapSceneBuilder builder(cache);
    const auto scene=builder.buildDocument(doc,0,view,{},{});
    const auto fill=std::find_if(scene->polygons.begin(),scene->polygons.end(),[](const auto& p){return p.object==pandoeditor::territorialRef("C");});
    require(fill!=scene->polygons.end()&&fill->style.color==0xa8c7db,"disabled color uses base land palette, not an invisible fill");
    require(std::any_of(scene->strokes.begin(),scene->strokes.end(),[](const auto& p){return p.object==pandoeditor::territorialRef("C");}),"disabled color retains boundary");
    require(doc.presentation.objectStyles.at({"territorial","C"}).color==0x112233,"display option does not overwrite saved color");
}
void distributionRangeRestylesUnchangedPeersInPatch() {
    using namespace pandoeditor;ProjectDocument doc;doc.documentId="distribution-range-test";doc.geometries.insert({"distribution",1},square(0));
    doc.distributionLayers.push_back({"D","Values","people"});
    for(int i=0;i<3;++i){DistributionEntry entry;entry.id="E"+std::to_string(i);entry.layerId="D";entry.geometry=GeometryRef{"distribution",1};entry.value=i*10;doc.distributionEntries.push_back(entry);}
    Project project;project.replace(doc);MapViewState view;GeometryPacketCache cache;MapSceneBuilder builder(cache);
    auto before=builder.build(project.snapshot(),view,{},{});
    auto changed=doc.distributionEntries.back();changed.value=40;ContentEdit edit;edit.target={"distributionEntry",changed.id};edit.value=changed;
    CommandArguments args;args.action=edit;auto prepared=CommandProcessor::prepare(project,CommandProcessor::makeRequest(project,"content.edit",args));
    require(prepared.ok()&&prepared.preview&&CommandProcessor::confirm(project,*prepared.preview).ok(),"distribution value commit");
    auto after=builder.buildPatch(project.snapshot(),view,{},before,{{"distributionEntry","E2"}});
    const auto packet=std::find_if(after->polygons.begin(),after->polygons.end(),[](const auto& p){return p.object==ObjectRef{"distributionEntry","E1"};});
    require(packet!=after->polygons.end()&&std::abs(packet->style.alpha-.265)<1e-6,"range change restyles unchanged peer");
}
void worldRangesKeepSourceSlotsAndLogicalOwnersSeparate() {
    pandoeditor::ProjectDocument doc({},{{"countries","Countries"}});
    appendTerritory(doc,{"OWNER","Owner",{},pandoeditor::UnitKind::General,false},{"world-country-OWNER",1});
    doc.geometries.insert({"world-country-OWNER",1},square(0));
    doc.presentation.membership.emplace(pandoeditor::territorialRef("OWNER"),"countries");
    doc.presentation.objectStyles.emplace(pandoeditor::territorialRef("OWNER"),
                                           pandoeditor::ObjectStyle{});
    auto mesh=std::make_shared<CountryBaseMesh>();
    mesh->preview=false;
    auto frame=std::make_shared<WorldBaseFrame>();
    frame->mesh=mesh;
    frame->ranges.reserve(258);
    frame->ranges.push_back({"SOURCE-A","OWNER","world-country-OWNER"});
    frame->ranges.push_back({"SOURCE-B","OWNER","world-country-OWNER"});
    for(int i=2;i<258;++i)
        frame->ranges.push_back({"MISSING-"+std::to_string(i),
                                 "MISSING-"+std::to_string(i),
                                 "world-country-MISSING-"+std::to_string(i)});
    MapViewState view;GeometryPacketCache cache;MapSceneBuilder builder(cache);
    builder.setWorldBase(frame);
    auto scene=builder.buildDocument(doc,1,view,{},{});
    require(scene->worldCountries.size()==258,"world keeps every GPU source range");
    require(scene->worldCountries[0].visible&&scene->worldCountries[1].visible,
            "all ranges owned by the logical unit remain visible");
    require(scene->worldCountries[0].id=="OWNER"&&scene->worldCountries[1].id=="OWNER",
            "source ranges resolve to the same logical owner");
    const auto owned=worldRangeIndicesForOwner(*frame,"OWNER");
    require(owned==std::vector<std::size_t>({0,1}),
            "selection highlight covers all ranges of a merged owner");
}
std::shared_ptr<WorldBaseFrame> zoomPreviewFrame(bool documentReady) {
    auto mesh=std::make_shared<CountryBaseMesh>();mesh->preview=true;
    auto frame=std::make_shared<WorldBaseFrame>();frame->mesh=mesh;
    frame->documentReady=documentReady;
    for(const auto& id:{"A","A","B","C","DELETED"})
        frame->ranges.push_back({"SOURCE-"+std::to_string(frame->ranges.size()),id,
                                 "world-country-"+std::string(id)});
    while(frame->ranges.size()<258) {
        const auto id="MISSING-"+std::to_string(frame->ranges.size());
        frame->ranges.push_back({id,id,"world-country-"+id});
    }
    return frame;
}
pandoeditor::ProjectDocument zoomPreviewDocument() {
    using namespace pandoeditor;
    ProjectDocument doc({},{{"countries","Countries"}});
    for(const auto& id:{"A","B","C"}) {
        const GeometryRef ref{"world-country-"+std::string(id),1};
        appendTerritory(doc,{id,id,{},UnitKind::General,false},ref);
        doc.geometries.insert(ref,square(id[0]-'A'));
        doc.presentation.membership[territorialRef(id)]="countries";
        doc.presentation.objectStyles[territorialRef(id)]={0x123456,.5};
    }
    return doc;
}
void startupPreviewRemainsVisibleBeforeDocumentMaterialization() {
    GeometryPacketCache cache;MapSceneBuilder builder(cache);MapViewState view;
    auto frame=zoomPreviewFrame(false);builder.setWorldBase(frame);
    const auto scene=builder.buildDocument({},0,view,{},{});
    require(frame->startupPreview(),"unmaterialized preview has bootstrap lifetime");
    require(scene->worldCountries.size()==258&&
            std::all_of(scene->worldCountries.begin(),scene->worldCountries.end(),
                        [](const auto& range){return range.visible;}),
            "bootstrap preview retains all world ranges without document owners");
    require(scene->drawSequence.empty()&&scene->polygons.empty(),
            "bootstrap preview uses its dedicated base submission only");
}
void documentReadyPreviewHonorsStylesVisibilityAndEditedShapes() {
    using namespace pandoeditor;
    auto doc=zoomPreviewDocument();
    doc.presentation.webPresentation.hiddenItems["countries"].insert("B");
    doc.presentation.webPresentation.objectStyles["territorial:entity:A"].boundaryVisible=false;
    doc.geometries.insert({"world-country-C",2},square(30));staticGeometryBinding(doc,doc.units[2].id).geometryRef.version=2;
    const auto originalA=doc.geometries.get({"world-country-A",1});
    const auto editedC=doc.geometries.get({"world-country-C",2});
    GeometryPacketCache cache;MapSceneBuilder builder(cache);MapViewState view;
    auto frame=zoomPreviewFrame(true);builder.setWorldBase(frame);
    InteractionRenderPacket interaction;interaction.hover=territorialRef("A");
    const auto scene=builder.buildDocument(doc,1,view,interaction,{});
    require(!frame->startupPreview()&&scene->worldBase->mesh->preview,
            "ready preview remains the single preview mesh with document semantics");
    require(scene->worldCountries[0].visible&&scene->worldCountries[1].visible,
            "ready preview preserves every source range of a merged owner");
    for(const auto slot:{0,1})require(scene->worldCountries[slot].fill.color==0x123456&&
            std::abs(scene->worldCountries[slot].fill.alpha-.5)<1e-6,
            "ready preview preserves document color and opacity");
    require(!scene->worldCountries[2].visible&&!scene->worldCountries[3].visible&&
            !scene->worldCountries[4].visible,
            "hidden deleted and edited owners never expose stale preview geometry");
    require(std::count_if(scene->drawSequence.begin(),scene->drawSequence.end(),[](const auto& draw) {
                return draw.primitive==PrimitiveKind::WorldFill;
            })==2,"ready preview contributes ordered fills only for visible unchanged owner slots");
    require(std::none_of(scene->drawSequence.begin(),scene->drawSequence.end(),[](const auto& draw) {
                return draw.primitive==PrimitiveKind::WorldStroke;
            }),"hidden document boundary is excluded from base draws");
    const auto edited=std::find_if(scene->polygons.begin(),scene->polygons.end(),[](const auto& draw) {
        return draw.object==territorialRef("C");
    });
    require(edited!=scene->polygons.end()&&edited->geometry==GeometryRef{"world-country-C",2},
            "edited canonical geometry remains a precise fallback over preview");
    require(scene->polygons.size()==1&&scene->interaction.hover==territorialRef("A")&&
            worldRangeIndicesForOwner(*frame,"A")==std::vector<std::size_t>({0,1}),
            "ready preview preserves owner highlight mapping without duplicate canonical fills");
    require(doc.geometries.get({"world-country-A",1})==originalA&&
            doc.geometries.get({"world-country-C",2})==editedC&&editedC->polygons[0][0][0].x==30,
            "preview rendering never changes canonical document geometry");
    doc.presentation.userLayers[0].visible=false;
    const auto hidden=builder.buildDocument(doc,2,view,{},scene);
    require(hidden->drawSequence.empty()&&hidden->polygons.empty()&&
            std::none_of(hidden->worldCountries.begin(),hidden->worldCountries.end(),
                         [](const auto& range){return range.visible;}),
            "ready preview respects layer hiding for base ranges and edited fallback");
}
void canonicalPreviewSwitchPublishesCorrectResourceAndDocumentState() {
    auto doc=zoomPreviewDocument();GeometryPacketCache cache;MapSceneBuilder builder(cache);
    MapViewState view;auto preview=zoomPreviewFrame(true);
    auto canonical=std::make_shared<WorldBaseFrame>(*preview);
    auto mesh=std::make_shared<CountryBaseMesh>();canonical->mesh=mesh;
    builder.setWorldBase(canonical);auto scene=builder.buildDocument(doc,1,view,{},{});
    const auto canonicalScene=scene;
    for(int i=0;i<4;++i) {
        builder.setWorldBase(preview);scene=builder.buildDocument(doc,1,view,{},scene);
        require(scene->worldBase==preview&&scene->worldCountries[0].fill.color==0x123456,
                "canonical to preview uses the requested resource and latest style");
        builder.setWorldBase(canonical);scene=builder.buildDocument(doc,1,view,{},scene);
        require(scene->worldBase==canonical&&scene->polygons.empty(),
                "preview to canonical preserves base owner handling without duplicate packets");
    }
    require(scene!=canonicalScene&&scene->revisions.dataset>canonicalScene->revisions.dataset,
            "resource switches publish new immutable scene dataset revisions");
    doc.units.clear();
    builder.setWorldBase(preview);const auto replaced=builder.buildDocument(doc,2,view,{},scene);
    require(replaced->drawSequence.empty()&&
            std::none_of(replaced->worldCountries.begin(),replaced->worldCountries.end(),
                         [](const auto& range){return range.visible;}),
            "replacement document cannot resurrect old preview owners");
}
void immutableSnapshotsReusePreparationButRespectInvalidation() {
    using namespace pandoeditor;
    ProjectDocument doc({},{{"countries","Countries"}});
    appendTerritory(doc,{"C","Country",{},UnitKind::General,false},{"C",1});
    doc.geometries.insert({"C",1},square(0));
    doc.presentation.membership.emplace(territorialRef("C"),"countries");
    doc.presentation.objectStyles.emplace(territorialRef("C"),ObjectStyle{});
    doc.presentation.webPresentation.styles["countries"].boundaryVisible=false;
    Project project;project.replace(doc);
    MapViewState view;GeometryPacketCache cache;MapSceneBuilder builder(cache);
    auto first=builder.build(project.snapshot(),view,{},{});
    require(first->strokes.empty(),"hidden boundary is initially unprepared");
    const auto count=builder.preparationCount();
    auto duplicate=builder.build(project.snapshot(),view,{},first);
    require(duplicate==first&&builder.preparationCount()==count,"duplicate notification does no preparation");
    view.revision++;view.centerLongitude=10;
    auto panned=builder.build(project.snapshot(),view,{},first);
    require(panned==first,"view-only keeps exact immutable preparation snapshot");
    require(builder.preparationCount()==count&&panned->preparationIdentity==first->preparationIdentity,
            "camera only updates view and culling");
    require(panned->polygons.front().geometryPacket.positions==first->polygons.front().geometryPacket.positions,
            "camera retains packet storage");
    InteractionRenderPacket hover;hover.hover=territorialRef("C");
    auto highlighted=builder.build(project.snapshot(),view,hover,panned);
    require(builder.preparationCount()==count+1&&!highlighted->strokes.empty(),
            "first hidden-boundary highlight prepares the missing stroke");
    auto cleared=builder.build(project.snapshot(),view,{},highlighted);
    require(builder.preparationCount()==count+1&&cleared->drawSequence.size()==1,
            "retaining highlight packet does not expose hidden base boundary");
    auto again=builder.build(project.snapshot(),view,hover,cleared);
    require(builder.preparationCount()==count+1&&again->revisions.selection>cleared->revisions.selection,
            "subsequent hover reuses prepared geometry");
    view.mode=ProjectionMode::Globe;view.revision++;
    auto globe=builder.build(project.snapshot(),view,hover,again);
    require(builder.preparationCount()==count+2,"projection preparation policy invalidates reuse");
    RenderQualityProfile quality;quality.backgroundLod=RenderLod::Coarse;builder.setQuality(quality);
    auto coarse=builder.build(project.snapshot(),view,hover,globe);
    require(builder.preparationCount()==count+3,"background LOD invalidates reuse");
    project.replace(doc);
    auto replaced=builder.build(project.snapshot(),view,hover,coarse);
    require(builder.preparationCount()==count+4&&replaced->preparationIdentity!=coarse->preparationIdentity,
            "replacement cannot reuse another immutable document's preparation");
    require(project.renameCountry("C","Renamed country"),"rename for snapshot invalidation");
    auto renamed=builder.build(project.snapshot(),view,hover,replaced);
    require(builder.preparationCount()==count+5,"document edit invalidates preparation");
    require(project.undo(),"undo rename");
    builder.build(project.snapshot(),view,hover,renamed);
    require(builder.preparationCount()==count+6,"undo revision invalidates preparation");
}
void interactionOnlyPreparesActualFallbackLodChanges() {
    using namespace pandoeditor;
    ProjectDocument doc({},{{"countries","Countries"}});doc.documentId="m92-fallback";
    appendTerritory(doc,{"C","Country",{},UnitKind::General},{"C",1});
    doc.presentation.membership[territorialRef("C")]="countries";
    doc.presentation.objectStyles[territorialRef("C")]={};
    doc.geometries.insert({"C",1},square(0));doc.geometries.insert({"G",1},square(3));
    GenericFeature feature;feature.id="G";feature.name="G";feature.geometry={"G",1};
    feature.fallbackOnly=true;
    doc.genericFeatures.push_back(feature);
    doc.presentation.objectStyles[{"generic","G"}]={};
    Project project;project.replace(doc);
    GeometryPacketCache cache;MapSceneBuilder builder(cache);MapViewState view;
    auto scene=builder.build(project.snapshot(),view,{},{});const auto high=builder.preparationCount();
    InteractionRenderPacket country;country.selected={territorialRef("C")};country.primary=territorialRef("C");
    scene=builder.build(project.snapshot(),view,country,scene);
    require(builder.preparationCount()==high,"unrelated fallback does not reprepare country selection");
    InteractionRenderPacket selected;selected.selected={{"generic","G"}};selected.primary=ObjectRef{"generic","G"};
    scene=builder.build(project.snapshot(),view,selected,scene);
    require(builder.preparationCount()==high,"already-high fallback selection needs no preparation");
    RenderQualityProfile quality;quality.backgroundLod=RenderLod::Coarse;builder.setQuality(quality);
    scene=builder.build(project.snapshot(),view,country,scene);const auto coarse=builder.preparationCount();
    scene=builder.build(project.snapshot(),view,selected,scene);
    require(builder.preparationCount()==coarse+1,"coarse fallback selection promotes geometry preparation");
    scene=builder.build(project.snapshot(),view,country,scene);
    require(builder.preparationCount()==coarse+2,"unprotected fallback restores background LOD");
}
void resourceIdentityCannotBeHiddenByEqualRevisions() {
    auto doc=sample();GeometryPacketCache cache;MapSceneBuilder builder(cache);MapViewState view;
    auto hydro=std::make_shared<BuiltinHydroRenderFrame>();hydro->revision=7;
    builder.setBuiltinHydro(hydro);
    auto first=builder.buildDocument(doc,3,view,{},{});
    auto replacement=std::make_shared<BuiltinHydroRenderFrame>(*hydro);
    builder.setBuiltinHydro(replacement);
    auto second=builder.buildDocument(doc,3,view,{},first);
    require(second!=first,"equal numeric hydro revision still replaces immutable frame");
    auto base=std::make_shared<WorldBaseFrame>();builder.setWorldBase(base);
    auto third=builder.buildDocument(doc,3,view,{},second);
    require(third!=second&&third->worldBase==base,"world frame identity invalidates publication");
    view.mode=ProjectionMode::Globe;
    auto fourth=builder.buildDocument(doc,3,view,{},third);
    require(fourth!=third,"projection policy cannot be hidden by an equal view revision");
}
pandoeditor::ProjectDocument patchFixture() {
    using namespace pandoeditor;
    ProjectDocument doc({},{{"countries","Countries"}});doc.documentId="m93-patch-fixture";
    for(const auto& id:{"A","B","C"}) {
        appendTerritory(doc,{id,id,{},UnitKind::General},{id,1});
        doc.geometries.insert({id,1},square(id[0]=='A'?0:id[0]=='B'?20:40));
        doc.presentation.membership[territorialRef(id)]="countries";
        doc.presentation.objectStyles[territorialRef(id)]=ObjectStyle{0x123456,1};
    }
    return doc;
}
const PolygonDrawPacket& patchFill(const RenderScene& scene,const char* id) {
    const auto found=std::find_if(scene.polygons.begin(),scene.polygons.end(),[&](const auto& p){return p.object==pandoeditor::territorialRef(id);});
    require(found!=scene.polygons.end(),"patch fixture retains expected fill");return *found;
}
const StrokeDrawPacket& patchStroke(const RenderScene& scene,const char* id) {
    const auto found=std::find_if(scene.strokes.begin(),scene.strokes.end(),[&](const auto& p){return p.object==pandoeditor::territorialRef(id);});
    require(found!=scene.strokes.end(),"patch fixture retains expected stroke");return *found;
}
void requirePatchStorageShared(const RenderScene& before,const RenderScene& after,const char* id) {
    const auto& a=patchFill(before,id).geometryPacket;const auto& b=patchFill(after,id).geometryPacket;
    require(a.positions==b.positions&&a.indices==b.indices&&a.unitSpherePositions==b.unitSpherePositions&&
            a.globeIndices==b.globeIndices&&a.ringOffsets==b.ringOffsets&&a.polygonOffsets==b.polygonOffsets,
            "unchanged fill retains all immutable geometry storage");
    const auto& sa=patchStroke(before,id).geometryPacket;const auto& sb=patchStroke(after,id).geometryPacket;
    require(sa.startsEnds==sb.startsEnds&&sa.unitSphereStartsEnds==sb.unitSphereStartsEnds,
            "unchanged boundary retains immutable geometry storage");
}
void presentationPatchNeverVisitsGeometryCache() {
    using namespace pandoeditor;Project project;project.replace(patchFixture());
    GeometryPacketCache cache;MapSceneBuilder builder(cache);MapViewState view;
    auto scene=builder.build(project.snapshot(),view,{},{});
    // Prepared packets outlive cache eviction; style updates must reuse them directly.
    cache.setBudget(0);
    for(int operation=0;operation<2;++operation) {
        const auto before=scene;const auto counters=cache.stats();
        require(operation==0?project.setColor("A",0xaabbcc):project.setCountryOpacity("A",.35),"presentation patch command changes project");
        scene=builder.buildPatch(project.snapshot(),view,{},before,{territorialRef("A")});
        for(const auto& id:{"A","B","C"})requirePatchStorageShared(*before,*scene,id);
        require(patchFill(*scene,"A").style.color==0xaabbcc,"color patch updates rendered fill style");
        if(operation==1)require(std::abs(patchFill(*scene,"A").style.alpha-.35f)<1e-6,"opacity patch updates rendered fill style");
        require(cache.stats().builds==counters.builds,"presentation patch has zero geometry cache misses/builds");
        require(cache.stats().hits==counters.hits,"presentation patch does not prepare cached geometry");
        std::cout<<"M93_BUILDER_DELTA style="<<(operation==0?"color":"opacity")
                 <<" geometry-build-delta="<<cache.stats().builds-counters.builds
                 <<" geometry-hit-delta="<<cache.stats().hits-counters.hits<<'\n';
    }
}
void singleTerritorialGeometryPatchRetainsUnrelatedStorageThroughHistory() {
    using namespace pandoeditor;Project project;project.replace(patchFixture());
    GeometryPacketCache cache;MapSceneBuilder builder(cache);MapViewState view;
    const auto original=builder.build(project.snapshot(),view,{},{});const auto counters=cache.stats();
    const auto plan=CommandProcessor::planTerritorial(project,ReplaceGeometryIntent{territorialRef("A")});
    require(plan.ok()&&plan.plan,"plan single territorial geometry replacement");
    GeometryPatch patch;patch.sourceRevision=project.revision();patch.replacements.push_back({territorialRef("A"),square(10)});
    CommandArguments args;args.action=ApplyTerritorialMutation{*plan.plan,patch};
    auto preview=CommandProcessor::prepare(project,CommandProcessor::makeRequest(project,"territorial.geometry.commit",args));
    require(preview.ok()&&preview.preview&&CommandProcessor::confirm(project,*preview.preview).changed(),"commit single territorial geometry replacement");
    auto changed=builder.buildPatch(project.snapshot(),view,{},original,{territorialRef("A")});
    require(patchFill(*changed,"A").geometryPacket.positions!=patchFill(*original,"A").geometryPacket.positions,
            "edited fill replaces immutable positions");
    require(patchStroke(*changed,"A").geometryPacket.startsEnds!=patchStroke(*original,"A").geometryPacket.startsEnds,
            "edited boundary replaces immutable endpoints");
    require(cache.stats().builds==counters.builds+2,"single geometry edit builds only fill and boundary packets");
    require(cache.stats().hits==counters.hits,"single geometry edit never visits unrelated geometry cache entries");
    std::cout<<"M93_BUILDER_DELTA single-geometry geometry-build-delta="<<cache.stats().builds-counters.builds
             <<" unrelated-hit-delta="<<cache.stats().hits-counters.hits<<'\n';
    for(const auto& id:{"B","C"})requirePatchStorageShared(*original,*changed,id);
    require(project.undo(),"undo territorial geometry patch");
    auto undone=builder.buildPatch(project.snapshot(),view,{},changed,{territorialRef("A")});
    require(*patchFill(*undone,"A").geometryPacket.positions==*patchFill(*original,"A").geometryPacket.positions,
            "undo restores original fill coordinates");
    for(const auto& id:{"B","C"})requirePatchStorageShared(*original,*undone,id);
    require(project.redo(),"redo territorial geometry patch");
    auto redone=builder.buildPatch(project.snapshot(),view,{},undone,{territorialRef("A")});
    require(*patchFill(*redone,"A").geometryPacket.positions==*patchFill(*changed,"A").geometryPacket.positions,
            "redo restores edited fill coordinates");
    for(const auto& id:{"B","C"})requirePatchStorageShared(*original,*redone,id);
}
void deletionPatchRemovesPacketsAndHistoryRestoresOnlyDeletedObject() {
    using namespace pandoeditor;Project project;project.replace(patchFixture());
    GeometryPacketCache cache;MapSceneBuilder builder(cache);MapViewState view;
    const auto original=builder.build(project.snapshot(),view,{},{});const auto counters=cache.stats();
    const auto plan=CommandProcessor::planTerritorial(project,DeleteTerritorialIntent{{territorialRef("A")}});
    require(plan.ok()&&plan.plan,"plan territorial deletion");
    CommandArguments args;args.action=ApplyTerritorialMutation{*plan.plan,{}};
    auto preview=CommandProcessor::prepare(project,CommandProcessor::makeRequest(project,"territorial.delete",args));
    require(preview.ok()&&preview.preview&&CommandProcessor::confirm(project,*preview.preview).changed(),"commit territorial deletion");
    const auto absent=[](const RenderScene& scene) {
        for(const auto& p:scene.polygons)require(p.object!=territorialRef("A"),"deleted object has no fill packet");
        for(const auto& p:scene.strokes)require(p.object!=territorialRef("A"),"deleted object has no stroke packet");
        for(const auto& command:scene.drawSequence) {
            if(command.primitive==PrimitiveKind::Polygon)require(command.index<scene.polygons.size(),"deletion repairs fill draw indices");
            if(command.primitive==PrimitiveKind::Stroke)require(command.index<scene.strokes.size(),"deletion repairs boundary draw indices");
        }
    };
    auto deleted=builder.buildPatch(project.snapshot(),view,{},original,{territorialRef("A")});absent(*deleted);
    require(cache.stats().builds==counters.builds&&cache.stats().hits==counters.hits,"deletion never prepares retained geometry");
    for(const auto& id:{"B","C"})requirePatchStorageShared(*original,*deleted,id);
    require(project.undo(),"undo territorial deletion");
    auto restored=builder.buildPatch(project.snapshot(),view,{},deleted,{territorialRef("A")});
    require(*patchFill(*restored,"A").geometryPacket.positions==*patchFill(*original,"A").geometryPacket.positions,
            "undo deletion restores deleted geometry");
    for(const auto& id:{"B","C"})requirePatchStorageShared(*original,*restored,id);
    require(project.redo(),"redo territorial deletion");
    auto redone=builder.buildPatch(project.snapshot(),view,{},restored,{territorialRef("A")});absent(*redone);
    for(const auto& id:{"B","C"})requirePatchStorageShared(*original,*redone,id);
}
void dirtyClassificationUsesActualObjectsAndReverseHistory() {
    using namespace pandoeditor;
    auto original=patchFixture();
    original.distributionLayers.push_back({"D","Distribution","count"});
    DistributionEntry entry;entry.id="E";entry.layerId="D";entry.territory=territorialRef("A");
    original.distributionEntries.push_back(entry);
    const auto contains=[](const auto& refs,const ObjectRef& object){return std::find(refs.begin(),refs.end(),object)!=refs.end();};
    auto styled=original;styled.presentation.objectStyles[territorialRef("A")].color=0xaabbcc;
    const auto color=calculateChangeImpact(original,styled).sceneDirty;
    require(!color.geometry&&color.presentation&&!color.fullRebuild&&color.geometryObjects.empty(),
            "targetless color change is presentation-only dirty state");
    require(contains(color.affectedObjects,territorialRef("A")),"targetless color finds actual changed owner");
    Project project;project.replace(original);const auto beforeLayer=project.snapshot();
    require(project.setLayerOpacity("countries",.4),"change layer opacity for dirty classification");
    const auto layer=calculateChangeImpact(beforeLayer.document(),project.document()).sceneDirty;
    require(!layer.geometry&&layer.presentation&&!layer.fullRebuild,"layer opacity is presentation-only dirty state");
    for(const auto& id:{"A","B","C"})require(contains(layer.affectedObjects,territorialRef(id)),"layer opacity expands affected members");
    auto edited=original;edited.geometries.insert({"A",2},square(10));staticGeometryBinding(edited,"A").geometryRef={"A",2};
    for(const auto& impact:{calculateChangeImpact(original,edited),calculateChangeImpact(edited,original)}) {
        require(impact.sceneDirty.geometry&&!impact.sceneDirty.fullRebuild,"geometry edit and undo use symmetric local dirty state");
        require(contains(impact.sceneDirty.geometryObjects,territorialRef("A")),"geometry owner is geometry dirty");
        require(contains(impact.sceneDirty.geometryObjects,ObjectRef{"distributionEntry","E"}),"territory geometry expands dependent distribution geometry");
        require(!contains(impact.sceneDirty.geometryObjects,territorialRef("B"))&&!contains(impact.sceneDirty.geometryObjects,territorialRef("C")),
                "geometry dirtiness excludes unrelated owners");
    }
    auto deleted=original;deleted.units.erase(deleted.units.begin());removeTerritorialRecords(deleted,{"A"});deleted.distributionEntries.clear();
    deleted.presentation.membership.erase(territorialRef("A"));deleted.presentation.objectStyles.erase(territorialRef("A"));
    for(const auto& impact:{calculateChangeImpact(original,deleted),calculateChangeImpact(deleted,original)}) {
        require(impact.sceneDirty.geometry&&!impact.sceneDirty.fullRebuild,"deletion and undo classify symmetric local geometry dirtiness");
        require(contains(impact.sceneDirty.affectedObjects,territorialRef("A"))&&contains(impact.sceneDirty.geometryObjects,territorialRef("A")),
                "deleted or restored owner remains in dirty object set");
    }
}
void requireSceneMatchesFullPreparation(const RenderScene& delta,const pandoeditor::ProjectSnapshot& snapshot,const MapViewState& view) {
    GeometryPacketCache cache;MapSceneBuilder fullBuilder(cache);
    const auto full=fullBuilder.build(snapshot,view,{},{});
    const auto sameStyle=[](const RenderStyle& a,const RenderStyle& b) {
        return a.color==b.color&&std::abs(a.alpha-b.alpha)<1e-6&&std::abs(a.fillAlpha-b.fillAlpha)<1e-6&&
            a.width==b.width&&a.blendMode==b.blendMode&&a.dashOn==b.dashOn&&a.dashOff==b.dashOff;
    };
    require(delta.polygons.size()==full->polygons.size()&&delta.strokes.size()==full->strokes.size()&&
            delta.points.size()==full->points.size(),"delta and full preparation produce the same visible packet set");
    for(const auto& draw:delta.polygons) {
        const auto found=std::find_if(full->polygons.begin(),full->polygons.end(),[&](const auto& p){return p.key==draw.key;});
        require(found!=full->polygons.end()&&sameStyle(draw.style,found->style),"delta fill style matches full preparation");
        require(*draw.geometryPacket.positions==*found->geometryPacket.positions&&*draw.geometryPacket.indices==*found->geometryPacket.indices,
                "delta fill geometry matches full preparation");
    }
    for(const auto& draw:delta.strokes) {
        const auto found=std::find_if(full->strokes.begin(),full->strokes.end(),[&](const auto& p){return p.key==draw.key;});
        require(found!=full->strokes.end()&&sameStyle(draw.style,found->style),"delta boundary style matches full preparation");
        require(*draw.geometryPacket.startsEnds==*found->geometryPacket.startsEnds,"delta boundary geometry matches full preparation");
    }
    for(const auto& draw:delta.points) {
        const auto found=std::find_if(full->points.begin(),full->points.end(),[&](const auto& p){return p.key==draw.key;});
        require(found!=full->points.end()&&sameStyle(draw.style,found->style)&&draw.labelText==found->labelText,
                "delta label and point style matches full preparation");
        require(*draw.geometryPacket.positions==*found->geometryPacket.positions,"delta label and point coordinates match full preparation");
    }
    const auto key=[](const RenderScene& scene,const SceneDrawRef& command) {
        if(command.primitive==PrimitiveKind::Polygon)return scene.polygons.at(command.index).key;
        if(command.primitive==PrimitiveKind::Stroke)return scene.strokes.at(command.index).key;
        if(command.primitive==PrimitiveKind::Point)return scene.points.at(command.index).key;
        return scene.worldCountries.at(command.index).id;
    };
    require(delta.drawSequence.size()==full->drawSequence.size(),"delta draw count matches full preparation");
    for(std::size_t i=0;i<delta.drawSequence.size();++i) {
        const auto& a=delta.drawSequence[i];const auto& b=full->drawSequence[i];
        require(a.primitive==b.primitive&&key(delta,a)==key(*full,b)&&a.layerOrder==b.layerOrder&&
                std::abs(a.layerOpacity-b.layerOpacity)<1e-6,"delta ordered draws match full preparation");
    }
}
void layerAndGroupPresentationDeltaMatchesFullPreparation() {
    using namespace pandoeditor;auto doc=patchFixture();
    doc.presentation.userLayers.push_back({"foreground","Foreground"});
    doc.presentation.membership[territorialRef("B")]="foreground";
    Project project;project.replace(doc);GeometryPacketCache cache;MapSceneBuilder builder(cache);MapViewState view;
    auto scene=builder.build(project.snapshot(),view,{},{});cache.setBudget(0);
    for(int operation=0;operation<3;++operation) {
        const auto before=project.snapshot();const auto previous=scene;const auto counters=cache.stats();
        if(operation==0)require(project.setLayerOpacity("countries",.4),"layer opacity changes");
        if(operation==1)require(project.moveLayer("foreground",-1),"layer order changes");
        if(operation==2) {PresentationStyle patch;patch.opacity=.6;patch.boundaryWidth=1;
            require(PresentationCommandProcessor::apply(project,PatchGroupPresentation{"countries",patch})==PresentationResult::Applied,"group presentation changes");}
        const auto dirty=calculateChangeImpact(before.document(),project.document()).sceneDirty;
        require(dirty.presentation&&!dirty.geometry&&!dirty.fullRebuild,"layer and group edits have minimal presentation cause");
        scene=builder.buildDelta(project.snapshot(),view,{},previous,dirty);
        for(const auto& id:{"A","B","C"})requirePatchStorageShared(*previous,*scene,id);
        require(cache.stats().builds==counters.builds&&cache.stats().hits==counters.hits,"layer and group changes never access geometry cache");
        requireSceneMatchesFullPreparation(*scene,project.snapshot(),view);
    }
}
void distributionGeometryDependencyDeltaReplacesOnlyRelatedOwners() {
    using namespace pandoeditor;auto doc=patchFixture();doc.distributionLayers.push_back({"D","Distribution","count"});
    for(const auto& owner:{"A","B"}) {DistributionEntry entry;entry.id=std::string("E")+owner;entry.layerId="D";
        entry.territory=territorialRef(owner);doc.distributionEntries.push_back(entry);}
    Project project;project.replace(doc);GeometryPacketCache cache;MapSceneBuilder builder(cache);MapViewState view;
    const auto before=project.snapshot();const auto original=builder.build(before,view,{},{});
    const auto plan=CommandProcessor::planTerritorial(project,ReplaceGeometryIntent{territorialRef("A")});
    require(plan.ok()&&plan.plan,"plan dependency geometry edit");
    GeometryPatch patch;patch.sourceRevision=project.revision();patch.replacements.push_back({territorialRef("A"),square(10)});
    CommandArguments args;args.action=ApplyTerritorialMutation{*plan.plan,patch};
    auto preview=CommandProcessor::prepare(project,CommandProcessor::makeRequest(project,"territorial.geometry.commit",args));
    require(preview.ok()&&preview.preview&&CommandProcessor::confirm(project,*preview.preview).changed(),"commit dependency geometry edit");
    const auto dirty=calculateChangeImpact(before.document(),project.document()).sceneDirty;
    const auto updated=builder.buildDelta(project.snapshot(),view,{},original,dirty);
    const auto distribution=[](const RenderScene& scene,const char* id)->const PolygonDrawPacket& {
        const auto found=std::find_if(scene.polygons.begin(),scene.polygons.end(),[&](const auto& p){return p.object==ObjectRef{"distributionEntry",id};});
        require(found!=scene.polygons.end(),"dependency distribution fill exists");return *found;
    };
    require(distribution(*updated,"EA").geometryPacket.positions!=distribution(*original,"EA").geometryPacket.positions,
            "territory geometry edit replaces dependent distribution positions");
    require(distribution(*updated,"EB").geometryPacket.positions==distribution(*original,"EB").geometryPacket.positions,
            "unrelated distribution retains positions");
    for(const auto& id:{"B","C"})requirePatchStorageShared(*original,*updated,id);
    requireSceneMatchesFullPreparation(*updated,project.snapshot(),view);
}
void refreshCoalescesChangesFromPreparedBaseline() {
    using namespace pandoeditor;Project project;project.replace(patchFixture());
    GeometryPacketCache cache;MapSceneBuilder builder(cache);MapViewState view;
    auto scene=builder.build(project.snapshot(),view,{},{});cache.setBudget(0);
    const auto counters=cache.stats();
    require(project.setColor("A",0xff0000)&&project.setColor("B",0x00ff00),"two color commits before refresh");
    // The last notification describes only B; refresh must include unrendered A too.
    SceneDirtySet last;last.presentation=true;last.fullRebuild=false;last.affectedObjects={territorialRef("B")};
    scene=builder.refresh(project.snapshot(),view,{},scene,&last);
    require(patchFill(*scene,"A").style.color==0xff0000&&patchFill(*scene,"B").style.color==0x00ff00,
            "coalesced refresh presents both committed colors");
    require(cache.stats().builds==counters.builds&&cache.stats().hits==counters.hits,"coalesced colors need no geometry preparation");
    requireSceneMatchesFullPreparation(*scene,project.snapshot(),view);
    const auto previous=scene;
    require(project.setColor("A",0xabcdef),"commit style before geometry");
    const auto plan=CommandProcessor::planTerritorial(project,ReplaceGeometryIntent{territorialRef("B")});
    require(plan.ok()&&plan.plan,"plan coalesced geometry edit");
    GeometryPatch patch;patch.sourceRevision=project.revision();patch.replacements.push_back({territorialRef("B"),square(25)});
    CommandArguments args;args.action=ApplyTerritorialMutation{*plan.plan,patch};
    auto preview=CommandProcessor::prepare(project,CommandProcessor::makeRequest(project,"territorial.geometry.commit",args));
    require(preview.ok()&&preview.preview&&CommandProcessor::confirm(project,*preview.preview).changed(),"commit coalesced geometry edit");
    last.geometry=true;last.presentation=false;last.geometryObjects={territorialRef("B")};
    scene=builder.refresh(project.snapshot(),view,{},previous,&last);
    require(patchFill(*scene,"A").style.color==0xabcdef,"coalesced geometry refresh includes earlier style commit");
    requirePatchStorageShared(*previous,*scene,"A");requirePatchStorageShared(*previous,*scene,"C");
    require(patchFill(*scene,"B").geometryPacket.positions!=patchFill(*previous,"B").geometryPacket.positions,"coalesced geometry refresh replaces edited owner");
    requireSceneMatchesFullPreparation(*scene,project.snapshot(),view);
}
void physicalVisibilityAndResourceChangesHaveDistinctDirtyCauses() {
    using namespace pandoeditor;const auto doc=patchFixture();
    auto hidden=doc;hidden.physicalData.hiddenHydroIds={"river-1"};
    const auto visibility=calculateChangeImpact(doc,hidden).sceneDirty;
    require(visibility.presentation&&!visibility.geometry&&!visibility.datasetResource,"hidden hydro IDs are presentation-only cause");
    auto resource=doc;resource.physicalData.source="replacement-source";
    const auto replaced=calculateChangeImpact(doc,resource).sceneDirty;
    require(replaced.datasetResource,"source replacement invalidates dataset resource even at equal version");
    auto oldSource=doc;oldSource.physicalData.dataset="hydro";oldSource.physicalData.version="v1";oldSource.physicalData.source="source-a";
    auto sameVersion=oldSource;sameVersion.physicalData.source="source-b";
    require(calculateChangeImpact(oldSource,sameVersion).sceneDirty.datasetResource,
            "same dataset and version cannot hide a different resource source");
    auto sameSource=oldSource;sameSource.physicalData.version="v2";
    const auto versioned=calculateChangeImpact(oldSource,sameSource).sceneDirty;
    require(versioned.datasetResource&&versioned.fullRebuild,
            "same source cannot hide a different resource version");
}
void mergeDeltaRewritesSurvivingReferencesAndMatchesFullPreparation() {
    using namespace pandoeditor;auto doc=patchFixture();
    doc.geometries.insert({"B",2},square(1));staticGeometryBinding(doc,"B").geometryRef={"B",2};
    doc.distributionLayers.push_back({"D","Distribution","count"});
    DistributionEntry entry;entry.id="E";entry.layerId="D";entry.territory=territorialRef("B");doc.distributionEntries.push_back(entry);
    Geometry point;point.type="Point";point.points={{1.5,.5}};doc.geometries.insert({"label",1},point);
    PlaceLabel label;label.id="L";label.name="Surviving label";label.geometry={"label",1};label.territory=territorialRef("B");
    doc.labels.push_back(label);
    Project project;project.replace(doc);GeometryPacketCache cache;MapSceneBuilder builder(cache);MapViewState view;
    const auto before=project.snapshot();const auto original=builder.build(before,view,{},{});
    const auto plan=CommandProcessor::planTerritorial(project,MergeTerritorialIntent{territorialRef("A"),{territorialRef("B")}});
    require(plan.ok()&&plan.plan,"plan adjacent territorial merge");
    auto merged=square(0);for(auto& p:merged.polygons.front().front())if(p.x==1)p.x=2;
    GeometryPatch patch;patch.sourceRevision=project.revision();patch.replacements.push_back({territorialRef("A"),merged});
    patch.removedGeometryOwners={territorialRef("B")};
    CommandArguments args;args.action=ApplyTerritorialMutation{*plan.plan,patch};
    auto preview=CommandProcessor::prepare(project,CommandProcessor::makeRequest(project,"territorial.geometry.commit",args));
    require(preview.ok()&&preview.preview&&CommandProcessor::confirm(project,*preview.preview).changed(),"commit territorial merge with checked patch");
    require(project.document().distributionEntries.size()==1&&project.document().distributionEntries.front().territory==territorialRef("A"),
            "merge rewrites surviving distribution territory to target");
    require(project.document().labels.size()==1&&project.document().labels.front().territory==territorialRef("A"),
            "merge rewrites surviving label territory to target");
    const auto dirty=calculateChangeImpact(before.document(),project.document()).sceneDirty;
    require(dirty.geometry&&!dirty.fullRebuild,"merge is a local geometry and reference delta");
    for(const auto& ref:{territorialRef("A"),territorialRef("B"),ObjectRef{"distributionEntry","E"},ObjectRef{"label","L"}})
        require(std::find(dirty.affectedObjects.begin(),dirty.affectedObjects.end(),ref)!=dirty.affectedObjects.end(),
                "merge dirty set includes geometry owners and surviving rewritten references");
    const auto updated=builder.buildDelta(project.snapshot(),view,{},original,dirty);
    requirePatchStorageShared(*original,*updated,"C");
    for(const auto& draw:updated->polygons)require(draw.object!=territorialRef("B"),"merge removes donor fill");
    for(const auto& draw:updated->strokes)require(draw.object!=territorialRef("B"),"merge removes donor boundary");
    requireSceneMatchesFullPreparation(*updated,project.snapshot(),view);
}
}
int main(){startupPreviewRemainsVisibleBeforeDocumentMaterialization();documentReadyPreviewHonorsStylesVisibilityAndEditedShapes();canonicalPreviewSwitchPublishesCorrectResourceAndDocumentState();presentationPatchNeverVisitsGeometryCache();singleTerritorialGeometryPatchRetainsUnrelatedStorageThroughHistory();deletionPatchRemovesPacketsAndHistoryRestoresOnlyDeletedObject();dirtyClassificationUsesActualObjectsAndReverseHistory();layerAndGroupPresentationDeltaMatchesFullPreparation();distributionGeometryDependencyDeltaReplacesOnlyRelatedOwners();refreshCoalescesChangesFromPreparedBaseline();physicalVisibilityAndResourceChangesHaveDistinctDirtyCauses();mergeDeltaRewritesSurvivingReferencesAndMatchesFullPreparation();builderPreservesM5DrawOrderAndCache();worldRangesKeepSourceSlotsAndLogicalOwnersSeparate();distributionRangeRestylesUnchangedPeersInPatch();disabledTerritorialColorPreservesFillAndBoundary();immutableSnapshotsReusePreparationButRespectInvalidation();interactionOnlyPreparesActualFallbackLodChanges();resourceIdentityCannotBeHiddenByEqualRevisions();}

#include <pandoeditor/map/mapscenebuilder.h>
#include <pandoeditor/commands.h>
#include <algorithm>
#include <stdexcept>
#include <cmath>

namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
pandoeditor::Geometry square(double west) {
    return {"Polygon",{},{},{{{{west,0},{west+1,0},{west+1,1},{west,1},{west,0}}}}};
}
pandoeditor::ProjectDocument sample() {
    pandoeditor::ProjectDocument doc;
    doc.units.push_back({"C","Country",{},pandoeditor::UnitKind::Country,{"C",1}});
    doc.units.push_back({"S","Subunit",{},pandoeditor::UnitKind::Subunit,{"S",1}});
    doc.units.push_back({"R","Region",{},pandoeditor::UnitKind::Region,{"R",1}});
    for(const auto& id:{"C","S","R"})doc.geometries.insert({id,1},square(0));
    doc.presentation.webPresentation.objectOrder={"territorial:region:R","territorial:subunit:S"};
    return doc;
}
void builderPreservesM5DrawOrderAndCache() {
    auto doc=sample();MapViewState view;GeometryPacketCache cache;MapSceneBuilder builder(cache);
    auto first=builder.buildDocument(doc,3,view,{},{});
    require(first->polygons.size()==3&&first->drawSequence.size()>=3,"country subunit region packets");
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
    doc.units.push_back({"OWNER","Owner",{},pandoeditor::UnitKind::Country,
                         {"world-country-OWNER",1}});
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
        doc.units.push_back({id,id,{},UnitKind::Country,ref});
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
    doc.presentation.webPresentation.objectStyles["territorial:country:A"].boundaryVisible=false;
    doc.geometries.insert({"world-country-C",2},square(30));doc.units[2].geometry.version=2;
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
    doc.units.push_back({"C","Country",{},UnitKind::Country,{"C",1}});
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
}
int main(){startupPreviewRemainsVisibleBeforeDocumentMaterialization();documentReadyPreviewHonorsStylesVisibilityAndEditedShapes();canonicalPreviewSwitchPublishesCorrectResourceAndDocumentState();builderPreservesM5DrawOrderAndCache();worldRangesKeepSourceSlotsAndLogicalOwnersSeparate();distributionRangeRestylesUnchangedPeersInPatch();disabledTerritorialColorPreservesFillAndBoundary();immutableSnapshotsReusePreparationButRespectInvalidation();}

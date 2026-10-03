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
int main(){builderPreservesM5DrawOrderAndCache();worldRangesKeepSourceSlotsAndLogicalOwnersSeparate();distributionRangeRestylesUnchangedPeersInPatch();disabledTerritorialColorPreservesFillAndBoundary();immutableSnapshotsReusePreparationButRespectInvalidation();}

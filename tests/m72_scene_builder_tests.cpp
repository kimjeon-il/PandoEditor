#include <pandoeditor/map/mapscenebuilder.h>
#include <stdexcept>

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
}
int main(){builderPreservesM5DrawOrderAndCache();worldRangesKeepSourceSlotsAndLogicalOwnersSeparate();}

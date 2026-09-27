#include "mapscenebuilder.h"
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
}
int main(){builderPreservesM5DrawOrderAndCache();}

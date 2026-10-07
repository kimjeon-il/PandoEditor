#include "territorial_fixture.h"
#include <pandoeditor/map/mapscenebuilder.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <functional>

// Independent fixed literals from ebcfae4 map-interaction-style.js
// (blob 8cad4db5a07c4286b5a5e1fccdbaa6c6988caa4e) and the valid-root
// subunit-internal classification in that revision's boundary-topology.js.
// No original fixture, captured expected result, or source is rewritten.
int main() {
 using namespace pandoeditor;
 ProjectDocument d;d.documentId="m98-fixed-stroke-literals";
 auto add=[&](const char* id,UnitKind kind,const char* parent){Geometry g{"Polygon",{},{},{{{{0,0},{0,2},{2,2},{2,0},{0,0}}}}};d.geometries.insert({id,1},g);appendTerritory(d,{id,id,{},kind,false},{id,1},parent);};
 add("root",UnitKind::General,"");add("child",UnitKind::General,"root");add("region",UnitKind::Regional,"");
 GeometryPacketCache cache;MapSceneBuilder builder(cache);MapViewState view;view.viewportWidth=1920;view.viewportHeight=929;view.scale=500;view.translateX=960;view.translateY=464.5;
 auto scene=builder.buildDocument(d,1,view,{},{});
 auto style=[&](const char* id){for(const auto& s:scene->strokes)if(s.object==territorialRef(id))return s.style;throw std::runtime_error("actual stroke packet missing");};
 auto equal=[](float actual,double expected){if(std::abs(double(actual)-expected)>1e-6)throw std::runtime_error("actual builder stroke differs from fixed Web literal");};
 const std::vector<std::pair<const char*,std::function<void()>>> tests{
  {"country .72 CSS pixels",[&]{equal(style("root").width,.72);}},
  {"valid General child internal 1.1 CSS pixels",[&]{equal(style("child").width,1.1);}},
  {"Regional 1.5 CSS pixels",[&]{equal(style("region").width,1.5);}},
  {"country solid",[&]{equal(style("root").dashOn,0);equal(style("root").dashOff,0);}},
  {"valid General child internal dash 3 2",[&]{equal(style("child").dashOn,3);equal(style("child").dashOff,2);}},
  {"Regional dash 7 3",[&]{equal(style("region").dashOn,7);equal(style("region").dashOff,3);}},
  {"every runtime style option publishes without copying geometry",[&]{
    std::vector<std::function<void(mapstyle::Options&)>> changes{
     [](auto& o){o.dark=false;},[](auto& o){o.outlineVisible=false;},[](auto& o){o.directManipulation=true;},
     [](auto& o){o.sharedBoundary=true;},[](auto& o){o.antiAlias=false;},[](auto& o){o.fillStrength=.6;},
     [](auto& o){o.selectionColor=0x123456;}};
    for(const auto& change:changes){InteractionRenderPacket interaction;change(interaction.styleOptions);
     const auto next=builder.buildDocument(d,1,view,interaction,scene);
     if(next->interactionSignature==scene->interactionSignature||!(next->interaction.styleOptions==interaction.styleOptions))throw std::runtime_error("style option omitted from current scene identity");
     if(next->strokes.front().geometryPacket.startsEnds!=scene->strokes.front().geometryPacket.startsEnds)throw std::runtime_error("style-only change copied retained geometry");}
   }}
 };
 int pass=0,fail=0;for(const auto& t:tests)try{t.second();++pass;std::cout<<"PASS "<<t.first<<'\n';}catch(const std::exception& e){++fail;std::cout<<"FAIL "<<t.first<<": "<<e.what()<<'\n';}
 std::cout<<"processed="<<tests.size()<<" pass="<<pass<<" fail="<<fail<<" skip=0\n";return fail?1:0;
}

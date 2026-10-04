#include <pandoeditor/map/labelengine.h>
#include <cmath>
#include <stdexcept>

namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}

MapViewState flatView() {
    MapViewState view;view.mode=ProjectionMode::Flat;
    view.viewportWidth=800;view.viewportHeight=600;
    view.scale=8*180/3.14159265358979323846;
    view.translateX=400;view.translateY=300;
    return view;
}

MapLabelSource label(const char* id,double lon,double lat,double priority) {
    MapLabelSource source;
    source.ref={"label",id};source.text=id;source.geographic={lon,lat};
    source.width=80;source.height=20;source.priority=priority;
    return source;
}

void spatialQueryAndCollisionAreEngineOwned() {
    MapLabelEngine engine;
    engine.setSources({
        label("high",0,0,100),
        label("low",1,0,10),
        label("far",120,60,90)
    },1);
    MapLabelLayoutOptions options;options.zoom=2;options.viewportWidth=800;options.viewportHeight=600;
    const auto& placed=engine.layout(flatView(),options);
    require(placed.size()==1,"collision keeps one visible label");
    require(placed.front().ref.id=="high","priority wins collision");
    require(engine.stats().cellCount>=2,"sources spatially sharded");
    require(engine.stats().candidatesExamined==2,"offscreen shard is not queried");
}

void selectedAndPinnedBypassOrdinaryCollisionAndBounds() {
    auto pinned=label("pinned",150,70,1);pinned.pinned=true;
    MapLabelEngine engine;
    engine.setSources({label("base",0,0,100),label("selected",1,0,1),pinned},1);
    MapLabelLayoutOptions options;options.viewportWidth=800;options.viewportHeight=600;
    const std::set<pandoeditor::ObjectRef> selected{{"label","selected"}};
    const auto& placed=engine.layout(flatView(),options,selected);
    const auto refs=engine.placedRefs();
    require(!refs.count({"label","base"}),"selected label owns the overlapping collision slot");
    require(refs.count({"label","selected"}),"selected bypasses ordinary collision rejection");
    require(refs.count({"label","pinned"}),"pinned is retained outside ordinary bounds");
    require(placed.size()==2,"selected and pinned forced labels are retained");
}

void interactionReprojectionDoesNotQueryOrRelayout() {
    MapLabelEngine engine;
    engine.setSources({label("one",0,0,100)},1);
    MapLabelLayoutOptions options;options.viewportWidth=800;options.viewportHeight=600;
    engine.layout(flatView(),options);
    const auto queries=engine.stats().queries,layouts=engine.stats().layouts;
    auto moved=flatView();moved.translateX+=50;moved.revision=1;
    const auto& placed=engine.reproject(moved,options.zoom);
    require(placed.size()==1&&std::abs(placed.front().x-450)<1e-8,"placement follows camera");
    require(engine.stats().queries==queries&&engine.stats().layouts==layouts,
            "reprojection performs no candidate query or collision layout");
    require(engine.stats().reprojects==1,"reprojection counted");
}

void globeReprojectionDropsBackHemisphere() {
    MapLabelEngine engine;
    engine.setSources({label("front",0,0,10),label("back",180,0,100)},1);
    auto globe=flatView();globe.mode=ProjectionMode::Globe;globe.scale=250;
    MapLabelLayoutOptions options;options.viewportWidth=800;options.viewportHeight=600;
    const auto& placed=engine.layout(globe,options);
    require(placed.size()==1&&placed.front().ref.id=="front","back hemisphere excluded");
    globe.centerLongitude=180;globe.revision=1;
    const auto& rotated=engine.reproject(globe,options.zoom);
    require(rotated.empty(),"accepted front label disappears when rotated behind");
    globe.centerLongitude=0;globe.revision=2;
    const auto& returned=engine.reproject(globe,options.zoom);
    require(returned.size()==1&&returned.front().ref.id=="front",
            "accepted label can reappear without a new query");
}

void candidateBudgetCapsMillionScaleWork() {
    std::vector<MapLabelSource> sources;sources.reserve(100000);
    for(int i=0;i<100000;++i)
        sources.push_back(label(("L"+std::to_string(i)).c_str(),
            -4.0+(i%1000)*.008,-4.0+((i/1000)%100)*.08,double(i%100)));
    MapLabelEngine engine;engine.setSources(std::move(sources),1);
    MapLabelLayoutOptions options;options.viewportWidth=800;options.viewportHeight=600;
    options.maxCandidates=2048;options.maxPlaced=512;
    engine.layout(flatView(),options);
    require(engine.stats().candidatesExamined<=2048,"candidate budget bounds layout work");
    require(engine.placements().size()<=512,"placement budget bounds collision work");
}

void territorialFlagsFollowWebZoomAndNeverEvictNames() {
    auto named=label("country",0,0,100);named.ref.domain="territorial";named.flagVisible=true;
    MapLabelEngine engine;engine.setSources({named},1);
    MapLabelLayoutOptions options;options.viewportWidth=800;options.viewportHeight=600;
    options.zoom=1.79;
    auto placed=engine.layout(flatView(),options);
    require(placed.size()==1&&placed[0].nameVisible&&!placed[0].flagVisible,
            "below zoom 1.8 the name remains but its flag is hidden");
    options.zoom=1.8;placed=engine.layout(flatView(),options);
    require(placed.size()==1&&placed[0].flagVisible,"flags appear at zoom 1.8");
    const auto queries=engine.stats().queries,layouts=engine.stats().layouts;
    placed=engine.reproject(flatView(),1.79);
    require(placed.size()==1&&!placed[0].flagVisible,"zoom interaction hides flags without a source rebuild");
    placed=engine.reproject(flatView(),1.8);
    require(placed.size()==1&&placed[0].flagVisible,"zoom interaction restores flags without a source rebuild");
    require(engine.stats().queries==queries&&engine.stats().layouts==layouts,"flag zoom does not trigger document-wide preparation");

    auto neighbor=label("neighbor",11.75,0,10); // 94 px apart: names fit, flag extension does not.
    engine.setSources({named,neighbor},2);
    placed=engine.layout(flatView(),options);
    require(placed.size()==2,"adding a flag must not evict either already placed name");
    require(!placed[0].flagVisible,"suppress only the flag when its expanded box overlaps another label");

    named.nameVisible=false;named.width=18;named.height=12;
    engine.setSources({named},3);options.zoom=1.79;
    require(engine.layout(flatView(),options).empty(),"flag-only labels vanish below the flag zoom threshold");
    options.zoom=1.8;
    require(engine.layout(flatView(),options).size()==1,"flag-only labels appear at the flag zoom threshold");
}
void workingSetBudgetDoesNotEvictActiveLabels() {
    MapLabelEngine engine;
    engine.setSources({label("one",0,0,1)},1);
    engine.setResourceBudget(0);
    auto snapshot=engine.resourceCacheSnapshot();
    require(snapshot.residentBytes>0&&snapshot.protectedBytes==snapshot.residentBytes,"active label working set protected");
    require(snapshot.protectedOverBudgetBytes==snapshot.residentBytes,"small budget reports overage");
    MapLabelLayoutOptions options;engine.layout(flatView(),options);
    const auto before=engine.stats();engine.reproject(flatView(),options.zoom);
    require(engine.stats().sourceRebuilds==before.sourceRebuilds&&engine.stats().layouts==before.layouts,"accounting does not rebuild");
    const auto epoch=engine.resourceCacheSnapshot().scopeEpoch;engine.clear();
    require(engine.resourceCacheSnapshot().residentBytes==0&&engine.resourceCacheSnapshot().scopeEpoch>epoch,"clear releases working set");
}

}

int main() {
    workingSetBudgetDoesNotEvictActiveLabels();
    spatialQueryAndCollisionAreEngineOwned();
    selectedAndPinnedBypassOrdinaryCollisionAndBounds();
    interactionReprojectionDoesNotQueryOrRelayout();
    globeReprojectionDropsBackHemisphere();
    candidateBudgetCapsMillionScaleWork();
    territorialFlagsFollowWebZoomAndNeverEvictNames();
}

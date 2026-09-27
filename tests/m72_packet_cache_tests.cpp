#include "geometrypacketcache.h"
#include "mapviewstate.h"
#include <stdexcept>

namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
pandoeditor::Geometry box(double west) {
    return {"Polygon",{},{},{{{{west,0},{west+1,0},{west+1,1},{west,1},{west,0}}}}};
}
void styleSelectionAndViewReuseGeometry() {
    GeometryPacketCache cache;
    const auto source=box(0);
    const pandoeditor::ObjectRef object{"territorial","A"};
    const pandoeditor::GeometryRef geometry{"A",1};
    auto first=cache.polygon(object,geometry,source);
    RenderStyle changed;changed.color=0xffeecc;changed.alpha=.4f;
    auto style=cache.polygon(object,geometry,source);
    auto selection=cache.polygon(object,geometry,source);
    MapViewState moved;moved.centerLongitude=12;moved.revision=3;
    auto view=cache.polygon(object,geometry,source);
    require(changed.color==0xffeecc&&moved.revision==3,"test inputs vary");
    require(first.positions==style.positions&&first.positions==selection.positions&&
        first.positions==view.positions,"style selection view reuse immutable positions");
    require(cache.stats().builds==1&&cache.stats().hits==3,"one build three reuse hits");
}
void oneGeometryVersionChangeRebuildsOnlyOne() {
    GeometryPacketCache cache;
    const pandoeditor::ObjectRef a{"territorial","A"},b{"territorial","B"};
    auto first=cache.polygon(a,{"A",1},box(0));
    auto unrelated=cache.polygon(b,{"B",1},box(20));
    auto modified=cache.polygon(a,{"A",2},box(1));
    auto sameB=cache.polygon(b,{"B",1},box(20));
    require(first.positions!=modified.positions,"new version rebuilds A");
    require(unrelated.positions==sameB.positions,"B packet unchanged");
    require(cache.stats().builds==3&&cache.stats().hits==1,"only affected object rebuilt");
}
void lodAndPolicyAreInKey() {
    GeometryPacketCache cache;auto source=box(0);
    auto a=cache.polygon({"territorial","A"},{"A",1},source,0,ProjectionPreparationPolicy::Geographic);
    auto lod=cache.polygon({"territorial","A"},{"A",1},source,1,ProjectionPreparationPolicy::Geographic);
    auto policy=cache.polygon({"territorial","A"},{"A",1},source,0,ProjectionPreparationPolicy::GlobeReady);
    require(a.positions!=lod.positions&&a.positions!=policy.positions,"LOD/policy partition cache");
}
}
int main(){styleSelectionAndViewReuseGeometry();oneGeometryVersionChangeRebuildsOnlyOne();lodAndPolicyAreInKey();}

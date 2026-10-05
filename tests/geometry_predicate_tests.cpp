#include <pandoeditor/geometrypredicates.h>
#include <cassert>
using namespace pandoeditor;
static Geometry square(double x,double y,double size){Geometry g;g.type="Polygon";g.polygons.push_back(Polygon{Ring{{x,y},{x+size,y},{x+size,y+size},{x,y+size},{x,y}}});return g;}
static Geometry rectangle(double west,double south,double east,double north) {
    Geometry g;g.type="Polygon";g.polygons={{{{west,south},{west,north},{east,north},{east,south},{west,south}}}};return g;
}
static Geometry seamPieces(double west,double south,double east,double north) {
    Geometry g;g.polygons=rectangle(west,south,180,north).polygons;g.polygons.push_back(rectangle(-180,south,east,north).polygons.front());return g;
}
int main() {
    const auto parent=square(0,0,10),child=square(1,1,2),outside=square(9,1,2);
    assert(geometryContains(parent,child));assert(!geometryContains(parent,outside));
    assert(!geometrySignificantOverlap(parent,square(10,0,1)));assert(geometrySignificantOverlap(parent,child));
    const auto rawParent=rectangle(178,-4,-178,4),rawChild=rectangle(179,-2,-179,2);
    const auto splitParent=seamPieces(178,-4,-178,4),splitChild=seamPieces(179,-2,-179,2);
    assert(geometryContains(rawParent,rawChild));
    assert(geometryContains(rawParent,splitChild));
    assert(geometryContains(splitParent,rawChild));
    assert(geometryContains(splitParent,splitChild));
    assert(geometrySignificantOverlap(rawParent,rawChild));
    assert(geometrySignificantOverlap(rawParent,splitChild));
    assert(!geometryContains(rawParent,rectangle(179,3,-179,5)));
    assert(!geometryContains(rawParent,square(-.5,-.5,1)));
    assert(!geometrySignificantOverlap(rawParent,square(-.5,-.5,1)));
    auto holed=rawParent;holed.polygons.front().push_back(rectangle(179,-1,-179,1).polygons.front().front());
    assert(!geometryContains(holed,rectangle(179.2,-.5,179.8,.5)));
    assert(!geometrySignificantOverlap(holed,rectangle(179.2,-.5,179.8,.5)));
    assert(!geometryContains(holed,splitChild));
    Geometry notched;notched.polygons={
        {{{178,-4},{178,4},{180,4},{180,1},{179,1},{179,-1},{180,-1},{180,-4},{178,-4}}},
        {{{-180,-4},{-180,-1},{-179,-1},{-179,1},{-180,1},{-180,4},{-178,4},{-178,-4},{-180,-4}}}};
    assert(!geometryContains(notched,splitChild));
    assert(!geometrySignificantOverlap(notched,seamPieces(179.2,-.5,-179.2,.5)));
    auto world=rectangle(-180,-80,180,80);world.polygons.front().push_back(rectangle(179,-1,-179,1).polygons.front().front());
    assert(geometryContains(world,square(-.5,-.5,1)));
    assert(!geometryContains(world,rectangle(179.2,-.5,179.8,.5)));
    assert(!geometryContains(world,rectangle(-179.8,-.5,-179.2,.5)));
    assert(!geometrySignificantOverlap(world,rectangle(-179.8,-.5,-179.2,.5)));
    auto worldWideHole=rectangle(-180,-80,180,80);worldWideHole.polygons.front().push_back(rectangle(176,-4,-176,4).polygons.front().front());
    assert(!geometrySignificantOverlap(worldWideHole,rectangle(178,-3,-178,3)));
    Geometry clippedWorld;clippedWorld.type="Polygon";clippedWorld.polygons={{{{-180,-80},{-180,-4},{-176,-4},{-176,4},{-180,4},{-180,80},{180,80},{180,4},{176,4},{176,-4},{180,-4},{180,-80},{-180,-80}}}};
    assert(geometryContains(clippedWorld,worldWideHole));
    assert(geometryContains(worldWideHole,clippedWorld));
    Geometry wide;wide.type="Polygon";wide.polygons={{{{-160,-5},{-160,5},{0,5},{160,5},{160,-5},{0,-5},{-160,-5}}}};
    assert(geometryContains(wide,square(-.5,-.5,1)));
    assert(!geometrySignificantOverlap(wide,rawChild));
    auto commonA=seamPieces(175,-5,-175,5),commonB=commonA;
    commonA.polygons.push_back(square(-10,0,1).polygons.front());commonB.polygons.push_back(square(9,0,1).polygons.front());
    assert(geometrySignificantOverlap(commonA,commonB));
    assert(geometrySignificantOverlap(commonB,commonA));
    // Reading a raw crossing geometry must remain stable and never rewrite it.
    assert(geometryContains(rawParent,splitChild));
    assert(rawParent.polygons.front().front()[2].x==-178);
    assert(rawChild.polygons.front().front()[2].x==-179);
    assert(wide.polygons.front().front().size()==7);
}

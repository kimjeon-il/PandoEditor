#include <pandoeditor/geobounds.h>
#include <cmath>
#include <stdexcept>

using namespace pandoeditor;
namespace {
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
Polygon box(double west,double south,double east,double north) {
    return {{{west,south},{east,south},{east,north},{west,north},{west,south}}};
}
void ordinaryBounds() {
    Geometry shape{"Polygon",{},{},{box(10,50,11,51)}};
    const auto b=geometryBounds(shape);
    require(!b.wrapsDateline&&b.west==10&&b.east==11&&b.south==50&&b.north==51,"ordinary bounds");
    require(contains(b,{10.5,50.5})&&!contains(b,{12,50}),"ordinary containment");
}
void datelineBoundsStayWrapped() {
    Geometry shape{"Polygon",{},{},{{
        {{179,70},{-179,70},{-179,80},{179,80},{179,70}},
        {{179.5,72},{-179.5,72},{-179.5,74},{179.5,74},{179.5,72}}
    }}};
    const auto b=geometryBounds(shape);
    require(b.wrapsDateline&&b.west==179&&b.east==-179,"wrapped longitude bounds");
    const auto halves=splitWrappedBounds(b);
    require(halves.size()==2&&halves[0].west==179&&halves[0].east==180&&
        halves[1].west==-180&&halves[1].east==-179,"split wrapped bounds");
    require(contains(b,{179.5,72})&&contains(b,{-179.5,72})&&!contains(b,{0,72}),"wrapped point");
    require(intersects(b,{178,72,180,74,false})&&intersects(b,{-180,72,-178,74,false})&&
        !intersects(b,{-5,72,5,74,false}),"wrapped intersections");
}
void polarBoundsStayFinite() {
    Geometry shape{"MultiPolygon",{},{},{{{{0,89.9},{120,89.8},{-120,89.8},{0,89.9}}},
        {{{10,-89.9},{20,-89.8},{0,-89.8},{10,-89.9}}}}};
    const auto b=geometryBounds(shape);
    require(std::isfinite(b.west)&&std::isfinite(b.east)&&std::isfinite(b.south)&&
        std::isfinite(b.north)&&b.south==-89.9&&b.north==89.9,"polar finite extent");
}
void holesDoNotExpandOuterBoundsIncorrectly() {
    Geometry shape{"Polygon",{},{},{{box(0,0,2,2).front(),
        box(50,50,51,51).front()}}};
    const auto b=geometryBounds(shape);
    require(b.west==0&&b.east==2&&b.south==0&&b.north==2,"holes do not enlarge outer bounds");
}
void multiPolygonBoundsAndParts() {
    Geometry shape{"MultiPolygon",{},{},{box(178,0,179,1),box(-179,0,-178,1)}};
    const auto pieces=geometryPartBounds(shape);
    require(pieces.size()==2&&!pieces[0].wrapsDateline&&!pieces[1].wrapsDateline,
        "separate island bounds");
    const auto whole=geometryBounds(shape);
    require(whole.wrapsDateline&&whole.west==178&&whole.east==-178,
        "multi-part wrapped union");
}
}
int main() {
    ordinaryBounds();datelineBoundsStayWrapped();polarBoundsStayFinite();
    holesDoNotExpandOuterBoundsIncorrectly();multiPolygonBoundsAndParts();
}

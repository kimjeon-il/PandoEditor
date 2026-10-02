#include <pandoeditor/map/projectionengine.h>
#include <pandoeditor/document.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
bool near(double left,double right,double tolerance=1e-9) {return std::abs(left-right)<tolerance;}
MapViewState flat() {
    MapViewState view;view.viewportWidth=600;view.viewportHeight=400;
    view.translateX=300;view.translateY=200;view.scale=100;
    return view;
}
void flatProjectUnprojectRoundTrip() {
    const auto view=flat();
    const auto projected=projectPoint({12.25,51.75},view);
    require(projected.finite&&projected.visibleHemisphere,"flat point visible and finite");
    const auto restored=unprojectFlat(projected.x,projected.y,view);
    require(near(restored.x,12.25)&&near(restored.y,51.75),"flat roundtrip");
}
void flatWorldOffsetsMatchWebOracle() {
    auto view=flat();
    require(visibleFlatWorldOffsets(view)==std::vector<double>{0},"center web world copy");
    view.translateX=0;
    require(visibleFlatWorldOffsets(view)==(std::vector<double>{0,360}),"right web world copy");
    view.translateX=5000;
    require(visibleFlatWorldOffsets(view)==(std::vector<double>{-360}),"web fallback nearest copy");
}
void datelineUsesNearestWorldCopy() {
    auto view=flat();view.centerLongitude=179;
    const auto ordinary=projectPoint({-179,70},view);
    const auto wrapped=projectPoint({-179,70},view,360);
    require(wrapped.finite&&std::abs(wrapped.x-view.translateX)<5,"near dateline copy visible");
    require(std::abs(ordinary.x-view.translateX)>600,"unwrapped copy is distant");
}
void globeFrontBackAndInverse() {
    auto view=flat();view.mode=ProjectionMode::Globe;
    const auto front=projectPoint({0,0},view);
    const auto back=projectPoint({180,0},view);
    require(front.visibleHemisphere&&front.frontness>0.99&&front.finite,"globe front");
    require(!back.visibleHemisphere&&back.frontness<-.99&&back.finite,"globe back");
    const auto point=projectPoint({30,20},view);
    const auto restored=unprojectGlobe(point.x,point.y,view);
    require(restored&&near(restored->x,30)&&near(restored->y,20),"globe inverse front");
    require(!unprojectGlobe(view.translateX+2*view.scale,view.translateY,view),"outside globe has no hit");
}
void bothPolesFinite() {
    auto view=flat();view.mode=ProjectionMode::Globe;
    view.centerLatitude=89;
    auto north=projectPoint({0,90},view);
    require(north.finite&&north.visibleHemisphere,"north pole finite");
    view.centerLatitude=-89;
    auto south=projectPoint({0,-90},view);
    require(south.finite&&south.visibleHemisphere,"south pole finite");
}
void viewChangeLeavesCanonicalGeometryUntouched() {
    pandoeditor::Geometry shape{"Polygon",{},{},{{{{179,70},{-179,70},{-179,80},{179,70}}}}};
    const auto before=shape.polygons.front().front();
    auto view=flat();view.mode=ProjectionMode::Globe;view.rotationRoll=25;view.revision=3;
    for(const auto& vertex:before)require(projectPoint(vertex,view).finite,"project all vertices");
    const auto& after=shape.polygons.front().front();
    for(std::size_t i=0;i<before.size();++i)
        require(before[i].x==after[i].x&&before[i].y==after[i].y,"canonical coordinates unchanged");
}
}
int main(int argc,char** argv) {
    if(argc==2&&std::string(argv[1])=="--flat-offsets") {
        auto view=flat();
        auto print=[&]() {
            const auto offsets=visibleFlatWorldOffsets(view);
            for(std::size_t i=0;i<offsets.size();++i) {
                if(i)std::cout<<',';
                std::cout<<offsets[i];
            }
            std::cout<<'\n';
        };
        print();view.translateX=0;print();view.translateX=5000;print();
        return 0;
    }
    flatProjectUnprojectRoundTrip();flatWorldOffsetsMatchWebOracle();datelineUsesNearestWorldCopy();
    globeFrontBackAndInverse();bothPolesFinite();viewChangeLeavesCanonicalGeometryUntouched();
}

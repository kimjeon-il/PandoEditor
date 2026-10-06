#include <pandoeditor/map/editcoordinates.h>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace pandoeditor;
using namespace pandoeditor::map;
namespace {
void require(bool ok,const char* message) { if(!ok)throw std::runtime_error(message); }
bool same(double a,double b) {
    return (std::isnan(a)&&std::isnan(b))||std::memcmp(&a,&b,sizeof(double))==0;
}
bool same(Point a,Point b) { return same(a.x,b.x)&&same(a.y,b.y); }
// Independent copies of the pre-extraction expressions. This translation unit
// also disables contraction, so exact comparisons cover each intermediate.
Point oldProject(Point p,const MapCameraMetrics& m) { return {p.x*m.cosLatitude-m.minX,m.maxLatitude-p.y}; }
Point oldUnproject(Point p,const MapCameraMetrics& m) { return {(p.x+m.minX)/m.cosLatitude,m.maxLatitude-p.y}; }
void projectionExpressions() {
    const double infinity=std::numeric_limits<double>::infinity();
    const double nan=std::numeric_limits<double>::quiet_NaN();
    const std::array<MapCameraMetrics,5> metrics{{{360,180,1,-180,90},
        {27,18,.17364817766693041,21.31415926535898,88},
        {1,.001,.01,-1.7,89.99999999999999},
        {1,1,1,0,0},{1,1,1,-0.,-0.}}};
    const std::array<Point,12> points{{{-180,-90},{180,90},{0,0},{-0.,-0.},
        {12.345678901234567,81.12345678901234},{540,100},{1e-300,-1e-300},
        {1e300,-1e300},{infinity,0},{0,-infinity},{nan,1},{1,nan}}};
    for(const auto& m:metrics)for(const auto p:points) {
        require(same(editGeographicToMap(p,m),oldProject(p,m)),"geographic-to-map expression changed");
        require(same(editMapToGeographic(p,m),oldUnproject(p,m)),"map-to-geographic expression changed");
    }
    require(editMapToGeographic({180,-10},metrics[0]).y==100,"raw edit input must retain invalid latitude for detached gestures");
    require(editGeographicToMap({540,0},metrics[0]).x==720,"raw longitude must not wrap");
}
void screenExpressions() {
    const double infinity=std::numeric_limits<double>::infinity();
    const double nan=std::numeric_limits<double>::quiet_NaN();
    const std::array<Point,8> points{{{.1,18},{0,-0.},{213,177},{-25.5,360},
        {1e-300,-1e-300},{1e300,-1e300},{infinity,-infinity},{nan,nan}}};
    for(double scale:std::array<double,9>{{3.7,.125,1e-200,1e200,0.,-0.,-1.,infinity,nan}}) {
        MapCameraDisplay display;display.mapScale=scale;display.originX=123.4;display.originY=-98.125;
        display.view.devicePixelRatio=3;
        for(const auto p:points) {
            require(same(editMapToScreen(p,display),Point{display.originX+p.x*scale,display.originY+p.y*scale}),"map-to-screen operation order changed");
            require(same(editScreenToMap(p,display),Point{(p.x-display.originX)/scale,(p.y-display.originY)/scale}),"screen-to-map operation order changed");
            require(same(editScreenDeltaToMap(p,display),Point{p.x/scale,p.y/scale}),"screen delta must divide without origin or DPR");
            require(same(editPixelRadiusToMap(p.x,display),p.x/scale),"radius must preserve raw division including negative/NaN");
        }
    }
    MapCameraDisplay display;display.mapScale=3.7;display.originX=123.4;
    const auto local=editScreenToMap({.1,0},display);
    const auto reconstructed=editMapToScreen(local,display);
    require(same(reconstructed.x,0x1.9999999999c00p-4),"pointer reconstruction lost its exact round trip");
    require(!same(reconstructed.x,.1),"test must expose a non-identity round trip");
    // An algebraic fma contracts this pair to a different nonzero value.
    display.mapScale=0x1.0000000000001p+0;display.originX=-0x1.0000000000002p+0;
    require(same(editMapToScreen({0x1.0000000000001p+0,0},display).x,0.),"screen multiplication and addition must not contract");
}
void snapCandidatePolicy() {
    const MapCameraMetrics metrics{360,180,.01,-1.8,90};
    MapCameraDisplay display;display.mapScale=2;display.originX=19;display.originY=-21;
    for(Point p:std::array<Point,4>{{{540,90},{-720,-90},{0,0},{-0.,-0.}}}) {
        const auto projected=editSnapPointToScreen(p,metrics,display);
        require(projected.has_value(),"finite noncanonical longitude or endpoint was rejected");
        const auto local=oldProject(p,metrics);
        require(same(*projected,Point{display.originX+local.x*display.mapScale,display.originY+local.y*display.mapScale}),"snap projection skipped an intermediate");
    }
    const double infinity=std::numeric_limits<double>::infinity(),nan=std::numeric_limits<double>::quiet_NaN();
    for(Point p:std::array<Point,6>{{{0,std::nextafter(90.,infinity)},{0,std::nextafter(-90.,-infinity)},
        {infinity,0},{0,-infinity},{nan,0},{0,nan}}})
        require(!editSnapPointToScreen(p,metrics,display),"invalid snap candidate accepted");
    display.mapScale=std::numeric_limits<double>::max();
    require(!editSnapPointToScreen({180,-90},metrics,display),"overflowed snap screen coordinate accepted");
}
}
int main() {
    int failures=0;
    for(const auto& test:std::array<std::pair<const char*,void(*)()>,3>{{
        {"projection expressions",projectionExpressions},{"screen expressions",screenExpressions},{"snap candidate policy",snapCandidatePolicy}}}) {
        try {test.second();std::cout<<"PASS "<<test.first<<'\n';}
        catch(const std::exception& error){++failures;std::cerr<<"FAIL "<<test.first<<": "<<error.what()<<'\n';}
    }
    return failures?1:0;
}

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
void presentationExpressions() {
    const double infinity=std::numeric_limits<double>::infinity();
    const double nan=std::numeric_limits<double>::quiet_NaN();
    const std::array<EditCoordinateRect,7> rectangles{{{.1,18,12.25,9.5},{0,-0.,0,-0.},
        {-25.5,360,-5,3},{1e-300,-1e-300,1e-200,1e-200},
        {1e300,-1e300,1e300,1e300},{infinity,-infinity,1,2},{nan,nan,nan,nan}}};
    for(double scale:std::array<double,9>{{3.7,.125,1e-200,1e200,0.,-0.,-1.,infinity,nan}}) {
        MapCameraDisplay display;display.mapScale=scale;display.originX=123.4;display.originY=-98.125;
        display.view.devicePixelRatio=3;
        for(const auto rect:rectangles) {
            const auto screen=editMapRectToScreen(rect,display);
            require(same(screen.x,display.originX+rect.x*scale)&&same(screen.y,display.originY+rect.y*scale)&&
                    same(screen.width,rect.width*scale)&&same(screen.height,rect.height*scale),"reference screen rect operation order changed");
            const Point start{rect.x,rect.y},delta{rect.width,rect.height};
            require(same(editMapDragPosition(start,delta,display),Point{start.x+delta.x/scale,start.y+delta.y/scale}),"reference drag must add divided delta to gesture origin");
            require(same(editLabelDragToMap(start,delta,display),Point{(start.x+delta.x-display.originX)/scale,(start.y+delta.y-display.originY)/scale}),"label release must add before subtracting camera origin");
        }
    }
    for(const auto metrics:std::array<MapCameraMetrics,4>{{{360,180,1,-180,90},
        {27,18,.17364817766693041,21.31415926535898,88},{1,1,1,0,0},{1,1,.01,-1.7,89.99999999999999}}})
        for(const auto rect:rectangles) {
            const auto bounds=editMapRectGeographicBounds(rect,metrics);
            require(same(bounds.west,(rect.x+metrics.minX)/metrics.cosLatitude)&&
                    same(bounds.east,(rect.x+rect.width+metrics.minX)/metrics.cosLatitude)&&
                    same(bounds.north,metrics.maxLatitude-rect.y)&&
                    same(bounds.south,metrics.maxLatitude-rect.y-rect.height),"reference geographic bounds operation order changed");
        }
    MapCameraMetrics metrics{1,1,4.119136647093361,506.1726181161889,46.16336361639907};
    const EditCoordinateRect rect{-474.8858372985021,73.05619662451696,-973.1410429796266,73.56835243422456};
    const auto bounds=editMapRectGeographicBounds(rect,metrics);
    require(same(bounds.east,-228.65331812348407),"east changed its left-associative intermediate");
    require(!same(bounds.east,(rect.x+metrics.minX+rect.width)/metrics.cosLatitude),"east sentinel must expose reassociation");
    require(same(bounds.south,-100.46118544234245),"south changed its left-associative intermediate");
    require(!same(bounds.south,metrics.maxLatitude-(rect.y+rect.height)),"south sentinel must expose reassociation");
    MapCameraDisplay display;display.mapScale=3.004110376932265;display.originX=67.84425566533832;
    require(same(editLabelDragToMap({937.5638261969304,0},{-495.95395331482916,0},display).x,124.41807068302353),"label release reassociated screen addition");
    display.mapScale=3.9520887890369942;
    require(same(editMapDragPosition({456.6768459788768,0},{-748.4936908874556,0},display).x,267.28492417697794),"reference drag changed its division-first intermediate");
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
    for(const auto& test:std::array<std::pair<const char*,void(*)()>,4>{{
        {"presentation expressions",presentationExpressions},{"projection expressions",projectionExpressions},{"screen expressions",screenExpressions},{"snap candidate policy",snapCandidatePolicy}}}) {
        try {test.second();std::cout<<"PASS "<<test.first<<'\n';}
        catch(const std::exception& error){++failures;std::cerr<<"FAIL "<<test.first<<": "<<error.what()<<'\n';}
    }
    return failures?1:0;
}

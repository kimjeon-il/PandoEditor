#include <pandoeditor/map/editgeometry.h>
#include <pandoeditor/map/editcoordinates.h>
#include <pandoeditor/map/sharedboundary.h>
#include "territorial_fixture.h"
#include <algorithm>
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
bool same(double a,double b) { return std::memcmp(&a,&b,sizeof(double))==0; }
bool same(Point a,Point b) { return same(a.x,b.x)&&same(a.y,b.y); }
bool same(const Ring& a,const Ring& b) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)if(!same(a[i],b[i]))return false;
    return true;
}
bool same(const Geometry& a,const Geometry& b) {
    if(a.type!=b.type||!same(a.points,b.points)||a.lines.size()!=b.lines.size()||a.polygons.size()!=b.polygons.size())return false;
    for(std::size_t i=0;i<a.lines.size();++i)if(!same(a.lines[i],b.lines[i]))return false;
    for(std::size_t i=0;i<a.polygons.size();++i) {
        if(a.polygons[i].size()!=b.polygons[i].size())return false;
        for(std::size_t j=0;j<a.polygons[i].size();++j)if(!same(a.polygons[i][j],b.polygons[i][j]))return false;
    }
    return true;
}
void index(EditVertexIndex i,int p,int r,int v) { require(i.polygon==p&&i.ring==r&&i.vertex==v,"hit/no-hit index or last-tie selection changed"); }
Geometry polygon(Ring ring) { return {"Polygon",{},{},{{std::move(ring)}}}; }
Geometry box(double x,double y,double w,double h) { return polygon({{x,y},{x+w,y},{x+w,y+h},{x,y+h},{x,y}}); }
void vertexPolicy() {
    const MapCameraMetrics metrics{4,4,.25,0,0};
    Geometry point{"Point",{{2,0}},{},{}};
    index(nearestEditVertex(point,{.5,0},0,metrics),0,0,0);
    Geometry points{"MultiPoint",{{-2,0},{2,0},{2,0}},{},{}};
    index(nearestEditVertex(points,{0,0},.5,metrics),0,0,2);
    index(nearestEditVertex(points,{0,0},.49,metrics),-1,0,-1);
    Geometry lines{"MultiLineString",{},{{{-2,0},{2,0}},{{2,0},{-2,0}}},{}};
    index(nearestEditVertex(lines,{0,0},.5,metrics),1,0,1);
    Geometry line{"LineString",{},{{{2,0},{2,2}}},{}};
    index(nearestEditVertex(line,{0,0},.5,metrics),0,0,0);
    const auto area=box(2,0,2,2);
    index(nearestEditVertex(area,{0,0},.5,metrics),-1,-1,-1); // geographic distance, unlike lines
    index(nearestEditVertex(area,{.5,0},0,metrics),0,0,0); // duplicate closing vertex excluded
    auto multi=area;multi.type="MultiPolygon";multi.polygons.push_back(area.polygons[0]);
    multi.polygons[1].push_back({{2,0},{3,0},{3,1},{2,0}});
    index(nearestEditVertex(multi,{.5,0},0,metrics),1,1,0);
    const double nan=std::numeric_limits<double>::quiet_NaN();
    index(nearestEditVertex(points,{0,0},nan,metrics),-1,0,-1);
    index(nearestEditVertex(area,{0,0},nan,metrics),-1,-1,-1);
    index(nearestEditVertex(points,{0,0},std::numeric_limits<double>::infinity(),metrics),0,0,2);
}
void segmentPolicy() {
    const MapCameraMetrics metrics{4,4,.25,0,0};
    const Geometry points{"MultiPoint",{{0,0},{1,1}},{},{}};
    require(!nearestEditSegment(points,{0,0},10,metrics),"points gained insertable segments");
    Geometry line{"LineString",{},{{{2,0},{2,2}}},{}};
    auto hit=nearestEditSegment(line,{0,-1},.5,metrics);
    require(hit.has_value(),"line overlay-space segment missed");index(hit->index,0,0,1);
    require(same(hit->coordinate,{2,1}),"segment geographic interpolation changed");
    const auto area=box(2,0,2,2);
    require(!nearestEditSegment(area,{0,-1},.5,metrics),"polygon segment stopped using geographic distance");
    hit=nearestEditSegment(area,{.5,0},0,metrics);
    require(hit.has_value(),"closed polygon segment missed");index(hit->index,0,0,4);
    require(same(hit->coordinate,{2,0}),"last closing segment tie changed");
    Geometry zero{"LineString",{},{{{2,1},{2,1}}},{}};
    hit=nearestEditSegment(zero,{.5,-1},0,metrics);require(hit.has_value(),"zero-length segment missed");
    index(hit->index,0,0,1);require(same(hit->coordinate,{2,1}),"zero-length segment t changed");
    hit=nearestEditSegment(line,{.5,1},1,metrics);require(hit.has_value(),"start clamp missed");require(same(hit->coordinate,{2,0}),"start t not clamped");
    hit=nearestEditSegment(line,{.5,-3},1,metrics);require(hit.has_value(),"end clamp missed");require(same(hit->coordinate,{2,2}),"end t not clamped");
    auto multi=line;multi.type="MultiLineString";multi.lines.push_back(line.lines.front());
    hit=nearestEditSegment(multi,{.5,-1},0,metrics);require(hit.has_value(),"multiline missed");index(hit->index,1,0,1);
    auto holes=box(0,0,10,10);holes.polygons[0].push_back(box(2,2,2,2).polygons[0][0]);
    hit=nearestEditSegment(holes,{.75,-2},0,metrics);require(hit.has_value(),"hole segment missed");index(hit->index,0,1,1);require(same(hit->coordinate,{3,2}),"hole interpolation changed");
    require(!nearestEditSegment(line,{.5,-1},std::numeric_limits<double>::quiet_NaN(),metrics),"NaN tolerance hit a segment");
    // Pin the original multiply/add interpolation separately from projected t.
    const Point a{1.23456789012345,2.34567890123456},b{4.56789012345678,7.89012345678901},query{2.3,-4.2};
    const MapCameraMetrics identity{};Geometry diagonal{"LineString",{},{{a,b}},{}};
    const Point pa{a.x,-a.y},pb{b.x,-b.y};
    const auto dx=pb.x-pa.x,dy=pb.y-pa.y,denominator=dx*dx+dy*dy;
    const double t=std::clamp(((query.x-pa.x)*dx+(query.y-pa.y)*dy)/denominator,0.,1.);
    hit=nearestEditSegment(diagonal,query,10,identity);require(hit.has_value(),"diagonal segment missed");
    require(same(hit->coordinate,{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t}),"interpolation evaluation order changed");
}
void append(ProjectDocument& document,const std::string& id,Geometry shape,bool locked=false) {
    const GeometryRef ref{id,1};document.geometries.insert(ref,std::move(shape));
    appendTerritory(document,{id,id,"",UnitKind::General,locked},ref);
    document.presentation.objectStyles[territorialRef(id)]={};
}
void boundaryPolicy() {
    ProjectDocument document;document.documentId="edit-hit-boundary";
    append(document,"A",box(0,0,1,1));append(document,"B",box(1,0,1,1));
    auto session=sharedboundary::Session::prepare(document,validateDocument(document),{territorialRef("A"),territorialRef("B")});
    require(session->valid(),"boundary fixture invalid");
    const MapCameraMetrics metrics{2,1,.25,0,0};
    int expected=-1;double best=.5*.5;
    for(std::size_t i=0;i<session->nodes().size();++i)if(session->canMove(i)) {
        const auto p=session->nodes()[i].coordinate;const double dx=p.x*.25-.25,dy=-p.y+.5;
        const double d=dx*dx+dy*dy;if(d<=best){best=d;expected=int(i);}
    }
    require(expected>=0&&nearestMovableBoundaryNode(*session,{.25,-.5},.5,metrics)==expected,"boundary overlay distance/order changed");
    require(nearestMovableBoundaryNode(*session,{.25,-.5},.49,metrics)==-1,"boundary no-hit changed");
    require(nearestMovableBoundaryNode(*session,{.25,-.5},std::numeric_limits<double>::quiet_NaN(),metrics)==-1,"boundary NaN tolerance changed");
    require(session->changedDrafts().empty()&&!session->canUndo()&&!session->dragging(),"hit test mutated session");
    append(document,"C",box(0,1,2,1));
    session=sharedboundary::Session::prepare(document,validateDocument(document),{territorialRef("A"),territorialRef("B")});
    require(session->valid(),"fixed-boundary fixture invalid");
    int ineligible=0;
    // An unselected third owner fixes the common upper endpoint.
    for(std::size_t i=0;i<session->nodes().size();++i)if(!session->canMove(i)) {
        ++ineligible;const auto p=session->nodes()[i].coordinate;
        require(nearestMovableBoundaryNode(*session,{p.x*.25,-p.y},0,metrics)==-1,"ineligible boundary node selected");
    }
    require(ineligible>0,"fixture must exercise Session::canMove rejection");
    append(document,"locked",box(.75,0,.25,.25),true);
    staticParentRelation(document,"locked").parentId="A";
    session=sharedboundary::Session::prepare(document,validateDocument(document),{territorialRef("A"),territorialRef("B")});
    require(session->valid(),"locked-descendant fixture invalid");
    bool locked=false;
    for(std::size_t i=0;i<session->nodes().size();++i)if(same(session->nodes()[i].coordinate,{1,0})) {
        locked=!session->nodes()[i].fixed&&!session->canMove(i);
    }
    require(locked&&nearestMovableBoundaryNode(*session,{.25,0},0,metrics)==-1,"locked descendant eligibility was bypassed");
    sharedboundary::Session empty;
    require(nearestMovableBoundaryNode(empty,{0,0},10,metrics)==-1,"invalid boundary session selected a node");
}
void containmentPolicy() {
    auto shape=box(0,0,10,10);shape.polygons[0].push_back(box(2,2,2,2).polygons[0][0]);
    require(editGeometryContainsPoint(shape,{1,1}),"outer interior missed");
    require(!editGeometryContainsPoint(shape,{3,3}),"hole not excluded");
    require(editGeometryContainsPoint(shape,{0,0})&&!editGeometryContainsPoint(shape,{10,10}),"plain ray-crossing boundary asymmetry changed");
    require(!editGeometryContainsPoint(shape,{2,2})&&editGeometryContainsPoint(shape,{4,4}),"hole boundary ray-crossing policy changed");
    shape.type="MultiPolygon";shape.polygons.push_back(box(20,20,2,2).polygons[0]);
    require(editGeometryContainsPoint(shape,{21,21}),"later polygon missed");
    shape.polygons.push_back({});shape.polygons.push_back({{}});
    require(!editGeometryContainsPoint(shape,{15,15}),"empty polygon/ring changed containment");
    require(!editGeometryContainsPoint(shape,{std::numeric_limits<double>::quiet_NaN(),1}),"NaN containment changed");
    const auto seam=polygon({{170,-1},{-170,-1},{-170,1},{170,1},{170,-1}});
    require(editGeometryContainsPoint(seam,{0,0})&&!editGeometryContainsPoint(seam,{179,0}),"edit containment acquired seam normalization");
}
void translationPolicy() {
    const MapCameraMetrics metrics{360,180,.17364817766693041,-21.31415926535898,88};
    Geometry original{"MultiPolygon",{{-0.,0}},{{{12.1,2.3},{-4.5,6.7}}},{{{{1,1},{2,1},{2,2},{1,1}}}}};
    const auto saved=original;const Point delta{.123456789012345,-.234567890123456};
    const Point origin{(0.+metrics.minX)/metrics.cosLatitude,metrics.maxLatitude-0.};
    const Point destination{(delta.x+metrics.minX)/metrics.cosLatitude,metrics.maxLatitude-delta.y};
    const double longitude=destination.x-origin.x,latitude=destination.y-origin.y;
    require(!same(longitude,delta.x/metrics.cosLatitude)||!same(latitude,-delta.y),"translation fixture must expose inverse-origin cancellation");
    auto expected=original;auto move=[&](Point& p){p.x+=longitude;p.y+=latitude;};
    for(auto& p:expected.points)move(p);for(auto& line:expected.lines)for(auto& p:line)move(p);
    for(auto& poly:expected.polygons)for(auto& ring:poly)for(auto& p:ring)move(p);
    const auto result=translateEditGeometry(original,delta,metrics);
    require(result&&result->moved&&same(result->geometry,expected),"translation arithmetic or container coverage changed");
    require(same(original,saved),"translation mutated drag origin");
    const auto zero=translateEditGeometry(original,{0,0},metrics);
    expected=original;auto addZero=[](Point& p){p.x+=0.;p.y+=0.;};
    for(auto& p:expected.points)addZero(p);for(auto& line:expected.lines)for(auto& p:line)addZero(p);
    for(auto& poly:expected.polygons)for(auto& ring:poly)for(auto& p:ring)addZero(p);
    require(zero&&!zero->moved&&same(zero->geometry,expected),"return to zero did not use original drag geometry/IEEE additions");
    for(Point invalid:std::array<Point,4>{{{181,0},{0,91},{std::numeric_limits<double>::infinity(),0},{0,std::numeric_limits<double>::quiet_NaN()}}}) {
        auto bad=original;bad.polygons.back().back().back()=invalid;
        require(!translateEditGeometry(bad,{0,0},metrics),"invalid member did not atomically reject translation");
    }
    const Geometry edge{"Point",{{180,90}},{},{}};
    require(!translateEditGeometry(edge,{1,0},MapCameraMetrics{}),"longitude overflow was clamped/wrapped");
    require(!translateEditGeometry(edge,{0,-1},MapCameraMetrics{}),"latitude overflow was clamped");
    require(!translateEditGeometry(original,{std::numeric_limits<double>::infinity(),0},metrics),"nonfinite geographic delta accepted");
    const MapCameraMetrics cancellation{1,1,1,1e20,1e20};
    const auto tiny=translateEditGeometry(original,{1,1},cancellation);
    require(tiny&&!tiny->moved,"moved was based on overlay delta instead of computed geographic delta");
}
void validationPolicy() {
    std::string detail="untouched";
    require(validateEditGeometry(box(0,0,10,10),&detail)&&detail=="untouched","successful validation changed detail");
    require(validateEditGeometry(Geometry{"Point",{{1,2}},{},{} }),"valid point rejected");
    require(validateEditGeometry(Geometry{"LineString",{},{{{1,2},{3,4}}},{} }),"valid line rejected");
    auto outside=box(0,0,2,2);outside.polygons[0].push_back(box(3,3,1,1).polygons[0][0]);
    require(!validateEditGeometry(outside,&detail)&&detail=="INVALID_GEOMETRY: hole outside exterior","hole validation/message changed");
    auto crossing=polygon({{0,0},{4,4},{0,3},{3,0},{0,0}});
    require(!validateEditGeometry(crossing,&detail)&&detail=="INVALID_GEOMETRY: self intersection","proper self-intersection/message changed");
    auto crossingHole=box(-1,-1,10,10);crossingHole.polygons[0].push_back(crossing.polygons[0][0]);
    require(!validateEditGeometry(crossingHole,&detail)&&detail=="INVALID_GEOMETRY: self intersection","hole self-intersection/message changed");
    auto open=box(0,0,2,2);open.polygons[0][0].pop_back();
    require(!validateEditGeometry(open,&detail)&&detail=="INVALID_GEOMETRY: open ring","GeometryStore open-ring rejection/message changed");
    require(!validateEditGeometry(Geometry{"Point",{{0,100}},{},{} },&detail)&&detail=="INVALID_GEOMETRY: coordinate","GeometryStore bounds rejection/message changed");
    require(!validateEditGeometry(box(0,0,1e-8,1e-8),&detail)&&detail=="INVALID_GEOMETRY: degenerate ring","GeometryStore area threshold changed");
    require(!validateEditGeometry(open),"validation without detail must still catch exceptions");
}
}
int main() {
    int failures=0;
    for(const auto& test:std::array<std::pair<const char*,void(*)()>,6>{{
        {"vertex policy",vertexPolicy},{"segment policy",segmentPolicy},{"boundary policy",boundaryPolicy},
        {"containment policy",containmentPolicy},{"translation policy",translationPolicy},{"validation policy",validationPolicy}}}) {
        try {test.second();std::cout<<"PASS "<<test.first<<'\n';}
        catch(const std::exception& error){++failures;std::cerr<<"FAIL "<<test.first<<": "<<error.what()<<'\n';}
    }
    return failures?1:0;
}

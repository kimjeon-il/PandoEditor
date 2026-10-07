#include <pandoeditor/map/editgeometry.h>
#include <pandoeditor/map/editcoordinates.h>
#include <pandoeditor/map/sharedboundary.h>
#include <pandoeditor/geometrypredicates.h>
#include <algorithm>
#include <cmath>

namespace pandoeditor::map {
void GeometryDraft::checkpointDraft() { undo.push_back(draft);redo.clear(); }
void GeometryDraft::recordVertexMove() {
    if(!dragBefore)undo.push_back(draft);
    redo.clear();
}
bool GeometryDraft::undoDraft() {
    if(undo.empty())return false;
    redo.push_back(draft);draft=std::move(undo.back());undo.pop_back();vertex=-1;return true;
}
bool GeometryDraft::redoDraft() {
    if(redo.empty())return false;
    undo.push_back(draft);draft=std::move(redo.back());redo.pop_back();vertex=-1;return true;
}
bool GeometryDraft::finishVertexDrag(bool cancel) {
    if(!dragBefore)return false;
    if(cancel)draft=*dragBefore;else undo.push_back(*dragBefore);
    dragBefore.reset();return true;
}
bool GeometryDraft::finishObjectDrag(bool cancel) {
    if(!dragBefore)return false;
    if(cancel)draft=*dragBefore;
    else if(objectDragMoved){undo.push_back(*dragBefore);redo.clear();}
    dragBefore.reset();objectDragMoved=false;return true;
}
namespace {
bool isArea(const Geometry& geometry) {
    return geometry.type=="Polygon"||geometry.type=="MultiPolygon";
}
const Ring* openPath(const Geometry& geometry,int path) {
    if(geometry.type=="Point"||geometry.type=="MultiPoint")return path==0?&geometry.points:nullptr;
    if(path<0||std::size_t(path)>=geometry.lines.size())return nullptr;
    return &geometry.lines[path];
}
bool samePoint(Point a,Point b) { return a.x==b.x&&a.y==b.y; }
double distanceSquared(Point a,Point b) { const auto x=a.x-b.x,y=a.y-b.y;return x*x+y*y; }
double segmentDistanceSquared(Point point,Point a,Point b,double& t) {
    const auto dx=b.x-a.x,dy=b.y-a.y,denominator=dx*dx+dy*dy;
    t=denominator?std::clamp(((point.x-a.x)*dx+(point.y-a.y)*dy)/denominator,0.,1.):0.;
    return distanceSquared(point,{a.x+dx*t,a.y+dy*t});
}
double orientation(Point a,Point b,Point c) { return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x); }
bool properIntersection(Point a,Point b,Point c,Point d) {
    const auto ab1=orientation(a,b,c),ab2=orientation(a,b,d),cd1=orientation(c,d,a),cd2=orientation(c,d,b);
    return (ab1>0&&ab2<0||ab1<0&&ab2>0)&&(cd1>0&&cd2<0||cd1<0&&cd2>0);
}
bool selfIntersects(const Ring& ring) {
    if(ring.size()<4)return false;const auto edgeCount=ring.size()-1;
    for(std::size_t a=0;a<edgeCount;++a)for(std::size_t b=a+1;b<edgeCount;++b) {
        if(b==a+1||(a==0&&b+1==edgeCount))continue;
        if(properIntersection(ring[a],ring[a+1],ring[b],ring[b+1]))return true;
    }
    return false;
}
bool pointInRing(const Ring& ring,Point point){bool inside=false;for(std::size_t i=0,j=ring.size()?ring.size()-1:0;i<ring.size();j=i++) {const auto a=ring[i],b=ring[j];if((a.y>point.y)!=(b.y>point.y)&&point.x<(b.x-a.x)*(point.y-a.y)/(b.y-a.y)+a.x)inside=!inside;}return inside;}
}

bool editGeometryContainsPoint(const Geometry& geometry,Point point){for(const auto& polygon:geometry.polygons){if(polygon.empty()||!pointInRing(polygon.front(),point))continue;bool hole=false;for(std::size_t i=1;i<polygon.size();++i)if(pointInRing(polygon[i],point))hole=true;if(!hole)return true;}return false;}

bool validateEditGeometry(const Geometry& geometry,std::string* detail) {
    try {
        GeometryStore check;check.insert({"geometry-edit",1},geometry);
        for(const auto& polygon:geometry.polygons) {
            if(polygon.empty()||selfIntersects(polygon.front()))throw std::invalid_argument("INVALID_GEOMETRY: self intersection");
            Geometry outer{"Polygon",{}, {}, {{polygon.front()}}};
            for(std::size_t hole=1;hole<polygon.size();++hole) {
                if(selfIntersects(polygon[hole]))throw std::invalid_argument("INVALID_GEOMETRY: self intersection");
                Geometry inner{"Polygon",{}, {}, {{polygon[hole]}}};
                if(!geometryContains(outer,inner))throw std::invalid_argument("INVALID_GEOMETRY: hole outside exterior");
            }
        }
        return true;
    }
    catch(const std::exception& error) { if(detail)*detail=error.what();return false; }
}

EditVertexIndex nearestEditVertex(const Geometry& geometry,Point mapPoint,double tolerance,
                                  const MapCameraMetrics& metrics) {
    const auto point=editMapToGeographic(mapPoint,metrics);
    double best=tolerance*tolerance;
    EditVertexIndex hit;
    if(!isArea(geometry)) {
        for(int p=0;;++p) {
            const auto path=openPath(geometry,p);if(!path)break;
            for(std::size_t v=0;v<path->size();++v) {
                const auto distance=distanceSquared(editGeographicToMap((*path)[v],metrics),mapPoint);
                if(distance<=best){best=distance;hit.polygon=p;hit.vertex=int(v);}
            }
        }
        hit.ring=0;
        return hit;
    }
    for(std::size_t p=0;p<geometry.polygons.size();++p)
        for(std::size_t r=0;r<geometry.polygons[p].size();++r) {
            const auto& ring=geometry.polygons[p][r];
            const auto limit=ring.size()>1&&samePoint(ring.front(),ring.back())?ring.size()-1:ring.size();
            for(std::size_t v=0;v<limit;++v) {
                const auto candidate=distanceSquared(point,ring[v]);
                if(candidate<=best){best=candidate;hit={int(p),int(r),int(v)};}
            }
        }
    return hit;
}

std::optional<EditSegmentHit> nearestEditSegment(const Geometry& geometry,Point mapPoint,
                                               double tolerance,const MapCameraMetrics& metrics) {
    const auto point=editMapToGeographic(mapPoint,metrics);
    double best=tolerance*tolerance;
    std::optional<EditSegmentHit> hit;
    if(!isArea(geometry)) {
        if(geometry.type=="Point"||geometry.type=="MultiPoint")return {};
        for(std::size_t p=0;p<geometry.lines.size();++p) {
            const auto& path=geometry.lines[p];
            for(std::size_t v=1;v<path.size();++v) {
                double t=0;
                const auto distance=segmentDistanceSquared(mapPoint,editGeographicToMap(path[v-1],metrics),
                                                           editGeographicToMap(path[v],metrics),t);
                if(distance<=best) {
                    best=distance;
                    hit=EditSegmentHit{{int(p),0,int(v)},
                        {path[v-1].x+(path[v].x-path[v-1].x)*t,path[v-1].y+(path[v].y-path[v-1].y)*t}};
                }
            }
        }
        return hit;
    }
    for(std::size_t p=0;p<geometry.polygons.size();++p)
        for(std::size_t r=0;r<geometry.polygons[p].size();++r) {
            const auto& ring=geometry.polygons[p][r];
            for(std::size_t v=1;v<ring.size();++v) {
                double t=0;
                const auto distance=segmentDistanceSquared(point,ring[v-1],ring[v],t);
                if(distance<=best) {
                    best=distance;
                    hit=EditSegmentHit{{int(p),int(r),int(v)},
                        {ring[v-1].x+(ring[v].x-ring[v-1].x)*t,ring[v-1].y+(ring[v].y-ring[v-1].y)*t}};
                }
            }
        }
    return hit;
}

int nearestMovableBoundaryNode(const sharedboundary::Session& session,Point mapPoint,
                               double tolerance,const MapCameraMetrics& metrics) {
    double best=tolerance*tolerance;int found=-1;
    const auto& nodes=session.nodes();
    for(std::size_t i=0;i<nodes.size();++i) {
        if(!session.canMove(i))continue;
        const auto distance=distanceSquared(editGeographicToMap(nodes[i].coordinate,metrics),mapPoint);
        if(distance<=best){best=distance;found=int(i);}
    }
    return found;
}

std::optional<EditTranslation> translateEditGeometry(const Geometry& dragOrigin,Point mapDelta,
                                                    const MapCameraMetrics& metrics) {
    const auto origin=editMapToGeographic({0,0},metrics),destination=editMapToGeographic(mapDelta,metrics);
    const double longitude=destination.x-origin.x,latitude=destination.y-origin.y;
    if(!std::isfinite(longitude)||!std::isfinite(latitude))return {};
    auto translated=dragOrigin;
    auto move=[&](Point& point){point.x+=longitude;point.y+=latitude;
        return std::isfinite(point.x)&&std::isfinite(point.y)&&point.x>=-180&&point.x<=180&&point.y>=-90&&point.y<=90;};
    for(auto& point:translated.points)if(!move(point))return {};
    for(auto& line:translated.lines)for(auto& point:line)if(!move(point))return {};
    for(auto& polygon:translated.polygons)for(auto& ring:polygon)for(auto& point:ring)if(!move(point))return {};
    return EditTranslation{std::move(translated),longitude!=0||latitude!=0};
}
}

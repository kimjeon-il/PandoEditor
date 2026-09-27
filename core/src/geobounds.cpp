#include <pandoeditor/geobounds.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pandoeditor {
namespace {
double longitude(double raw) {
    if(!std::isfinite(raw))throw std::invalid_argument("non-finite longitude");
    if(raw>=-180&&raw<180)return raw;
    if(raw==180)return -180;
    double result=std::fmod(raw+180.0,360.0);
    if(result<0)result+=360.0;
    return result-180.0;
}

GeoBounds boundsOf(const std::vector<Point>& points,bool polarPolygon) {
    if(points.empty())throw std::invalid_argument("empty geographic geometry");
    GeoBounds bounds;
    bounds.south=90;bounds.north=-90;
    std::vector<double> values;values.reserve(points.size());
    for(const auto point:points) {
        if(!std::isfinite(point.y)||point.y < -90 || point.y > 90)
            throw std::invalid_argument("invalid geographic latitude");
        values.push_back(longitude(point.x));
        bounds.south=std::min(bounds.south,point.y);
        bounds.north=std::max(bounds.north,point.y);
    }
    if(polarPolygon&&(bounds.north>=89.8||bounds.south<=-89.8)) {
        bounds.west=-180;bounds.east=180;return bounds;
    }
    std::sort(values.begin(),values.end());
    double largest=-1;std::size_t after=0;
    for(std::size_t i=0;i<values.size();++i) {
        const double next=i+1<values.size()?values[i+1]:values.front()+360;
        const double gap=next-values[i];
        if(gap>largest){largest=gap;after=(i+1)%values.size();}
    }
    bounds.west=values[after];
    bounds.east=values[(after+values.size()-1)%values.size()];
    bounds.wrapsDateline=bounds.west>bounds.east;
    return bounds;
}

std::vector<Point> outerPoints(const Geometry& geometry) {
    std::vector<Point> result;
    if(!geometry.polygons.empty()) {
        for(const auto& polygon:geometry.polygons) {
            if(polygon.empty())throw std::invalid_argument("polygon without outer ring");
            result.insert(result.end(),polygon.front().begin(),polygon.front().end());
        }
    } else if(!geometry.lines.empty()) {
        for(const auto& line:geometry.lines)result.insert(result.end(),line.begin(),line.end());
    } else result=geometry.points;
    return result;
}

bool linearIntersects(const GeoBounds& a,const GeoBounds& b) {
    if(a.east>=b.west&&b.east>=a.west)return true;
    // -180 and +180 are the same meridian, including closed cell edges.
    return (a.east==180&&b.west==-180)||(b.east==180&&a.west==-180);
}
}

GeoBounds geometryBounds(const Geometry& geometry) {
    return boundsOf(outerPoints(geometry),!geometry.polygons.empty());
}

std::vector<GeoBounds> geometryPartBounds(const Geometry& geometry) {
    std::vector<GeoBounds> result;
    if(!geometry.polygons.empty()) {
        for(const auto& polygon:geometry.polygons) {
            if(polygon.empty())throw std::invalid_argument("polygon without outer ring");
            result.push_back(boundsOf(polygon.front(),true));
        }
    } else if(!geometry.lines.empty()) {
        for(const auto& line:geometry.lines)result.push_back(boundsOf(line,false));
    } else {
        for(const auto& point:geometry.points)result.push_back(boundsOf({point},false));
    }
    if(result.empty())throw std::invalid_argument("empty geographic geometry");
    return result;
}

std::vector<GeoBounds> splitWrappedBounds(const GeoBounds& bounds) {
    if(!bounds.wrapsDateline)return {bounds};
    if(bounds.west<=bounds.east)throw std::invalid_argument("invalid wrapped bounds");
    return {{bounds.west,bounds.south,180,bounds.north,false},
        {-180,bounds.south,bounds.east,bounds.north,false}};
}

bool intersects(const GeoBounds& left,const GeoBounds& right) {
    if(left.north<right.south||right.north<left.south)return false;
    for(const auto& a:splitWrappedBounds(left))
        for(const auto& b:splitWrappedBounds(right))
            if(linearIntersects(a,b))return true;
    return false;
}

bool contains(const GeoBounds& bounds,Point point) {
    if(!std::isfinite(point.x)||!std::isfinite(point.y)||
       point.y<bounds.south||point.y>bounds.north)return false;
    const double lon=longitude(point.x);
    for(const auto& part:splitWrappedBounds(bounds))
        if((lon>=part.west&&lon<=part.east)||
           (lon==-180&&part.east==180))return true;
    return false;
}
}

#include "renderlod.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace {
using namespace pandoeditor;
bool same(Point a,Point b) {return a.x==b.x&&a.y==b.y;}
bool datelineJump(const Ring& line) {
    for(std::size_t i=1;i<line.size();++i)
        if(std::abs(line[i].x-line[i-1].x)>180)return true;
    return false;
}
double segmentDistanceSquared(Point p,Point a,Point b) {
    const double dx=b.x-a.x,dy=b.y-a.y;
    if(dx==0&&dy==0)return (p.x-a.x)*(p.x-a.x)+(p.y-a.y)*(p.y-a.y);
    const auto ratio=std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/(dx*dx+dy*dy),0.,1.);
    const auto x=a.x+ratio*dx,y=a.y+ratio*dy;
    return (p.x-x)*(p.x-x)+(p.y-y)*(p.y-y);
}
Ring simplify(const Ring& source,double tolerance) {
    if(source.size()<=2||tolerance<=0||datelineJump(source))return source;
    std::vector<std::uint8_t> keep(source.size());
    keep.front()=keep.back()=1;
    std::vector<std::pair<std::size_t,std::size_t>> stack{{0,source.size()-1}};
    while(!stack.empty()) {
        const auto [first,last]=stack.back();stack.pop_back();
        double maximum=tolerance*tolerance;
        std::size_t farthest=source.size();
        for(auto i=first+1;i<last;++i) {
            const auto distance=segmentDistanceSquared(source[i],source[first],source[last]);
            if(distance>maximum){maximum=distance;farthest=i;}
        }
        if(farthest==source.size())continue;
        keep[farthest]=1;stack.emplace_back(first,farthest);stack.emplace_back(farthest,last);
    }
    Ring result;result.reserve(source.size());
    for(std::size_t i=0;i<source.size();++i)if(keep[i])result.push_back(source[i]);
    return result;
}
Ring densify(const Ring& source,double maxEdge) {
    if(maxEdge<=0||source.size()<2)return source;
    Ring result{source.front()};
    for(std::size_t i=1;i<source.size();++i) {
        const auto a=source[i-1],b=source[i];
        double longitude=b.x-a.x;
        while(longitude>180)longitude-=360;
        while(longitude< -180)longitude+=360;
        const auto steps=std::max(1,int(std::ceil(std::hypot(longitude,b.y-a.y)/maxEdge)));
        for(int step=1;step<=steps;++step) {
            const double ratio=double(step)/steps;
            double x=a.x+longitude*ratio;
            while(x>180)x-=360;
            while(x< -180)x+=360;
            result.push_back({x,a.y+(b.y-a.y)*ratio});
        }
    }
    return result;
}
Ring prepare(const Ring& source,double tolerance,double maxEdge,bool closed) {
    if(source.empty())return {};
    if(!closed)return densify(simplify(source,tolerance),maxEdge);
    Ring open=source;
    if(open.size()>1&&same(open.front(),open.back()))open.pop_back();
    if(open.size()<3)return {};
    auto ring=open;ring.push_back(open.front());
    auto simplified=simplify(ring,tolerance);
    if(simplified.size()<4)simplified=std::move(ring);
    auto result=densify(simplified,maxEdge);
    if(result.size()>1)result.back()=result.front();
    return result;
}
}

RenderLod resolveRenderLod(RenderLod requested,LodPolicy policy,bool protectedGeometry) noexcept {
    return protectedGeometry||policy==LodPolicy::Exact?RenderLod::High:requested;
}

pandoeditor::Geometry prepareRenderGeometry(const pandoeditor::Geometry& source,
    RenderLod requested,LodPolicy policy,bool protectedGeometry,bool globeReady) {
    const auto lod=resolveRenderLod(requested,policy,protectedGeometry);
    if(lod==RenderLod::High)return source;
    const double tolerance=lod==RenderLod::Coarse?.075:.018;
    const double maxEdge=globeReady?(lod==RenderLod::Coarse?4.:2.5):0.;
    auto result=source;
    if(source.type=="LineString"||source.type=="MultiLineString") {
        for(auto& line:result.lines)line=prepare(line,tolerance,maxEdge,false);
        result.lines.erase(std::remove_if(result.lines.begin(),result.lines.end(),
            [](const auto& line){return line.size()<2;}),result.lines.end());
    }else if(source.type=="Polygon"||source.type=="MultiPolygon") {
        for(auto& polygon:result.polygons) {
            // Never drop the outer ring and reinterpret a hole as a fill.
            if(polygon.empty())continue;
            for(auto& ring:polygon)ring=prepare(ring,tolerance,maxEdge,true);
            if(polygon.front().size()<4){polygon.clear();continue;}
            polygon.erase(std::remove_if(polygon.begin()+1,polygon.end(),
                [](const auto& ring){return ring.size()<4;}),polygon.end());
        }
        result.polygons.erase(std::remove_if(result.polygons.begin(),result.polygons.end(),
            [](const auto& polygon){return polygon.empty();}),result.polygons.end());
    }
    return result;
}

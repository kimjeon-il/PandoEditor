#include "countryculling.h"
#include "projectionengine.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace {
constexpr double radians=3.14159265358979323846/180.;
bool visible(const CountryBaseMesh& mesh,std::size_t i,const MapViewState& view,double padding) {
    const auto west=mesh.countryBounds[i*4]/1e6,south=mesh.countryBounds[i*4+1]/1e6;
    const auto east=mesh.countryBounds[i*4+2]/1e6,north=mesh.countryBounds[i*4+3]/1e6;
    const auto flags=mesh.countryBoundsFlags[i];
    if(flags&2u)return true;
    const auto wrapped=(flags&1u)||west>east;
    const auto span=wrapped?360-west+east:east-west;
    if(view.mode==ProjectionMode::Flat) {
        const auto top=projectPoint({0,north},view).y;
        const auto bottom=projectPoint({0,south},view).y;
        if(std::max(top,bottom)<-padding||std::min(top,bottom)>view.viewportHeight+padding)
            return false;
        const auto offsets=visibleFlatWorldOffsets(view);
        for(const auto offset:offsets) {
            const auto interval=[&](double left,double right) {
                const auto a=projectPoint({left,0},view,offset).x;
                const auto b=projectPoint({right,0},view,offset).x;
                return std::max(a,b)>=-padding&&std::min(a,b)<=view.viewportWidth+padding;
            };
            if(wrapped?(interval(west,180)||interval(-180,east)):interval(west,east))
                return true;
        }
        return false;
    }
    const auto mid=west+span/2>180?west+span/2-360:west+span/2;
    const auto center=projectPoint({mid,(south+north)/2},view);
    const auto radius=std::min(3.14159265358979323846,
        std::hypot(span*radians/2,(north-south)*radians/2)+2*radians);
    if(radius>=3.14159265358979323846/2)return true;
    const auto paddingAngle=padding/std::abs(view.scale);
    if(std::acos(std::clamp(center.frontness,-1.,1.))>
       3.14159265358979323846/2+radius+paddingAngle)return false;
    const auto radiusPixels=std::abs(view.scale)*2*std::sin(radius/2)+padding;
    return center.x+radiusPixels>=0&&center.x-radiusPixels<=view.viewportWidth&&
        center.y+radiusPixels>=0&&center.y-radiusPixels<=view.viewportHeight;
}
}

CountryDrawPlan countryDrawRangesForView(const CountryBaseMesh& mesh,const MapViewState& view,
    CountryRangeKind kind,double padding,double threshold,std::size_t maxRanges) {
    CountryDrawPlan plan;
    const auto& indices=kind==CountryRangeKind::Triangle?mesh.triangleIndices:mesh.lineIndices;
    const auto& ranges=kind==CountryRangeKind::Triangle?mesh.countryTriangleRanges:mesh.countryBoundaryRanges;
    plan.fullIndexCount=indices.size();
    const auto countries=ranges.size()/2;
    plan.visible.assign(countries,true);
    const auto full=[&](bool fallback) {
        plan.ranges=indices.empty()?std::vector<CountryDrawRange>{}:
            std::vector<CountryDrawRange>{{0,indices.size()}};
        plan.indexCount=indices.size();plan.visibleCountryCount=countries;
        plan.culled=false;plan.fallback=fallback;
        return plan;
    };
    if(!validMapViewState(view)||ranges.empty()||ranges.size()%2||
       mesh.countryBounds.size()!=countries*4||mesh.countryBoundsFlags.size()!=countries)
        return full(true);
    if(view.mode==ProjectionMode::Flat&&
       std::abs(view.scale)*2*3.14159265358979323846<=view.viewportWidth+padding*2&&
       std::abs(view.scale)*3.14159265358979323846<=view.viewportHeight+padding*2)
        return full(false);
    plan.visibleCountryCount=0;
    for(std::size_t i=0;i<countries;++i) {
        plan.visible[i]=visible(mesh,i,view,padding);
        const auto first=ranges[i*2],count=ranges[i*2+1];
        if(!count||!plan.visible[i])continue;
        ++plan.visibleCountryCount;
        if(!plan.ranges.empty()&&first<=plan.ranges.back().first+plan.ranges.back().count) {
            auto& last=plan.ranges.back();
            last.count=std::max(last.first+last.count,std::size_t(first)+count)-last.first;
        }else plan.ranges.push_back({first,count});
    }
    plan.indexCount=std::accumulate(plan.ranges.begin(),plan.ranges.end(),std::size_t(0),
        [](auto sum,const auto& range){return sum+range.count;});
    if((view.mode==ProjectionMode::Flat&&plan.indexCount>=indices.size()*threshold)||
       plan.ranges.size()>maxRanges)return full(true);
    plan.culled=true;
    return plan;
}

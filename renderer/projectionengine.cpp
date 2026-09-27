#include "projectionengine.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
constexpr double pi=3.141592653589793238462643383279502884;
constexpr double radians=pi/180;
constexpr double degrees=180/pi;
void requireView(const MapViewState& view) {
    if(!validMapViewState(view))throw std::invalid_argument("invalid projection view");
}
double normalizedLongitude(double value) {
    if(value>=-180&&value<180)return value;
    double result=std::fmod(value+180,360);
    if(result<0)result+=360;
    return result-180;
}
}

ProjectedPoint projectPoint(pandoeditor::Point point,const MapViewState& view,double offset) {
    requireView(view);
    if(!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(offset)||
       point.y< -90||point.y>90)return {};
    ProjectedPoint result;
    if(view.mode==ProjectionMode::Flat) {
        result.x=view.translateX+view.scale*((point.x+offset-view.centerLongitude)*radians);
        result.y=view.translateY-view.scale*((point.y-view.centerLatitude)*radians);
        result.frontness=1;result.visibleHemisphere=true;
    } else {
        const double centerLon=(view.centerLongitude+view.rotationLongitude)*radians;
        const double centerLat=std::clamp(view.centerLatitude+view.rotationLatitude,-90.,90.)*radians;
        const double lon=(point.x+offset)*radians,lat=point.y*radians;
        const double delta=lon-centerLon;
        const double horizontal=std::cos(lat)*std::sin(delta);
        const double vertical=std::sin(lat)*std::cos(centerLat)-
            std::cos(lat)*std::sin(centerLat)*std::cos(delta);
        const double roll=view.rotationRoll*radians;
        result.x=view.translateX+view.scale*(std::cos(roll)*horizontal-std::sin(roll)*vertical);
        result.y=view.translateY-view.scale*(std::sin(roll)*horizontal+std::cos(roll)*vertical);
        result.frontness=std::sin(lat)*std::sin(centerLat)+
            std::cos(lat)*std::cos(centerLat)*std::cos(delta);
        result.depth=result.frontness;
        result.visibleHemisphere=result.frontness>=0;
    }
    result.finite=std::isfinite(result.x)&&std::isfinite(result.y)&&
        std::isfinite(result.depth)&&std::isfinite(result.frontness);
    if(!result.finite)result.visibleHemisphere=false;
    return result;
}

pandoeditor::Point unprojectFlat(double x,double y,const MapViewState& view) {
    requireView(view);
    if(view.mode!=ProjectionMode::Flat||!std::isfinite(x)||!std::isfinite(y))
        throw std::invalid_argument("invalid flat inverse projection");
    return {view.centerLongitude+(x-view.translateX)/view.scale*degrees,
        view.centerLatitude+(view.translateY-y)/view.scale*degrees};
}

std::optional<pandoeditor::Point> unprojectGlobe(double x,double y,const MapViewState& view) {
    requireView(view);
    if(view.mode!=ProjectionMode::Globe||!std::isfinite(x)||!std::isfinite(y))return std::nullopt;
    const double u=(x-view.translateX)/view.scale;
    const double v=(view.translateY-y)/view.scale;
    const double roll=view.rotationRoll*radians;
    const double horizontal=std::cos(roll)*u+std::sin(roll)*v;
    const double vertical=-std::sin(roll)*u+std::cos(roll)*v;
    const double square=horizontal*horizontal+vertical*vertical;
    if(square>1)return std::nullopt;
    const double front=std::sqrt(std::max(0.,1-square));
    const double centerLat=std::clamp(view.centerLatitude+view.rotationLatitude,-90.,90.)*radians;
    const double latitude=std::asin(std::clamp(vertical*std::cos(centerLat)+
        front*std::sin(centerLat),-1.,1.))*degrees;
    const double longitude=(view.centerLongitude+view.rotationLongitude)+
        std::atan2(horizontal,front*std::cos(centerLat)-vertical*std::sin(centerLat))*degrees;
    return pandoeditor::Point{normalizedLongitude(longitude),latitude};
}

std::optional<pandoeditor::Point> unprojectView(double x,double y,const MapViewState& view) {
    if(view.mode==ProjectionMode::Globe)return unprojectGlobe(x,y,view);
    const auto point=unprojectFlat(x,y,view);
    if(point.y< -90||point.y>90)return std::nullopt;
    return point;
}

std::vector<pandoeditor::GeoBounds> geographicPickWindows(
    double x,double y,double radiusPixels,const MapViewState& view) {
    requireView(view);
    if(!std::isfinite(radiusPixels)||radiusPixels<0)
        throw std::invalid_argument("invalid pick radius");
    const auto point=unprojectView(x,y,view);
    if(!point)return {};
    double latitudeMargin=radiusPixels/view.scale*degrees;
    double longitudeMargin=latitudeMargin;
    if(view.mode==ProjectionMode::Globe) {
        const double dx=(x-view.translateX)/view.scale,dy=(y-view.translateY)/view.scale;
        const double outer=std::hypot(dx,dy)+radiusPixels/view.scale;
        if(outer>=1)return {{-180,-90,180,90}}; // Near the limb, inverse distortion is unbounded.
        const double factor=1/std::sqrt(1-outer*outer);
        latitudeMargin*=factor;
        const double farthest=std::min(90.,std::abs(point->y)+latitudeMargin);
        longitudeMargin=farthest>=89.8?360:latitudeMargin/
            std::max(.001,std::cos(farthest*radians));
    }
    const double south=std::max(-90.,point->y-latitudeMargin);
    const double north=std::min(90.,point->y+latitudeMargin);
    if(longitudeMargin>=180)return {{-180,south,180,north}};
    return {{point->x-longitudeMargin,south,point->x+longitudeMargin,north}};
}

std::vector<double> visibleFlatWorldOffsets(const MapViewState& view) {
    requireView(view);
    if(view.mode!=ProjectionMode::Flat)return {0};
    constexpr double copies[]{-360,0,360};
    std::vector<double> visible;
    for(double offset:copies) {
        const double left=view.translateX+view.scale*((-180+offset-view.centerLongitude)*radians);
        const double right=view.translateX+view.scale*((180+offset-view.centerLongitude)*radians);
        if(std::min(right,view.viewportWidth)-std::max(left,0.)>0.5)visible.push_back(offset);
    }
    if(!visible.empty())return visible;
    double nearest=0,distance=std::numeric_limits<double>::infinity();
    for(double offset:copies) {
        const double center=view.translateX+view.scale*((offset-view.centerLongitude)*radians);
        const double gap=std::abs(center-view.viewportWidth/2);
        if(gap<distance){nearest=offset;distance=gap;}
    }
    return {nearest};
}

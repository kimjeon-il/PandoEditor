#include <pandoeditor/map/viewportresourcescheduler.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace {
constexpr double Pi=3.1415926535897932384626433832795;
constexpr double DegreesPerRadian=180.0/Pi;

bool finite(double value) {return std::isfinite(value);}

auto viewFields(const MapViewState& value) {
    return std::tie(value.mode,value.viewportWidth,value.viewportHeight,
        value.centerLongitude,value.centerLatitude,
        value.rotationLongitude,value.rotationLatitude,value.rotationRoll,
        value.scale,value.translateX,value.translateY,value.devicePixelRatio);
}

bool sameMetrics(const MapCameraMetrics& left,const MapCameraMetrics& right) {
    return std::tie(left.mapWidth,left.mapHeight,left.cosLatitude,left.minX,left.maxLatitude)==
           std::tie(right.mapWidth,right.mapHeight,right.cosLatitude,right.minX,right.maxLatitude);
}

ViewportResourceKind merge(ViewportResourceKind left,ViewportResourceKind right) {
    return left|right;
}
}

ViewportResourceRequest buildViewportResourceRequest(
    const MapCameraDisplay& display,const MapCameraMetrics& metrics,
    ViewportResourceKind resources,std::uint64_t generation) {
    if(!validMapViewState(display.view)||!validMapCameraMetrics(metrics)||
       !finite(display.flatZoom)||display.flatZoom<=0||
       !finite(display.mapScale)||display.mapScale<=0||
       !finite(display.originX)||!finite(display.originY)||
       !anyViewportResource(resources)||generation==0)
        throw std::invalid_argument("invalid viewport resource request");

    ViewportResourceRequest result;
    result.generation=generation;
    result.resources=resources;
    result.view=display.view;
    result.metrics=metrics;
    result.flatZoom=display.flatZoom;
    result.mapScale=display.mapScale;
    result.originX=display.originX;
    result.originY=display.originY;

    const auto threshold=pandoeditor::webHydroThreshold(display.flatZoom);
    const auto& view=display.view;
    if(view.mode==ProjectionMode::Globe) {
        result.hydroWindow={threshold,view.viewportWidth,view.viewportHeight,view.scale,
            view.centerLongitude+view.rotationLongitude,
            std::clamp(view.centerLatitude+view.rotationLatitude,-90.,90.)};
    } else {
        const double mapX=(view.viewportWidth/2-display.originX)/display.mapScale;
        const double mapY=(view.viewportHeight/2-display.originY)/display.mapScale;
        const double longitude=(mapX+metrics.minX)/metrics.cosLatitude;
        const double latitude=metrics.maxLatitude-mapY;
        const double scale=display.mapScale*std::min(1.,metrics.cosLatitude)*DegreesPerRadian;
        result.hydroWindow={threshold,view.viewportWidth,view.viewportHeight,scale,
            longitude,latitude};
    }
    return result;
}

bool ViewportResourceScheduler::sameInput(
    const MapCameraDisplay& display,const MapCameraMetrics& metrics) const noexcept {
    if(!latestDisplay_||!latestMetrics_)return false;
    const auto& current=*latestDisplay_;
    return viewFields(current.view)==viewFields(display.view)&&
        current.flatZoom==display.flatZoom&&current.mapScale==display.mapScale&&
        current.originX==display.originX&&current.originY==display.originY&&
        sameMetrics(*latestMetrics_,metrics);
}

bool ViewportResourceScheduler::noteViewport(
    const MapCameraDisplay& display,const MapCameraMetrics& metrics,
    ViewportResourceKind resources) {
    if(!anyViewportResource(resources)||!validMapViewState(display.view)||
       !validMapCameraMetrics(metrics))return false;
    const bool unchanged=sameInput(display,metrics);
    ++stats_.viewportUpdates;
    if(unchanged&&pending())++stats_.coalescedUpdates;
    latestDisplay_=display;
    latestMetrics_=metrics;
    pendingResources_=merge(pendingResources_,resources);
    if(interacting()) {
        ++stats_.deferredUpdates;
        return false;
    }
    return true;
}

bool ViewportResourceScheduler::invalidate(ViewportResourceKind resources) {
    if(!anyViewportResource(resources)||!latestDisplay_||!latestMetrics_)return false;
    ++stats_.invalidations;
    pendingResources_=merge(pendingResources_,resources);
    if(interacting()) {
        ++stats_.deferredUpdates;
        return false;
    }
    return true;
}

bool ViewportResourceScheduler::beginInteraction() {
    if(interactions_==std::numeric_limits<int>::max())
        throw std::overflow_error("viewport resource interaction overflow");
    ++interactions_;
    return interactions_==1;
}

bool ViewportResourceScheduler::endInteraction() {
    if(interactions_<=0)return false;
    --interactions_;
    return interactions_==0&&pending();
}

std::optional<ViewportResourceRequest> ViewportResourceScheduler::takeReady() {
    if(interacting()||!pending()||!latestDisplay_||!latestMetrics_)return std::nullopt;
    if(generation_==std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("viewport resource generation overflow");
    const auto resources=pendingResources_;
    pendingResources_=ViewportResourceKind::None;
    ++generation_;
    ++stats_.issuedRequests;
    return buildViewportResourceRequest(*latestDisplay_,*latestMetrics_,resources,generation_);
}

void ViewportResourceScheduler::reset() {
    latestDisplay_.reset();
    latestMetrics_.reset();
    pendingResources_=ViewportResourceKind::None;
    interactions_=0;
    generation_=0;
    stats_={};
}

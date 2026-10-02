#include <pandoeditor/map/mapcamera.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>

namespace {
constexpr double Pi=3.1415926535897932384626433832795;
constexpr double DegreesPerRadian=180.0/Pi;
constexpr double FlatPadding=48.0;
constexpr double MinZoom=0.5;
constexpr double MaxZoom=20.0;
constexpr double GlobeRadiusFactor=0.44;

double clampZoom(double value) {
    return std::clamp(value,MinZoom,MaxZoom);
}
bool finitePositive(double value) {
    return std::isfinite(value)&&value>0;
}
bool sameMetrics(const MapCameraMetrics& a,const MapCameraMetrics& b) {
    return std::tie(a.mapWidth,a.mapHeight,a.cosLatitude,a.minX,a.maxLatitude)==
           std::tie(b.mapWidth,b.mapHeight,b.cosLatitude,b.minX,b.maxLatitude);
}
}

bool validMapCameraMetrics(const MapCameraMetrics& metrics) noexcept {
    return finitePositive(metrics.mapWidth)&&finitePositive(metrics.mapHeight)&&
        finitePositive(metrics.cosLatitude)&&std::isfinite(metrics.minX)&&
        std::isfinite(metrics.maxLatitude);
}

MapCamera::MapCamera() {
    flat_.mode=ProjectionMode::Flat;
    flat_.translateX=.5;
    flat_.translateY=.5;
    globe_.mode=ProjectionMode::Globe;
    globe_.scale=.45;
    globe_.translateX=.5;
    globe_.translateY=.5;
}

const MapViewState& MapCamera::view() const noexcept {
    return active_==ProjectionMode::Globe?globe_:flat_;
}

MapCamera::FlatControls MapCamera::flatControls(
    const MapViewState& source,const MapCameraMetrics& metrics) const {
    FlatControls result;
    const double usableWidth=std::max(1.0,source.viewportWidth-FlatPadding);
    const double usableHeight=std::max(1.0,source.viewportHeight-FlatPadding);
    result.fitScale=std::max(.01,std::min(usableWidth/metrics.mapWidth,
                                         usableHeight/metrics.mapHeight));
    if(source.viewportWidth<=1&&source.viewportHeight<=1) {
        result.zoom=1;
        result.mapScale=result.fitScale;
        result.originX=(source.viewportWidth-metrics.mapWidth*result.mapScale)/2;
        result.originY=(source.viewportHeight-metrics.mapHeight*result.mapScale)/2;
        return result;
    }
    result.mapScale=source.scale/(metrics.cosLatitude*DegreesPerRadian);
    result.zoom=clampZoom(result.mapScale/result.fitScale);
    result.mapScale=result.fitScale*result.zoom;
    result.originX=source.translateX+metrics.minX*result.mapScale;
    result.originY=source.translateY-metrics.maxLatitude*result.mapScale;
    result.panX=result.originX-(source.viewportWidth-metrics.mapWidth*result.mapScale)/2;
    result.panY=result.originY-(source.viewportHeight-metrics.mapHeight*result.mapScale)/2;
    return result;
}

double MapCamera::globeZoom(const MapViewState& source) const {
    if(source.viewportWidth<=1&&source.viewportHeight<=1)return 1;
    const double fit=std::max(1.0,std::min(source.viewportWidth,source.viewportHeight)*GlobeRadiusFactor);
    return clampZoom(source.scale/fit);
}

MapViewState MapCamera::flatFromControls(
    const MapViewState& base,const FlatControls& controls,
    double width,double height,double devicePixelRatio) const {
    MapViewState next=base;
    next.mode=ProjectionMode::Flat;
    next.viewportWidth=width;
    next.viewportHeight=height;
    next.devicePixelRatio=devicePixelRatio;
    const double usableWidth=std::max(1.0,width-FlatPadding);
    const double usableHeight=std::max(1.0,height-FlatPadding);
    const double fit=std::max(.01,std::min(usableWidth/metrics_.mapWidth,
                                          usableHeight/metrics_.mapHeight));
    const double mapScale=fit*clampZoom(controls.zoom);
    const double originX=(width-metrics_.mapWidth*mapScale)/2+controls.panX;
    const double originY=(height-metrics_.mapHeight*mapScale)/2+controls.panY;
    next.scale=mapScale*metrics_.cosLatitude*DegreesPerRadian;
    next.translateX=originX-metrics_.minX*mapScale;
    next.translateY=originY+metrics_.maxLatitude*mapScale;
    next.centerLongitude=0;
    next.centerLatitude=0;
    return next;
}

MapViewState MapCamera::globeFromZoom(
    const MapViewState& base,double zoom,double width,double height,double devicePixelRatio) const {
    MapViewState next=base;
    next.mode=ProjectionMode::Globe;
    next.viewportWidth=width;
    next.viewportHeight=height;
    next.devicePixelRatio=devicePixelRatio;
    next.scale=std::max(1.0,std::min(width,height)*GlobeRadiusFactor*clampZoom(zoom));
    next.translateX=width/2;
    next.translateY=height/2;
    next.centerLatitude=std::clamp(next.centerLatitude,-90.0,90.0);
    return next;
}

bool MapCamera::replace(MapViewState& target,const MapViewState& proposed) {
    if(!validMapViewState(proposed))throw std::invalid_argument("invalid map camera view");
    const auto next=advanceViewRevision(target,proposed);
    if(next.revision==target.revision)return false;
    target=next;
    return true;
}

MapCameraDisplay MapCamera::display() const {
    MapCameraDisplay result;
    result.view=view();
    const auto flat=flatControls(flat_,metrics_);
    result.flatZoom=flat.zoom;
    result.globeZoom=globeZoom(globe_);
    result.zoom=active_==ProjectionMode::Globe?result.globeZoom:result.flatZoom;
    result.panX=flat.panX;
    result.panY=flat.panY;
    result.fitScale=flat.fitScale;
    result.mapScale=flat.mapScale;
    result.originX=flat.originX;
    result.originY=flat.originY;
    return result;
}

bool MapCamera::setMetrics(const MapCameraMetrics& metrics) {
    if(!validMapCameraMetrics(metrics))throw std::invalid_argument("invalid map camera metrics");
    if(sameMetrics(metrics_,metrics))return false;
    const auto controls=flatControls(flat_,metrics_);
    metrics_=metrics;
    const auto proposed=flatFromControls(flat_,controls,flat_.viewportWidth,
                                         flat_.viewportHeight,flat_.devicePixelRatio);
    replace(flat_,proposed);
    panStart_.reset();
    return true;
}

bool MapCamera::resize(double width,double height,double devicePixelRatio) {
    if(!finitePositive(width)||!finitePositive(height)||!finitePositive(devicePixelRatio))
        return false;
    const auto controls=flatControls(flat_,metrics_);
    const auto globeScale=globeZoom(globe_);
    bool changed=false;
    changed=replace(flat_,flatFromControls(flat_,controls,width,height,devicePixelRatio))||changed;
    changed=replace(globe_,globeFromZoom(globe_,globeScale,width,height,devicePixelRatio))||changed;
    panStart_.reset();
    return changed;
}

bool MapCamera::adoptView(const MapViewState& value) {
    if(!validMapViewState(value))return false;
    active_=value.mode;
    if(value.mode==ProjectionMode::Globe)globe_=value;
    else flat_=value;
    panStart_.reset();
    return true;
}

bool MapCamera::acceptPublishedView(const MapViewState& value) {
    if(!validMapViewState(value))return false;
    if(value.mode==ProjectionMode::Globe)globe_=value;
    else flat_=value;
    active_=value.mode;
    return true;
}

bool MapCamera::setProjectionMode(ProjectionMode mode) {
    if(active_==mode)return false;
    active_=mode;
    panStart_.reset();
    return true;
}

bool MapCamera::zoomAt(double factor,double screenX,double screenY) {
    if(!finitePositive(factor)||!std::isfinite(screenX)||!std::isfinite(screenY))return false;
    if(active_==ProjectionMode::Globe) {
        const double current=globeZoom(globe_);
        const double nextZoom=clampZoom(current*factor);
        if(nextZoom==current)return false;
        return replace(globe_,globeFromZoom(globe_,nextZoom,globe_.viewportWidth,
                                             globe_.viewportHeight,globe_.devicePixelRatio));
    }
    auto controls=flatControls(flat_,metrics_);
    const double nextZoom=clampZoom(controls.zoom*factor);
    if(nextZoom==controls.zoom)return false;
    const double ratio=nextZoom/controls.zoom;
    controls.panX=(screenX-flat_.viewportWidth/2)*(1-ratio)+controls.panX*ratio;
    controls.panY=(screenY-flat_.viewportHeight/2)*(1-ratio)+controls.panY*ratio;
    controls.zoom=nextZoom;
    return replace(flat_,flatFromControls(flat_,controls,flat_.viewportWidth,
                                           flat_.viewportHeight,flat_.devicePixelRatio));
}

bool MapCamera::fit() {
    if(active_==ProjectionMode::Globe) {
        auto next=globe_;
        next.centerLongitude=0;
        next.centerLatitude=0;
        next=globeFromZoom(next,1,next.viewportWidth,next.viewportHeight,next.devicePixelRatio);
        return replace(globe_,next);
    }
    FlatControls controls;
    controls.zoom=1;
    controls.panX=0;
    controls.panY=0;
    return replace(flat_,flatFromControls(flat_,controls,flat_.viewportWidth,
                                           flat_.viewportHeight,flat_.devicePixelRatio));
}

bool MapCamera::focusRect(double left,double top,double width,double height,double maxZoom) {
    if(!std::isfinite(left)||!std::isfinite(top)||!finitePositive(width)||
       !finitePositive(height)||!finitePositive(maxZoom))return false;
    auto controls=flatControls(flat_,metrics_);
    const double targetWidth=std::max(96.0,flat_.viewportWidth-FlatPadding)*.82;
    const double targetHeight=std::max(96.0,flat_.viewportHeight-FlatPadding)*.82;
    const double fitted=std::min(targetWidth/std::max(1.0,width*controls.fitScale),
                                 targetHeight/std::max(1.0,height*controls.fitScale))*.88;
    controls.zoom=std::max(1.25,std::min(maxZoom,fitted));
    controls.mapScale=controls.fitScale*controls.zoom;
    controls.panX=(metrics_.mapWidth/2-left-width/2)*controls.mapScale;
    controls.panY=(metrics_.mapHeight/2-top-height/2)*controls.mapScale;
    return replace(flat_,flatFromControls(flat_,controls,flat_.viewportWidth,
                                           flat_.viewportHeight,flat_.devicePixelRatio));
}

void MapCamera::beginPan() {
    panStart_=view();
}

bool MapCamera::panFromGesture(double deltaX,double deltaY) {
    if(!panStart_||panStart_->mode!=active_||
       !std::isfinite(deltaX)||!std::isfinite(deltaY))return false;
    auto next=*panStart_;
    if(active_==ProjectionMode::Globe) {
        const double radius=std::max(1.0,panStart_->scale);
        next.centerLongitude=panStart_->centerLongitude-deltaX/radius*DegreesPerRadian;
        next.centerLatitude=std::clamp(panStart_->centerLatitude+deltaY/radius*DegreesPerRadian,-90.0,90.0);
        return replace(globe_,next);
    }
    next.translateX=panStart_->translateX+deltaX;
    next.translateY=panStart_->translateY+deltaY;
    return replace(flat_,next);
}

#include <pandoeditor/map/mapviewstate.h>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

bool validMapViewState(const MapViewState& view) noexcept {
    return (view.mode==ProjectionMode::Flat||view.mode==ProjectionMode::Globe)&&
        std::isfinite(view.viewportWidth)&&view.viewportWidth>0&&
        std::isfinite(view.viewportHeight)&&view.viewportHeight>0&&
        std::isfinite(view.centerLongitude)&&std::isfinite(view.centerLatitude)&&
        view.centerLatitude>=-90&&view.centerLatitude<=90&&
        std::isfinite(view.rotationLongitude)&&std::isfinite(view.rotationLatitude)&&
        std::isfinite(view.rotationRoll)&&std::isfinite(view.scale)&&view.scale>0&&
        std::isfinite(view.translateX)&&std::isfinite(view.translateY)&&
        std::isfinite(view.devicePixelRatio)&&view.devicePixelRatio>0;
}

MapViewState advanceViewRevision(const MapViewState& previous,const MapViewState& proposed) {
    if(!validMapViewState(previous)||!validMapViewState(proposed))
        throw std::invalid_argument("invalid map view state");
    auto fields=[](const MapViewState& v) {
        return std::tie(v.mode,v.viewportWidth,v.viewportHeight,v.centerLongitude,v.centerLatitude,
            v.rotationLongitude,v.rotationLatitude,v.rotationRoll,v.scale,v.translateX,
            v.translateY,v.devicePixelRatio);
    };
    if(fields(previous)==fields(proposed))return previous;
    if(previous.revision==std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("map view revision overflow");
    auto next=proposed;next.revision=previous.revision+1;return next;
}

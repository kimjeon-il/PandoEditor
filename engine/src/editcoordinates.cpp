#include <pandoeditor/map/editcoordinates.h>
#include <cmath>

namespace pandoeditor::map {
Point editGeographicToMap(Point point,const MapCameraMetrics& metrics) noexcept {
    return {point.x*metrics.cosLatitude-metrics.minX,metrics.maxLatitude-point.y};
}
Point editMapToGeographic(Point point,const MapCameraMetrics& metrics) noexcept {
    return {(point.x+metrics.minX)/metrics.cosLatitude,metrics.maxLatitude-point.y};
}
Point editMapToScreen(Point point,const MapCameraDisplay& display) noexcept {
    return {display.originX+point.x*display.mapScale,display.originY+point.y*display.mapScale};
}
Point editScreenToMap(Point point,const MapCameraDisplay& display) noexcept {
    return {(point.x-display.originX)/display.mapScale,(point.y-display.originY)/display.mapScale};
}
Point editScreenDeltaToMap(Point delta,const MapCameraDisplay& display) noexcept {
    return {delta.x/display.mapScale,delta.y/display.mapScale};
}
double editPixelRadiusToMap(double pixels,const MapCameraDisplay& display) noexcept {
    return pixels/display.mapScale;
}
std::optional<Point> editSnapPointToScreen(Point point,const MapCameraMetrics& metrics,
                                         const MapCameraDisplay& display) noexcept {
    if(!std::isfinite(point.x)||!std::isfinite(point.y)||point.y < -90||point.y > 90)return {};
    const auto local=editGeographicToMap(point,metrics);
    const auto projected=editMapToScreen(local,display);
    if(!std::isfinite(projected.x)||!std::isfinite(projected.y))return {};
    return projected;
}
}

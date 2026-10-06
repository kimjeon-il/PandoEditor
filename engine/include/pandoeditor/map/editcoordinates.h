#pragma once
#include <pandoeditor/geometry-types.h>
#include <pandoeditor/map/mapcamera.h>
#include <optional>

namespace pandoeditor::map {
// Editing uses the existing affine overlay, not the geographic renderer's
// projection. Raw transforms preserve IEEE results; callers own input policy.
Point editGeographicToMap(Point geographic,const MapCameraMetrics&) noexcept;
Point editMapToGeographic(Point mapPoint,const MapCameraMetrics&) noexcept;
Point editMapToScreen(Point mapPoint,const MapCameraDisplay&) noexcept;
Point editScreenToMap(Point screenPoint,const MapCameraDisplay&) noexcept;
Point editScreenDeltaToMap(Point screenDelta,const MapCameraDisplay&) noexcept;
double editPixelRadiusToMap(double pixels,const MapCameraDisplay&) noexcept;
// These records carry the existing reference-image rectangle and geographic
// bounds. They do not impose renderer projection or image gesture policy.
struct EditCoordinateRect { double x=0,y=0,width=0,height=0; };
struct EditGeographicBounds { double west=0,east=0,north=0,south=0; };
EditCoordinateRect editMapRectToScreen(EditCoordinateRect,const MapCameraDisplay&) noexcept;
Point editMapDragPosition(Point startMap,Point screenDelta,const MapCameraDisplay&) noexcept;
Point editLabelDragToMap(Point labelScreen,Point screenDelta,const MapCameraDisplay&) noexcept;
EditGeographicBounds editMapRectGeographicBounds(EditCoordinateRect,const MapCameraMetrics&) noexcept;
// Only snap candidates enforce finite coordinates and latitude bounds. Finite
// noncanonical longitudes remain accepted, matching the existing snap callback.
std::optional<Point> editSnapPointToScreen(Point geographic,const MapCameraMetrics&,
                                         const MapCameraDisplay&) noexcept;
}

#pragma once

#include <pandoeditor/map/mapviewstate.h>
#include <optional>

struct MapCameraMetrics {
    double mapWidth=1;
    double mapHeight=1;
    double cosLatitude=1;
    double minX=0;
    double maxLatitude=0;
};

struct MapCameraDisplay {
    MapViewState view;
    double zoom=1;
    double flatZoom=1;
    double globeZoom=1;
    double panX=0;
    double panY=0;
    double fitScale=1;
    double mapScale=1;
    double originX=0;
    double originY=0;
};

bool validMapCameraMetrics(const MapCameraMetrics& metrics) noexcept;

class MapCamera {
public:
    MapCamera();

    ProjectionMode mode() const noexcept { return active_; }
    const MapViewState& view() const noexcept;
    const MapViewState& flatView() const noexcept { return flat_; }
    const MapViewState& globeView() const noexcept { return globe_; }
    const MapCameraMetrics& metrics() const noexcept { return metrics_; }
    MapCameraDisplay display() const;

    bool setMetrics(const MapCameraMetrics& metrics);
    bool resize(double width,double height,double devicePixelRatio=1);
    bool adoptView(const MapViewState& view);
    bool acceptPublishedView(const MapViewState& view);
    bool setProjectionMode(ProjectionMode mode);
    bool zoomAt(double factor,double screenX,double screenY);
    bool fit();
    bool focusRect(double left,double top,double width,double height,double maxZoom);

    void beginPan();
    bool panFromGesture(double deltaX,double deltaY);
    void endPan() noexcept { panStart_.reset(); }

private:
    struct FlatControls {
        double zoom=1;
        double panX=0;
        double panY=0;
        double fitScale=1;
        double mapScale=1;
        double originX=0;
        double originY=0;
    };

    FlatControls flatControls(const MapViewState& view,const MapCameraMetrics& metrics) const;
    double globeZoom(const MapViewState& view) const;
    MapViewState flatFromControls(const MapViewState& base,const FlatControls& controls,
                                  double width,double height,double devicePixelRatio) const;
    MapViewState globeFromZoom(const MapViewState& base,double zoom,
                               double width,double height,double devicePixelRatio) const;
    bool replace(MapViewState& target,const MapViewState& proposed);

    MapViewState flat_;
    MapViewState globe_;
    ProjectionMode active_=ProjectionMode::Globe;
    MapCameraMetrics metrics_;
    std::optional<MapViewState> panStart_;
};

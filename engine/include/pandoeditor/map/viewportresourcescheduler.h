#pragma once

#include <pandoeditor/hydroviewport.h>
#include <pandoeditor/map/mapcamera.h>
#include <cstdint>
#include <optional>

enum class ViewportResourceKind : std::uint8_t {
    None=0,
    Terrain=1,
    Hydro=2,
    Labels=4,
    All=7
};

constexpr ViewportResourceKind operator|(ViewportResourceKind left,
                                         ViewportResourceKind right) noexcept {
    return static_cast<ViewportResourceKind>(
        static_cast<std::uint8_t>(left)|static_cast<std::uint8_t>(right));
}
constexpr ViewportResourceKind operator&(ViewportResourceKind left,
                                         ViewportResourceKind right) noexcept {
    return static_cast<ViewportResourceKind>(
        static_cast<std::uint8_t>(left)&static_cast<std::uint8_t>(right));
}
constexpr bool anyViewportResource(ViewportResourceKind value) noexcept {
    return value!=ViewportResourceKind::None;
}

struct ViewportResourceRequest {
    std::uint64_t generation=0;
    ViewportResourceKind resources=ViewportResourceKind::None;
    MapViewState view;
    MapCameraMetrics metrics;
    double flatZoom=1;
    double mapScale=1;
    double originX=0;
    double originY=0;
    pandoeditor::HydroFlatWindow hydroWindow;
};

struct ViewportResourceSchedulerStats {
    std::uint64_t viewportUpdates=0;
    std::uint64_t invalidations=0;
    std::uint64_t deferredUpdates=0;
    std::uint64_t issuedRequests=0;
    std::uint64_t coalescedUpdates=0;
};

ViewportResourceRequest buildViewportResourceRequest(
    const MapCameraDisplay&,const MapCameraMetrics&,ViewportResourceKind,
    std::uint64_t generation);

class ViewportResourceScheduler {
public:
    static constexpr int SettleDelayMs=40;

    // Returns true when the platform adapter should arm/restart the settle timer.
    bool noteViewport(const MapCameraDisplay&,const MapCameraMetrics&,
                      ViewportResourceKind resources=ViewportResourceKind::All);
    bool invalidate(ViewportResourceKind resources=ViewportResourceKind::All);

    // Interaction nesting matches gesture nesting in the UI adapter.
    bool beginInteraction();
    // Returns true when a deferred request is ready to flush immediately.
    bool endInteraction();

    bool interacting() const noexcept {return interactions_>0;}
    bool pending() const noexcept {return anyViewportResource(pendingResources_);}
    std::optional<ViewportResourceRequest> takeReady();
    void reset();

    std::uint64_t lastIssuedGeneration() const noexcept {return generation_;}
    const ViewportResourceSchedulerStats& stats() const noexcept {return stats_;}

private:
    bool sameInput(const MapCameraDisplay&,const MapCameraMetrics&) const noexcept;

    std::optional<MapCameraDisplay> latestDisplay_;
    std::optional<MapCameraMetrics> latestMetrics_;
    ViewportResourceKind pendingResources_=ViewportResourceKind::None;
    int interactions_=0;
    std::uint64_t generation_=0;
    ViewportResourceSchedulerStats stats_;
};

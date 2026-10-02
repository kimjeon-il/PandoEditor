#pragma once

#include <pandoeditor/hydroformat.h>
#include <pandoeditor/map/mapcamera.h>
#include <pandoeditor/project.h>
#include <pandoeditor/spatialindex.h>
#include <array>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <vector>

struct MapExternalHydroPickFeature {
    pandoeditor::ObjectRef ref;
    std::string displayName;
    std::string category;
    std::array<double,4> bounds{};
    const pandoeditor::HydroPhysicalFeature* feature=nullptr;
};

struct MapPickContext {
    bool mobile=false;
    double zoom=1;
    std::optional<pandoeditor::ObjectRef> primary;
    std::set<pandoeditor::ObjectRef> placedLabels;
    std::vector<MapExternalHydroPickFeature> externalHydro;
};

struct MapPickMapRequest {
    double mapX=0;
    double mapY=0;
    double pixelsPerMapUnit=1;
};

struct MapPickScreenRequest {
    double screenX=0;
    double screenY=0;
};

class MapPicker {
public:
    std::vector<pandoeditor::ObjectRef> pickMap(
        const pandoeditor::ProjectSnapshot&,const MapCameraMetrics&,
        const MapPickMapRequest&,const MapPickContext&);
    std::vector<pandoeditor::ObjectRef> pickScreen(
        const pandoeditor::ProjectSnapshot&,const MapViewState&,const MapCameraMetrics&,
        const MapPickScreenRequest&,const MapPickContext&);

    std::optional<pandoeditor::ObjectRef> topCandidate(
        const pandoeditor::ProjectSnapshot&,
        const std::vector<pandoeditor::ObjectRef>&) const;
    std::vector<pandoeditor::ObjectRef> normalizeSelectionCandidates(
        const pandoeditor::ProjectSnapshot&,
        std::vector<pandoeditor::ObjectRef>) const;

    void applyImpact(const pandoeditor::ProjectSnapshot&,
                     const std::vector<pandoeditor::ObjectRef>& changed);
    void reset();
    std::size_t incrementalUpdateCount() const noexcept {return incrementalUpdates_;}

private:
    void ensureIndex(const pandoeditor::ProjectSnapshot&);
    std::vector<pandoeditor::ObjectRef> pickGeographic(
        const pandoeditor::ProjectSnapshot&,const MapCameraMetrics&,
        pandoeditor::Point,double,const MapPickContext&);

    pandoeditor::GeoSpatialIndex spatialIndex_;
    std::string instanceId_;
    std::uint64_t indexedRevision_=0;
    std::size_t incrementalUpdates_=0;
};

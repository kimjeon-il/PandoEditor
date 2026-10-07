#pragma once

#include <pandoeditor/map/mapviewstate.h>
#include <pandoeditor/map/resourcecachepolicy.h>
#include <pandoeditor/presentation.h>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

inline constexpr double MapFlagWidth=18,MapFlagHeight=12,MapFlagGap=5,MapFlagMinZoom=1.8;
inline constexpr std::size_t MapLabelCandidateLimit=2048;

struct MapLabelSource {
    pandoeditor::ObjectRef ref;
    std::string text;
    std::string collisionGroup="map";
    pandoeditor::Point geographic{};
    double width=1,height=1;
    double priority=0,minZoom=0,maxZoom=1e100;
    bool pinned=false;
    bool nameVisible=true;
    bool flagVisible=false;
};

struct MapLabelPlacement {
    pandoeditor::ObjectRef ref;
    std::string text;
    pandoeditor::Point geographic{};
    double x=0,y=0,width=1,height=1;
    bool pinned=false;
    bool nameVisible=true;
    bool flagVisible=false;
};

struct MapLabelLayoutOptions {
    double zoom=1;
    double viewportWidth=1,viewportHeight=1;
    double bottomInset=0;
    double collisionPadding=3;
    std::size_t maxCandidates=MapLabelCandidateLimit;
    std::size_t maxPlaced=MapLabelCandidateLimit;
    // App quality prefilter precedes layoutLabels in the fixed Web. Direct
    // layout callers leave it unset and retain the production layout contract.
    std::optional<double> labelDensity;
};

struct MapLabelEngineStats {
    std::uint64_t sourceRevision=0,layoutRevision=0;
    std::uint64_t sourceRebuilds=0,queries=0,layouts=0,reprojects=0;
    std::uint64_t candidatesExamined=0,placements=0;
    std::size_t sourceCount=0,cellCount=0;
    std::uint64_t builtinSourceRevision=0,builtinSourceRebuilds=0;
    std::size_t builtinSourceCount=0,builtinCellCount=0;
};

class MapLabelEngine {
public:
    void setSources(std::vector<MapLabelSource> sources,std::uint64_t sourceRevision);
    // Viewport/provider snapshots have independent storage and spatial indexes.
    void setBuiltinSources(std::vector<MapLabelSource> sources,std::uint64_t sourceRevision);
    void setBuiltinSuppressedIds(std::set<std::string> ids);
    const std::vector<MapLabelSource>& sources() const noexcept {return sources_;}
    const std::vector<MapLabelSource>& builtinSources() const noexcept {return builtinSources_;}
    const std::vector<MapLabelPlacement>& placements() const noexcept {return placements_;}
    const std::set<pandoeditor::ObjectRef>& placedRefs() const noexcept {return placedRefs_;}

    const std::vector<MapLabelPlacement>& layout(
        const MapViewState&,const MapLabelLayoutOptions&,
        const std::set<pandoeditor::ObjectRef>& selected={});
    const std::vector<MapLabelPlacement>& reproject(const MapViewState&,double zoom);

    void clear();
    void setResourceBudget(std::size_t bytes);
    bool compatibilityResourceBudget() const noexcept {return !resourceBudget_;}
    pandoeditor::ResourceCacheSnapshot resourceCacheSnapshot() const {return resourcePolicy_.snapshot();}
    const MapLabelEngineStats& stats() const noexcept {return stats_;}

private:
    static constexpr double CellDegrees=10.0;
    static int cellKey(int x,int y) noexcept {return y*36+x;}
    static int longitudeCell(double longitude) noexcept;
    static int latitudeCell(double latitude) noexcept;

    static constexpr std::size_t BuiltinIndexBase=std::size_t(1)<<(sizeof(std::size_t)*8-1);
    std::vector<int> visibleCells(const MapViewState&,double paddingPixels,
        const std::map<int,std::vector<std::size_t>>&,bool wrapped) const;
    const MapLabelSource& sourceAt(std::size_t) const;
    bool sourceAvailable(std::size_t) const;
    void invalidatePlacements();
    bool projectPlacement(std::size_t,const MapViewState&,MapLabelPlacement&) const;
    void decorateFlags(double zoom);

    std::vector<MapLabelSource> sources_;
    std::map<pandoeditor::ObjectRef,std::size_t> sourceByRef_;
    std::map<int,std::vector<std::size_t>> cells_;
    std::vector<std::size_t> pinned_;
    std::vector<MapLabelSource> builtinSources_;
    std::map<pandoeditor::ObjectRef,std::size_t> builtinSourceByRef_;
    std::map<int,std::vector<std::size_t>> builtinCells_;
    std::vector<std::size_t> builtinPinned_;
    std::set<std::string> builtinSuppressedIds_;
    std::vector<std::size_t> accepted_;
    std::vector<MapLabelPlacement> placements_;
    std::set<pandoeditor::ObjectRef> placedRefs_;
    MapLabelEngineStats stats_;
    std::size_t sourceBytes_=0;
    std::optional<std::size_t> resourceBudget_;
    pandoeditor::ResourceCachePolicy<int> resourcePolicy_;
    void accountSources();
    void accountWorkingSet();
};

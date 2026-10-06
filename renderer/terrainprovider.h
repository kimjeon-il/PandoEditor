#pragma once
#include <pandoeditor/map/mapviewstate.h>
#include <pandoeditor/map/resourcecachepolicy.h>
#include <QByteArray>
#include <QImage>
#include <QString>
#include <QSize>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <mutex>
#include <functional>

struct TerrainTileSpec {
    int level=0,column=0,row=0;
    int worldOffsetDegrees=0;
    double west=0,south=0,east=0,north=0;
    QString path;
};

struct TerrainTileDemand {
    TerrainTileSpec spec;
    double priority=0;
};

// A value snapshot of geographic demand, with canonical asset identities.
// Planning neither decodes bytes nor changes cache/protection/request state.
struct TerrainDemandPlan {
    int targetLevel=-1;
    std::vector<TerrainTileSpec> baseTiles,targetTiles,prefetchTiles;
    std::vector<TerrainTileDemand> requests;
};

class TerrainTileProvider final {
public:
    // A separate immutable manifest selects raster or DEM; sources never alias.
    TerrainTileProvider(const QByteArray& pinnedManifest,const QString& dataRoot,
                        std::function<QString(const QString&)> assetResolver={});
    bool available() const {return available_;}
    QString error() const {return error_;}
    QString decodeError() const;
    bool isDem() const {return dem_;}
    int gutter() const {return 1;}
    QString manifestVersion() const {return version_;}
    QSize levelSize(int level) const;
    QString relativeTilePath(const TerrainTileSpec& spec) const;
    QImage loadTint() const;
    int targetLevelForView(const MapViewState& view,bool mobileLayout=false) const;
    TerrainDemandPlan planForView(const MapViewState& view,bool mobileLayout=false) const;
    std::vector<TerrainTileSpec> tilesForView(const MapViewState& view,bool mobileLayout=false) const;
    QImage loadTile(const TerrainTileSpec& spec,bool gray=false) const;
    QImage loadTile(int level,int column,int row,bool gray=false) const;
    void setCacheBudget(std::size_t bytes);
    void protectVisible(const std::vector<TerrainTileSpec>& tiles,bool gray=false);
    pandoeditor::ResourceCacheSnapshot resourceCacheSnapshot() const;
    void switchVisibleVariant(bool gray);
    std::size_t cachedBytes() const;
private:
    struct Level {int id=0,width=0,height=0,columns=0,rows=0,tileSize=0;};
    QString root_,error_,version_;
    std::vector<Level> levels_;
    bool available_=false;
    bool dem_=false;
    using CacheKey=std::pair<QString,bool>;
    struct CachedImage {QImage image;std::size_t bytes=0;};
    void trim() const;
    void applyProtection() const;
    void finishPending(const CacheKey&) const;
    TerrainTileSpec tileSpec(int level,int column,int row) const;
    std::vector<TerrainTileSpec> visibleTargetSpecs(const MapViewState& view,int level) const;
    QString tilePath(int level,int column,int row) const;
    void recordDecodeFailure(const QString& path,const QString& reason) const;
    void clearDecodeFailure(const QString& path) const;
    std::function<QString(const QString&)> assetResolver_;
    mutable std::mutex mutex_;
    mutable std::map<CacheKey,CachedImage> images_;
    mutable std::set<CacheKey> visible_,fallback_,pending_;
    mutable bool displayGray_=false;
    mutable pandoeditor::ResourceCachePolicy<CacheKey> policy_{128ull*1024*1024};
    mutable std::size_t resident_=0;
    mutable std::uint64_t failedDecodes_=0;
    mutable QString decodeFailurePath_,decodeFailureReason_;
    std::size_t budget_=128ull*1024*1024;
};

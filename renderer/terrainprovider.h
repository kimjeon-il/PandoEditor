#pragma once
#include "mapviewstate.h"
#include <QByteArray>
#include <QImage>
#include <QString>
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

class TerrainTileProvider final {
public:
    // dataRoot contains terrain/v0.12.6 and hydro/v0.13.1.
    TerrainTileProvider(const QByteArray& pinnedManifest,const QString& dataRoot,
                        std::function<QString(const QString&)> assetResolver={});
    bool available() const {return available_;}
    QString error() const {return error_;}
    std::vector<TerrainTileSpec> tilesForView(const MapViewState& view) const;
    QImage loadTile(const TerrainTileSpec& spec) const;
    QImage loadTile(int level,int column,int row) const;
    void setCacheBudget(std::size_t bytes);
    void protectVisible(const std::vector<TerrainTileSpec>& tiles);
    std::size_t cachedBytes() const;
private:
    struct Level {int id=0,width=0,height=0,columns=0,rows=0,tileSize=0;};
    QString root_,error_;
    std::vector<Level> levels_;
    bool available_=false;
    struct CachedImage {QImage image;std::size_t bytes=0;std::uint64_t used=0;};
    void trim() const;
    QString tilePath(int level,int column,int row) const;
    std::function<QString(const QString&)> assetResolver_;
    mutable std::mutex mutex_;
    mutable std::map<QString,CachedImage> images_;
    mutable std::set<QString> visible_;
    mutable std::uint64_t clock_=0;
    mutable std::size_t resident_=0;
    std::size_t budget_=128ull*1024*1024;
};

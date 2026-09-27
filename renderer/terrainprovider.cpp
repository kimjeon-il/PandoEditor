#include "terrainprovider.h"
#include "projectionengine.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>

TerrainTileProvider::TerrainTileProvider(const QByteArray& pinned,const QString& root):root_(root) {
    if(root_.isEmpty()){error_=QStringLiteral("Terrain tile package not installed");return;}
    QFile installed(QDir(root_).filePath("terrain/v0.12.6/manifest.json"));
    if(!installed.open(QIODevice::ReadOnly)){
        error_=QStringLiteral("Terrain manifest unavailable");return;
    }
    if(installed.readAll()!=pinned){error_=QStringLiteral("Terrain manifest identity mismatch");return;}
    const auto object=QJsonDocument::fromJson(pinned).object();
    if(object.value("version").toString()!=QStringLiteral("0.12.6")||
       object.value("crs").toString()!=QStringLiteral("EPSG:4326")||
       object.value("tileFormat").toString()!=QStringLiteral("lossless WebP RGBA")||
       object.value("gutter").toInt()!=1){
        error_=QStringLiteral("Unsupported terrain dataset");return;
    }
    for(const auto& value:object.value("levels").toArray()) {
        const auto level=value.toObject();
        Level result{level.value("id").toInt(-1),level.value("width").toInt(),
            level.value("height").toInt(),level.value("columns").toInt(),
            level.value("rows").toInt(),level.value("tileSize").toInt()};
        if(result.id!=int(levels_.size())||result.columns<1||result.rows<1||
           result.width<1||result.height<1||result.tileSize!=1024) {
            error_=QStringLiteral("Invalid terrain level grid");return;
        }
        levels_.push_back(result);
    }
    if(levels_.size()!=5){error_=QStringLiteral("Incomplete terrain pyramid");return;}
    if(!QImageReader::supportedImageFormats().contains("webp")) {
        error_=QStringLiteral("Qt WebP image decoder unavailable");return;
    }
    for(int column=0;column<levels_.front().columns;++column)
        if(!QFileInfo::exists(QDir(root_).filePath(
                QString("terrain/v0.12.6/0/%1-0.webp").arg(column)))) {
            error_=QStringLiteral("Terrain level 0 tiles unavailable");return;
        }
    available_=true;
}

std::vector<TerrainTileSpec> TerrainTileProvider::tilesForView(const MapViewState& view) const {
    if(!available_||!validMapViewState(view))return {};
    int level=0;
    if(view.mode==ProjectionMode::Flat) {
        // Display selection only; source terrain resolution remains unchanged.
        const auto pixelsAcrossWorld=std::abs(view.scale)*2*3.14159265358979323846;
        while(level+1<int(levels_.size())&&pixelsAcrossWorld>levels_[level].width*1.6)
            ++level;
    }
    const auto& grid=levels_[level];
    std::vector<TerrainTileSpec> result;
    const auto northSouth=[&] {
        if(view.mode==ProjectionMode::Globe)return std::pair<double,double>{-90,90};
        const auto upper=unprojectFlat(0,0,view).y;
        const auto lower=unprojectFlat(view.viewportWidth,view.viewportHeight,view).y;
        return std::pair<double,double>{std::max(-90.,std::min(upper,lower)),
                                        std::min(90.,std::max(upper,lower))};
    }();
    const auto lonMin=view.mode==ProjectionMode::Globe?-180.:
        std::min(unprojectFlat(0,0,view).x,
                 unprojectFlat(view.viewportWidth,view.viewportHeight,view).x);
    const auto lonMax=view.mode==ProjectionMode::Globe?180.:
        std::max(unprojectFlat(0,0,view).x,
                 unprojectFlat(view.viewportWidth,view.viewportHeight,view).x);
    for(int row=0;row<grid.rows;++row)for(int column=0;column<grid.columns;++column) {
        const auto tileLeft=column*grid.tileSize;
        const auto tileRight=std::min(grid.width,(column+1)*grid.tileSize);
        const auto tileTop=row*grid.tileSize;
        const auto tileBottom=std::min(grid.height,(row+1)*grid.tileSize);
        const auto west=-180.+360.*tileLeft/grid.width;
        const auto east=-180.+360.*tileRight/grid.width;
        const auto north=90.-180.*tileTop/grid.height;
        const auto south=90.-180.*tileBottom/grid.height;
        if(south>northSouth.second||north<northSouth.first)continue;
        for(int copy=-1;copy<=1;++copy) {
            if(east+360*copy<lonMin||west+360*copy>lonMax)continue;
            TerrainTileSpec spec{level,column,row,copy*360,west,south,east,north,
                QDir(root_).filePath(QString("terrain/v0.12.6/%1/%2-%3.webp")
                                     .arg(level).arg(column).arg(row))};
            result.push_back(std::move(spec));
        }
    }
    return result;
}
QImage TerrainTileProvider::loadTile(const TerrainTileSpec& spec) const {
    if(!available_||spec.level<0||spec.level>=int(levels_.size())||
       spec.column<0||spec.column>=levels_[spec.level].columns||
       spec.row<0||spec.row>=levels_[spec.level].rows)return {};
    const auto expected=QDir(root_).filePath(QString("terrain/v0.12.6/%1/%2-%3.webp")
                                             .arg(spec.level).arg(spec.column).arg(spec.row));
    if(spec.path!=expected)return {};
    std::lock_guard lock(mutex_);
    if(auto found=images_.find(expected);found!=images_.end()) {
        found->second.used=++clock_;return found->second.image;
    }
    QImageReader reader(expected,"webp");
    auto image=reader.read();
    if(image.isNull())return {}; // Missing tiles stay explicitly unavailable.
    const auto bytes=std::size_t(image.sizeInBytes());
    images_.emplace(expected,CachedImage{image,bytes,++clock_});
    resident_+=bytes;trim();return image;
}
QImage TerrainTileProvider::loadTile(int level,int column,int row) const {
    TerrainTileSpec spec;
    spec.level=level;spec.column=column;spec.row=row;
    spec.path=QDir(root_).filePath(QString("terrain/v0.12.6/%1/%2-%3.webp")
                                   .arg(level).arg(column).arg(row));
    return loadTile(spec);
}
void TerrainTileProvider::setCacheBudget(std::size_t bytes) {
    std::lock_guard lock(mutex_);budget_=bytes;trim();
}
void TerrainTileProvider::protectVisible(const std::vector<TerrainTileSpec>& tiles) {
    std::lock_guard lock(mutex_);
    visible_.clear();for(const auto& tile:tiles)visible_.insert(tile.path);
    trim();
}
std::size_t TerrainTileProvider::cachedBytes() const {
    std::lock_guard lock(mutex_);return resident_;
}
void TerrainTileProvider::trim() const {
    while(resident_>budget_) {
        auto victim=images_.end();
        for(auto it=images_.begin();it!=images_.end();++it) {
            if(visible_.count(it->first))continue;
            if(victim==images_.end()||it->second.used<victim->second.used)victim=it;
        }
        if(victim==images_.end())break;
        resident_-=victim->second.bytes;images_.erase(victim);
    }
}

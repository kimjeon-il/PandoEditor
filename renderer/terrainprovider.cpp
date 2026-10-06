#include "terrainprovider.h"
#include <pandoeditor/map/projectionengine.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace {
constexpr double terrainPi=3.141592653589793238462643383279502884;
constexpr double terrainRadians=terrainPi/180;
double wrappedLongitude(double longitude) {
    return std::remainder(longitude,360.);
}
std::pair<double,double> demandCenter(const MapViewState& view) {
    // Native flat pan moves translation, whereas Web supplies its geographic
    // viewport center. Normalize that representation at the provider boundary.
    if(view.mode==ProjectionMode::Flat) {
        const auto point=unprojectFlat(view.viewportWidth/2,view.viewportHeight/2,view);
        return {point.x,point.y};
    }
    // Native projectPoint uses center + rotation; Web's D3 rotation has the
    // opposite sign. Demand must follow the native projected camera center.
    return {view.centerLongitude+view.rotationLongitude,
        std::clamp(view.centerLatitude+view.rotationLatitude,-90.,90.)};
}
double greatCircleDistance(double longitude,double latitude,double otherLongitude,double otherLatitude) {
    const double a=latitude*terrainRadians,b=otherLatitude*terrainRadians;
    const double delta=wrappedLongitude(otherLongitude-longitude)*terrainRadians;
    return std::acos(std::clamp(std::sin(a)*std::sin(b)+std::cos(a)*std::cos(b)*std::cos(delta),-1.,1.));
}
}

TerrainTileProvider::TerrainTileProvider(const QByteArray& pinned,const QString& root,
                                         std::function<QString(const QString&)> resolver)
    :root_(root),assetResolver_(std::move(resolver)) {
    const auto object=QJsonDocument::fromJson(pinned).object();
    version_=object.value("version").toString();
    dem_=version_==QStringLiteral("0.13.3")&&object.value("representation")==QStringLiteral("dem-relief-v1");
    const auto manifestPath=QString("terrain/v%1/manifest.json").arg(version_);
    const auto resolved=assetResolver_?assetResolver_(manifestPath):QString{};
    QFile installed(resolved.isEmpty()?QDir(root_).filePath(manifestPath):resolved);
    if(installed.exists()&&(!installed.open(QIODevice::ReadOnly)||installed.readAll()!=pinned))
        {error_=QStringLiteral("Terrain manifest identity mismatch");return;}
    if((!dem_&&version_!=QStringLiteral("0.12.6"))||
       object.value("crs").toString()!=QStringLiteral("EPSG:4326")||
       object.value("tileFormat").toString()!=QStringLiteral("lossless WebP RGBA")||
       object.value("gutter").toInt()!=1){
        error_=QStringLiteral("Unsupported terrain dataset");return;
    }
    if(dem_) {
        const auto elevation=object.value("elevation").toObject(),tint=object.value("tint").toObject();
        if(elevation.value("decode")!="R*256+G-12000"||elevation.value("biasMeters").toInt()!=12000||
           elevation.value("spacingMeters").toInt()!=1||tint.value("width").toInt()!=4096||
           tint.value("height").toInt()!=2048||tint.value("sha256")!=
             "1ae4c70e05494917c11d3c6b32869db61bf4f9e7e4b0239f9b4bd2b9bec582f3") {
            error_=QStringLiteral("Unsupported DEM encoding or tint identity");return;
        }
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
        if(dem_&&(result.id>=6||result.width!=(1350<<result.id)||result.height!=(675<<result.id)||
            result.columns!=(result.width+1023)/1024||result.rows!=(result.height+1023)/1024)) {
            error_=QStringLiteral("Invalid DEM level grid");return;
        }
        levels_.push_back(result);
    }
    if(levels_.size()!=std::size_t(dem_?6:5)){error_=QStringLiteral("Incomplete terrain pyramid");return;}
    available_=true;
}

QString TerrainTileProvider::tilePath(int level,int column,int row) const {
    TerrainTileSpec spec;spec.level=level;spec.column=column;spec.row=row;
    const auto relative=relativeTilePath(spec);
    // An installed verifier's refusal is authoritative, including corrupt
    // files already present under root_. Do not read them through a fallback.
    if(assetResolver_)return assetResolver_(relative);
    return QDir(root_).filePath(relative);
}
QString TerrainTileProvider::decodeError() const {
    std::lock_guard lock(mutex_);return decodeFailureReason_;
}
// Called while mutex_ protects decoder/cache state. A different successful
// asset must not erase the reason a current optional source fell back.
void TerrainTileProvider::recordDecodeFailure(const QString& path,const QString& reason) const {
    if(failedDecodes_!=std::numeric_limits<std::uint64_t>::max())++failedDecodes_;
    decodeFailurePath_=path;decodeFailureReason_=QString("Terrain decode failed: %1 (%2)").arg(path,reason);
}
void TerrainTileProvider::clearDecodeFailure(const QString& path) const {
    if(decodeFailurePath_==path){decodeFailurePath_.clear();decodeFailureReason_.clear();}
}

QString TerrainTileProvider::relativeTilePath(const TerrainTileSpec& spec) const {
    return QString("terrain/v%1/%2/%3-%4.webp").arg(dem_?"0.13.0":"0.12.6")
        .arg(spec.level).arg(spec.column).arg(spec.row);
}
QSize TerrainTileProvider::levelSize(int level) const {
    return level>=0&&level<int(levels_.size())?QSize(levels_[level].width,levels_[level].height):QSize{};
}
QImage TerrainTileProvider::loadTint() const {
    if(!available_||!dem_)return {};
    const QString relative="terrain/v0.13.3/tint.webp";
    const auto path=assetResolver_?assetResolver_(relative):QDir(root_).filePath(relative);
    if(path.isEmpty())return {};
    std::lock_guard lock(mutex_);const CacheKey key{path,false};policy_.touch(key);
    if(const auto found=images_.find(key);found!=images_.end())return found->second.image;
    QImageReader reader(path,"webp");
    if(reader.size()!=QSize(4096,2048)){
        recordDecodeFailure(path,QString("expected 4096x2048 tint; %1").arg(reader.errorString()));return {};}
    auto image=reader.read();if(image.isNull()){recordDecodeFailure(path,reader.errorString());return {};}
    clearDecodeFailure(path);
    image=image.convertToFormat(QImage::Format_RGBA8888);
    const auto bytes=std::size_t(image.sizeInBytes());
    auto inserted=images_.emplace(key,CachedImage{image,bytes});
    try {if(!policy_.admit(key,bytes)){images_.erase(inserted.first);return {};}}
    catch(...){images_.erase(inserted.first);throw;}
    policy_.setProtection(key,pandoeditor::ResourceProtection::Visible,!visible_.empty());
    trim();return image;
}

int TerrainTileProvider::targetLevelForView(const MapViewState& view,bool mobileLayout) const {
    if(!available_||!validMapViewState(view))return -1;
    // MapViewState.scale is already CSS pixels. Backing/frame DPR and country
    // mesh quality are not terrain source-resolution inputs.
    const double sourceDpr=std::min(mobileLayout?2.:3.,std::max(1.,view.devicePixelRatio));
    const double requiredWidth=1.12*std::max(1.,2*terrainPi*view.scale*sourceDpr);
    for(const auto& level:levels_)if(level.width>=requiredWidth)return level.id;
    return levels_.back().id;
}

TerrainTileSpec TerrainTileProvider::tileSpec(int level,int column,int row) const {
    const auto& grid=levels_[level];
    const auto left=column*grid.tileSize,right=std::min(grid.width,(column+1)*grid.tileSize);
    const auto top=row*grid.tileSize,bottom=std::min(grid.height,(row+1)*grid.tileSize);
    return {level,column,row,0,-180.+360.*left/grid.width,90.-180.*bottom/grid.height,
        -180.+360.*right/grid.width,90.-180.*top/grid.height,tilePath(level,column,row)};
}

std::vector<TerrainTileSpec> TerrainTileProvider::visibleTargetSpecs(const MapViewState& view,int level) const {
    const auto& grid=levels_[level];
    std::vector<TerrainTileSpec> result;
    const auto center=demandCenter(view);
    const double scale=std::max(1.,view.scale);
    const double halfLongitude=view.viewportWidth/scale*90/terrainPi;
    const double halfLatitude=view.viewportHeight/scale*90/terrainPi;
    const double globeRadius=std::asin(std::min(1.,std::hypot(view.viewportWidth,view.viewportHeight)*.5/scale));
    for(int row=0;row<grid.rows;++row)for(int column=0;column<grid.columns;++column) {
        // Form bounds before asking an installed verifier for an asset path:
        // selecting a view must not resolve all fine-level world assets.
        const double west=-180.+360.*column*grid.tileSize/grid.width;
        const double east=-180.+360.*std::min(grid.width,(column+1)*grid.tileSize)/grid.width;
        const double north=90.-180.*row*grid.tileSize/grid.height;
        const double south=90.-180.*std::min(grid.height,(row+1)*grid.tileSize)/grid.height;
        const double longitude=(west+east)/2,latitude=(north+south)/2;
        const double halfLon=(east-west)/2,halfLat=(north-south)/2;
        const bool visible=view.mode==ProjectionMode::Flat
            ? std::abs(wrappedLongitude(longitude-center.first))<=halfLongitude+halfLon+2
                &&std::abs(latitude-center.second)<=halfLatitude+halfLat+2
            : greatCircleDistance(center.first,center.second,longitude,latitude)
                <=globeRadius+std::hypot(halfLon,halfLat)*terrainRadians+.04;
        if(visible)result.push_back(tileSpec(level,column,row));
    }
    return result;
}

TerrainDemandPlan TerrainTileProvider::planForView(const MapViewState& view,bool mobileLayout) const {
    TerrainDemandPlan plan;plan.targetLevel=targetLevelForView(view,mobileLayout);
    if(plan.targetLevel<0)return plan;
    const auto& base=levels_.front();
    for(int row=0;row<base.rows;++row)for(int column=0;column<base.columns;++column)
        plan.baseTiles.push_back(tileSpec(base.id,column,row));
    plan.targetTiles=visibleTargetSpecs(view,plan.targetLevel);
    const auto& grid=levels_[plan.targetLevel];
    std::set<std::pair<int,int>> seen;
    for(const auto& spec:plan.targetTiles)seen.insert({spec.column,spec.row});
    for(const auto& spec:plan.targetTiles)
        for(int row=std::max(0,spec.row-1);row<=std::min(grid.rows-1,spec.row+1);++row)
            for(int column=std::max(0,spec.column-1);column<=std::min(grid.columns-1,spec.column+1);++column)
                if(seen.insert({column,row}).second)plan.prefetchTiles.push_back(tileSpec(grid.id,column,row));
    const auto center=demandCenter(view);
    std::set<std::tuple<int,int,int>> requested;
    const auto add=[&](const TerrainTileSpec& spec,double priority) {
        if(requested.insert({spec.level,spec.column,spec.row}).second)plan.requests.push_back({spec,priority});
    };
    for(const auto& spec:plan.baseTiles)add(spec,50000);
    for(const auto& spec:plan.targetTiles) {
        const double longitude=(spec.west+spec.east)/2,latitude=(spec.north+spec.south)/2;
        const double distance=view.mode==ProjectionMode::Flat
            ? std::hypot(wrappedLongitude(longitude-center.first),latitude-center.second)*terrainRadians
            : greatCircleDistance(center.first,center.second,longitude,latitude);
        add(spec,30000-distance*1000);
    }
    for(const auto& spec:plan.prefetchTiles)add(spec,1000);
    std::sort(plan.requests.begin(),plan.requests.end(),[](const auto& left,const auto& right) {
        if(left.priority!=right.priority)return left.priority>right.priority;
        const auto key=[](const TerrainTileSpec& spec) {
            return QString("%1/%2-%3").arg(spec.level).arg(spec.column).arg(spec.row);
        };
        return key(left.spec)<key(right.spec);
    });
    return plan;
}

std::vector<TerrainTileSpec> TerrainTileProvider::tilesForView(const MapViewState& view,bool mobileLayout) const {
    const int level=targetLevelForView(view,mobileLayout);
    if(level<0)return {};
    auto targets=visibleTargetSpecs(view,level);
    if(view.mode==ProjectionMode::Globe)return targets;
    const auto center=demandCenter(view);
    const double halfLongitude=view.viewportWidth/std::max(1.,view.scale)*90/terrainPi;
    const auto offsets=visibleFlatWorldOffsets(view);
    std::vector<TerrainTileSpec> result;
    // Draw offsets follow the shared native projection snapshot. They never
    // become distinct asset/cache keys, and enumeration stays bounded at three.
    for(const auto& tile:targets)for(double offset:offsets) {
        if(tile.east+offset<center.first-halfLongitude-2||tile.west+offset>center.first+halfLongitude+2)continue;
        auto replica=tile;replica.worldOffsetDegrees=int(offset);result.push_back(std::move(replica));
    }
    return result;
}
QImage TerrainTileProvider::loadTile(const TerrainTileSpec& spec,bool grayMode) const {
    if(!available_||spec.level<0||spec.level>=int(levels_.size())||
       spec.column<0||spec.column>=levels_[spec.level].columns||
       spec.row<0||spec.row>=levels_[spec.level].rows)return {};
    const auto expected=tilePath(spec.level,spec.column,spec.row);
    if(expected.isEmpty())return {};
    if(spec.path!=expected)return {};
    std::lock_guard lock(mutex_);
    const CacheKey key{expected,dem_?false:grayMode};
    policy_.touch(key);
    if(auto found=images_.find(key);found!=images_.end()){const auto image=found->second.image;finishPending(key);return image;}
    QImageReader reader(expected,"webp");
    auto image=reader.read();
    if(image.isNull()){recordDecodeFailure(expected,reader.errorString());return {};}
    if(dem_) {
        const auto& grid=levels_[spec.level];
        const QSize size(std::min(grid.tileSize,grid.width-spec.column*grid.tileSize)+2,
                         std::min(grid.tileSize,grid.height-spec.row*grid.tileSize)+2);
        if(image.size()!=size){recordDecodeFailure(expected,"DEM dimensions/gutter mismatch");return {};}
        image=image.convertToFormat(QImage::Format_RGBA8888);
        for(int y=0;y<image.height();++y){const auto* row=image.constScanLine(y);
            for(int x=0;x<image.width();++x)if(row[x*4+3]!=255){recordDecodeFailure(expected,"DEM A channel must be 255");return {};}}
    }
    clearDecodeFailure(expected);
    if(grayMode&&!dem_) {
        image=image.convertToFormat(QImage::Format_ARGB32);
        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
            // Raster A stores Gray Earth, not coverage or transparency.
            const auto gray=qAlpha(image.pixel(x,y));
            image.setPixel(x,y,qRgba(gray,gray,gray,255));
        }
    }
    const auto bytes=std::size_t(image.sizeInBytes());
    auto inserted=images_.emplace(key,CachedImage{image,bytes});
    try {if(!policy_.admit(key,bytes)){images_.erase(inserted.first);return image;}}
    catch(...){images_.erase(inserted.first);throw;}
    policy_.setProtection(key,pandoeditor::ResourceProtection::Visible,visible_.count(key)!=0);
    policy_.setProtection(key,pandoeditor::ResourceProtection::Fallback,fallback_.count(key)!=0);
    finishPending(key);trim();return image;
}
QImage TerrainTileProvider::loadTile(int level,int column,int row,bool gray) const {
    TerrainTileSpec spec;
    spec.level=level;spec.column=column;spec.row=row;
    spec.path=tilePath(level,column,row);
    return loadTile(spec,gray);
}
void TerrainTileProvider::setCacheBudget(std::size_t bytes) {
    std::lock_guard lock(mutex_);budget_=bytes;policy_.setBudget(bytes);trim();
}
void TerrainTileProvider::protectVisible(const std::vector<TerrainTileSpec>& tiles,bool gray) {
    std::lock_guard lock(mutex_);
    if(dem_)gray=false;
    displayGray_=gray;
    if(tiles.empty()){visible_.clear();fallback_.clear();pending_.clear();}
    else {
        if(pending_.empty())fallback_=visible_;
        visible_.clear();for(const auto& tile:tiles)visible_.insert({tile.path,gray});
        pending_=visible_;for(const auto& key:fallback_)if(key.second==gray)pending_.insert(key);
        for(const auto& image:images_)pending_.erase(image.first);
        if(pending_.empty())fallback_.clear();
    }
    applyProtection();trim();
}
std::size_t TerrainTileProvider::cachedBytes() const {
    std::lock_guard lock(mutex_);return resident_;
}
pandoeditor::ResourceCacheSnapshot TerrainTileProvider::resourceCacheSnapshot() const {
    std::lock_guard lock(mutex_);auto snapshot=policy_.snapshot();
    snapshot.pendingCount=pending_.size();snapshot.pendingUnknownCount=pending_.size();
    snapshot.failureCount=failedDecodes_;return snapshot;
}
void TerrainTileProvider::trim() const {
    for(const auto& key:policy_.trim())images_.erase(key);
    resident_=policy_.snapshot().residentBytes;
}

void TerrainTileProvider::applyProtection() const {
    for(const auto& entry:images_) {
        const bool tint=dem_&&entry.first.first.endsWith("/terrain/v0.13.3/tint.webp");
        policy_.setProtection(entry.first,pandoeditor::ResourceProtection::Visible,
            visible_.count(entry.first)!=0||(tint&&!visible_.empty()));
        policy_.setProtection(entry.first,pandoeditor::ResourceProtection::Fallback,fallback_.count(entry.first)!=0);
    }
}
void TerrainTileProvider::finishPending(const CacheKey& key) const {
    pending_.erase(key);bool changed=false;
    if(key.second==displayGray_&&fallback_.count(key))changed=fallback_.erase({key.first,!key.second})!=0;
    if(pending_.empty()&&!fallback_.empty()){fallback_.clear();changed=true;}
    if(changed){applyProtection();trim();}
}

void TerrainTileProvider::switchVisibleVariant(bool gray) {
    std::lock_guard lock(mutex_);
    if(dem_)return; // DEM bytes are decoded once; gray/color are material state.
    std::set<CacheKey> next;for(const auto& key:visible_)next.insert({key.first,gray});
    if(next==visible_&&displayGray_==gray)return;
    displayGray_=gray;
    if(pending_.empty())fallback_=visible_;
    auto fallback=fallback_;
    for(const auto& key:fallback_)fallback.insert({key.first,gray});
    for(auto it=fallback.begin();it!=fallback.end();) {
        if(it->second!=gray&&images_.count({it->first,gray}))it=fallback.erase(it);else ++it;
    }
    fallback_.swap(fallback);visible_.swap(next);pending_=visible_;
    for(const auto& key:fallback_)if(key.second==gray)pending_.insert(key);
    for(const auto& image:images_)pending_.erase(image.first);
    if(pending_.empty())fallback_.clear();
    applyProtection();trim();
}

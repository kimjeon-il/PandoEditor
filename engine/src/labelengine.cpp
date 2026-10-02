#include <pandoeditor/map/labelengine.h>
#include <pandoeditor/map/projectionengine.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <tuple>

namespace {
constexpr double CellDegrees=10.0;

double normalizeLongitude(double value) {
    if(!std::isfinite(value))return 0;
    double result=std::fmod(value+180.0,360.0);
    if(result<0)result+=360.0;
    return result-180.0;
}

bool finiteSource(const MapLabelSource& source) {
    return std::isfinite(source.geographic.x)&&std::isfinite(source.geographic.y)&&
        source.geographic.y>=-90&&source.geographic.y<=90&&
        std::isfinite(source.width)&&source.width>0&&
        std::isfinite(source.height)&&source.height>0&&
        std::isfinite(source.priority)&&std::isfinite(source.minZoom)&&
        std::isfinite(source.maxZoom)&&source.minZoom<=source.maxZoom&&
        !source.collisionGroup.empty();
}

bool sourceOrder(const MapLabelSource& left,const MapLabelSource& right) {
    if(left.pinned!=right.pinned)return left.pinned>right.pinned;
    if(left.priority!=right.priority)return left.priority>right.priority;
    if(left.ref!=right.ref)return left.ref<right.ref;
    return left.text<right.text;
}

struct CollisionBox {
    double left=0,top=0,right=0,bottom=0;
    std::string group;
};

bool overlaps(const CollisionBox& a,const CollisionBox& b) {
    return a.group==b.group&&a.left<b.right&&a.right>b.left&&
        a.top<b.bottom&&a.bottom>b.top;
}

std::pair<int,int> collisionRange(double minimum,double maximum,double cellSize) {
    return {static_cast<int>(std::floor(minimum/cellSize)),
            static_cast<int>(std::floor(maximum/cellSize))};
}
}

int MapLabelEngine::longitudeCell(double longitude) noexcept {
    const auto normalized=normalizeLongitude(longitude);
    return std::clamp(static_cast<int>(std::floor((normalized+180.0)/CellDegrees)),0,35);
}

int MapLabelEngine::latitudeCell(double latitude) noexcept {
    return std::clamp(static_cast<int>(std::floor((std::clamp(latitude,-90.0,90.0)+90.0)/
                                                   CellDegrees)),0,17);
}

void MapLabelEngine::setSources(std::vector<MapLabelSource> sources,
                                std::uint64_t sourceRevision) {
    if(!sourceRevision)throw std::invalid_argument("label source revision must be nonzero");
    cells_.clear();pinned_.clear();accepted_.clear();placements_.clear();
    sources_.clear();sources_.reserve(sources.size());
    for(auto& source:sources) {
        if(!finiteSource(source))continue;
        source.geographic.x=normalizeLongitude(source.geographic.x);
        const auto index=sources_.size();
        sources_.push_back(std::move(source));
        const auto& stored=sources_.back();
        cells_[cellKey(longitudeCell(stored.geographic.x),
                       latitudeCell(stored.geographic.y))].push_back(index);
        if(stored.pinned)pinned_.push_back(index);
    }
    for(auto& [unused,indices]:cells_) {
        (void)unused;
        std::stable_sort(indices.begin(),indices.end(),[&](std::size_t a,std::size_t b) {
            return sourceOrder(sources_[a],sources_[b]);
        });
    }
    stats_.sourceRevision=sourceRevision;
    stats_.layoutRevision=0;
    ++stats_.sourceRebuilds;
    stats_.sourceCount=sources_.size();
    stats_.cellCount=cells_.size();
    stats_.placements=0;
}

std::vector<int> MapLabelEngine::visibleCells(
    const MapViewState& view,double paddingPixels) const {
    std::vector<int> result;
    if(cells_.empty())return result;
    if(view.mode==ProjectionMode::Flat) {
        const auto topLeft=unprojectFlat(-paddingPixels,-paddingPixels,view);
        const auto bottomRight=unprojectFlat(view.viewportWidth+paddingPixels,
                                             view.viewportHeight+paddingPixels,view);
        const double south=std::max(-90.0,std::min(topLeft.y,bottomRight.y));
        const double north=std::min(90.0,std::max(topLeft.y,bottomRight.y));
        const double west=std::min(topLeft.x,bottomRight.x);
        const double east=std::max(topLeft.x,bottomRight.x);
        if(east<-180||west>180)return result;
        const int y0=latitudeCell(south),y1=latitudeCell(north);
        const int x0=longitudeCell(std::max(-179.999999,west));
        const int x1=longitudeCell(std::min(179.999999,east));
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
            const int key=cellKey(x,y);
            if(cells_.count(key))result.push_back(key);
        }
        return result;
    }

    // Globe queries inspect only the fixed 10-degree cell grid (<=648 cells),
    // never the full label population. Nine samples make limb/pole cells
    // conservative while the per-label projection remains authoritative.
    for(const auto& [key,unused]:cells_) {
        (void)unused;
        const int x=key%36,y=key/36;
        const double west=-180+x*CellDegrees,east=west+CellDegrees;
        const double south=-90+y*CellDegrees,north=south+CellDegrees;
        bool possible=false;
        for(int sy=0;sy<3&&!possible;++sy)for(int sx=0;sx<3&&!possible;++sx) {
            const pandoeditor::Point sample{
                west+(east-west)*sx/2.0,
                south+(north-south)*sy/2.0};
            const auto projected=projectPoint(sample,view);
            if(projected.finite&&projected.visibleHemisphere&&
               projected.x>=-paddingPixels&&projected.x<=view.viewportWidth+paddingPixels&&
               projected.y>=-paddingPixels&&projected.y<=view.viewportHeight+paddingPixels)
                possible=true;
        }
        if(possible)result.push_back(key);
    }
    return result;
}

bool MapLabelEngine::projectPlacement(
    std::size_t index,const MapViewState& view,MapLabelPlacement& output) const {
    if(index>=sources_.size())return false;
    const auto& source=sources_[index];
    const auto projected=projectPoint(source.geographic,view);
    if(!projected.finite||!projected.visibleHemisphere)return false;
    output={source.ref,source.text,source.geographic,projected.x,projected.y,
            source.width,source.height,source.pinned,source.nameVisible,source.flagVisible};
    return true;
}

const std::vector<MapLabelPlacement>& MapLabelEngine::layout(
    const MapViewState& view,const MapLabelLayoutOptions& options,
    const std::set<pandoeditor::ObjectRef>& selected) {
    if(!validMapViewState(view)||!std::isfinite(options.zoom)||
       !std::isfinite(options.viewportWidth)||!std::isfinite(options.viewportHeight)||
       !std::isfinite(options.bottomInset)||!std::isfinite(options.collisionPadding)||
       options.viewportWidth<=0||options.viewportHeight<=0||
       options.bottomInset<0||options.collisionPadding<0||
       !options.maxCandidates||!options.maxPlaced)
        throw std::invalid_argument("invalid label layout options");

    ++stats_.queries;++stats_.layouts;
    const auto cells=visibleCells(view,64);
    struct Cursor {
        int key=0;
        std::size_t position=0;
    };
    struct Worse {
        const MapLabelEngine* engine=nullptr;
        bool operator()(const Cursor& a,const Cursor& b) const {
            const auto& ai=engine->cells_.at(a.key);
            const auto& bi=engine->cells_.at(b.key);
            return sourceOrder(engine->sources_[bi[b.position]],
                               engine->sources_[ai[a.position]]);
        }
    };
    std::priority_queue<Cursor,std::vector<Cursor>,Worse> queue{Worse{this}};
    for(const auto key:cells)if(!cells_.at(key).empty())queue.push({key,0});

    std::vector<std::size_t> candidates;
    candidates.reserve(std::min(options.maxCandidates,sources_.size()));
    std::set<std::size_t> seen;
    const auto addForced=[&](std::size_t index) {
        if(index<sources_.size()&&seen.insert(index).second)candidates.push_back(index);
    };
    for(const auto index:pinned_)addForced(index);
    if(!selected.empty())for(std::size_t i=0;i<sources_.size();++i)
        if(selected.count(sources_[i].ref))addForced(i);

    while(!queue.empty()&&candidates.size()<options.maxCandidates) {
        const auto cursor=queue.top();queue.pop();
        const auto& bucket=cells_.at(cursor.key);
        const auto index=bucket[cursor.position];
        if(seen.insert(index).second)candidates.push_back(index);
        if(cursor.position+1<bucket.size())queue.push({cursor.key,cursor.position+1});
    }
    stats_.candidatesExamined+=candidates.size();

    std::stable_sort(candidates.begin(),candidates.end(),[&](std::size_t a,std::size_t b) {
        const bool aSelected=selected.count(sources_[a].ref);
        const bool bSelected=selected.count(sources_[b].ref);
        if(aSelected!=bSelected)return aSelected>bSelected;
        return sourceOrder(sources_[a],sources_[b]);
    });

    accepted_.clear();placements_.clear();
    accepted_.reserve(std::min(options.maxPlaced,candidates.size()));
    placements_.reserve(std::min(options.maxPlaced,candidates.size()));
    std::vector<CollisionBox> placedBoxes;
    placedBoxes.reserve(std::min(options.maxPlaced,candidates.size()));
    constexpr double CollisionCell=64.0;
    std::map<std::pair<int,int>,std::vector<std::size_t>> collisionGrid;
    const double contentBottom=std::max(0.0,options.viewportHeight-options.bottomInset);

    for(const auto index:candidates) {
        if(accepted_.size()>=options.maxPlaced)break;
        const auto& source=sources_[index];
        if(options.zoom<source.minZoom||options.zoom>source.maxZoom)continue;
        MapLabelPlacement placement;
        if(!projectPlacement(index,view,placement))continue;
        const bool forced=source.pinned||selected.count(source.ref);
        const double halfW=source.width/2,halfH=source.height/2;
        if(!forced&&(placement.x-halfW<0||placement.x+halfW>options.viewportWidth||
                    placement.y-halfH<0||placement.y+halfH>contentBottom))
            continue;
        CollisionBox box{
            placement.x-halfW-options.collisionPadding,
            placement.y-halfH-options.collisionPadding,
            placement.x+halfW+options.collisionPadding,
            placement.y+halfH+options.collisionPadding,
            source.collisionGroup};
        bool collision=false;
        const auto xr=collisionRange(box.left,box.right,CollisionCell);
        const auto yr=collisionRange(box.top,box.bottom,CollisionCell);
        if(!forced)for(int y=yr.first;y<=yr.second&&!collision;++y)
            for(int x=xr.first;x<=xr.second&&!collision;++x)
                if(const auto cell=collisionGrid.find({x,y});cell!=collisionGrid.end())
                    for(const auto placedIndex:cell->second)
                        if(overlaps(box,placedBoxes[placedIndex])){collision=true;break;}
        if(collision)continue;
        const auto placedIndex=placedBoxes.size();
        placedBoxes.push_back(std::move(box));
        for(int y=yr.first;y<=yr.second;++y)for(int x=xr.first;x<=xr.second;++x)
            collisionGrid[{x,y}].push_back(placedIndex);
        accepted_.push_back(index);
        placements_.push_back(std::move(placement));
    }

    if(stats_.layoutRevision==std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("label layout revision overflow");
    ++stats_.layoutRevision;
    stats_.placements=placements_.size();
    return placements_;
}

const std::vector<MapLabelPlacement>& MapLabelEngine::reproject(const MapViewState& view) {
    if(!validMapViewState(view))throw std::invalid_argument("invalid label reproject view");
    ++stats_.reprojects;
    placements_.clear();placements_.reserve(accepted_.size());
    for(const auto index:accepted_) {
        MapLabelPlacement placement;
        if(!projectPlacement(index,view,placement))continue;
        if(placement.x+placement.width/2<0||placement.x-placement.width/2>view.viewportWidth||
           placement.y+placement.height/2<0||placement.y-placement.height/2>view.viewportHeight)
            continue;
        placements_.push_back(std::move(placement));
    }
    stats_.placements=placements_.size();
    return placements_;
}

std::set<pandoeditor::ObjectRef> MapLabelEngine::placedRefs() const {
    std::set<pandoeditor::ObjectRef> result;
    for(const auto& placement:placements_)result.insert(placement.ref);
    return result;
}

void MapLabelEngine::clear() {
    sources_.clear();cells_.clear();pinned_.clear();accepted_.clear();placements_.clear();
    stats_={};
}

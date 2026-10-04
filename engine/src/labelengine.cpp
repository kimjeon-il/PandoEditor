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
        !std::isnan(source.maxZoom)&&source.minZoom<=source.maxZoom&&
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
    sourceByRef_.clear();cells_.clear();pinned_.clear();accepted_.clear();placements_.clear();placedRefs_.clear();
    sources_.clear();sources_.reserve(sources.size());
    for(auto& source:sources) {
        if(!finiteSource(source))continue;
        source.geographic.x=normalizeLongitude(source.geographic.x);
        const auto index=sources_.size();
        sources_.push_back(std::move(source));
        const auto& stored=sources_.back();
        sourceByRef_[stored.ref]=index;
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
    accountSources();accountWorkingSet();
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
    // never the full label population. A conservative angular cell radius
    // prevents tiny high-zoom viewport intersections and limb cells from being
    // missed without scanning every label in those cells.
    constexpr double cellRadiusRadians=8.0*3.14159265358979323846/180.0;
    const double projectedRadius=view.scale*std::sin(cellRadiusRadians);
    const double frontMargin=std::sin(cellRadiusRadians);
    for(const auto& [key,unused]:cells_) {
        (void)unused;
        const int x=key%36,y=key/36;
        const pandoeditor::Point center{
            -180+(x+.5)*CellDegrees,
            -90+(y+.5)*CellDegrees};
        const auto projected=projectPoint(center,view);
        if(!projected.finite||projected.frontness< -frontMargin)continue;
        if(projected.x+projectedRadius< -paddingPixels||
           projected.x-projectedRadius>view.viewportWidth+paddingPixels||
           projected.y+projectedRadius< -paddingPixels||
           projected.y-projectedRadius>view.viewportHeight+paddingPixels)continue;
        result.push_back(key);
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
        std::size_t position=0,source=0;
    };
    struct Worse {
        const std::vector<MapLabelSource>* sources=nullptr;
        bool operator()(const Cursor& a,const Cursor& b) const {
            return sourceOrder(sources->at(b.source),sources->at(a.source));
        }
    };
    std::priority_queue<Cursor,std::vector<Cursor>,Worse> queue{Worse{&sources_}};
    for(const auto key:cells)if(!cells_.at(key).empty()) {
        const auto& bucket=cells_.at(key);
        queue.push({key,0,bucket.front()});
    }

    std::vector<std::size_t> candidates;
    candidates.reserve(std::min(options.maxCandidates,sources_.size()));
    std::set<std::size_t> seen;
    const auto addForced=[&](std::size_t index) {
        if(index<sources_.size()&&seen.insert(index).second)candidates.push_back(index);
    };
    for(const auto index:pinned_)addForced(index);
    for(const auto& ref:selected)
        if(const auto found=sourceByRef_.find(ref);found!=sourceByRef_.end())
            addForced(found->second);

    while(!queue.empty()&&candidates.size()<options.maxCandidates) {
        const auto cursor=queue.top();queue.pop();
        const auto& bucket=cells_.at(cursor.key);
        const auto index=cursor.source;
        if(seen.insert(index).second)candidates.push_back(index);
        if(cursor.position+1<bucket.size())
            queue.push({cursor.key,cursor.position+1,bucket[cursor.position+1]});
    }
    stats_.candidatesExamined+=candidates.size();

    std::stable_sort(candidates.begin(),candidates.end(),[&](std::size_t a,std::size_t b) {
        const bool aSelected=selected.count(sources_[a].ref);
        const bool bSelected=selected.count(sources_[b].ref);
        if(aSelected!=bSelected)return aSelected>bSelected;
        return sourceOrder(sources_[a],sources_[b]);
    });

    accepted_.clear();placements_.clear();placedRefs_.clear();
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
        placedRefs_.insert(placement.ref);
        placements_.push_back(std::move(placement));
    }

    if(stats_.layoutRevision==std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("label layout revision overflow");
    ++stats_.layoutRevision;
    stats_.placements=placements_.size();
    accountWorkingSet();
    return placements_;
}

const std::vector<MapLabelPlacement>& MapLabelEngine::reproject(const MapViewState& view) {
    if(!validMapViewState(view))throw std::invalid_argument("invalid label reproject view");
    ++stats_.reprojects;
    placements_.clear();placedRefs_.clear();placements_.reserve(accepted_.size());
    for(const auto index:accepted_) {
        MapLabelPlacement placement;
        if(!projectPlacement(index,view,placement))continue;
        if(placement.x+placement.width/2<0||placement.x-placement.width/2>view.viewportWidth||
           placement.y+placement.height/2<0||placement.y-placement.height/2>view.viewportHeight)
            continue;
        placedRefs_.insert(placement.ref);
        placements_.push_back(std::move(placement));
    }
    stats_.placements=placements_.size();
    accountWorkingSet();
    return placements_;
}

void MapLabelEngine::clear() {
    decltype(sources_){}.swap(sources_);sourceByRef_.clear();cells_.clear();
    decltype(pinned_){}.swap(pinned_);decltype(accepted_){}.swap(accepted_);
    decltype(placements_){}.swap(placements_);placedRefs_.clear();
    stats_={};sourceBytes_=0;resourcePolicy_.resetScope();
}

namespace {
void labelBytesAdd(std::size_t& total,std::size_t count,std::size_t size=1) {
    if(size&&count>(std::numeric_limits<std::size_t>::max()-total)/size)
        throw std::overflow_error("label resource size overflow");
    total+=count*size;
}
std::size_t labelStringStorage(const std::string& text) {
    // Count heap backing only; SSO lies inside an already-counted object.
    const auto address=reinterpret_cast<std::uintptr_t>(text.data());
    const auto object=reinterpret_cast<std::uintptr_t>(&text);
    return address>=object&&address<object+sizeof(text)?0:text.capacity()+1;
}
}
void MapLabelEngine::accountSources() {
    std::size_t bytes=0;labelBytesAdd(bytes,sources_.capacity(),sizeof(MapLabelSource));
    for(const auto& source:sources_) {
        labelBytesAdd(bytes,labelStringStorage(source.text));
        labelBytesAdd(bytes,labelStringStorage(source.collisionGroup));
        labelBytesAdd(bytes,labelStringStorage(source.ref.domain));
        labelBytesAdd(bytes,labelStringStorage(source.ref.id));
    }
    // Container-node allocator bookkeeping is intentionally outside payload accounting.
    labelBytesAdd(bytes,sourceByRef_.size(),sizeof(decltype(sourceByRef_)::value_type));
    for(const auto& item:sourceByRef_){labelBytesAdd(bytes,labelStringStorage(item.first.domain));labelBytesAdd(bytes,labelStringStorage(item.first.id));}
    labelBytesAdd(bytes,cells_.size(),sizeof(decltype(cells_)::value_type));
    for(const auto& cell:cells_)labelBytesAdd(bytes,cell.second.capacity(),sizeof(std::size_t));
    labelBytesAdd(bytes,pinned_.capacity(),sizeof(std::size_t));sourceBytes_=bytes;
}
void MapLabelEngine::accountWorkingSet() {
    std::size_t bytes=sourceBytes_;
    labelBytesAdd(bytes,accepted_.capacity(),sizeof(std::size_t));
    labelBytesAdd(bytes,placements_.capacity(),sizeof(MapLabelPlacement));
    for(const auto& placement:placements_) {
        labelBytesAdd(bytes,labelStringStorage(placement.text));
        labelBytesAdd(bytes,labelStringStorage(placement.ref.domain));
        labelBytesAdd(bytes,labelStringStorage(placement.ref.id));
    }
    labelBytesAdd(bytes,placedRefs_.size(),sizeof(pandoeditor::ObjectRef));
    for(const auto& ref:placedRefs_){labelBytesAdd(bytes,labelStringStorage(ref.domain));labelBytesAdd(bytes,labelStringStorage(ref.id));}
    resourcePolicy_.setBudget(resourceBudget_.value_or(bytes));
    if(!resourcePolicy_.admit(0,bytes))throw std::overflow_error("label working set overflow");
    resourcePolicy_.setProtection(0,pandoeditor::ResourceProtection::Visible,true);
    resourcePolicy_.trim(); // Protected active snapshot never loses individual sources.
}
void MapLabelEngine::setResourceBudget(std::size_t bytes) {
    resourceBudget_=bytes;resourcePolicy_.setBudget(bytes);resourcePolicy_.trim();
}

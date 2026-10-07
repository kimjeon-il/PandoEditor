#include <pandoeditor/map/labelengine.h>
#include <pandoeditor/map/projectionengine.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <string_view>

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
    // Web builtin and editable places both use label:<id> as their source key.
    // Keep the native readonly domain distinct while comparing canonical keys.
    const auto domain=[](const pandoeditor::ObjectRef& ref)->std::string_view {
        return ref.domain=="placeBuiltin"?std::string_view("label"):std::string_view(ref.domain);
    };
    if(domain(left.ref)!=domain(right.ref))return domain(left.ref)<domain(right.ref);
    if(left.ref.id!=right.ref.id)return left.ref.id<right.ref.id;
    if(left.ref!=right.ref)return left.ref<right.ref;
    return left.text<right.text;
}

struct CollisionBox {
    double left=0,top=0,right=0,bottom=0;
    std::string group;
};

bool overlaps(const CollisionBox& a,const CollisionBox& b,double padding) {
    return a.group==b.group&&!(a.right+padding<b.left||a.left-padding>b.right||
        a.bottom+padding<b.top||a.top-padding>b.bottom);
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
    if(!sourceRevision||sources.size()>=BuiltinIndexBase)throw std::invalid_argument("label source revision must be nonzero and size bounded");
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

void MapLabelEngine::invalidatePlacements() {
    accepted_.clear();placements_.clear();placedRefs_.clear();
    stats_.layoutRevision=0;stats_.placements=0;
}
const MapLabelSource& MapLabelEngine::sourceAt(std::size_t index) const {
    return index>=BuiltinIndexBase?builtinSources_.at(index-BuiltinIndexBase):sources_.at(index);
}
bool MapLabelEngine::sourceAvailable(std::size_t index) const {
    if(index<BuiltinIndexBase)return index<sources_.size();
    const auto local=index-BuiltinIndexBase;
    return local<builtinSources_.size()&&!builtinSuppressedIds_.count(builtinSources_[local].ref.id);
}
void MapLabelEngine::setBuiltinSources(std::vector<MapLabelSource> sources,std::uint64_t revision) {
    if(!revision||sources.size()>=BuiltinIndexBase)throw std::invalid_argument("invalid builtin label source revision or size");
    builtinSourceByRef_.clear();builtinCells_.clear();builtinPinned_.clear();invalidatePlacements();
    std::vector<MapLabelSource> snapshot;snapshot.reserve(sources.size());
    for(auto& source:sources) {
        if(!finiteSource(source))continue;
        source.geographic.x=normalizeLongitude(source.geographic.x);
        const auto index=BuiltinIndexBase+snapshot.size();snapshot.push_back(std::move(source));
        const auto& stored=snapshot.back();builtinSourceByRef_[stored.ref]=index;
        builtinCells_[cellKey(longitudeCell(stored.geographic.x),latitudeCell(stored.geographic.y))].push_back(index);
        if(stored.pinned)builtinPinned_.push_back(index);
    }
    builtinSources_.swap(snapshot);
    for(auto& [unused,indices]:builtinCells_) {
        (void)unused;std::stable_sort(indices.begin(),indices.end(),[&](std::size_t a,std::size_t b){return sourceOrder(sourceAt(a),sourceAt(b));});
    }
    stats_.builtinSourceRevision=revision;++stats_.builtinSourceRebuilds;
    stats_.builtinSourceCount=builtinSources_.size();stats_.builtinCellCount=builtinCells_.size();
    accountSources();accountWorkingSet();
}
void MapLabelEngine::setBuiltinSuppressedIds(std::set<std::string> ids) {
    if(ids==builtinSuppressedIds_)return;
    builtinSuppressedIds_=std::move(ids);invalidatePlacements();accountSources();accountWorkingSet();
}

std::vector<int> MapLabelEngine::visibleCells(
    const MapViewState& view,double paddingPixels,const std::map<int,std::vector<std::size_t>>& sourceCells,bool wrapped) const {
    std::vector<int> result;
    if(sourceCells.empty())return result;
    if(view.mode==ProjectionMode::Flat) {
        const auto topLeft=unprojectFlat(-paddingPixels,-paddingPixels,view);
        const auto bottomRight=unprojectFlat(view.viewportWidth+paddingPixels,
                                             view.viewportHeight+paddingPixels,view);
        const double south=std::max(-90.0,std::min(topLeft.y,bottomRight.y));
        const double north=std::min(90.0,std::max(topLeft.y,bottomRight.y));
        const auto offsets=wrapped?visibleFlatWorldOffsets(view):std::vector<double>{0};
        std::set<int> seen;
        for(const double offset:offsets) {
            const double west=std::min(topLeft.x,bottomRight.x)-offset;
            const double east=std::max(topLeft.x,bottomRight.x)-offset;
            if(east<-180||west>180)continue;
            const int y0=latitudeCell(south),y1=latitudeCell(north);
            const int x0=longitudeCell(std::max(-179.999999,west));
            const int x1=longitudeCell(std::min(179.999999,east));
            for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
                const int key=cellKey(x,y);
                if(sourceCells.count(key)&&seen.insert(key).second)result.push_back(key);
            }
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
    for(const auto& [key,unused]:sourceCells) {
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
    if(!sourceAvailable(index))return false;
    const auto& source=sourceAt(index);
    auto projected=projectPoint(source.geographic,view);
    if(index>=BuiltinIndexBase&&view.mode==ProjectionMode::Flat) {
        // Canonical provider identities stay fixed while drawing one nearest
        // native world replica, including translation-based dateline panning.
        double distance=std::numeric_limits<double>::infinity();
        for(const double offset:visibleFlatWorldOffsets(view)) {
            const auto candidate=projectPoint(source.geographic,view,offset);
            const auto gap=std::abs(candidate.x-view.viewportWidth/2);
            if(candidate.finite&&gap<distance){projected=candidate;distance=gap;}
        }
    }
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
    struct Cursor {
        int key=0;
        std::size_t position=0,source=0;
        bool builtin=false;
    };
    struct Worse {
        const MapLabelEngine* engine=nullptr;
        bool operator()(const Cursor& a,const Cursor& b) const {
            return sourceOrder(engine->sourceAt(b.source),engine->sourceAt(a.source));
        }
    };
    std::priority_queue<Cursor,std::vector<Cursor>,Worse> queue{Worse{this}};
    const auto enqueue=[&](const auto& sourceCells,bool builtin) {
        for(const auto key:visibleCells(view,64,sourceCells,builtin))if(!sourceCells.at(key).empty()) {
            const auto& bucket=sourceCells.at(key);queue.push({key,0,bucket.front(),builtin});
        }
    };
    enqueue(cells_,false);enqueue(builtinCells_,true);

    const auto candidateLimit=std::min(options.maxCandidates,MapLabelCandidateLimit);
    const double contentBottom=std::max(0.0,options.viewportHeight-options.bottomInset);
    const auto orderedBefore=[&](std::size_t a,std::size_t b) {
        const bool aSelected=selected.count(sourceAt(a).ref);
        const bool bSelected=selected.count(sourceAt(b).ref);
        if(aSelected!=bSelected)return aSelected>bSelected;
        if(sourceOrder(sourceAt(a),sourceAt(b)))return true;
        if(sourceOrder(sourceAt(b),sourceAt(a)))return false;
        return a<b;
    };
    std::optional<std::set<std::size_t>> qualityCandidates;
    const auto visibleCandidate=[&](std::size_t index,bool forced) {
        if(!forced&&qualityCandidates&&!qualityCandidates->count(index))return false;
        if(!sourceAvailable(index))return false;
        const auto& source=sourceAt(index);
        if(options.zoom<source.minZoom||options.zoom>source.maxZoom)return false;
        if(!source.nameVisible&&(!source.flagVisible||options.zoom<MapFlagMinZoom))return false;
        MapLabelPlacement p;if(!projectPlacement(index,view,p))return false;
        return forced||(p.x-source.width/2>=0&&p.x+source.width/2<=options.viewportWidth&&
            p.y-source.height/2>=0&&p.y+source.height/2<=contentBottom);
    };
    // The fixed Web cap follows zoom/projection/bounds filtering and includes
    // selected/pinned sources. Bound their temporary storage even for a large
    // selection, then fill remaining slots through the existing spatial heap.
    std::set<std::size_t,decltype(orderedBefore)> forced(orderedBefore);
    const auto addForced=[&](std::size_t index) {
        if(!visibleCandidate(index,true))return;
        forced.insert(index);if(forced.size()>candidateLimit)forced.erase(std::prev(forced.end()));
    };
    for(const auto index:pinned_)addForced(index);
    for(const auto index:builtinPinned_)addForced(index);
    for(const auto& ref:selected) {
        if(const auto found=sourceByRef_.find(ref);found!=sourceByRef_.end())
            addForced(found->second);
        else if(const auto found=builtinSourceByRef_.find(ref);found!=builtinSourceByRef_.end())
            addForced(found->second);
    }

    if(options.labelDensity) {
        auto density=*options.labelDensity;
        if(density==0||std::isnan(density))density=1; // Number(value) || 1
        density=std::clamp(density,.25,1.);
        const double backgroundLimit=density>=.99?double(MapLabelCandidateLimit):
            std::max(density<.6?42.:72.,std::floor(options.viewportWidth*options.viewportHeight/8500*density));
        const auto available=sources_.size()+builtinSources_.size();
        const auto limit=backgroundLimit>=double(available)?available:std::size_t(backgroundLimit);
        const auto qualityBefore=[&](std::size_t a,std::size_t b) {
            const auto pa=sourceAt(a).priority,pb=sourceAt(b).priority;
            return pa!=pb?pa>pb:a<b; // Web stable priority sort, original source order
        };
        std::set<std::size_t,decltype(qualityBefore)> background(qualityBefore);
        auto scan=queue;
        while(!scan.empty()) {
            const auto cursor=scan.top();scan.pop();
            const auto& bucket=cursor.builtin?builtinCells_.at(cursor.key):cells_.at(cursor.key);
            const auto index=cursor.source;const auto& source=sourceAt(index);
            // App prefilter runs after geographic projection/zoom and before
            // layout box bounds. Selected and pinned sources have no quota here.
            if(!source.pinned&&!selected.count(source.ref)&&visibleCandidate(index,true)) {
                background.insert(index);
                if(background.size()>limit)background.erase(std::prev(background.end()));
            }
            if(cursor.position+1<bucket.size())
                scan.push({cursor.key,cursor.position+1,bucket[cursor.position+1],cursor.builtin});
        }
        qualityCandidates.emplace(background.begin(),background.end());
    }

    std::vector<std::size_t> candidates(forced.begin(),forced.end());
    candidates.reserve(std::min(candidateLimit,sources_.size()+builtinSources_.size()));
    while(!queue.empty()&&candidates.size()<candidateLimit) {
        const auto cursor=queue.top();queue.pop();
        const auto& bucket=cursor.builtin?builtinCells_.at(cursor.key):cells_.at(cursor.key);
        const auto index=cursor.source;
        const auto& source=sourceAt(index);
        if(!source.pinned&&!selected.count(source.ref)&&visibleCandidate(index,false))candidates.push_back(index);
        if(cursor.position+1<bucket.size())
            queue.push({cursor.key,cursor.position+1,bucket[cursor.position+1],cursor.builtin});
    }
    stats_.candidatesExamined+=candidates.size();

    std::stable_sort(candidates.begin(),candidates.end(),orderedBefore);

    accepted_.clear();placements_.clear();placedRefs_.clear();
    accepted_.reserve(std::min(options.maxPlaced,candidates.size()));
    placements_.reserve(std::min(options.maxPlaced,candidates.size()));
    std::vector<CollisionBox> placedBoxes;
    placedBoxes.reserve(std::min(options.maxPlaced,candidates.size()));
    constexpr double CollisionCell=64.0;
    std::map<std::pair<int,int>,std::vector<std::size_t>> collisionGrid;

    for(const auto index:candidates) {
        if(accepted_.size()>=options.maxPlaced)break;
        const auto& source=sourceAt(index);
        if(options.zoom<source.minZoom||options.zoom>source.maxZoom)continue;
        if(!source.nameVisible&&(!source.flagVisible||options.zoom<MapFlagMinZoom))continue;
        MapLabelPlacement placement;
        if(!projectPlacement(index,view,placement))continue;
        const bool forced=source.pinned||selected.count(source.ref);
        const double halfW=source.width/2,halfH=source.height/2;
        if(!forced&&(placement.x-halfW<0||placement.x+halfW>options.viewportWidth||
                    placement.y-halfH<0||placement.y+halfH>contentBottom))
            continue;
        CollisionBox box{
            placement.x-halfW,
            placement.y-halfH,
            placement.x+halfW,
            placement.y+halfH,
            source.collisionGroup};
        bool collision=false;
        const auto xr=collisionRange(box.left-options.collisionPadding,box.right+options.collisionPadding,CollisionCell);
        const auto yr=collisionRange(box.top-options.collisionPadding,box.bottom+options.collisionPadding,CollisionCell);
        if(!forced)for(int y=yr.first;y<=yr.second&&!collision;++y)
            for(int x=xr.first;x<=xr.second&&!collision;++x)
                if(const auto cell=collisionGrid.find({x,y});cell!=collisionGrid.end())
                    for(const auto placedIndex:cell->second)
                        if(overlaps(box,placedBoxes[placedIndex],options.collisionPadding)){collision=true;break;}
        if(collision)continue;
        const auto placedIndex=placedBoxes.size();
        placedBoxes.push_back(std::move(box));
        const auto insertX=collisionRange(placedBoxes.back().left,placedBoxes.back().right,CollisionCell);
        const auto insertY=collisionRange(placedBoxes.back().top,placedBoxes.back().bottom,CollisionCell);
        for(int y=insertY.first;y<=insertY.second;++y)for(int x=insertX.first;x<=insertX.second;++x)
            collisionGrid[{x,y}].push_back(placedIndex);
        accepted_.push_back(index);
        placedRefs_.insert(placement.ref);
        placements_.push_back(std::move(placement));
    }

    decorateFlags(options.zoom);
    if(stats_.layoutRevision==std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("label layout revision overflow");
    ++stats_.layoutRevision;
    stats_.placements=placements_.size();
    accountWorkingSet();
    return placements_;
}

void MapLabelEngine::decorateFlags(double zoom) {
    // Port of web territorial-label-flags: decorate accepted names, never evict
    // them. The screen grid preserves its ordered decisions without an O(n²) scan.
    const auto finish=[&] {
        placements_.erase(std::remove_if(placements_.begin(),placements_.end(),[](const auto& placement) {
            return !placement.nameVisible&&!placement.flagVisible;
        }),placements_.end());
        placedRefs_.clear();for(const auto& placement:placements_)placedRefs_.insert(placement.ref);
    };
    if(zoom<MapFlagMinZoom) {
        for(auto& placement:placements_)if(placement.ref.domain=="territorial")placement.flagVisible=false;
        finish();return;
    }
    if(std::none_of(placements_.begin(),placements_.end(),[](const auto& placement) {
        return placement.flagVisible&&placement.ref.domain=="territorial";
    }))return;
    std::vector<CollisionBox> boxes;boxes.reserve(placements_.size());
    std::map<std::pair<int,int>,std::vector<std::size_t>> grid;
    constexpr double cellSize=64;
    const auto insert=[&](std::size_t index,const CollisionBox& box) {
        const auto xr=collisionRange(box.left,box.right,cellSize);
        const auto yr=collisionRange(box.top,box.bottom,cellSize);
        for(int y=yr.first;y<=yr.second;++y)for(int x=xr.first;x<=xr.second;++x)
            grid[{x,y}].push_back(index);
    };
    for(const auto& placement:placements_) {
        boxes.push_back({placement.x-placement.width/2,placement.y-placement.height/2,
                         placement.x+placement.width/2,placement.y+placement.height/2,{}});
        insert(boxes.size()-1,boxes.back());
    }
    for(std::size_t i=0;i<placements_.size();++i) {
        auto& placement=placements_[i];
        if(!placement.flagVisible||placement.ref.domain!="territorial")continue;
        placement.flagVisible=false;
        auto box=boxes[i];
        if(placement.nameVisible){box.left-=12;box.right+=12;}
        const auto xr=collisionRange(box.left-3,box.right+3,cellSize);
        const auto yr=collisionRange(box.top-3,box.bottom+3,cellSize);
        bool collision=false;
        for(int y=yr.first;y<=yr.second&&!collision;++y)
            for(int x=xr.first;x<=xr.second&&!collision;++x)
                if(const auto cell=grid.find({x,y});cell!=grid.end())
                    for(const auto other:cell->second)if(other!=i) {
                        const auto& b=boxes[other];
                        if(!(box.right+3<b.left||b.right+3<box.left||
                             box.bottom+3<b.top||b.bottom+3<box.top)){collision=true;break;}
                    }
        if(collision)continue;
        boxes[i]=box;insert(i,box);placement.flagVisible=true;
    }
    finish();
}

const std::vector<MapLabelPlacement>& MapLabelEngine::reproject(const MapViewState& view,double zoom) {
    if(!validMapViewState(view)||!std::isfinite(zoom)||zoom<=0)throw std::invalid_argument("invalid label reproject view");
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
    decorateFlags(zoom);
    stats_.placements=placements_.size();
    accountWorkingSet();
    return placements_;
}

void MapLabelEngine::clear() {
    decltype(sources_){}.swap(sources_);sourceByRef_.clear();cells_.clear();
    decltype(pinned_){}.swap(pinned_);decltype(accepted_){}.swap(accepted_);
    decltype(placements_){}.swap(placements_);placedRefs_.clear();
    decltype(builtinSources_){}.swap(builtinSources_);builtinSourceByRef_.clear();builtinCells_.clear();
    decltype(builtinPinned_){}.swap(builtinPinned_);builtinSuppressedIds_.clear();
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
    std::size_t bytes=0;
    const auto bucket=[&](const auto& sources,const auto& byRef,const auto& cells,const auto& pinned) {
        labelBytesAdd(bytes,sources.capacity(),sizeof(MapLabelSource));
        for(const auto& source:sources) {
            labelBytesAdd(bytes,labelStringStorage(source.text));
            labelBytesAdd(bytes,labelStringStorage(source.collisionGroup));
            labelBytesAdd(bytes,labelStringStorage(source.ref.domain));
            labelBytesAdd(bytes,labelStringStorage(source.ref.id));
        }
        // Container-node allocator bookkeeping stays outside payload accounting.
        labelBytesAdd(bytes,byRef.size(),sizeof(typename std::decay_t<decltype(byRef)>::value_type));
        for(const auto& item:byRef){labelBytesAdd(bytes,labelStringStorage(item.first.domain));labelBytesAdd(bytes,labelStringStorage(item.first.id));}
        labelBytesAdd(bytes,cells.size(),sizeof(typename std::decay_t<decltype(cells)>::value_type));
        for(const auto& cell:cells)labelBytesAdd(bytes,cell.second.capacity(),sizeof(std::size_t));
        labelBytesAdd(bytes,pinned.capacity(),sizeof(std::size_t));
    };
    bucket(sources_,sourceByRef_,cells_,pinned_);bucket(builtinSources_,builtinSourceByRef_,builtinCells_,builtinPinned_);
    labelBytesAdd(bytes,builtinSuppressedIds_.size(),sizeof(std::string));
    for(const auto& id:builtinSuppressedIds_)labelBytesAdd(bytes,labelStringStorage(id));
    sourceBytes_=bytes;
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

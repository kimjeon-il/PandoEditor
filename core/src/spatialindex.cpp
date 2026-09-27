#include <pandoeditor/spatialindex.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace pandoeditor {
namespace {
constexpr int longitudeCells=72,latitudeCells=36;
int longitudeCell(double lon) {return std::clamp(static_cast<int>(std::floor((lon+180)/5)),0,longitudeCells-1);}
int latitudeCell(double lat) {return std::clamp(static_cast<int>(std::floor((lat+90)/5)),0,latitudeCells-1);}
void mix(std::uint64_t& hash,const std::string& value) {
    for(unsigned char c:value){hash^=c;hash*=1099511628211ull;}
    hash^=0xff;hash*=1099511628211ull;
}
void mix(std::uint64_t& hash,std::uint32_t value) {
    for(int i=0;i<4;++i){hash^=(value>>(8*i))&0xff;hash*=1099511628211ull;}
}
double longitude(double raw) {
    double value=std::fmod(raw+180,360);
    if(value<0)value+=360;
    return value-180;
}
std::vector<GeoBounds> windowsFor(GeoBounds input) {
    if(!std::isfinite(input.west)||!std::isfinite(input.east)||
       !std::isfinite(input.south)||!std::isfinite(input.north)||input.south>input.north)
        throw std::invalid_argument("invalid geographic query window");
    input.south=std::clamp(input.south,-90.,90.);
    input.north=std::clamp(input.north,-90.,90.);
    if(input.south>input.north)return {};
    double span=input.east-input.west;
    if(input.wrapsDateline&&span<0)span+=360;
    if(span<0)throw std::invalid_argument("invalid geographic longitude interval");
    if(span>=360)return {{-180,input.south,180,input.north}};
    const double west=longitude(input.west);
    const double east=west+span;
    if(east<=180)return {{west,input.south,east,input.north}};
    return {{west,input.south,180,input.north},
        {-180,input.south,east-360,input.north}};
}
template<typename Callback> void visitCells(const GeoBounds& part,Callback callback) {
    const auto minX=longitudeCell(part.west),maxX=longitudeCell(part.east);
    const auto minY=latitudeCell(part.south),maxY=latitudeCell(part.north);
    for(int x=minX;x<=maxX;++x)for(int y=minY;y<=maxY;++y)callback(x,y);
    // The two grid edges represent the same meridian.
    if(part.west==-180||part.east==180) {
        const int seam=part.west==-180?longitudeCells-1:0;
        for(int y=minY;y<=maxY;++y)callback(seam,y);
    }
}
}

void GeoSpatialIndex::rebuild(const ProjectDocument& document,const DocumentIndex& index) {
    std::uint64_t signature=14695981039346656037ull;
    mix(signature,document.physicalData.dataset);
    mix(signature,document.physicalData.version);
    mix(signature,document.physicalData.source);
    std::map<ObjectRef,GeometryRef> references;
    for(const auto& [object,position]:index.objects) {
        (void)position;
        const auto ref=objectGeometry(document,index,object);
        if(!ref)continue;
        references.emplace(object,*ref);
        mix(signature,object.domain);mix(signature,object.id);
        mix(signature,ref->id);mix(signature,ref->version);
    }
    if(geometryRevision_&&signature==signature_)return;
    std::map<std::pair<int,int>,std::set<ObjectRef>> cells;
    std::map<ObjectRef,std::vector<GeoBounds>> bounds;
    std::map<ObjectRef,GeoBounds> legacyWrappedBounds;
    for(const auto& [object,ref]:references) {
        auto geometry=document.geometries.get(ref);
        if(!geometry)throw std::invalid_argument("spatial index dangling geometry ref");
        auto parts=geometryPartBounds(*geometry);
        if(std::any_of(parts.begin(),parts.end(),[](const auto& part){return part.wrapsDateline;})) {
            GeoBounds raw{180,90,-180,-90};
            const auto include=[&](const Point& point) {
                raw.west=std::min(raw.west,point.x);raw.east=std::max(raw.east,point.x);
                raw.south=std::min(raw.south,point.y);raw.north=std::max(raw.north,point.y);
            };
            for(const auto& polygon:geometry->polygons)if(!polygon.empty())
                for(const auto& point:polygon.front())include(point);
            for(const auto& line:geometry->lines)for(const auto& point:line)include(point);
            legacyWrappedBounds.emplace(object,raw);
        }
        for(const auto& part:parts)
            for(const auto& split:splitWrappedBounds(part))
                visitCells(split,[&](int x,int y){cells[{x,y}].insert(object);});
        bounds.emplace(object,std::move(parts));
    }
    if(geometryRevision_==std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("spatial index geometry revision overflow");
    cells_=std::move(cells);bounds_=std::move(bounds);
    legacyWrappedBounds_=std::move(legacyWrappedBounds);
    references_=std::move(references);
    dataset_=document.physicalData.dataset;
    datasetVersion_=document.physicalData.version;
    datasetSource_=document.physicalData.source;
    signature_=signature;++geometryRevision_;
}

void GeoSpatialIndex::update(const ProjectDocument& document,const DocumentIndex& index,
                             const std::vector<ObjectRef>& removed,const std::vector<ObjectRef>& changed) {
    if(!geometryRevision_||dataset_!=document.physicalData.dataset||
       datasetVersion_!=document.physicalData.version||datasetSource_!=document.physicalData.source) {
        rebuild(document,index);return;
    }
    struct Replacement {
        ObjectRef object;
        std::optional<GeometryRef> ref;
        std::vector<GeoBounds> parts;
        std::optional<GeoBounds> legacy;
    };
    std::set<ObjectRef> targets(removed.begin(),removed.end());
    targets.insert(changed.begin(),changed.end());
    std::vector<Replacement> replacements;
    replacements.reserve(targets.size());
    bool different=false;
    for(const auto& object:targets) {
        Replacement next;next.object=object;
        if(index.objects.count(object))next.ref=objectGeometry(document,index,object);
        const auto old=references_.find(object);
        if((old==references_.end()&&!next.ref)||
           (old!=references_.end()&&next.ref&&old->second==*next.ref))continue;
        different=true;
        if(next.ref) {
            const auto geometry=document.geometries.get(*next.ref);
            if(!geometry)throw std::invalid_argument("spatial index dangling geometry ref");
            next.parts=geometryPartBounds(*geometry);
            if(std::any_of(next.parts.begin(),next.parts.end(),[](const auto& part){return part.wrapsDateline;})) {
                GeoBounds raw{180,90,-180,-90};
                const auto include=[&](Point point) {
                    raw.west=std::min(raw.west,point.x);raw.east=std::max(raw.east,point.x);
                    raw.south=std::min(raw.south,point.y);raw.north=std::max(raw.north,point.y);
                };
                for(const auto& polygon:geometry->polygons)if(!polygon.empty())
                    for(const auto& point:polygon.front())include(point);
                for(const auto& line:geometry->lines)for(const auto& point:line)include(point);
                next.legacy=raw;
            }
        }
        replacements.push_back(std::move(next));
    }
    if(!different)return;
    if(geometryRevision_==std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("spatial index geometry revision overflow");
    // Stage membership changes before publication so an allocation failure cannot
    // leave half of an edit visible to candidate queries.
    auto cells=cells_;
    auto bounds=bounds_;
    auto legacy=legacyWrappedBounds_;
    auto references=references_;
    for(const auto& next:replacements) {
        if(const auto old=bounds.find(next.object);old!=bounds.end()) {
            for(const auto& part:old->second)
                for(const auto& split:splitWrappedBounds(part))
                    visitCells(split,[&](int x,int y){
                        const auto cell=cells.find({x,y});
                        if(cell!=cells.end()) {cell->second.erase(next.object);if(cell->second.empty())cells.erase(cell);}
                    });
        }
        bounds.erase(next.object);legacy.erase(next.object);references.erase(next.object);
        if(!next.ref)continue;
        references.emplace(next.object,*next.ref);
        if(next.legacy)legacy.emplace(next.object,*next.legacy);
        for(const auto& part:next.parts)
            for(const auto& split:splitWrappedBounds(part))
                visitCells(split,[&](int x,int y){cells[{x,y}].insert(next.object);});
        bounds.emplace(next.object,next.parts);
    }
    std::uint64_t signature=14695981039346656037ull;
    mix(signature,dataset_);mix(signature,datasetVersion_);mix(signature,datasetSource_);
    for(const auto& [object,ref]:references) {
        mix(signature,object.domain);mix(signature,object.id);
        mix(signature,ref.id);mix(signature,ref.version);
    }
    cells_.swap(cells);bounds_.swap(bounds);legacyWrappedBounds_.swap(legacy);
    references_.swap(references);signature_=signature;++geometryRevision_;
}

std::vector<ObjectRef> GeoSpatialIndex::query(const std::vector<GeoBounds>& windows) const {
    std::set<ObjectRef> candidates;
    std::vector<GeoBounds> normalized;
    for(const auto& window:windows) {
        const auto parts=windowsFor(window);normalized.insert(normalized.end(),parts.begin(),parts.end());
        for(const auto& part:parts)
            visitCells(part,[&](int x,int y) {
                const auto it=cells_.find({x,y});
                if(it!=cells_.end())candidates.insert(it->second.begin(),it->second.end());
            });
    }
    std::vector<ObjectRef> result;
    for(const auto& object:candidates) {
        const auto it=bounds_.find(object);
        if(it!=bounds_.end()&&std::any_of(it->second.begin(),it->second.end(),[&](const auto& bound) {
            return std::any_of(normalized.begin(),normalized.end(),[&](const auto& window) {
                return intersects(bound,window);
            });
        }))result.push_back(object);
    }
    return result;
}

std::vector<ObjectRef> GeoSpatialIndex::queryLegacyFlat(const std::vector<GeoBounds>& windows) const {
    const auto clean=query(windows);
    std::set<ObjectRef> candidates(clean.begin(),clean.end());
    for(const auto& [object,raw]:legacyWrappedBounds_)
        for(const auto& window:windows)
            for(const auto& part:windowsFor(window))if(intersects(raw,part)) {
                candidates.insert(object);break;
            }
    return {candidates.begin(),candidates.end()};
}
}

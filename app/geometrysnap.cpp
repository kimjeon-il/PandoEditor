#include "geometrysnap.h"
#include <pandoeditor/geobounds.h>
#include <pandoeditor/spatialindex.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace geometrysnap {
namespace {
using namespace pandoeditor;
// Behavioral ports of map-edit-snap-calculation, geometry-segment-index,
// boundary-spatial-index and geometry-snap at web ad78780 (original snap modules
// unchanged from 53dbd3c). Intersection longitude alignment follows the explicitly
// approved app-cut-geometry dateline correction. Preserve operation order here.
struct Bounds {double west,south,east,north;};
bool overlaps(Bounds a,Bounds b){return a.west<=b.east&&a.east>=b.west&&a.south<=b.north&&a.north>=b.south;}
Bounds segmentBounds(Point a,Point b){return {std::min(a.x,b.x),std::min(a.y,b.y),std::max(a.x,b.x),std::max(a.y,b.y)};}
double unwrap(double longitude,double reference){
    // Preserve the web's repeated arithmetic for supported coordinates. Reject
    // unrepresentable transient input if an iteration cannot progress; this is
    // an API safety guard, not a normalization or a web parity claim.
    while(longitude-reference>180){const auto next=longitude-360;if(next==longitude)throw std::invalid_argument("snap longitude unwrap cannot make progress");longitude=next;}
    while(longitude-reference< -180){const auto next=longitude+360;if(next==longitude)throw std::invalid_argument("snap longitude unwrap cannot make progress");longitude=next;}
    return longitude;
}
bool area(const Geometry& geometry){return geometry.type=="Polygon"||geometry.type=="MultiPolygon";}
Bounds rawBounds(const Geometry& geometry){Bounds b{INFINITY,INFINITY,-INFINITY,-INFINITY};for(const auto& p:geometry.polygons)for(const auto& r:p)for(const auto point:r){b.west=std::min(b.west,point.x);b.south=std::min(b.south,point.y);b.east=std::max(b.east,point.x);b.north=std::max(b.north,point.y);}return b;}

// One-degree buckets, large-entry-first order and insertion-ordered buckets are
// observable web behavior. A sorted segment result would change stable ties.
template<class T> class OrderedGrid {
    struct Entry {T value;Bounds bounds;std::vector<std::pair<int,int>> cells;bool large=false;};
    std::map<std::size_t,Entry> entries_;
    std::map<std::pair<int,int>,std::vector<std::size_t>> cells_;
    std::vector<std::size_t> large_;
    std::size_t sequence_=0;
    Bounds extent_{INFINITY,INFINITY,-INFINITY,-INFINITY};
    static std::optional<std::vector<std::pair<int,int>>> keys(Bounds b){
        const double x0=std::floor(b.west),x1=std::floor(b.east),y0=std::floor(b.south),y1=std::floor(b.north);
        if(!std::isfinite(x0)||!std::isfinite(x1)||!std::isfinite(y0)||!std::isfinite(y1)||
           x0<std::numeric_limits<int>::min()||x1>=std::numeric_limits<int>::max()||
           y0<std::numeric_limits<int>::min()||y1>=std::numeric_limits<int>::max()||
           (x1-x0+1)*(y1-y0+1)>4096)return {};
        std::vector<std::pair<int,int>> result;
        for(int x=int(x0);x<=int(x1);++x)for(int y=int(y0);y<=int(y1);++y)result.emplace_back(x,y);
        return result;
    }
public:
    std::size_t insert(T value,Bounds bounds){
        extent_.west=std::min(extent_.west,bounds.west);extent_.south=std::min(extent_.south,bounds.south);
        extent_.east=std::max(extent_.east,bounds.east);extent_.north=std::max(extent_.north,bounds.north);
        const auto id=sequence_++;const auto locations=keys(bounds);
        Entry entry{std::move(value),bounds,locations.value_or(std::vector<std::pair<int,int>>{}),!locations};
        if(locations)for(const auto& key:*locations)cells_[key].push_back(id);else large_.push_back(id);
        entries_.emplace(id,std::move(entry));return id;
    }
    void remove(std::size_t id){const auto found=entries_.find(id);if(found==entries_.end())return;auto erase=[id](auto& values){values.erase(std::remove(values.begin(),values.end(),id),values.end());};for(const auto& key:found->second.cells){auto bucket=cells_.find(key);erase(bucket->second);if(bucket->second.empty())cells_.erase(bucket);}erase(large_);entries_.erase(found);}
    std::vector<const T*> query(Bounds bounds,std::size_t* examined=nullptr) const{
        // The monotonically conservative extent also rejects extreme finite pointer
        // coordinates without converting them to integer cells or scanning entries.
        if(entries_.empty()||!overlaps(extent_,bounds))return {};
        const auto locations=keys(bounds);std::vector<const T*> result;std::set<std::size_t> seen;
        const auto append=[&](std::size_t id){if(!seen.insert(id).second)return;if(examined)++*examined;const auto& e=entries_.at(id);if(overlaps(e.bounds,bounds))result.push_back(&e.value);};
        if(!locations){for(const auto& entry:entries_)append(entry.first);}
        else{for(const auto id:large_)append(id);for(const auto& key:*locations){const auto found=cells_.find(key);if(found!=cells_.end())for(const auto id:found->second)append(id);}}
        return result;
    }
};
struct Segment {const Point *a,*b;std::size_t polygon,ring,segment;};
using GeometryKey=std::pair<const Geometry*,std::uint64_t>;
struct GeometryKeyLess {
    bool operator()(const GeometryKey& a,const GeometryKey& b) const {
        const auto less=std::less<const Geometry*>{};
        return less(a.first,b.first)||(!less(b.first,a.first)&&a.second<b.second);
    }
};
struct GeometryIndex {std::shared_ptr<const Geometry> geometry;OrderedGrid<Segment> segments;};
GeometryIndex buildGeometryIndex(std::shared_ptr<const Geometry> geometry){
    GeometryIndex result;result.geometry=std::move(geometry);
    for(std::size_t p=0;p<result.geometry->polygons.size();++p)for(std::size_t r=0;r<result.geometry->polygons[p].size();++r){const auto& ring=result.geometry->polygons[p][r];for(std::size_t s=0;s+1<ring.size();++s){const Segment segment{&ring[s],&ring[s+1],p,r,s};result.segments.insert(segment,segmentBounds(*segment.a,{unwrap(segment.b->x,segment.a->x),segment.b->y}));}}
    return result;
}
bool coveredByGeographicIndex(Bounds b,const std::vector<GeoBounds>& coreBounds){
    double west=std::fmod(b.west+180,360);if(west<0)west+=360;west-=180;
    const double east=west+(b.east-b.west);
    std::vector<Bounds> parts{{west,b.south,std::min(east,180.),b.north}};
    if(east>180)parts.push_back({-180,b.south,east-360,b.north});
    for(const auto& part:parts){bool covered=false;for(const auto& bounds:coreBounds)for(const auto& c:splitWrappedBounds(bounds))covered|=c.west<=part.west&&c.east>=part.east&&c.south<=part.south&&c.north>=part.north;if(!covered)return false;}
    return true;
}
// Exact ECMAScript toFixed(7) for finite geographic coordinates. Work on the
// binary significand, avoiding an extra floating-point rounding at value*1e7.
// The 70-bit product is represented as two 64-bit words (also works on MSVC).
std::string fixed7(double value){
    if(!std::isfinite(value))throw std::invalid_argument("snap coordinate must be finite");
    const bool negative=value<0;const double magnitude=std::abs(value);
    // This geographic adapter is not a general Number.toFixed implementation.
    // Keep scaled rounding within uint64_t, including every canonical and
    // approved shifted source coordinate; reject unsupported magnitudes explicitly.
    if(magnitude>1e12)throw std::invalid_argument("snap node-key magnitude is outside the supported geographic range");
    int exponent=0;const double fraction=std::frexp(magnitude,&exponent);
    const auto significand=static_cast<std::uint64_t>(std::ldexp(fraction,53));
    const std::uint64_t bottom=(significand&0xffffffffull)*78125ull;
    const std::uint64_t top=(significand>>32)*78125ull;
    const std::uint64_t low=bottom+(top<<32);
    const std::uint64_t high=(top>>32)+(low<bottom?1:0);
    const int shift=46-exponent;std::uint64_t rounded=0;
    if(shift>=128)rounded=0;
    else if(shift>64){rounded=high>>(shift-64);rounded+=(high>>(shift-65))&1;}
    else if(shift==64){rounded=high+(low>>63);}
    else if(shift>0){rounded=(low>>shift)|(high<<(64-shift));rounded+=(low>>(shift-1))&1;}
    else if(shift>=-10)rounded=low<<(-shift);
    else throw std::invalid_argument("snap coordinate is outside geographic range");
    const auto whole=rounded/10000000,decimal=rounded%10000000;std::string decimals=std::to_string(decimal);
    return (negative?"-":"")+std::to_string(whole)+"."+std::string(7-decimals.size(),'0')+decimals;
}
std::optional<Point> interiorIntersection(const Candidate& first,const Candidate& second){
    const auto a=*first.a,b=*first.b,c=*second.a,d=*second.b;
    const Point p=a;Point q{unwrap(c.x,p.x),c.y};const double bLon=unwrap(b.x,p.x);double dLon=unwrap(d.x,q.x);const double lineMid=(p.x+bLon)/2;
    while((q.x+dLon)/2-lineMid>180){q.x-=360;dLon-=360;}
    while((q.x+dLon)/2-lineMid< -180){q.x+=360;dLon+=360;}
    const Point r{bLon-p.x,b.y-p.y},s{dLon-q.x,d.y-q.y},qp{q.x-p.x,q.y-p.y};
    const auto cross=[](Point u,Point v){return u.x*v.y-u.y*v.x;};const auto denominator=cross(r,s);
    // Both parallel-disjoint and collinear-overlap results are excluded by snap.
    if(std::abs(denominator)<=1e-10)return {};
    double lineT=cross(qp,s)/denominator,boundaryT=cross(qp,r)/denominator;
    if(lineT< -1e-10||lineT>1+1e-10||boundaryT< -1e-10||boundaryT>1+1e-10)return {};
    lineT=std::clamp(lineT,0.,1.);boundaryT=std::clamp(boundaryT,0.,1.);
    if(lineT<=1e-7||lineT>=1-1e-7||boundaryT<=1e-7||boundaryT>=1-1e-7)return {};
    return Point{a.x+(unwrap(b.x,a.x)-a.x)*lineT,a.y+(b.y-a.y)*lineT};
}
// V8 13.6.233.17-node.51 FastMathHypot two-argument operation order:
// https://github.com/nodejs/node/blob/v24.19.0/deps/v8/src/builtins/math.tq
// Confirmed by actual Chromium 151.0.7922.34 / V8 15.1.206.8
// production resolveSnap diagnostics; libc hypot has different final rounding.
// Keep the normalized products separate and disable FMA for this source.
double screenDistance(double x,double y){
    const double a=std::abs(x),b=std::abs(y);
    if(std::isinf(a)||std::isinf(b))return std::numeric_limits<double>::infinity();
    if(std::isnan(a)||std::isnan(b))return std::numeric_limits<double>::quiet_NaN();
    const double maximum=std::max(a,b);if(maximum==0)return 0;
    const double normalizedA=a/maximum,normalizedB=b/maximum;
    return std::sqrt(normalizedA*normalizedA+normalizedB*normalizedB)*maximum;
}
int priority(const std::string& kind){if(kind=="vertex")return 0;if(kind=="intersection")return 1;if(kind=="boundary")return 2;if(kind=="edge")return 3;if(kind=="neighbor")return 4;return 99;}
}

struct Index::Impl {
    struct Owner {std::size_t order;GeometryRef ref;std::shared_ptr<const Geometry> geometry;Bounds bounds;std::vector<std::size_t> supplementIds;};
    std::recursive_mutex mutex;
    std::optional<ProjectSnapshot> snapshot;
    GeoSpatialIndex geographic;
    OrderedGrid<ObjectRef> supplement;
    std::map<ObjectRef,Owner> owners;
    std::map<GeometryKey,GeometryIndex,GeometryKeyLess> geometryIndexes;
    std::vector<std::pair<const Geometry*,std::uint64_t>> draftKeys;
    std::set<const Geometry*> canonicalGeometry;
    struct DraftMetadata {std::shared_ptr<const Geometry> geometry;Bounds bounds;};
    std::map<GeometryKey,DraftMetadata,GeometryKeyLess> draftBounds;
    std::size_t sequence=0,preparedObjects=0;
    GeometryIndex& geometryIndex(std::shared_ptr<const Geometry> geometry,std::uint64_t revision,Diagnostics& diagnostics){
        const auto key=std::make_pair(geometry.get(),revision);auto found=geometryIndexes.find(key);
        if(found==geometryIndexes.end()){found=geometryIndexes.emplace(key,buildGeometryIndex(std::move(geometry))).first;++diagnostics.geometryIndexBuilds;}
        return found->second;
    }
};
Index::Index():impl_(std::make_unique<Impl>()){}
Index::~Index()=default;
void Index::prepare(const ProjectSnapshot& snapshot){
    auto& state=*impl_;std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if(state.snapshot&&state.snapshot->instanceId()==snapshot.instanceId()&&state.snapshot->revision()==snapshot.revision())return;
    const bool reset=!state.snapshot||state.snapshot->instanceId()!=snapshot.instanceId();
    if(reset){state.owners.clear();state.geographic=GeoSpatialIndex{};state.supplement=OrderedGrid<ObjectRef>{};state.geometryIndexes.clear();state.draftKeys.clear();state.draftBounds.clear();state.sequence=0;}
    const auto& document=snapshot.document();const auto& index=snapshot.index();
    std::vector<ObjectRef> ordered;ordered.reserve(document.units.size()+document.genericFeatures.size());
    for(const auto& unit:document.units)ordered.push_back(territorialRef(unit.id));
    for(const auto& feature:document.genericFeatures)ordered.push_back({"generic",feature.id});
    std::set<ObjectRef> present(ordered.begin(),ordered.end());std::vector<ObjectRef> removed,changed;
    for(auto it=state.owners.begin();it!=state.owners.end();){if(!present.count(it->first)){removed.push_back(it->first);for(const auto id:it->second.supplementIds)state.supplement.remove(id);it=state.owners.erase(it);}else ++it;}
    state.preparedObjects=0;
    for(const auto& object:ordered){const auto ref=objectGeometry(document,index,object);if(!ref)continue;const auto geometry=document.geometries.get(*ref);if(!geometry)throw std::runtime_error("snap geometry reference is missing");auto old=state.owners.find(object);
        if(old!=state.owners.end()&&old->second.ref==*ref&&old->second.geometry==geometry)continue;
        ++state.preparedObjects;changed.push_back(object);const auto order=old==state.owners.end()?state.sequence++:old->second.order;
        if(old!=state.owners.end())for(const auto id:old->second.supplementIds)state.supplement.remove(id);
        Impl::Owner next{order,*ref,geometry,rawBounds(*geometry),{}};
        if(area(*geometry)){
            const auto coreBounds=geometryPartBounds(*geometry);
            for(const auto& polygon:geometry->polygons)for(const auto& ring:polygon)for(std::size_t i=0;i+1<ring.size();++i){const auto bounds=segmentBounds(ring[i],{unwrap(ring[i+1].x,ring[i].x),ring[i+1].y});if(!coveredByGeographicIndex(bounds,coreBounds))next.supplementIds.push_back(state.supplement.insert(object,bounds));}
        }
        state.owners.insert_or_assign(object,std::move(next));
    }
    if(reset)state.geographic.rebuild(document,index);else state.geographic.update(document,index,removed,changed);
    // Release replaced geometry indexes. Draft entries have the same bounded eight
    // source-identity retention as the web worker; canonical geometry is shared.
    state.canonicalGeometry.clear();for(const auto& owner:state.owners)state.canonicalGeometry.insert(owner.second.geometry.get());
    auto retained=state.canonicalGeometry;for(const auto& key:state.draftKeys)retained.insert(key.first);
    for(auto it=state.geometryIndexes.begin();it!=state.geometryIndexes.end();)if(!retained.count(it->first.first))it=state.geometryIndexes.erase(it);else ++it;
    state.snapshot=snapshot;
}
CandidateBatch Index::collect(const Request& request){
    auto& state=*impl_;std::lock_guard<std::recursive_mutex> guard(state.mutex);
    if(!state.snapshot)throw std::runtime_error("snap index has not been prepared");
    if(request.sourceRanks&&(request.sourceRanksInstance!=state.snapshot->instanceId()||request.sourceRanksRevision!=state.snapshot->revision()))
        throw std::invalid_argument("snap source-order snapshot does not match geometry snapshot");
    if(!std::isfinite(request.coordinate.x)||!std::isfinite(request.coordinate.y)||!std::isfinite(request.margin)||request.margin<0)throw std::invalid_argument("invalid snap query");
    const Bounds bounds{request.coordinate.x-request.margin,request.coordinate.y-request.margin,request.coordinate.x+request.margin,request.coordinate.y+request.margin};
    CandidateBatch result;result.diagnostics.preparedObjects=std::exchange(state.preparedObjects,0);
    std::set<ObjectRef> nearby;
    for(const auto& object:state.geographic.query({{bounds.west,bounds.south,bounds.east,bounds.north}}))if(state.owners.count(object))nearby.insert(object);
    for(const double shift:{-360.,0.,360.})for(const auto* object:state.supplement.query({bounds.west+shift,bounds.south,bounds.east+shift,bounds.north}))nearby.insert(*object);
    struct OrderedOwner {ObjectRef ref;const Impl::Owner* owner;std::uint64_t rank;};
    std::vector<OrderedOwner> ordered;std::set<std::uint64_t> nearbyRanks;
    for(const auto& object:nearby){
        const auto* owner=&state.owners.at(object);std::uint64_t rank=owner->order;
        if(request.sourceRanks){
            const auto found=request.sourceRanks->find(object);
            if(found==request.sourceRanks->end())throw std::invalid_argument("canonical snap source has no insertion rank");
            rank=found->second;
            if(!nearbyRanks.insert(rank).second)throw std::invalid_argument("canonical snap sources have duplicate insertion ranks");
        }
        ordered.push_back({object,owner,rank});
    }
    std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a.rank<b.rank;});
    std::set<std::string> vertices;const std::set<std::string> active(request.activeOwnerIds.begin(),request.activeOwnerIds.end());
    const auto append=[&](const std::string& id,std::shared_ptr<const Geometry> geometry,std::uint64_t revision,Bounds featureBounds,bool source){
        if(!area(*geometry))return;
        if(featureBounds.east-featureBounds.west>180){if(featureBounds.north<bounds.south||featureBounds.south>bounds.north)return;}
        else{bool overlap=false;for(const double shift:{-360.,0.,360.})overlap|=overlaps({bounds.west+shift,bounds.south,bounds.east+shift,bounds.north},featureBounds);if(!overlap)return;}
        ++result.diagnostics.nearbyObjects;auto& index=state.geometryIndex(geometry,revision,result.diagnostics);
        const std::vector<std::string> owners=source?request.activeOwnerIds:std::vector<std::string>{id};std::set<const Segment*> seen;
        for(const double shift:{-360.,0.,360.})for(const auto* segment:index.segments.query({bounds.west+shift,bounds.south,bounds.east+shift,bounds.north},&result.diagnostics.segmentEntriesExamined)){
            if(!seen.insert(segment).second)continue;
            ++result.diagnostics.visitedSegments;
            for(const auto point:{*segment->a,*segment->b}){const auto key=nodeKey(point);if(vertices.insert(key).second){Candidate c;c.kind="vertex";c.coordinate=point;c.ownerIds=owners;c.nodeKey=key;result.candidates.push_back(std::move(c));}}
            Candidate c;c.kind=source?"boundary":!active.empty()&&!active.count(id)?"neighbor":"edge";c.a=*segment->a;c.b=*segment->b;c.ownerIds=owners;c.segmentKey=id+":"+std::to_string(segment->polygon)+":"+std::to_string(segment->ring)+":"+std::to_string(segment->segment);result.candidates.push_back(std::move(c));
        }
    };
    for(const auto& owner:ordered)append(owner.ref.id,owner.owner->geometry,owner.owner->ref.version,owner.owner->bounds,owner.ref.id==request.sourceKey);
    if(request.sourceGeometry){const auto key=std::make_pair(request.sourceGeometry.get(),request.sourceRevision);if(std::find(state.draftKeys.begin(),state.draftKeys.end(),key)==state.draftKeys.end()){state.draftKeys.push_back(key);state.draftBounds.emplace(key,Impl::DraftMetadata{request.sourceGeometry,rawBounds(*request.sourceGeometry)});if(state.draftKeys.size()>8){const auto old=state.draftKeys.front();state.draftKeys.erase(state.draftKeys.begin());state.draftBounds.erase(old);const bool canonical=state.canonicalGeometry.count(old.first)!=0;if(!canonical)state.geometryIndexes.erase(old);}}append(request.sourceKey,request.sourceGeometry,request.sourceRevision,state.draftBounds.at(key).bounds,true);}
    std::vector<std::size_t> edges;for(std::size_t i=0;i<result.candidates.size();++i)if(result.candidates[i].a&&result.candidates[i].b)edges.push_back(i);
    OrderedGrid<std::size_t> intersections;for(std::size_t i=0;i<edges.size();++i){const auto& edge=result.candidates[edges[i]];intersections.insert(i,segmentBounds(*edge.a,*edge.b));}
    for(std::size_t i=0;i<edges.size();++i){const auto edge=result.candidates[edges[i]];for(const auto* other:intersections.query(segmentBounds(*edge.a,*edge.b))){if(*other<=i)continue;const auto& right=result.candidates[edges[*other]];if(edge.segmentKey==right.segmentKey)continue;++result.diagnostics.intersectionTests;const auto hit=interiorIntersection(edge,right);if(!hit)continue;Candidate c;c.kind="intersection";c.coordinate=*hit;for(const auto* owners:{&edge.ownerIds,&right.ownerIds})for(const auto& owner:*owners)if(std::find(c.ownerIds.begin(),c.ownerIds.end(),owner)==c.ownerIds.end())c.ownerIds.push_back(owner);result.candidates.push_back(std::move(c));}}
    return result;
}
CandidateBatch Index::prepareAndCollect(const ProjectSnapshot& snapshot,const Request& request){
    std::lock_guard<std::recursive_mutex> guard(impl_->mutex);
    prepare(snapshot);return collect(request);
}
double marginForScale(double scale){return std::clamp(26*180/(3.141592653589793238462643383279502884*std::max(1.,scale)),.03,4.);}
double snapThreshold(const std::string& type){return type=="touch"?18:10;}
std::string nodeKey(Point point){return fixed7(point.x)+","+fixed7(point.y);}
std::optional<Result> resolveSnap(Point coordinate,Point screenPoint,const std::vector<Candidate>& candidates,const ProjectPoint& project,const std::string& pointerType,const std::string& excludeNodeKey){
    (void)coordinate;if(!project)return {};std::optional<Result> best;
    for(const auto& candidate:candidates){if(priority(candidate.kind)==99||(!excludeNodeKey.empty()&&candidate.nodeKey==excludeNodeKey))continue;Result result;result.candidate=candidate;
        if(candidate.kind=="vertex"||candidate.kind=="intersection"){if(!candidate.coordinate)continue;const auto point=project(*candidate.coordinate);if(!point)continue;result.coordinate=*candidate.coordinate;result.distancePx=screenDistance(screenPoint.x-point->x,screenPoint.y-point->y);}
        else{if(!candidate.a||!candidate.b)continue;const auto a=project(*candidate.a),b=project(*candidate.b);if(!a||!b)continue;const double dx=b->x-a->x,dy=b->y-a->y,length2=dx*dx+dy*dy;const auto rawT=length2?((screenPoint.x-a->x)*dx+(screenPoint.y-a->y)*dy)/length2:0.;const auto t=rawT<=0?0.:rawT>=1?1.:rawT;const Point projected{a->x+dx*t,a->y+dy*t};result.coordinate={candidate.a->x+(candidate.b->x-candidate.a->x)*t,candidate.a->y+(candidate.b->y-candidate.a->y)*t};result.distancePx=screenDistance(screenPoint.x-projected.x,screenPoint.y-projected.y);result.segmentT=t;result.segmentEndpoints=std::array<Point,2>{*candidate.a,*candidate.b};}
        if(!(result.distancePx<=snapThreshold(pointerType)))continue;
        if(!best||result.distancePx<best->distancePx||(result.distancePx==best->distancePx&&priority(candidate.kind)<priority(best->candidate.kind)))best=std::move(result);
    }
    return best;
}
}

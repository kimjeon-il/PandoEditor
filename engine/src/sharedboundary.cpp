#include <pandoeditor/map/sharedboundary.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <charconv>
#include <stdexcept>
#include <tuple>

namespace sharedboundary {
using namespace pandoeditor;
namespace {
constexpr double epsilon=1e-7;
constexpr double factor=1e7;
bool same(Point a,Point b){return a.x==b.x&&a.y==b.y;}
bool sameGeometry(const Geometry& a,const Geometry& b){
    if(a.type!=b.type||a.polygons.size()!=b.polygons.size())return false;
    for(std::size_t p=0;p<a.polygons.size();++p){if(a.polygons[p].size()!=b.polygons[p].size())return false;for(std::size_t r=0;r<a.polygons[p].size();++r){const auto& x=a.polygons[p][r];const auto& y=b.polygons[p][r];if(x.size()!=y.size())return false;for(std::size_t v=0;v<x.size();++v)if(!same(x[v],y[v]))return false;}}return true;
}
bool sameDrafts(const Drafts& a,const Drafts& b){if(a.size()!=b.size())return false;for(const auto& [id,g]:a){const auto found=b.find(id);if(found==b.end()||!sameGeometry(g,found->second))return false;}return true;}
template<class T> void uniqueAppend(std::vector<T>& values,const T& value){if(std::find(values.begin(),values.end(),value)==values.end())values.push_back(value);}
bool hasRef(const std::vector<Reference>& refs,const Reference& ref){return std::any_of(refs.begin(),refs.end(),[&](const auto& r){return r.owner==ref.owner&&r.polygon==ref.polygon&&r.ring==ref.ring&&r.index==ref.index;});}
std::optional<double> onSegment(Point point,Point a,Point b){
    const double dx=b.x-a.x,dy=b.y-a.y,length2=dx*dx+dy*dy;if(!length2)return {};
    const double t=((point.x-a.x)*dx+(point.y-a.y)*dy)/length2;if(t < -epsilon||t > 1+epsilon)return {};
    if(std::hypot(point.x-(a.x+dx*t),point.y-(a.y+dy*t))>epsilon)return {};
    return std::clamp(t,0.,1.);
}
bool touches(const Geometry& geometry,Point point){
    for(const auto& polygon:geometry.polygons)for(const auto& ring:polygon)for(std::size_t i=1;i<ring.size();++i){const auto a=ring[i-1],b=ring[i];const double dx=b.x-a.x,dy=b.y-a.y,length=std::hypot(dx,dy);if(!length)continue;const double t=((point.x-a.x)*dx+(point.y-a.y)*dy)/(length*length);if(t>=-epsilon&&t<=1+epsilon&&std::abs(dx*(point.y-a.y)-dy*(point.x-a.x))/length<=epsilon)return true;}return false;
}
using Bounds=std::array<double,4>;
struct Raw {Reference ref;std::size_t end=0;Point a,b;Bounds bounds;};
std::vector<Raw> sourceSegments(const TerritorialUnit& unit,const Geometry& geometry){
    std::vector<Raw> rows;for(std::size_t p=0;p<geometry.polygons.size();++p)for(std::size_t r=0;r<geometry.polygons[p].size();++r){const auto& ring=geometry.polygons[p][r];const auto limit=ring.empty()?0:ring.size()-1;for(std::size_t v=0;v<limit;++v){const auto a=ring[v],b=ring[v+1];rows.push_back({{unit.id,p,r,v,0},(v+1)%limit,a,b,{std::min(a.x,b.x),std::min(a.y,b.y),std::max(a.x,b.x),std::max(a.y,b.y)}});}}return rows;
}
bool overlap(Bounds a,Bounds b){return a[0]<=b[2]&&a[2]>=b[0]&&a[1]<=b[3]&&a[3]>=b[1];}
Bounds padded(Bounds b){const auto margin=epsilon*(1+std::hypot(b[2]-b[0],b[3]-b[1]));return {b[0]-margin,b[1]-margin,b[2]+margin,b[3]+margin};}
// Same ordered geographic broad phase as boundary-spatial-index.js. Buckets
// preserve source insertion order; queries visit x then y, with large rows first.
class SpatialIndex {
    std::map<std::pair<int,int>,std::vector<std::size_t>> cells_;
    std::vector<std::size_t> large_;
    const std::vector<Raw>& rows_;
public:
    explicit SpatialIndex(const std::vector<Raw>& rows):rows_(rows){for(std::size_t i=0;i<rows.size();++i){const auto b=rows[i].bounds;const int x0=std::floor(b[0]),x1=std::floor(b[2]),y0=std::floor(b[1]),y1=std::floor(b[3]);if(double(x1-x0+1)*(y1-y0+1)>4096)large_.push_back(i);else for(int x=x0;x<=x1;++x)for(int y=y0;y<=y1;++y)cells_[{x,y}].push_back(i);}}
    std::vector<std::size_t> query(Bounds b)const{const int x0=std::floor(b[0]),x1=std::floor(b[2]),y0=std::floor(b[1]),y1=std::floor(b[3]);std::vector<std::size_t> result;std::set<std::size_t> seen;auto add=[&](std::size_t i){if(seen.insert(i).second&&overlap(rows_[i].bounds,b))result.push_back(i);};if(double(x1-x0+1)*(y1-y0+1)>4096){for(std::size_t i=0;i<rows_.size();++i)add(i);}else{for(auto i:large_)add(i);for(int x=x0;x<=x1;++x)for(int y=y0;y<=y1;++y){const auto found=cells_.find({x,y});if(found!=cells_.end())for(auto i:found->second)add(i);}}return result;}
};
std::string parentId(const ProjectDocument& d,const std::string& id){return staticParentRelation(d,id).parentId;}
}
NodeKey nodeKey(Point point){
    // JavaScript Math.round ties toward +infinity, unlike std::round.
    const auto quantize=[](double value){const double scaled=value*factor,lower=std::floor(scaled);return (lower+(scaled-lower>=.5?1:0))/factor;};
    return {quantize(point.x),quantize(point.y)};
}
std::string nodeKeyText(Point point){
    const auto number=[](double value){
        if(value==0)return std::string("0");
        char buffer[512];const auto magnitude=std::abs(value);
        const auto format=magnitude>=1e-6&&magnitude<1e21?std::chars_format::fixed:std::chars_format::general;
        const auto result=std::to_chars(buffer,buffer+sizeof(buffer),value,format);
        std::string text(buffer,result.ptr);const auto exponent=text.find('e');
        if(exponent!=std::string::npos){const auto power=std::stoi(text.substr(exponent+1));text=text.substr(0,exponent)+"e"+(power>=0?"+":"")+std::to_string(power);}
        return text;
    };
    const auto key=nodeKey(point);return number(key.first)+","+number(key.second);
}
std::string Session::eligibility(const ProjectDocument& d,const DocumentIndex& index,const std::vector<ObjectRef>& owners,bool checkAncestors){
    try {
        if(owners.size()<2)return "BOUNDARY_REQUIRES_TWO_OWNERS";
        std::set<std::string> selected;std::optional<std::string> parent;
        for(const auto& ref:owners){
            const auto found=index.objects.find(ref);if(ref.domain!="territorial"||found==index.objects.end())return "BOUNDARY_OWNER_NOT_FOUND";
            const auto& unit=d.units.at(found->second);if(unit.kind!=UnitKind::General)return "BOUNDARY_REQUIRES_GENERAL_OWNERS";
            if(!selected.insert(ref.id).second)return "DUPLICATE_OWNER";
            if(objectLocked(d,index,ref))return "LOCKED";
            const auto immediate=parentId(d,ref.id);if(parent&&*parent!=immediate)return "ADMINISTRATIVE_PARENT_MISMATCH";parent=immediate;
            std::set<std::string> visited{ref.id};auto ancestor=immediate;
            while(checkAncestors&&!ancestor.empty()){if(!visited.insert(ancestor).second)return "BOUNDARY_PARENT_CYCLE";const auto it=index.objects.find(territorialRef(ancestor));if(it==index.objects.end())return "BOUNDARY_PARENT_NOT_FOUND";if(objectLocked(d,index,territorialRef(ancestor)))return "BOUNDARY_ANCESTOR_LOCKED";ancestor=parentId(d,ancestor);}
            const auto geometry=d.geometries.get(staticGeometryBinding(d,ref.id).geometryRef);if(!geometry||geometry->polygons.empty()||(geometry->type!="Polygon"&&geometry->type!="MultiPolygon"))return "BOUNDARY_INVALID_GEOMETRY";
        }
        return {};
    }catch(const std::exception& e){return e.what();}
}
std::shared_ptr<Session> Session::prepare(const ProjectDocument& d,const DocumentIndex& index,const std::vector<ObjectRef>& owners,const std::function<bool()>& cancelled,const std::string& autoSeed){
    auto session=std::make_shared<Session>();session->error_=eligibility(d,index,owners);if(!session->error_.empty())return session;
    const auto checkpoint=[&]{if(cancelled&&cancelled())throw std::runtime_error("BOUNDARY_PREPARATION_CANCELLED");};
    try {
        for(const auto& ref:owners){session->selected_.insert(ref.id);session->originals_[ref.id]=*d.geometries.get(staticGeometryBinding(d,ref.id).geometryRef);}
        session->drafts_=session->originals_;const auto parent=parentId(d,owners.front().id);
        std::vector<Raw> raw;std::map<std::string,std::vector<std::size_t>> byOwner;std::set<std::string> allowed;
        std::set<std::string> roots;for(const auto& ref:owners){auto root=ref.id;for(auto ancestor=parentId(d,root);!ancestor.empty();ancestor=parentId(d,root)){root=ancestor;}roots.insert(root);}
        for(const auto& unit:d.units){checkpoint();const auto ref=territorialRef(unit.id);const auto geometry=d.geometries.get(staticGeometryBinding(d,unit.id).geometryRef);if(!geometry)continue;
            if(objectLocked(d,index,ref)&&unit.kind==UnitKind::General){auto root=unit.id;std::set<std::string> visited;for(auto ancestor=parentId(d,root);!ancestor.empty()&&visited.insert(root).second;ancestor=parentId(d,root))root=ancestor;if(roots.count(root))session->lockedDescendants_.push_back(*geometry);}
            if(unit.kind!=UnitKind::General||(parentId(d,unit.id)!=parent&&unit.id!=parent))continue;
            allowed.insert(unit.id);for(auto row:sourceSegments(unit,*geometry)){byOwner[unit.id].push_back(raw.size());raw.push_back(std::move(row));}
        }
        const SpatialIndex indexRows(raw);std::vector<std::size_t> selectedRows;std::set<std::size_t> selectedRowSet;auto add=[&](std::size_t i){if(selectedRowSet.insert(i).second)selectedRows.push_back(i);};
        for(const auto& id:session->selected_)for(const auto i:byOwner[id]){checkpoint();add(i);for(auto candidate:indexRows.query(padded(raw[i].bounds)))add(candidate);}
        std::map<NodeKey,std::size_t> nodeIds;
        for(auto i:selectedRows){checkpoint();const auto& row=raw[i];for(const auto endpoint:{std::pair{row.a,row.ref.index},std::pair{row.b,row.end}}){const auto key=nodeKey(endpoint.first);auto [it,inserted]=nodeIds.emplace(key,session->nodes_.size());if(inserted)session->nodes_.push_back({key,endpoint.first});auto& node=session->nodes_[it->second];uniqueAppend(node.owners,row.ref.owner);auto ref=row.ref;ref.index=endpoint.second;if(!hasRef(node.refs,ref))node.refs.push_back(ref);}}
        // Preserve the web bucket scan order when splitting differently segmented borders.
        std::map<std::pair<int,int>,std::vector<std::size_t>> buckets;for(std::size_t i=0;i<session->nodes_.size();++i){const auto p=session->nodes_[i].coordinate;buckets[{int(std::floor(p.x/.25)),int(std::floor(p.y/.25))}].push_back(i);}
        std::vector<Segment> segments;std::map<std::pair<NodeKey,NodeKey>,std::size_t> segmentIds;
        for(auto i:selectedRows){checkpoint();const auto& row=raw[i];const int x0=std::floor((row.bounds[0]-epsilon)/.25),x1=std::floor((row.bounds[2]+epsilon)/.25),y0=std::floor((row.bounds[1]-epsilon)/.25),y1=std::floor((row.bounds[3]+epsilon)/.25);std::vector<std::size_t> nearby;
            if(double(x1-x0+1)*(y1-y0+1)>4096){for(std::size_t n=0;n<session->nodes_.size();++n)nearby.push_back(n);}else for(int x=x0;x<=x1;++x)for(int y=y0;y<=y1;++y){const auto bucket=buckets.find({x,y});if(bucket!=buckets.end())nearby.insert(nearby.end(),bucket->second.begin(),bucket->second.end());}
            std::vector<std::pair<std::size_t,double>> split;for(auto n:nearby)if(auto t=onSegment(session->nodes_[n].coordinate,row.a,row.b))split.emplace_back(n,*t);
            std::stable_sort(split.begin(),split.end(),[](const auto& a,const auto& b){return a.second<b.second;});
            for(const auto [n,t]:split){auto& node=session->nodes_[n];uniqueAppend(node.owners,row.ref.owner);if(t>epsilon&&t<1-epsilon&&!hasRef(node.virtualRefs,row.ref)){auto ref=row.ref;ref.t=t;node.virtualRefs.push_back(std::move(ref));}}
            for(std::size_t j=1;j<split.size();++j){if(split[j].second-split[j-1].second<=epsilon)continue;const auto a=split[j-1].first,b=split[j].first;const auto left=session->nodes_[a].key,right=session->nodes_[b].key;const auto key=std::minmax(left,right);auto [it,inserted]=segmentIds.emplace(std::make_pair(key.first,key.second),segments.size());if(inserted)segments.push_back({a,b,{}});uniqueAppend(segments[it->second].owners,row.ref.owner);}
        }
        for(const auto& segment:segments) {
            const auto endpoints=std::make_pair(session->nodes_[segment.start].coordinate,session->nodes_[segment.end].coordinate);
            session->nodes_[segment.start].incidentSegments.push_back(endpoints);
            session->nodes_[segment.end].incidentSegments.push_back(endpoints);
        }
        if(!autoSeed.empty()&&session->selected_.count(autoSeed)) {
            // Only automatic single-child entry prunes disconnected siblings.
            // Explicit multi-selection may contain multiple adjacent components.
            std::map<std::string,std::set<std::string>> adjacent;
            for(const auto& segment:segments)if(segment.owners.size()>=2&&std::all_of(segment.owners.begin(),segment.owners.end(),[&](const auto& id){return session->selected_.count(id);}))
                for(const auto& a:segment.owners)for(const auto& b:segment.owners)adjacent[a].insert(b);
            std::set<std::string> connected{autoSeed};std::vector<std::string> queue{autoSeed};
            for(std::size_t i=0;i<queue.size();++i)for(const auto& id:adjacent[queue[i]])if(connected.insert(id).second)queue.push_back(id);
            session->selected_=std::move(connected);
            for(auto it=session->drafts_.begin();it!=session->drafts_.end();)if(!session->selected_.count(it->first))it=session->drafts_.erase(it);else ++it;
        }
        std::set<std::string> participants;std::set<std::size_t> handleIds;
        for(const auto& segment:segments)if(segment.owners.size()>=2&&std::all_of(segment.owners.begin(),segment.owners.end(),[&](const auto& id){return session->selected_.count(id);})) {session->segments_.push_back(segment);handleIds.insert(segment.start);handleIds.insert(segment.end);participants.insert(segment.owners.begin(),segment.owners.end());}
        if(session->segments_.empty()||participants!=session->selected_){session->error_="BOUNDARY_ISOLATED_OWNER";session->nodes_.clear();session->segments_.clear();return session;}
        std::vector<Node> handles;std::map<std::size_t,std::size_t> remap;
        // Editable handles first, followed by fixed handles, as production preparation.
        for(const bool fixed:{false,true})for(const auto& segment:session->segments_)for(const auto id:{segment.start,segment.end}){auto& node=session->nodes_[id];node.fixed=node.owners.size()<2||!std::all_of(node.owners.begin(),node.owners.end(),[&](const auto& owner){return session->selected_.count(owner);});if(node.fixed!=fixed||remap.count(id))continue;remap[id]=handles.size();handles.push_back(node);}
        for(auto& segment:session->segments_){segment.start=remap.at(segment.start);segment.end=remap.at(segment.end);}session->nodes_=std::move(handles);session->originalNodes_=session->nodes_;
    }catch(const std::exception& e){session->error_=e.what();session->nodes_.clear();session->segments_.clear();}
    return session;
}
Session::State Session::snapshot()const{return {drafts_,nodes_,movedOwners_};}
void Session::restore(State state){drafts_=std::move(state.drafts);nodes_=std::move(state.nodes);movedOwners_=std::move(state.movedOwners);}
bool Session::canMove(std::size_t id)const{if(!valid()||id>=nodes_.size()||nodes_[id].fixed)return false;return std::none_of(lockedDescendants_.begin(),lockedDescendants_.end(),[&](const auto& g){return touches(g,nodes_[id].coordinate);});}
bool Session::beginDrag(std::size_t id){if(dragBefore_||!canMove(id))return false;dragBefore_=snapshot();dragNode_=int(id);return true;}
bool Session::move(std::size_t id,Point next){
    if(!std::isfinite(next.x)||!std::isfinite(next.y))return false;
    if(dragBefore_) {
        if(dragNode_!=int(id))return false;
        auto& node=nodes_[id];if(std::abs(node.coordinate.x-next.x)<=1e-9&&std::abs(node.coordinate.y-next.y)<=1e-9)return false;
        // Production moveBoundaryGesture only updates detached display segments.
        // Materialize owner drafts once on release, using the original topology.
        node.coordinate=next;return true;
    }
    if(std::abs(next.y)>90||!canMove(id))return false;
    const auto before=snapshot();const auto node=nodes_[id];
    if(std::abs(node.coordinate.x-next.x)<=1e-9&&std::abs(node.coordinate.y-next.y)<=1e-9)return false;
    auto refs=node.virtualRefs;std::stable_sort(refs.begin(),refs.end(),[](const auto& a,const auto& b){if(std::tie(a.owner,a.polygon,a.ring)!=std::tie(b.owner,b.polygon,b.ring))return std::tie(a.owner,a.polygon,a.ring)<std::tie(b.owner,b.polygon,b.ring);return a.index!=b.index?a.index>b.index:a.t>b.t;});
    for(const auto& ref:refs){auto found=drafts_.find(ref.owner);if(found==drafts_.end())continue;auto& ring=found->second.polygons.at(ref.polygon).at(ref.ring);if(std::any_of(ring.begin(),ring.end(),[&](Point p){return nodeKey(p)==node.key;}))continue;
        const auto at=ref.index+1;if(at>=ring.size())return false;ring.insert(ring.begin()+at,node.coordinate);
        for(auto& other:nodes_){for(auto& actual:other.refs)if(actual.owner==ref.owner&&actual.polygon==ref.polygon&&actual.ring==ref.ring&&actual.index>=at)++actual.index;for(auto& virt:other.virtualRefs)if(virt.owner==ref.owner&&virt.polygon==ref.polygon&&virt.ring==ref.ring){if(virt.index>ref.index)++virt.index;else if(virt.index==ref.index&&virt.t>ref.t){++virt.index;virt.t=(virt.t-ref.t)/(1-ref.t);}else if(virt.index==ref.index&&ref.t>0)virt.t/=ref.t;}}
    }
    for(const auto& owner:node.owners){auto found=drafts_.find(owner);if(found==drafts_.end())continue;for(auto& polygon:found->second.polygons)for(auto& ring:polygon){bool changed=false;for(auto& coordinate:ring)if(nodeKey(coordinate)==node.key){coordinate=next;changed=true;}if(changed&&ring.size()>1){const auto first=nodeKey(ring.front()),last=nodeKey(ring.back());if(first!=last&&(first==nodeKey(next)||last==nodeKey(next)))ring.back()=ring.front();}}}
    for(auto& item:nodes_)for(auto& segment:item.incidentSegments){if(nodeKey(segment.first)==node.key)segment.first=next;if(nodeKey(segment.second)==node.key)segment.second=next;}
    nodes_[id].coordinate=next;nodes_[id].key=nodeKey(next);nodes_[id].virtualRefs.clear();
    if(sameDrafts(before.drafts,drafts_))return false;
    for(const auto& owner:node.owners)uniqueAppend(movedOwners_,owner);
    if(!dragBefore_){undo_.push_back(before);redo_.clear();}return true;
}
bool Session::endDrag(bool cancel){
    if(!dragBefore_)return false;const auto id=std::size_t(dragNode_);const auto coordinate=nodes_[id].coordinate;
    restore(std::move(*dragBefore_));dragBefore_.reset();dragNode_=-1;
    if(!cancel&&std::abs(coordinate.y)>90){error_="BOUNDARY_INVALID_COORDINATE";return false;}
    return !cancel&&move(id,coordinate);
}
std::vector<std::pair<Point,Point>> Session::activeSegments()const {
    if(!dragBefore_)return {};
    const auto& source=dragBefore_->nodes[std::size_t(dragNode_)];const auto coordinate=nodes_[std::size_t(dragNode_)].coordinate;
    auto result=source.incidentSegments;const auto sameSource=[&](Point point){return std::abs(point.x-source.coordinate.x)<=1e-9&&std::abs(point.y-source.coordinate.y)<=1e-9;};
    for(auto& segment:result){if(sameSource(segment.first))segment.first=coordinate;if(sameSource(segment.second))segment.second=coordinate;}
    return result;
}
void Session::resetDraft(){
    drafts_.clear();for(const auto& id:selected_)drafts_.emplace(id,originals_.at(id));nodes_=originalNodes_;
    movedOwners_.clear();undo_.clear();redo_.clear();dragBefore_.reset();dragNode_=-1;
}
bool Session::undo(){if(dragBefore_||undo_.empty())return false;redo_.push_back(snapshot());restore(std::move(undo_.back()));undo_.pop_back();return true;}
bool Session::redo(){if(dragBefore_||redo_.empty())return false;undo_.push_back(snapshot());restore(std::move(redo_.back()));redo_.pop_back();return true;}
std::vector<DraftGeometry> Session::changedDrafts()const{
    std::vector<DraftGeometry> result;
    // The worker initializes its feature Map in node.ownerIds insertion order.
    // Preserve that order through the native intent; it also governs receiver
    // iteration in hierarchy reconciliation. Every actual node owner is included,
    // even when quantization means one owner already has the destination point.
    for(const auto& owner:movedOwners_)result.push_back({territorialRef(owner),drafts_.at(owner)});
    return result;
}
}

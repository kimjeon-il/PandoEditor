#include "worldresourcecache.h"
#include <limits>
#include <stdexcept>

namespace {
void addBytes(std::size_t& total,std::size_t count,std::size_t size=1) {
    const auto max=std::numeric_limits<std::size_t>::max();
    if(size&&count>(max-total)/size)throw std::overflow_error("world resource size overflow");
    total+=count*size;
}
}
std::size_t WorldResourceCache::index(WorldDetail detail) {
    const auto i=static_cast<std::size_t>(detail);if(i>1)throw std::invalid_argument("invalid world detail");return i;
}
std::size_t WorldResourceCache::frameBytes(const WorldBaseFrame& frame) {
    std::size_t n=sizeof(frame);addBytes(n,frame.ranges.capacity(),sizeof(WorldBaseRange));
    for(const auto& range:frame.ranges){addBytes(n,range.sourceId.capacity());addBytes(n,range.ownerId.capacity());addBytes(n,range.geometryId.capacity());}
    if(frame.mesh) {
        const auto& m=*frame.mesh;addBytes(n,1,sizeof(m));
        addBytes(n,m.positionsMicrodegrees.capacity(),sizeof(std::int32_t));
        addBytes(n,m.countryIndices.capacity(),sizeof(std::uint16_t));
        addBytes(n,m.triangleIndices.capacity(),sizeof(std::uint32_t));
        addBytes(n,m.lineIndices.capacity(),sizeof(std::uint32_t));
        addBytes(n,m.countryTriangleRanges.capacity(),sizeof(std::uint32_t));
        addBytes(n,m.countryBoundaryRanges.capacity(),sizeof(std::uint32_t));
        addBytes(n,m.countryBounds.capacity(),sizeof(std::int32_t));
        addBytes(n,m.countryBoundsFlags.capacity(),sizeof(std::uint32_t));
    }
    return n;
}
pandoeditor::ResourceRequestToken WorldResourceCache::beginRequest(WorldDetail detail) {
    auto token=policy_.beginRequest(detail,std::nullopt);requests_[index(detail)]=token;return token;
}
bool WorldResourceCache::admit(WorldDetail detail,std::shared_ptr<const WorldBaseFrame> frame,
                              pandoeditor::ResourceRequestToken token) {
    const auto i=index(detail);const auto expected=requests_[i];
    if(expected.scopeEpoch!=token.scopeEpoch||expected.sequence!=token.sequence) {
        if(token.scopeEpoch!=policy_.snapshot().scopeEpoch)policy_.completeRequest(token);
        // This token cannot consume another detail's request.
        return false;
    }
    if(!frame||!frame->mesh||frame->mesh->preview!=(detail==WorldDetail::Preview)) {
        policy_.failRequest(token);return false;
    }
    const auto bytes=frameBytes(*frame);
    auto target=workingSetBytes_;target[i]=bytes;
    std::size_t total=target[0];addBytes(total,target[1]);
    if(!policy_.completeRequest(token))return false;
    if(!policy_.admit(detail,bytes))return false;
    frames_[i]=std::move(frame);workingSetBytes_=target;
    policy_.setBudget(override_.value_or(total));
    for(std::size_t reason=0;reason<5;++reason)
        policy_.setProtection(detail,static_cast<pandoeditor::ResourceProtection>(reason),protections_[i][reason]);
    trim();return true;
}
std::shared_ptr<const WorldBaseFrame> WorldResourceCache::get(WorldDetail detail) {
    const auto i=index(detail);policy_.touch(detail);return frames_[i];
}
void WorldResourceCache::protect(WorldDetail detail,pandoeditor::ResourceProtection reason,bool on) {
    const auto i=index(detail),r=static_cast<std::size_t>(reason);
    if(r>=5)throw std::invalid_argument("invalid world protection");
    protections_[i][r]=on;policy_.setProtection(detail,reason,on);trim();
}
void WorldResourceCache::setBudget(std::size_t bytes){override_=bytes;policy_.setBudget(bytes);trim();}
void WorldResourceCache::useCompatibilityBudget(){override_.reset();updateBudget();trim();}
void WorldResourceCache::updateBudget(){std::size_t n=workingSetBytes_[0];addBytes(n,workingSetBytes_[1]);policy_.setBudget(override_.value_or(n));}
void WorldResourceCache::trim(){for(auto detail:policy_.trim())frames_[index(detail)].reset();}
void WorldResourceCache::reset(){policy_.resetScope();frames_={};workingSetBytes_={};protections_={};requests_={};updateBudget();}

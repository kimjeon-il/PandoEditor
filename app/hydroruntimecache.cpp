#include "hydroruntimecache.h"
#include <limits>
#include <stdexcept>

void HydroRuntimeCache::setBudget(std::size_t bytes) {budget_=bytes;policy_.setBudget(bytes);trim();}
void HydroRuntimeCache::protect(std::set<std::uint32_t> active,std::set<std::uint32_t> pinned) {
    protectReasons(std::move(active),{},std::move(pinned),{});
}
void HydroRuntimeCache::protectReasons(std::set<std::uint32_t> visible,
    std::set<std::uint32_t> selected,std::set<std::uint32_t> editing,std::set<std::uint32_t> fallback,
    std::set<std::uint32_t> requested) {
    active_.swap(visible);selected_.swap(selected);pinned_.swap(editing);fallback_.swap(fallback);requested_.swap(requested);
    for(const auto& entry:packs_)applyProtection(entry.first);
    trim();
}
void HydroRuntimeCache::applyProtection(std::uint32_t id) {
    using pandoeditor::ResourceProtection;
    policy_.setProtection(id,ResourceProtection::Visible,active_.count(id)!=0);
    policy_.setProtection(id,ResourceProtection::Selected,selected_.count(id)!=0);
    policy_.setProtection(id,ResourceProtection::Editing,pinned_.count(id)!=0);
    policy_.setProtection(id,ResourceProtection::Fallback,fallback_.count(id)!=0);
    bool inFlight=requested_.count(id)!=0;
    for(const auto& copy:copies_)if(copy.second.count(id)){inFlight=true;break;}
    policy_.setProtection(id,ResourceProtection::InFlight,inFlight);
}
std::shared_ptr<const pandoeditor::HydroPack> HydroRuntimeCache::get(std::uint32_t id) {
    policy_.touch(id);
    auto found=packs_.find(id);return found==packs_.end()?nullptr:found->second.pack;
}
void HydroRuntimeCache::put(std::uint32_t id,std::shared_ptr<const pandoeditor::HydroPack> pack,std::size_t bytes) {
    const auto found=packs_.find(id);
    const auto old=found==packs_.end()?0:found->second.bytes;
    if(!pack||bytes>std::numeric_limits<std::size_t>::max()-(resident_-old))
        throw std::invalid_argument("invalid hydro cache entry");
    // Allocate a new map slot before changing policy; replacement cannot allocate.
    const bool inserted=found==packs_.end();
    auto entry=inserted?packs_.emplace(id,Entry{pack,bytes}).first:found;
    try {if(!policy_.admit(id,bytes))throw std::invalid_argument("hydro cache bytes overflow");}
    catch(...){if(inserted)packs_.erase(entry);throw;}
    entry->second={std::move(pack),bytes};
    resident_=resident_-old+bytes;
    applyProtection(id);trim();
}
void HydroRuntimeCache::trim() {
    for(const auto id:policy_.trim())packs_.erase(id);
    resident_=policy_.snapshot().residentBytes;
}

pandoeditor::ResourceRequestToken HydroRuntimeCache::beginCopy(std::set<std::uint32_t> ids) {
    if(nextCopy_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("hydro copy sequence exhausted");
    const auto token=copyRequests_.beginRequest(++nextCopy_,std::nullopt);
    try{copies_.emplace(token.sequence,std::move(ids));}
    catch(...){copyRequests_.failRequest(token);throw;}
    for(const auto& entry:packs_)applyProtection(entry.first);
    return token;
}
void HydroRuntimeCache::endCopy(pandoeditor::ResourceRequestToken token,bool failed) {
    if(token.scopeEpoch!=copyRequests_.snapshot().scopeEpoch||!copies_.count(token.sequence)) {
        copyRequests_.completeRequest(token);return;
    }
    if(failed)copyRequests_.failRequest(token);else copyRequests_.completeRequest(token);
    copies_.erase(token.sequence);
    for(const auto& entry:packs_)applyProtection(entry.first);
    trim();
}
void HydroRuntimeCache::resetScope() {
    policy_.resetScope();copyRequests_.resetScope();copies_.clear();packs_.clear();
    active_.clear();pinned_.clear();selected_.clear();fallback_.clear();requested_.clear();resident_=0;
}
pandoeditor::ResourceCacheSnapshot HydroRuntimeCache::resourceCacheSnapshot() const {
    auto result=policy_.snapshot();const auto copies=copyRequests_.snapshot();
    result.pendingCount+=copies.pendingCount;result.pendingUnknownCount+=copies.pendingUnknownCount;
    result.failureCount+=copies.failureCount;result.staleCompletionCount+=copies.staleCompletionCount;
    return result;
}

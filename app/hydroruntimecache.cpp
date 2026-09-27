#include "hydroruntimecache.h"
#include <limits>
#include <stdexcept>

void HydroRuntimeCache::setBudget(std::size_t bytes) {budget_=bytes;trim();}

void HydroRuntimeCache::protect(std::set<std::uint32_t> active,std::set<std::uint32_t> pinned) {
    active_.swap(active);pinned_.swap(pinned);trim();
}
std::shared_ptr<const pandoeditor::HydroPack> HydroRuntimeCache::get(std::uint32_t id) {
    auto found=packs_.find(id);
    if(found==packs_.end())return {};
    found->second.used=++clock_;return found->second.pack;
}
void HydroRuntimeCache::put(std::uint32_t id,std::shared_ptr<const pandoeditor::HydroPack> pack,std::size_t bytes) {
    if(!pack||bytes>std::numeric_limits<std::size_t>::max()-resident_)
        throw std::invalid_argument("invalid hydro cache entry");
    const auto found=packs_.find(id);
    if(found==packs_.end()){
        packs_.emplace(id,Entry{std::move(pack),bytes,++clock_});
        resident_+=bytes;
    }else{
        auto& entry=found->second;
        resident_=resident_-entry.bytes+bytes;
        entry={std::move(pack),bytes,++clock_};
    }
    trim();
}
void HydroRuntimeCache::trim() {
    while(resident_>budget_){
        auto victim=packs_.end();
        for(auto it=packs_.begin();it!=packs_.end();++it){
            if(active_.count(it->first)||pinned_.count(it->first))continue;
            if(victim==packs_.end()||it->second.used<victim->second.used)victim=it;
        }
        if(victim==packs_.end())break;
        resident_-=victim->second.bytes;packs_.erase(victim);
    }
}

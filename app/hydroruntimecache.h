#pragma once
#include <pandoeditor/hydroformat.h>
#include <pandoeditor/map/resourcecachepolicy.h>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>

class HydroRuntimeCache {
public:
    explicit HydroRuntimeCache(std::size_t budgetBytes):budget_(budgetBytes),policy_(budgetBytes){}
    void protect(std::set<std::uint32_t> active,std::set<std::uint32_t> pinned);
    std::shared_ptr<const pandoeditor::HydroPack> get(std::uint32_t id);
    void put(std::uint32_t id,std::shared_ptr<const pandoeditor::HydroPack> pack,std::size_t bytes);
    void setBudget(std::size_t bytes);
    void protectReasons(std::set<std::uint32_t> visible,std::set<std::uint32_t> selected,
                        std::set<std::uint32_t> editing,std::set<std::uint32_t> fallback,
                        std::set<std::uint32_t> requested={});
    pandoeditor::ResourceCacheSnapshot resourceCacheSnapshot() const;
    pandoeditor::ResourceRequestToken beginCopy(std::set<std::uint32_t> packs);
    void endCopy(pandoeditor::ResourceRequestToken,bool failed=false);
    void resetScope();
    std::size_t budget() const {return budget_;}
    std::size_t residentBytes() const {return resident_;}
    std::size_t packCount() const {return packs_.size();}
private:
    struct Entry {
        std::shared_ptr<const pandoeditor::HydroPack> pack;
        std::size_t bytes=0;

    };
    void trim();
    std::size_t budget_=0,resident_=0;
    pandoeditor::ResourceCachePolicy<std::uint32_t> policy_;
    void applyProtection(std::uint32_t);
    pandoeditor::ResourceCachePolicy<std::uint64_t> copyRequests_;
    std::map<std::uint64_t,std::set<std::uint32_t>> copies_;
    std::uint64_t nextCopy_=0;
    std::map<std::uint32_t,Entry> packs_;
    std::set<std::uint32_t> active_,pinned_,selected_,fallback_,requested_;
};

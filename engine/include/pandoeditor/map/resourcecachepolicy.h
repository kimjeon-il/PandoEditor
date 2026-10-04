#pragma once

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>

namespace pandoeditor {

enum class ResourceProtection : std::size_t { Visible, Selected, Editing, InFlight, Fallback, Count };
struct ResourceScope { std::uint64_t epoch=1; };
struct ResourceRequestToken { std::uint64_t scopeEpoch=0,sequence=0; };
struct ResourceLeaseToken { std::uint64_t scopeEpoch=0,sequence=0; };
struct ResourceCacheSnapshot {
    std::uint64_t scopeEpoch=1;
    std::size_t budgetBytes=0,residentCount=0,residentBytes=0,activeBytes=0,protectedBytes=0;
    std::optional<std::size_t> retiredBytes; // Not observable from metadata alone.
    std::size_t pendingCount=0,pendingEstimatedBytes=0,pendingUnknownCount=0;
    std::size_t evictableBytes=0,overBudgetBytes=0,protectedOverBudgetBytes=0;
    std::uint64_t hitCount=0,missCount=0,evictionCount=0,evictionBytes=0;
    std::uint64_t invalidationCount=0,staleCompletionCount=0,failureCount=0,trimBlockedCount=0;
    std::array<std::size_t,static_cast<std::size_t>(ResourceProtection::Count)> protectionCounts{};
};

// Metadata only. The owner serializes access, owns immutable payloads and removes
// each returned trim victim before publishing a new snapshot. Key includes content
// identity; request sequence is not a replacement for dataset/project scope.
template<class Key> class ResourceCachePolicy {
    static constexpr std::size_t reasonCount=static_cast<std::size_t>(ResourceProtection::Count);
    struct Entry {
        std::size_t bytes=0;
        std::uint64_t used=0;
        std::array<bool,reasonCount> flags{};
        std::array<std::size_t,reasonCount> pins{};
        bool protectedEntry() const noexcept {
            for(std::size_t i=0;i<reasonCount;++i) if(flags[i]||pins[i])return true;
            return false;
        }
    };
    struct Lease { Key key; ResourceProtection reason; };
    struct Request { Key key; std::optional<std::size_t> bytes; };
public:
    explicit ResourceCachePolicy(std::size_t budget=0):budget_(budget){}
    void setBudget(std::size_t bytes) noexcept {budget_=bytes;}
    bool admit(const Key& key,std::size_t bytes) {
        auto it=entries_.find(key);
        const auto old=it==entries_.end()?0:it->second.bytes;
        const auto base=resident_-old;
        if(bytes>std::numeric_limits<std::size_t>::max()-base)return false;
        const auto tick=next(clock_);
        if(it==entries_.end()) {
            Entry e;e.bytes=bytes;e.used=tick;
            entries_.emplace(key,e); // Allocation failure preserves existing state.
        } else {it->second.bytes=bytes;it->second.used=tick;}
        resident_=base+bytes;
        return true;
    }
    bool touch(const Key& key) {
        auto it=entries_.find(key);
        if(it==entries_.end()){increment(stats_.missCount);return false;}
        it->second.used=next(clock_);increment(stats_.hitCount);return true;
    }
    void setProtection(const Key& key,ResourceProtection reason,bool enabled) {
        const auto index=reasonIndex(reason);
        auto it=entries_.find(key);if(it!=entries_.end())it->second.flags[index]=enabled;
    }
    ResourceLeaseToken pin(const Key& key,ResourceProtection reason) {
        const auto index=reasonIndex(reason);
        auto it=entries_.find(key);if(it==entries_.end())return {};
        if(it->second.pins[index]==std::numeric_limits<std::size_t>::max())throw std::overflow_error("resource pin overflow");
        const auto id=next(sequence_);
        leases_.emplace(id,Lease{key,reason});
        ++it->second.pins[index];return {scope_.epoch,id};
    }
    void unpin(ResourceLeaseToken token) {
        if(token.scopeEpoch!=scope_.epoch)return;
        auto lease=leases_.find(token.sequence);if(lease==leases_.end())return;
        auto entry=entries_.find(lease->second.key);
        if(entry!=entries_.end()) {
            auto& count=entry->second.pins[reasonIndex(lease->second.reason)];
            if(count)--count;
        }
        leases_.erase(lease);
    }
    std::vector<Key> trim() {
        std::vector<Key> victims;
        if(resident_<=budget_&&budget_!=0)return victims;
        using Iterator=typename std::map<Key,Entry>::iterator;
        std::vector<Iterator> candidates;
        candidates.reserve(entries_.size());
        for(auto it=entries_.begin();it!=entries_.end();++it)
            if(!it->second.protectedEntry())candidates.push_back(it);
        std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){
            if(a->second.used!=b->second.used)return a->second.used<b->second.used;
            return a->first<b->first;
        });
        victims.reserve(candidates.size());
        std::size_t remaining=resident_,count=0;
        for(const auto& candidate:candidates) {
            if(remaining<=budget_&&budget_!=0)break;
            victims.push_back(candidate->first); // Complete all throwing key copies first.
            remaining-=candidate->second.bytes;++count;
        }
        for(std::size_t i=0;i<count;++i) {
            increment(stats_.evictionCount);saturatingAdd(stats_.evictionBytes,candidates[i]->second.bytes);
            entries_.erase(candidates[i]);
        }
        resident_=remaining;
        if(resident_>budget_)increment(stats_.trimBlockedCount);
        return victims;
    }
    // Content invalidation differs from capacity eviction; caller owns payload removal.
    bool invalidate(const Key& key) {
        auto it=entries_.find(key);if(it==entries_.end())return false;
        for(auto lease=leases_.begin();lease!=leases_.end();)
            if(equal(lease->second.key,key))lease=leases_.erase(lease);else ++lease;
        resident_-=it->second.bytes;entries_.erase(it);increment(stats_.invalidationCount);return true;
    }
    std::uint64_t resetScope() {
        const auto epoch=next(scope_.epoch); // Overflow throws before invalidation.
        entries_.clear();leases_.clear();requests_.clear();resident_=0;
        stats_={};return epoch;
    }
    ResourceRequestToken beginRequest(const Key& key,std::optional<std::size_t> estimate) {
        for(const auto& pair:requests_)if(equal(pair.second.key,key))return {scope_.epoch,pair.first};
        if(estimate) {
            std::size_t total=*estimate;
            for(const auto& pair:requests_)if(pair.second.bytes) {
                if(*pair.second.bytes>std::numeric_limits<std::size_t>::max()-total)throw std::overflow_error("resource pending bytes overflow");
                total+=*pair.second.bytes;
            }
        }
        const auto id=next(sequence_);requests_.emplace(id,Request{key,estimate});return {scope_.epoch,id};
    }
    bool completeRequest(ResourceRequestToken token) {
        if(token.scopeEpoch!=scope_.epoch){increment(stats_.staleCompletionCount);return false;}
        return requests_.erase(token.sequence)!=0;
    }
    void failRequest(ResourceRequestToken token) {if(completeRequest(token))increment(stats_.failureCount);}
    ResourceCacheSnapshot snapshot() const {
        auto result=stats_;result.scopeEpoch=scope_.epoch;result.budgetBytes=budget_;
        result.residentCount=entries_.size();result.residentBytes=resident_;
        for(const auto& pair:entries_) {
            const auto& e=pair.second;
            if(e.protectedEntry())result.protectedBytes+=e.bytes;
            if(e.flags[0]||e.pins[0])result.activeBytes+=e.bytes;
            for(std::size_t i=0;i<reasonCount;++i)if(e.flags[i]||e.pins[i])++result.protectionCounts[i];
        }
        result.evictableBytes=resident_-result.protectedBytes;
        result.overBudgetBytes=resident_>budget_?resident_-budget_:0;
        result.protectedOverBudgetBytes=result.protectedBytes>budget_?result.protectedBytes-budget_:0;
        result.pendingCount=requests_.size();
        for(const auto& pair:requests_)if(pair.second.bytes)result.pendingEstimatedBytes+=*pair.second.bytes;else ++result.pendingUnknownCount;
        return result;
    }
private:
    static std::size_t reasonIndex(ResourceProtection reason) {
        const auto i=static_cast<std::size_t>(reason);
        if(i>=reasonCount)throw std::invalid_argument("invalid resource protection reason");
        return i;
    }
    static bool equal(const Key& a,const Key& b){const std::less<Key> less;return !less(a,b)&&!less(b,a);}
    static std::uint64_t next(std::uint64_t& value) {
        if(value==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("resource sequence exhausted");
        return ++value;
    }
    static void increment(std::uint64_t& value) noexcept {if(value!=std::numeric_limits<std::uint64_t>::max())++value;}
    static void saturatingAdd(std::uint64_t& value,std::size_t amount) noexcept {
        const auto limit=std::numeric_limits<std::uint64_t>::max();value=amount>limit-value?limit:value+amount;
    }
    ResourceScope scope_;
    std::size_t budget_=0,resident_=0;
    std::uint64_t clock_=0,sequence_=0;
    std::map<Key,Entry> entries_;
    std::map<std::uint64_t,Lease> leases_;
    std::map<std::uint64_t,Request> requests_;
    ResourceCacheSnapshot stats_;
};
} // namespace pandoeditor

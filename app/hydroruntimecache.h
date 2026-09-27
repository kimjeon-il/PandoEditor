#pragma once
#include <pandoeditor/hydroformat.h>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>

class HydroRuntimeCache {
public:
    explicit HydroRuntimeCache(std::size_t budgetBytes):budget_(budgetBytes){}
    void protect(std::set<std::uint32_t> active,std::set<std::uint32_t> pinned);
    std::shared_ptr<const pandoeditor::HydroPack> get(std::uint32_t id);
    void put(std::uint32_t id,std::shared_ptr<const pandoeditor::HydroPack> pack,std::size_t bytes);
    void setBudget(std::size_t bytes);
    std::size_t budget() const {return budget_;}
    std::size_t residentBytes() const {return resident_;}
    std::size_t packCount() const {return packs_.size();}
private:
    struct Entry {
        std::shared_ptr<const pandoeditor::HydroPack> pack;
        std::size_t bytes=0;
        std::uint64_t used=0;
    };
    void trim();
    std::size_t budget_=0,resident_=0;
    std::uint64_t clock_=0;
    std::map<std::uint32_t,Entry> packs_;
    std::set<std::uint32_t> active_,pinned_;
};

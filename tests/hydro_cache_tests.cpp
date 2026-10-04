#include "hydroruntimecache.h"
#include <cassert>
#include <memory>
#include <stdexcept>
#include <limits>

int main() {
    auto pack=[]{return std::make_shared<const pandoeditor::HydroPack>();};
    HydroRuntimeCache cache(100);
    cache.protect({1},{});
    cache.put(1,pack(),70);
    cache.put(2,pack(),60);
    assert(cache.packCount()==1 && cache.get(1));
    cache.protect({2},{1});
    cache.put(2,pack(),60);
    assert(cache.packCount()==2 && cache.residentBytes()==130);
    cache.protect({2},{});
    assert(cache.packCount()==1 && !cache.get(1) && cache.get(2));
    cache.protect({},{});
    cache.put(3,pack(),50);
    assert(cache.packCount()==1 && cache.residentBytes()==50);
    assert(!cache.get(2) && cache.get(3));
    cache.put(3,pack(),40);
    assert(cache.residentBytes()==40);
    try {cache.put(4,{},10);assert(false);}catch(const std::invalid_argument&){}
    assert(cache.packCount()==1 && cache.get(3));
    HydroRuntimeCache lru(100);
    lru.put(1,pack(),30);lru.put(2,pack(),30);lru.get(1);
    lru.put(3,pack(),50);
    assert(lru.get(1) && !lru.get(2) && lru.get(3));

    HydroRuntimeCache shared(0);
    shared.protectReasons({1},{1},{1},{});
    auto held=pack();shared.put(1,held,70);
    assert(shared.resourceCacheSnapshot().protectedBytes==70);
    shared.protectReasons({}, {}, {1}, {});
    assert(shared.packCount()==1);
    shared.protectReasons({}, {}, {}, {});
    assert(shared.packCount()==0&&held);
    assert(shared.resourceCacheSnapshot().evictionCount==1);
    HydroRuntimeCache replace(std::numeric_limits<std::size_t>::max());
    replace.put(1,pack(),std::numeric_limits<std::size_t>::max());
    replace.put(1,pack(),1);
    assert(replace.residentBytes()==1);

    HydroRuntimeCache jobs(0);
    const auto first=jobs.beginCopy({1});const auto second=jobs.beginCopy({1,2});
    jobs.put(1,pack(),40);jobs.put(2,pack(),40);jobs.protect({},{});
    assert(jobs.packCount()==2&&jobs.resourceCacheSnapshot().pendingCount==2);
    jobs.endCopy(first);jobs.endCopy(first);assert(jobs.packCount()==2);
    jobs.endCopy(second);assert(jobs.packCount()==0&&jobs.resourceCacheSnapshot().pendingCount==0);
    const auto oldJob=jobs.beginCopy({1});jobs.resetScope();const auto newJob=jobs.beginCopy({1});
    jobs.put(1,pack(),20);jobs.endCopy(oldJob);assert(jobs.packCount()==1);
    jobs.endCopy(newJob,true);assert(jobs.packCount()==0&&jobs.resourceCacheSnapshot().failureCount==1);
}

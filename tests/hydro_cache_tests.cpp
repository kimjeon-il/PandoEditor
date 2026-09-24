#include "hydroruntimecache.h"
#include <cassert>
#include <memory>
#include <stdexcept>

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
}

#pragma once
#include <cstddef>
#include <cstdint>

class MapUploadScheduler {
public:
    void beginFrame(std::uint64_t generation,std::size_t budget) noexcept;
    bool reserve(std::size_t bytes,bool protectedGeometry=false) noexcept;
    std::size_t frameBytes() const noexcept {return frameBytes_;}
private:
    std::size_t budget_=0,frameBytes_=0;
};

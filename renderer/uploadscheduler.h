#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct RenderUpload {
    std::string key;
    std::size_t bytes=0;
    std::uint64_t generation=0;
    bool protectedGeometry=false;
};
struct UploadBatch {std::vector<RenderUpload> uploads;std::size_t bytes=0;};

class MapUploadScheduler {
public:
    void enqueue(RenderUpload request);
    UploadBatch takeForFrame(std::size_t budget);
    void cancelGeneration(std::uint64_t generation);
    void beginFrame(std::uint64_t generation,std::size_t budget) noexcept;
    bool reserve(std::size_t bytes,bool protectedGeometry=false) noexcept;
    std::size_t frameBytes() const noexcept {return frameBytes_;}
private:
    std::vector<RenderUpload> queued_;
    std::uint64_t generation_=0;
    std::size_t budget_=0,frameBytes_=0;
};

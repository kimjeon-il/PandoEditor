#include "uploadscheduler.h"
#include <algorithm>
#include <utility>

void MapUploadScheduler::enqueue(RenderUpload request) {queued_.push_back(std::move(request));}
UploadBatch MapUploadScheduler::takeForFrame(std::size_t budget) {
    UploadBatch batch;
    std::stable_sort(queued_.begin(),queued_.end(),[](const auto& a,const auto& b) {
        return a.protectedGeometry&&!b.protectedGeometry;
    });
    std::vector<RenderUpload> pending;
    for(auto& item:queued_) {
        if(item.bytes<=budget-batch.bytes) {
            batch.bytes+=item.bytes;batch.uploads.push_back(std::move(item));
        }else pending.push_back(std::move(item));
    }
    queued_=std::move(pending);
    return batch;
}
void MapUploadScheduler::cancelGeneration(std::uint64_t generation) {
    queued_.erase(std::remove_if(queued_.begin(),queued_.end(),[&](const auto& item) {
        return item.generation==generation;
    }),queued_.end());
}
void MapUploadScheduler::beginFrame(std::uint64_t generation,std::size_t budget) noexcept {
    generation_=generation;budget_=budget;frameBytes_=0;
}
bool MapUploadScheduler::reserve(std::size_t bytes,bool protectedGeometry) noexcept {
    if(protectedGeometry) {frameBytes_+=bytes;return true;}
    // A single indivisible SG node must make forward progress. Report this
    // exceptional over-budget frame through uploadBytesThisFrame.
    if(frameBytes_==0&&bytes>budget_) {frameBytes_=bytes;return true;}
    if(bytes>budget_-std::min(frameBytes_,budget_))return false;
    frameBytes_+=bytes;return true;
}

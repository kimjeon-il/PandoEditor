#include "uploadscheduler.h"
#include <algorithm>

void MapUploadScheduler::beginFrame(std::uint64_t,std::size_t budget) noexcept {
    budget_=budget;frameBytes_=0;
}
bool MapUploadScheduler::reserve(std::size_t bytes,bool protectedGeometry) noexcept {
    if(protectedGeometry) {frameBytes_+=bytes;return true;}
    // A single indivisible SG node must make forward progress. Report this
    // exceptional over-budget frame through uploadBytesThisFrame.
    if(frameBytes_==0&&bytes>budget_) {frameBytes_=bytes;return true;}
    if(bytes>budget_-std::min(frameBytes_,budget_))return false;
    frameBytes_+=bytes;return true;
}

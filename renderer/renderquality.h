#pragma once
#include "renderlod.h"
#include <cstddef>
#include <cstdint>
#include <deque>

enum class RenderQualityTier {Coarse,Medium,High};
struct RenderQualityProfile {
    RenderQualityTier tier=RenderQualityTier::High;
    RenderLod backgroundLod=RenderLod::High;
    bool interaction=false;
    std::uint64_t revision=1;
    double dprCap=3,labelDensity=1,targetFrameMs=20;
    double p95FrameMs=0,p99FrameMs=0;
    std::size_t longFrameCount=0;
    std::size_t terrainCacheBudgetBytes=0,hydroCacheBudgetBytes=0;
    std::size_t overlayGpuBudgetBytes=0,renderPacketCacheBudgetBytes=0;
    std::size_t uploadBudgetBytes=0;
};

class AdaptiveRenderQuality {
public:
    explicit AdaptiveRenderQuality(bool mobile=false);
    RenderQualityProfile profile() const;
    bool setTier(RenderQualityTier tier,double nowMs);
    bool beginInteraction();
    bool endInteraction();
    bool recordFrame(double durationMs,double nowMs);
    std::size_t changeCount() const noexcept {return changes_;}
private:
    double percentile(double ratio) const;
    bool mobile_=false,interaction_=false;
    RenderQualityTier tier_=RenderQualityTier::High;
    std::uint64_t revision_=1;
    std::deque<double> frames_;
    double lastChangeMs_=-1e30;
    int samples_=0,over_=0,under_=0;
    std::size_t longFrames_=0,changes_=0;
};

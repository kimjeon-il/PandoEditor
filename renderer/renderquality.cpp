#include "renderquality.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace {constexpr std::size_t mib=1024*1024;}
AdaptiveRenderQuality::AdaptiveRenderQuality(bool mobile)
    :mobile_(mobile),tier_(mobile?RenderQualityTier::Medium:RenderQualityTier::High) {}
RenderQualityProfile AdaptiveRenderQuality::profile() const {
    RenderQualityProfile result;
    result.tier=tier_;result.interaction=interaction_;result.revision=revision_;
    result.targetFrameMs=mobile_?33:20;
    result.p95FrameMs=percentile(.95);result.p99FrameMs=percentile(.99);
    result.longFrameCount=longFrames_;
    switch(tier_) {
    case RenderQualityTier::Coarse:
        result.backgroundLod=RenderLod::Coarse;result.dprCap=1.25;result.labelDensity=.52;
        result.terrainCacheBudgetBytes=32*mib;result.hydroCacheBudgetBytes=40*mib;
        result.overlayGpuBudgetBytes=48*mib;result.renderPacketCacheBudgetBytes=48*mib;
        result.uploadBudgetBytes=interaction_?256*1024:2*mib;break;
    case RenderQualityTier::Medium:
        result.backgroundLod=RenderLod::Medium;result.dprCap=mobile_?1.5:1.75;
        result.labelDensity=.76;result.terrainCacheBudgetBytes=64*mib;
        result.hydroCacheBudgetBytes=72*mib;result.overlayGpuBudgetBytes=96*mib;
        result.renderPacketCacheBudgetBytes=96*mib;
        result.uploadBudgetBytes=interaction_?512*1024:4*mib;break;
    case RenderQualityTier::High:
        result.backgroundLod=RenderLod::High;result.dprCap=mobile_?2:3;
        result.labelDensity=1;result.terrainCacheBudgetBytes=128*mib;
        result.hydroCacheBudgetBytes=96*mib;result.overlayGpuBudgetBytes=192*mib;
        result.renderPacketCacheBudgetBytes=192*mib;
        result.uploadBudgetBytes=interaction_?768*1024:8*mib;break;
    }
    return result;
}
double AdaptiveRenderQuality::percentile(double ratio) const {
    if(frames_.empty())return 0;
    std::vector<double> sorted(frames_.begin(),frames_.end());
    std::sort(sorted.begin(),sorted.end());
    return sorted[std::min(sorted.size()-1,std::size_t(std::ceil(sorted.size()*ratio)-1))];
}
bool AdaptiveRenderQuality::setTier(RenderQualityTier tier,double nowMs) {
    if(tier==tier_)return false;
    tier_=tier;lastChangeMs_=nowMs;++changes_;++revision_;over_=under_=0;
    return true;
}
bool AdaptiveRenderQuality::beginInteraction() {
    if(interaction_)return false;
    interaction_=true;++revision_;return true;
}
bool AdaptiveRenderQuality::endInteraction() {
    if(!interaction_)return false;
    interaction_=false;++revision_;return true;
}
bool AdaptiveRenderQuality::recordFrame(double ms,double nowMs) {
    if(!std::isfinite(ms)||ms<=0||!std::isfinite(nowMs))return false;
    frames_.push_back(ms);if(frames_.size()>120)frames_.pop_front();
    if(ms>=45)++longFrames_;
    if(++samples_<20)return false;
    samples_=0;
    const auto target=mobile_?33.:20.;
    const auto p95=percentile(.95),p99=percentile(.99);
    if(p95>target||p99>std::max(45.,target*1.7)){++over_;under_=0;}
    else if(p95<target*.68&&p99<std::max(28.,target*1.05)) {++under_;over_=0;}
    else over_=under_=0;
    if(nowMs-lastChangeMs_<2500)return false;
    if(over_>=2&&tier_!=RenderQualityTier::Coarse)
        return setTier(tier_==RenderQualityTier::High?RenderQualityTier::Medium:
            RenderQualityTier::Coarse,nowMs);
    if(!interaction_&&under_>=5&&tier_!=RenderQualityTier::High)
        return setTier(tier_==RenderQualityTier::Coarse?RenderQualityTier::Medium:
            RenderQualityTier::High,nowMs);
    return false;
}

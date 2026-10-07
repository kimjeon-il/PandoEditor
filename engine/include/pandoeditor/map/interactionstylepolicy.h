#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>

// Exact CSS-pixel policy from fixed Web ebcfae4d27b29cbbea6416a7045a4806930204be,
// assets/js/modules/map-interaction-style.js, Git blob
// 8cad4db5a07c4286b5a5e1fccdbaa6c6988caa4e. Device scaling belongs to a renderer;
// QSG's logical qt_Matrix already applies it. Nothing here is document state.
namespace mapstyle {
enum class InteractionRole {Candidate,Reference,UnchosenResult,Hover,Secondary,
    SelectedProvider,SelectedComponent,Primary,EditTarget,ChosenResult};
enum class BaseRole {Country,CountryInternal,Subunit,SubunitInternal,Region,Generic,
    Distribution,River,HydroBoundary};
enum class Cap {Round,Butt};
enum class Join {Round,Miter};
struct FrameContext {double width=0,height=0,cssScale=0;bool flat=true;double left=0,right=0,top=0,bottom=0;};
struct Options {bool dark=true,outlineVisible=true,directManipulation=false,sharedBoundary=false,antiAlias=true;
    double fillStrength=.35;std::optional<std::uint32_t> selectionColor;
    bool operator==(const Options& b) const {return dark==b.dark&&outlineVisible==b.outlineVisible&&
        directManipulation==b.directManipulation&&sharedBoundary==b.sharedBoundary&&antiAlias==b.antiAlias&&
        fillStrength==b.fillStrength&&selectionColor==b.selectionColor;}
};
struct Stroke {double width=1,alpha=1,fillAlpha=0,dashOn=0,dashOff=0,interactionScale=1;
    std::uint32_t color=0;Cap cap=Cap::Round;Join join=Join::Round;bool antiAlias=true;};
inline int priority(InteractionRole role) {
    switch(role){case InteractionRole::Candidate:case InteractionRole::Reference:case InteractionRole::UnchosenResult:return 1;
    case InteractionRole::Hover:return 2;case InteractionRole::Secondary:case InteractionRole::SelectedProvider:case InteractionRole::SelectedComponent:return 3;
    case InteractionRole::Primary:return 4;case InteractionRole::EditTarget:case InteractionRole::ChosenResult:return 5;}
    throw std::invalid_argument("Unknown interaction stroke role");
}
inline double interactionStrokeScale(const FrameContext& f) {
    if(!(f.width>0)||!(f.height>0)||!(f.cssScale>0))return 1;
    constexpr double pi=3.14159265358979323846;
    const auto max=[](double a,double b){return std::isnan(a)?a:std::isnan(b)?b:std::max(a,b);};
    const auto min=[](double a,double b){return std::isnan(a)?a:std::isnan(b)?b:std::min(a,b);};
    // JS safeInset uses Number(value || 0): a numeric NaN falls back to zero.
    const auto inset=[](double v){return std::isnan(v)?0:v;};
    const auto width=max(1.,f.width-inset(f.left)-inset(f.right)),height=max(1.,f.height-inset(f.top)-inset(f.bottom));
    const double base=f.flat?max(30.,width/(2*pi)):max(60.,min(width,height)*.455);
    const double factor=.4+((f.cssScale/base-.72)/.28)*.6;
    return std::isnan(factor)?factor:std::clamp(factor,.45,1.);
}
inline Stroke interactionStroke(InteractionRole role,const Options& o={},const FrameContext& f={}) {
    const auto p=priority(role);Stroke s;s.color=o.selectionColor.value_or(o.dark?0xcda95d:0x315e9d);s.antiAlias=o.antiAlias;
    const double strength=std::isfinite(o.fillStrength)?std::clamp(o.fillStrength,0.,1.):.35;
    if(p==1){s.width=1;s.alpha=.45;}
    else if(p==2){s.width=1.5;s.alpha=.85;s.fillAlpha=(o.dark?.10:.08)*strength;}
    else {const bool primary=p>=4;s.width=(o.outlineVisible||o.directManipulation)?(primary?2.5:1.5):0;
        s.alpha=(o.outlineVisible||o.directManipulation)?(primary?1:.72):0;
        s.fillAlpha=(o.dark?(primary?.30:.18):(primary?.24:.14))*strength;
        if(o.sharedBoundary){s.dashOn=6;s.dashOff=3;}}
    s.interactionScale=interactionStrokeScale(f);s.width*=s.interactionScale;return s;
}
inline Stroke baseStroke(BaseRole role,double widthMultiplier=1,double alpha=1) {
    Stroke s;s.alpha=alpha;
    switch(role){case BaseRole::Country:s.width=.72;s.cap=Cap::Butt;break;
    case BaseRole::CountryInternal:s.width=1.1;s.cap=Cap::Butt;s.join=Join::Miter;s.dashOn=3;s.dashOff=2;break;
    case BaseRole::Subunit:s.width=2;break;
    case BaseRole::SubunitInternal:s.width=1.1;s.dashOn=3;s.dashOff=2;break;
    case BaseRole::Region:s.width=1.5;s.dashOn=7;s.dashOff=3;break;
    case BaseRole::Generic:case BaseRole::Distribution:case BaseRole::River:case BaseRole::HydroBoundary:s.width=1;break;
    default:throw std::invalid_argument("Unknown base stroke role");}
    if(widthMultiplier==0||std::isnan(widthMultiplier))widthMultiplier=1;
    s.width*=std::max(.5,widthMultiplier);return s;
}
}

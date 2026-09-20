#include <pandoeditor/presentationcommands.h>
#include "documentstate.h"
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace pandoeditor {
namespace {
bool group(const std::string& g){return g=="countries"||g=="subunits"||g=="regions";}
void require(bool v){if(!v)throw std::invalid_argument("INVALID_PRESENTATION_COMMAND");}
void merge(PresentationStyle& s,const PresentationStyle& p){if(p.opacity)s.opacity=p.opacity;if(p.boundaryVisible)s.boundaryVisible=p.boundaryVisible;if(p.labelsVisible)s.labelsVisible=p.labelsVisible;if(p.boundaryWidth)s.boundaryWidth=p.boundaryWidth;if(p.blendMode)s.blendMode=p.blendMode;}
}
PresentationResult PresentationCommandProcessor::apply(Project& project,const PresentationAction& action) noexcept {
    try {
        auto d=project.document();auto& p=d.presentation.webPresentation;
        auto targetGroup=[&](const ObjectRef& ref){auto i=project.index().objects.find(ref);require(i!=project.index().objects.end());return territorialGroup(d.units.at(i->second).kind);};
        auto set=[&](const ObjectRef& ref,bool visible){auto g=targetGroup(ref);if(visible)p.hiddenItems[g].erase(ref.id);else p.hiddenItems[g].insert(ref.id);};
        std::visit([&](const auto& a){using T=std::decay_t<decltype(a)>;
            if constexpr(std::is_same_v<T,SetPresentationVisibility>) {
                require(group(a.key)||a.key=="basemapLabels"||a.key=="countryFlags"||a.key=="subunitLabels"||a.key=="subunitFlags"||a.key=="regionLabels"||a.key=="regionFlags");
                if(groupVisible(p,a.key)!=a.visible)p.visibility[a.key]=a.visible;
            } else if constexpr(std::is_same_v<T,SetScopedVisibility>) {
                require(group(a.group)&&!a.targets.empty());for(const auto& r:a.targets)require(targetGroup(r)==a.group);
                if(!groupVisible(p,a.group)){for(const auto& u:d.units)if(territorialGroup(u.kind)==a.group)p.hiddenItems[a.group].insert(u.id);p.visibility[a.group]=true;}
                for(const auto& r:a.targets)set(r,a.visible);
            } else if constexpr(std::is_same_v<T,SetBatchVisibility>) {
                require(!a.targets.empty());bool all=true;for(const auto& r:a.targets)all= itemVisible(p,targetGroup(r),r.id)&&all;
                const bool visible=a.visible.value_or(!all);for(const auto& r:a.targets)set(r,visible);
            } else {
                require(group(a.group));merge(p.styles[a.group],a.patch);
                if(a.group=="subunits")for(auto& [key,s]:p.objectStyles)if(key.rfind("territorial:subunit:",0)==0)merge(s,a.patch);
            }
        },action);
        validatePresentation(d);normalizePresentation(d);
        if(p==project.document().presentation.webPresentation)return PresentationResult::NoOp;
        require(project.presentationRevision_!=std::numeric_limits<std::uint64_t>::max());
        auto next=std::make_shared<const detail::DocumentState>(std::move(d));
        project.state_=std::move(next);++project.presentationRevision_;
        return PresentationResult::Applied;
    } catch(const std::invalid_argument&){return PresentationResult::InvalidArguments;}
    catch(...){return PresentationResult::Failed;}
}
}

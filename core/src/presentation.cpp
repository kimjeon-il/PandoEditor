#include <pandoeditor/document.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace pandoeditor {
std::string territorialGroup(UnitKind k) { return k==UnitKind::Country?"countries":k==UnitKind::Subunit?"subunits":"regions"; }
std::string territorialPresentationKey(UnitKind k,const std::string& id) { return "territorial:"+std::string(k==UnitKind::Country?"country":k==UnitKind::Subunit?"subunit":"region")+":"+id; }
bool groupVisible(const WebPresentation& p,const std::string& g) { auto i=p.visibility.find(g);return i==p.visibility.end()||i->second; }
bool itemVisible(const WebPresentation& p,const std::string& g,const std::string& id) { auto i=p.hiddenItems.find(g);return i==p.hiddenItems.end()||!i->second.count(id); }
namespace {
const TerritorialUnit* unit(const ProjectDocument& d,const ObjectRef& ref) { if(ref.domain!="territorial")return nullptr;for(const auto& u:d.units)if(u.id==ref.id)return &u;return nullptr; }
PresentationStyle style(const std::map<std::string,PresentationStyle>& m,const std::string& key) { auto i=m.find(key);return i==m.end()?PresentationStyle{}:i->second; }
}
bool effectiveMapVisibility(const ProjectDocument& d,const ObjectRef& ref) {
    auto u=unit(d,ref);if(!u)return false;
    auto g=territorialGroup(u->kind);const auto& p=d.presentation.webPresentation;
    if(!groupVisible(p,g)||!itemVisible(p,g,u->id))return false;
    auto m=d.presentation.membership.find(ref);
    if(m!=d.presentation.membership.end())for(const auto& l:d.presentation.userLayers)if(l.id==m->second)return l.visible;
    return m==d.presentation.membership.end();
}
ResolvedTerritorialPresentation resolvedTerritorialPresentation(const ProjectDocument& d,const ObjectRef& ref,double terrainAlpha) {
    const auto& p=d.presentation.webPresentation;std::set<std::string> visiting;
    std::function<ResolvedTerritorialPresentation(const TerritorialUnit&)> resolve=[&](const TerritorialUnit& u) {
        ResolvedTerritorialPresentation r;auto country=style(p.styles,"countries");r.opacity=country.opacity.value_or(1);r.blendMode=country.blendMode.value_or("normal");
        auto group=territorialGroup(u.kind);auto gs=style(p.styles,group);auto os=style(p.objectStyles,territorialPresentationKey(u.kind,u.id));
        if(u.kind!=UnitKind::Country) {
            if(!visiting.insert(u.id).second)throw std::invalid_argument("PRESENTATION_CYCLE");
            for(const auto& rel:d.relations)if(rel.unit==territorialRef(u.id)&&!rel.dated&&rel.parent) {
                auto parent=unit(d,*rel.parent);if(parent&&parent->kind==UnitKind::Subunit)r=resolve(*parent);break;
            }
            visiting.erase(u.id);
            if(os.opacity)r.opacity=*os.opacity;else if(gs.opacity&&*gs.opacity!=1)r.opacity=*gs.opacity;
            if(os.blendMode)r.blendMode=*os.blendMode;else if(gs.blendMode==std::optional<std::string>("multiply"))r.blendMode="multiply";
        }
        r.boundaryVisible=os.boundaryVisible.value_or(gs.boundaryVisible.value_or(true));
        const std::string name=u.kind==UnitKind::Country?"basemapLabels":u.kind==UnitKind::Subunit?"subunitLabels":"regionLabels";
        const std::string flag=u.kind==UnitKind::Country?"countryFlags":u.kind==UnitKind::Subunit?"subunitFlags":"regionFlags";
        const bool visible=effectiveMapVisibility(d,territorialRef(u.id));
        r.nameVisible=visible&&groupVisible(p,name);r.flagVisible=visible&&groupVisible(p,flag);
        r.effectiveAlpha=r.opacity*terrainAlpha;
        return r;
    };
    auto u=unit(d,ref);if(!u)return {};
    auto r=resolve(*u);auto m=d.presentation.membership.find(ref);
    if(m!=d.presentation.membership.end())for(const auto& l:d.presentation.userLayers)if(l.id==m->second)r.effectiveAlpha*=l.opacity;
    return r;
}
double territorialRenderOrder(const ProjectDocument& d,const ObjectRef& ref,double offset) {
    auto u=unit(d,ref);if(!u)return 0;
    const auto& order=d.presentation.webPresentation.objectOrder;
    double rank=0;if(u->kind!=UnitKind::Country&&!order.empty()) {auto i=std::find(order.begin(),order.end(),territorialPresentationKey(u->kind,u->id));rank=double(i-order.begin())/double(order.size()+1);}
    return (u->kind==UnitKind::Country?-1000:u->kind==UnitKind::Subunit?3000:4000)+offset+rank;
}
void normalizePresentation(ProjectDocument& d) {
    auto& p=d.presentation.webPresentation;std::set<std::string> keys;std::map<std::string,std::set<std::string>> ids;
    for(const auto& u:d.units){ids[territorialGroup(u.kind)].insert(u.id);if(u.kind!=UnitKind::Country)keys.insert(territorialPresentationKey(u.kind,u.id));}
    for(auto& [g,hidden]:p.hiddenItems)for(auto i=hidden.begin();i!=hidden.end();)if(!ids[g].count(*i))i=hidden.erase(i);else ++i;
    for(auto i=p.hiddenItems.begin();i!=p.hiddenItems.end();)if(i->second.empty())i=p.hiddenItems.erase(i);else ++i;
    for(auto i=p.objectStyles.begin();i!=p.objectStyles.end();)if(!keys.count(i->first))i=p.objectStyles.erase(i);else ++i;
    std::set<std::string> seen;p.objectOrder.erase(std::remove_if(p.objectOrder.begin(),p.objectOrder.end(),[&](const auto& k){return !keys.count(k)||!seen.insert(k).second;}),p.objectOrder.end());
    for(auto* styles:{&p.styles,&p.objectStyles})for(auto& [key,s]:*styles){if(s.opacity&&std::isfinite(*s.opacity))s.opacity=std::clamp(*s.opacity,0.,1.);if(s.boundaryWidth)s.boundaryWidth=1;if(s.blendMode&&*s.blendMode!="multiply")s.blendMode="normal";}
}
void validatePresentation(const ProjectDocument& d) {
    const auto& p=d.presentation.webPresentation;
    for(const auto* styles:{&p.styles,&p.objectStyles})for(const auto& [key,s]:*styles) {
        if((s.opacity&&(!std::isfinite(*s.opacity)||*s.opacity<0||*s.opacity>1))||(s.boundaryWidth&&*s.boundaryWidth!=1)||(s.blendMode&&*s.blendMode!="normal"&&*s.blendMode!="multiply"))throw std::invalid_argument("INVALID_PRESENTATION_STYLE");
    }
}
WebPresentation rebasePresentation(const ProjectDocument& current,const ProjectDocument& from,const ProjectDocument& to) {
    auto candidate=to;candidate.presentation.webPresentation=current.presentation.webPresentation;
    auto& out=candidate.presentation.webPresentation;
    for(const auto& u:to.units) {
        const auto prior=unit(from,territorialRef(u.id));if(prior&&prior->kind==u.kind)continue;
        const auto group=territorialGroup(u.kind),key=territorialPresentationKey(u.kind,u.id);
        const auto& restore=to.presentation.webPresentation;
        if(!itemVisible(restore,group,u.id))out.hiddenItems[group].insert(u.id);else out.hiddenItems[group].erase(u.id);
        if(auto s=restore.objectStyles.find(key);s!=restore.objectStyles.end())out.objectStyles[key]=s->second;
        const auto position=std::find(restore.objectOrder.begin(),restore.objectOrder.end(),key);
        if(position!=restore.objectOrder.end()) {
            auto insert=out.objectOrder.end();
            for(auto next=position+1;next!=restore.objectOrder.end();++next){auto found=std::find(out.objectOrder.begin(),out.objectOrder.end(),*next);if(found!=out.objectOrder.end()){insert=found;break;}}
            out.objectOrder.insert(insert,key);
        }
    }
    normalizePresentation(candidate);return candidate.presentation.webPresentation;
}
}

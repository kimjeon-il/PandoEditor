#include <pandoeditor/document.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace pandoeditor {
bool LabelSettings::operator==(const LabelSettings& b) const {
    const auto point=[](const std::optional<Point>& p){return p?std::make_tuple(true,p->x,p->y):std::make_tuple(false,0.,0.);};
    return priority==b.priority&&minZoom==b.minZoom&&maxZoom==b.maxZoom&&point(manualPosition)==point(b.manualPosition)&&pinned==b.pinned&&collisionGroup==b.collisionGroup;
}
std::string territorialGroup(const ProjectDocument& d,const std::string& id) {
    requireStaticTimeline(d);
    const auto unit=std::find_if(d.units.begin(),d.units.end(),[&](const auto& row){return row.id==id;});
    if(unit==d.units.end())throw std::invalid_argument("INVALID_TARGETS");
    return unit->kind==UnitKind::Regional?"regions":staticParentRelation(d,id).parentId.empty()?"countries":"subunits";
}
std::string territorialPresentationKey(const std::string& id) { return "territorial:entity:"+id; }
bool groupVisible(const WebPresentation& p,const std::string& g) { auto i=p.visibility.find(g);return i==p.visibility.end()||i->second; }
bool itemVisible(const WebPresentation& p,const std::string& g,const std::string& id) { auto i=p.hiddenItems.find(g);return i==p.hiddenItems.end()||!i->second.count(id); }
namespace {
const TerritorialUnit* unit(const ProjectDocument& d,const ObjectRef& ref) { if(ref.domain!="territorial")return nullptr;for(const auto& u:d.units)if(u.id==ref.id)return &u;return nullptr; }
PresentationStyle style(const std::map<std::string,PresentationStyle>& m,const std::string& key) { auto i=m.find(key);return i==m.end()?PresentationStyle{}:i->second; }
std::vector<ObjectRef> contentRefs(const ProjectDocument& d) {
    std::vector<ObjectRef> refs;
    auto add=[&](const auto& values,const char* domain){for(const auto& v:values)refs.push_back({domain,v.id});};
    add(d.labels,"label");add(d.hydro,"hydro");add(d.genericFeatures,"generic");add(d.distributionLayers,"distributionLayer");add(d.distributionEntries,"distributionEntry");return refs;
}
}
bool effectiveMapVisibility(const ProjectDocument& d,const ObjectRef& ref) {
    auto u=unit(d,ref);
    auto g=u?territorialGroup(d,u->id):contentGroup(d,ref);const auto& p=d.presentation.webPresentation;
    if(g.empty()||!groupVisible(p,g)||!itemVisible(p,g,ref.id))return false;
    if(ref.domain=="hydro"&&std::find(d.physicalData.hiddenHydroIds.begin(),d.physicalData.hiddenHydroIds.end(),ref.id)!=d.physicalData.hiddenHydroIds.end())for(const auto& h:d.hydro)if(h.id==ref.id&&h.source.kind=="builtin")return false;
    if(ref.domain=="distributionEntry") for(const auto& e:d.distributionEntries) if(e.id==ref.id && !itemVisible(p,g,e.layerId)) return false;
    auto m=d.presentation.membership.find(ref);
    if(m!=d.presentation.membership.end())for(const auto& l:d.presentation.userLayers)if(l.id==m->second)return l.visible;
    return m==d.presentation.membership.end();
}
ResolvedTerritorialPresentation resolvedTerritorialPresentation(const ProjectDocument& d,const ObjectRef& ref,double terrainAlpha) {
    const auto& p=d.presentation.webPresentation;std::set<std::string> visiting;
    std::function<ResolvedTerritorialPresentation(const TerritorialUnit&)> resolve=[&](const TerritorialUnit& u) {
        ResolvedTerritorialPresentation r;auto country=style(p.styles,"countries");r.opacity=country.opacity.value_or(1);r.blendMode=country.blendMode.value_or("normal");
        auto group=territorialGroup(d,u.id);auto gs=style(p.styles,group);auto os=style(p.objectStyles,territorialPresentationKey(u.id));
        if(!isRootGeneral(d,u)) {
            if(!visiting.insert(u.id).second)throw std::invalid_argument("PRESENTATION_CYCLE");
            const auto& relation=staticParentRelation(d,u.id);
            if(!relation.parentId.empty()) {
                const auto parent=unit(d,territorialRef(relation.parentId));
                if(parent&&!isRootGeneral(d,*parent))r=resolve(*parent);
            }
            visiting.erase(u.id);
            if(os.opacity)r.opacity=*os.opacity;else if(gs.opacity&&*gs.opacity!=1)r.opacity=*gs.opacity;
            if(os.blendMode)r.blendMode=*os.blendMode;else if(gs.blendMode==std::optional<std::string>("multiply"))r.blendMode="multiply";
        }
        else {if(os.opacity)r.opacity=*os.opacity;if(os.blendMode)r.blendMode=*os.blendMode;}
        r.boundaryVisible=os.boundaryVisible.value_or(gs.boundaryVisible.value_or(true));
        r.colorVisible=os.colorVisible.value_or(gs.colorVisible.value_or(true));
        const std::string name=group=="countries"?"basemapLabels":group=="subunits"?"subunitLabels":"regionLabels";
        const std::string flag=group=="countries"?"countryFlags":group=="subunits"?"subunitFlags":"regionFlags";
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
    double rank=0;if(!order.empty()) {auto i=std::find(order.begin(),order.end(),territorialPresentationKey(u->id));rank=double(i-order.begin())/double(order.size()+1);}
    return (isRootGeneral(d,*u)?-1000:u->kind==UnitKind::General?3000:4000)+offset+rank;
}
void normalizePresentation(ProjectDocument& d) {
    auto& p=d.presentation.webPresentation;std::set<std::string> keys;std::map<std::string,std::set<std::string>> ids;
    for(const auto& u:d.units){ids[territorialGroup(d,u.id)].insert(u.id);keys.insert(territorialPresentationKey(u.id));}
    for(const auto& ref:contentRefs(d))ids[contentGroup(d,ref)].insert(ref.id);
    for(auto& [g,hidden]:p.hiddenItems)for(auto i=hidden.begin();i!=hidden.end();)if(!ids[g].count(*i))i=hidden.erase(i);else ++i;
    for(auto i=p.objectStyles.begin();i!=p.objectStyles.end();)if(!keys.count(i->first))i=p.objectStyles.erase(i);else ++i;
    std::set<std::string> seen;p.objectOrder.erase(std::remove_if(p.objectOrder.begin(),p.objectOrder.end(),[&](const auto& k){return !keys.count(k)||!seen.insert(k).second;}),p.objectOrder.end());
    for(auto i=p.labelSettings.begin();i!=p.labelSettings.end();) {
        if(!std::any_of(d.units.begin(),d.units.end(),[&](const auto& u){return i->first==territorialRef(u.id);})&&
           !std::any_of(d.labels.begin(),d.labels.end(),[&](const auto& v){return i->first==ObjectRef{"label",v.id};})) i=p.labelSettings.erase(i);
        else {if(i->second.collisionGroup.empty())i->second.collisionGroup="map";++i;}
    }
    for(auto* styles:{&p.styles,&p.objectStyles})for(auto& [key,s]:*styles){if(s.opacity&&std::isfinite(*s.opacity))s.opacity=std::clamp(*s.opacity,0.,1.);if(s.boundaryWidth)s.boundaryWidth=1;if(s.blendMode&&*s.blendMode!="multiply")s.blendMode="normal";}
}
void validatePresentation(const ProjectDocument& d) {
    const auto& p=d.presentation.webPresentation;
    std::set<std::string> keys;for(const auto& unit:d.units)keys.insert(territorialPresentationKey(unit.id));
    for(const auto& [key,style]:p.objectStyles)if(!keys.count(key))throw std::invalid_argument("DANGLING_REF: object presentation");
    const std::string prefix="territorial:entity:";
    std::set<std::string> order;for(const auto& key:p.objectOrder)if(key.rfind(prefix,0)!=0||key.size()==prefix.size()||!order.insert(key).second)throw std::invalid_argument("INVALID_PRESENTATION_ORDER");
    if(!p.overlayOrderPresent&&!p.overlayOrder.empty())throw std::invalid_argument("INVALID_OVERLAY_ORDER");
    std::set<std::string> overlays;for(const auto& group:p.overlayOrder)if((group!="genericFeatures"&&group!="distributions"&&group!="subunits"&&group!="regions")||!overlays.insert(group).second)throw std::invalid_argument("INVALID_OVERLAY_ORDER");
    for(const auto* styles:{&p.styles,&p.objectStyles})for(const auto& [key,s]:*styles) {
        if((s.opacity&&(!std::isfinite(*s.opacity)||*s.opacity<0||*s.opacity>1))||(s.boundaryWidth&&*s.boundaryWidth!=1)||(s.blendMode&&*s.blendMode!="normal"&&*s.blendMode!="multiply"))throw std::invalid_argument("INVALID_PRESENTATION_STYLE");
    }
    for(const auto& [ref,s]:p.labelSettings) {
        const bool owner=ref.domain=="territorial"?std::any_of(d.units.begin(),d.units.end(),[&](const auto& u){return u.id==ref.id;}):ref.domain=="label"&&std::any_of(d.labels.begin(),d.labels.end(),[&](const auto& label){return label.id==ref.id;});
        if(!owner)throw std::invalid_argument("DANGLING_REF: label settings");
        auto finite=[](const auto& v){return !v||std::isfinite(*v);};
        if((ref.domain!="territorial"&&ref.domain!="label")||!finite(s.priority)||!finite(s.minZoom)||!finite(s.maxZoom)||
           (s.minZoom&&s.maxZoom&&*s.minZoom>*s.maxZoom)||s.collisionGroup.empty()||
           (s.manualPosition&&(!std::isfinite(s.manualPosition->x)||!std::isfinite(s.manualPosition->y))))throw std::invalid_argument("INVALID_LABEL_SETTINGS");
    }
}

LabelSettings automaticLabelSettings(const std::string& kind,const LabelSettings& stored) {
    LabelSettings r=stored;
    struct Policy{double priority,min,max;const char* group;};
    static const std::map<std::string,Policy> policies={{"country",{100,0,INFINITY,"country"}},{"capital",{90,0,INFINITY,"place"}},
        {"city",{70,1.25,INFINITY,"place"}},{"region",{60,1,INFINITY,"place"}},{"town",{40,2.5,INFINITY,"place"}},
        {"mountain",{40,2,INFINITY,"place"}},{"water",{40,1.5,INFINITY,"place"}},{"custom",{40,1.5,INFINITY,"place"}}};
    const auto i=policies.find(kind);const auto& p=i==policies.end()?policies.at("custom"):i->second;
    r.priority=p.priority;r.minZoom=p.min;r.maxZoom=p.max;r.collisionGroup=p.group;r.pinned=r.pinned||r.manualPosition.has_value();return r;
}
std::vector<ObjectRef> layoutLabels(const std::vector<LabelLayoutCandidate>& input,double zoom,double padding,
                                    std::optional<LabelLayoutBounds> bounds) {
    auto rows=input;rows.erase(std::remove_if(rows.begin(),rows.end(),[&](const auto& c){
        if(zoom<c.minZoom||zoom>c.maxZoom)return true;
        return bounds&&!c.selected&&!c.pinned&&
            (c.x-c.width/2<bounds->left||c.x+c.width/2>bounds->right||
             c.y-c.height/2<bounds->top||c.y+c.height/2>bounds->bottom);
    }),rows.end());
    std::stable_sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){if(a.selected!=b.selected)return a.selected>b.selected;if(a.pinned!=b.pinned)return a.pinned>b.pinned;if(a.priority!=b.priority)return a.priority>b.priority;return a.key<b.key;});
    struct Box{double l,t,r,b;std::string group;};std::vector<Box> placed;std::vector<ObjectRef> result;
    for(const auto& c:rows){Box box{c.x-c.width/2-padding,c.y-c.height/2-padding,c.x+c.width/2+padding,c.y+c.height/2+padding,c.collisionGroup};
        bool overlap=false;for(const auto& p:placed)if(p.group==box.group&&box.l<p.r&&box.r>p.l&&box.t<p.b&&box.b>p.t){overlap=true;break;}
        if(overlap&&!c.selected&&!c.pinned)continue;placed.push_back(box);result.push_back(c.ref);
    }return result;
}
std::vector<ObjectRef> visibleDistributionEntries(const ProjectDocument& d) {
    const auto& settings=d.presentation.webPresentation.distributionSettings;std::vector<std::string> visible;
    for(const auto& layer:d.distributionLayers){const auto group=contentGroup(d,{"distributionLayer",layer.id});if(groupVisible(d.presentation.webPresentation,group)&&itemVisible(d.presentation.webPresentation,group,layer.id))visible.push_back(layer.id);}
    std::vector<ObjectRef> result;
    if(visible.empty())return result;
    const auto active=std::find(visible.begin(),visible.end(),settings.activeLayerId)!=visible.end()?settings.activeLayerId:visible.front();
    for(const auto& layer:visible) {
        if(settings.renderMode==DistributionRenderMode::Single&&layer!=active)continue;
        for(const auto& entry:d.distributionEntries)if(entry.layerId==layer)result.push_back({"distributionEntry",entry.id});
    }
    return result;
}
std::optional<DistributionValueRange> distributionValueRange(const ProjectDocument& d,const std::string& layerId) {
    const auto layer=std::find_if(d.distributionLayers.begin(),d.distributionLayers.end(),[&](const auto& v){return v.id==layerId;});
    if(layer==d.distributionLayers.end())return {};
    if(layer->valueScale.manual)return DistributionValueRange{layer->valueScale.min,layer->valueScale.max};
    std::optional<DistributionValueRange> range;
    for(const auto& entry:d.distributionEntries)if(entry.layerId==layerId&&std::isfinite(entry.value)) {
        if(!range)range=DistributionValueRange{entry.value,entry.value};
        else {range->min=std::min(range->min,entry.value);range->max=std::max(range->max,entry.value);}
    }
    return range;
}
double distributionValueAlpha(double value,const std::optional<DistributionValueRange>& range,double opacity) {
    if(!range)return 0;
    const auto span=range->max-range->min;
    const auto ratio=span>0?std::clamp((value-range->min)/span,0.,1.):1.;
    return (0.12+0.58*ratio)*opacity;
}
WebPresentation rebasePresentation(const ProjectDocument& current,const ProjectDocument& from,const ProjectDocument& to) {
    auto candidate=to;candidate.presentation.webPresentation=current.presentation.webPresentation;
    auto& out=candidate.presentation.webPresentation;
    const auto priorContent=contentRefs(from);
    for(const auto& ref:contentRefs(to)) {
        if(std::find(priorContent.begin(),priorContent.end(),ref)!=priorContent.end())continue;
        const auto group=contentGroup(to,ref);
        if(!itemVisible(to.presentation.webPresentation,group,ref.id))out.hiddenItems[group].insert(ref.id);
        else out.eraseHiddenItem(group,ref.id);
        if(auto settings=to.presentation.webPresentation.labelSettings.find(ref);settings!=to.presentation.webPresentation.labelSettings.end())out.labelSettings[ref]=settings->second;
    }
    for(const auto& u:to.units) {
        const auto prior=unit(from,territorialRef(u.id));if(prior&&prior->kind==u.kind&&territorialGroup(from,prior->id)==territorialGroup(to,u.id))continue;
        const auto group=territorialGroup(to,u.id),key=territorialPresentationKey(u.id);
        const auto& restore=to.presentation.webPresentation;
        if(!itemVisible(restore,group,u.id))out.hiddenItems[group].insert(u.id);else out.eraseHiddenItem(group,u.id);
        if(auto settings=restore.labelSettings.find(territorialRef(u.id));settings!=restore.labelSettings.end())out.labelSettings[territorialRef(u.id)]=settings->second;
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

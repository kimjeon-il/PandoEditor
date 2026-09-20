#include <pandoeditor/objectproperties.h>
#include <algorithm>
#include <set>
#include <string_view>
namespace pandoeditor {
std::string trimWebText(const std::string& input) {
    // ECMAScript String.trim whitespace, not locale-dependent isspace.
    static const std::string_view whitespace[]={"\t","\n","\v","\f","\r"," ","\xc2\xa0","\xe1\x9a\x80",
      "\xe2\x80\x80","\xe2\x80\x81","\xe2\x80\x82","\xe2\x80\x83","\xe2\x80\x84","\xe2\x80\x85",
      "\xe2\x80\x86","\xe2\x80\x87","\xe2\x80\x88","\xe2\x80\x89","\xe2\x80\x8a","\xe2\x80\xa8",
      "\xe2\x80\xa9","\xe2\x80\xaf","\xe2\x81\x9f","\xe3\x80\x80","\xef\xbb\xbf"};
    std::string_view value(input); bool changed=true;
    while(changed && !value.empty()) { changed=false; for(auto s:whitespace) {
      if(value.size()>=s.size() && value.substr(0,s.size())==s) {value.remove_prefix(s.size());changed=true;break;}
    }}
    changed=true;
    while(changed && !value.empty()) { changed=false; for(auto s:whitespace) {
      if(value.size()>=s.size() && value.substr(value.size()-s.size())==s) {value.remove_suffix(s.size());changed=true;break;}
    }}
    return std::string(value);
}
std::string objectDisplayName(const TerritorialUnit& u) {
    if(u.kind!=UnitKind::Country) return u.name.empty() ? (u.kind==UnitKind::Subunit?"이름 없는 하위단위":"이름 없는 지방") : u.name;
    if(u.nameExplicit && !u.name.empty()) return u.name;
    static const std::map<std::string,std::pair<std::string,std::string>> known={
      {"TUR",{"터키","튀르키예"}},{"ESP",{"스페인","에스파냐"}},
      {"ALD",{"올란드 제도","올란드제도"}},{"FRO",{"페로 제도","페로제도"}},
      {"PCN",{"핏케언 제도","핏케언제도"}},{"MHL",{"마셜 제도","마셜제도"}},
      {"CYM",{"케이맨 제도","케이맨제도"}},{"COK",{"쿡 제도","쿡제도"}},
      {"SLB",{"솔로몬 제도","솔로몬제도"}},{"FLK",{"포클랜드 제도","포클랜드제도"}},
      {"MNP",{"북마리아나 제도","북마리아나제도"}},{"CSI",{"산호해 제도","산호해제도"}}};
    auto name=u.baseName;const auto it=known.find(u.id);
    if(it!=known.end() && name==it->second.first)name=it->second.second;
    return name.empty()?"국가":name;
}
const TerritorialRelation* baseRelation(const ProjectDocument& d,const ObjectRef& ref) {
    for(const auto& r:d.relations)if(!r.dated && r.unit==ref)return &r;
    return nullptr;
}
std::uint32_t effectiveObjectColor(const ProjectDocument& d,const ObjectRef& ref,std::uint32_t countryDefault,std::uint32_t fallback,bool ignoreOwnExplicit) {
    std::map<ObjectRef,const TerritorialUnit*> byId;for(const auto& u:d.units)byId.emplace(territorialRef(u.id),&u);
    std::set<ObjectRef> seen;auto current=byId.find(ref);
    while(current!=byId.end() && seen.insert(current->first).second) {
      const auto& u=*current->second;const auto style=d.presentation.objectStyles.find(current->first);
      if(style!=d.presentation.objectStyles.end() && style->second.explicitColor && !(ignoreOwnExplicit && current->first==ref))return style->second.color;
      if(u.kind==UnitKind::Country)return countryDefault;
      const auto r=baseRelation(d,current->first);if(!r)return fallback;
      auto parent=r->parent?byId.find(*r->parent):byId.end();
      if(parent!=byId.end() && parent->second->kind==UnitKind::Subunit){current=parent;continue;}
      if(parent==byId.end() || parent->second->kind!=UnitKind::Country)parent=r->sovereign?byId.find(*r->sovereign):byId.end();
      if(parent==byId.end() || parent->second->kind!=UnitKind::Country)return fallback;
      const auto s=d.presentation.objectStyles.find(parent->first);
      return s!=d.presentation.objectStyles.end()&&s->second.explicitColor?s->second.color:countryDefault;
    }
    return fallback;
}
std::map<ObjectRef,ObjectPropertyView> objectPropertyViews(const ProjectDocument& d) {
    std::map<ObjectRef,ObjectPropertyView> result;
    for(const auto& u:d.units){auto ref=territorialRef(u.id);result.emplace(ref,ObjectPropertyView{objectDisplayName(u),effectiveObjectColor(d,ref)});}
    for(const auto& v:d.labels) result.emplace(ObjectRef{"label",v.id},ObjectPropertyView{v.name.empty()?v.id:v.name,0x253b50});
    for(const auto& v:d.hydro) result.emplace(ObjectRef{"hydro",v.id},ObjectPropertyView{v.name.empty()?v.id:v.name,v.color});
    for(const auto& v:d.genericFeatures) result.emplace(ObjectRef{"generic",v.id},ObjectPropertyView{v.name.empty()?v.id:v.name,v.color});
    for(const auto& v:d.distributionLayers) result.emplace(ObjectRef{"distributionLayer",v.id},ObjectPropertyView{v.name.empty()?v.id:v.name,v.color});
    for(const auto& v:d.distributionEntries) {
        const auto layer=result.find({"distributionLayer",v.layerId});
        if(layer!=result.end()) result.emplace(ObjectRef{"distributionEntry",v.id},ObjectPropertyView{layer->second.displayName+" · "+v.id,layer->second.effectiveColor});
    }
    return result;
}
}

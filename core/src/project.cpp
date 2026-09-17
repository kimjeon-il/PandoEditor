#include <pandoeditor/project.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <set>
#include <stdexcept>

namespace pandoeditor {
namespace {
int inRing(Point p, const Ring& ring)
{
    bool inside = false;
    for (std::size_t i=0,j=ring.size()-1;i<ring.size();j=i++) {
        const auto a=ring[j], b=ring[i];
        const double cross=(p.x-a.x)*(b.y-a.y)-(p.y-a.y)*(b.x-a.x);
        if (std::abs(cross)<=1e-10 && p.x>=std::min(a.x,b.x)-1e-10 && p.x<=std::max(a.x,b.x)+1e-10 &&
            p.y>=std::min(a.y,b.y)-1e-10 && p.y<=std::max(a.y,b.y)+1e-10) return 2;
        if ((a.y>p.y)!=(b.y>p.y) && p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x) inside=!inside;
    }
    return inside ? 1 : 0;
}
bool contains(Point point, const CountryView& country)
{
    for (const auto& polygon:country.polygons) {
        int outer=inRing(point,polygon[0]);
        if (outer==2) return true;
        if (!outer) continue;
        bool hole=false;
        for (std::size_t i=1;i<polygon.size();++i) {
            int result=inRing(point,polygon[i]);
            if (result==2) return true;
            if (result==1) hole=true;
        }
        if (!hole) return true;
    }
    return false;
}
bool validOpacity(double value) { return std::isfinite(value) && value>=0 && value<=1; }
CountryProperties properties(const CountryView& c) { return {c.name,c.memo,c.color,c.opacity,c.layerId}; }
bool sameLayers(const std::vector<Layer>& a,const std::vector<Layer>& b)
{
    if (a.size()!=b.size()) return false;
    for(std::size_t i=0;i<a.size();++i)
        if (a[i].id!=b[i].id || a[i].name!=b[i].name || a[i].visible!=b[i].visible ||
            a[i].locked!=b[i].locked || a[i].opacity!=b[i].opacity) return false;
    return true;
}
}
bool CountryProperties::operator==(const CountryProperties& b) const
{
    return name==b.name && memo==b.memo && color==b.color && opacity==b.opacity && layerId==b.layerId;
}
std::string Project::normalizeName(const std::string& name)
{
    auto first=std::find_if_not(name.begin(),name.end(),[](unsigned char c){return std::isspace(c);});
    auto last=std::find_if_not(name.rbegin(),name.rend(),[](unsigned char c){return std::isspace(c);}).base();
    return first<last ? std::string(first,last) : std::string();
}
void Project::validate(const std::vector<Country>& countries)
{
    validate(ProjectDocument{countries,{{"countries","국가"}}});
}
void Project::validate(const ProjectDocument& document)
{
    (void)validateDocument(document);
}
void Project::replace(std::vector<Country> countries) { replace(ProjectDocument{std::move(countries),{{"countries","국가"}}}); }
void Project::replace(ProjectDocument document)
{
    Project candidate;
    candidate.document_=std::move(document);
    candidate.index_=validateDocument(candidate.document_);
    candidate.countryViews_=countryViews(candidate.document_);
    for(std::size_t i=0;i<candidate.countryViews_.size();++i)
        candidate.countryIndex_.emplace(candidate.countryViews_[i].id,i);
    candidate.markSaved();
    *this=std::move(candidate);
}
const CountryView* Project::country(const std::string& id) const
{
    auto it=countryIndex_.find(id);
    return it==countryIndex_.end()?nullptr:&countryViews_[it->second];
}
const Layer* Project::layer(const std::string& id) const
{
    auto it=index_.layers.find(id);
    return it==index_.layers.end()?nullptr:&layers()[it->second];
}
bool Project::editable(const std::string& id) const
{
    const auto c=country(id);
    const auto l=c ? layer(c->layerId) : nullptr;
    return l && !l->locked && !c->locked;
}
std::string Project::pick(Point point) const
{
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) return {};
    for(auto it=layers().rbegin();it!=layers().rend();++it) {
        if(!it->visible || it->locked || it->opacity==0) continue;
        for(const auto& c:countries())
            if(!c.locked && c.layerId==it->id && c.opacity>0 && contains(point,c)) return c.id;
    }
    return {};
}
bool Project::changeCountry(const std::string& id,const CountryProperties& next)
{
    const auto c=country(id);
    if(!c || !editable(id) || next.name.empty() || next.color>0xffffff || !validOpacity(next.opacity)) return false;
    const auto destination=layer(next.layerId);
    if(!destination || (c->layerId!=next.layerId && destination->locked)) return false;
    auto before=properties(*c);
    if(before==next) return false;
    const auto allowed=[&](bool changed,const char* effect){return !changed||effectAllowed(document_,territorialRef(id),effect);};
    if(!allowed(before.name!=next.name,"name") || !allowed(before.memo!=next.memo,"notes") ||
       !allowed(before.color!=next.color,"color") || !allowed(before.opacity!=next.opacity,"opacity") ||
       !allowed(before.layerId!=next.layerId,"membership")) return false;
    if(before.layerId!=next.layerId &&
       (!effectAllowed(document_,{"userLayer",before.layerId},"membership") ||
        !effectAllowed(document_,{"userLayer",next.layerId},"membership"))) return false;
    commands_.resize(cursor_); commands_.push_back(CountryChange{id,before,next});
    apply(commands_.back(),true); ++cursor_; return true;
}
void Project::apply(const Command& command,bool forward)
{
    if(auto change=std::get_if<CountryChange>(&command)) {
        auto& value=forward ? change->after : change->before;
        auto ref=territorialRef(change->id);
        auto& unit=document_.units[index_.objects.at(ref)];
        auto& style=document_.presentation.objectStyles.at(ref);
        unit.name=value.name; unit.notes=value.memo;
        style.color=value.color; style.opacity=value.opacity;
        document_.presentation.membership.at(ref)=value.layerId;
    } else {
        const auto& layerChange=std::get<LayersChange>(command);
        document_.presentation.userLayers=forward ? layerChange.after : layerChange.before;
    }
    index_=validateDocument(document_);
}
bool Project::changeLayers(std::vector<Layer> next)
{
    if(sameLayers(layers(),next)) return false;
    for(std::size_t i=0;i<layers().size();++i) {
        const auto& old=layers()[i];
        auto it=std::find_if(next.begin(),next.end(),[&](const auto& l){return l.id==old.id;});
        const ObjectRef ref{"userLayer",old.id};
        auto allow=[&](bool changed,const char* effect){return !changed||effectAllowed(document_,ref,effect);};
        if(it==next.end()) { if(!allow(true,"delete")) return false; continue; }
        if(!allow(old.name!=it->name,"name") || !allow(old.opacity!=it->opacity,"opacity") ||
           !allow(old.visible!=it->visible,"visibility") || !allow(old.locked!=it->locked,"locked") ||
           !allow(static_cast<std::size_t>(it-next.begin())!=i,"order")) return false;
    }
    for(const auto& l:next) if(!layer(l.id)&&!effectAllowed(document_,{"userLayer",l.id},"add")) return false;
    // Validate the entire candidate before touching document or undo history.
    // GeometryStore snapshots share immutable geometry; existing canonical views
    // continue to refer to the original units, styles, and memberships.
    auto candidate=document_;
    candidate.presentation.userLayers=std::move(next);
    DocumentIndex candidateIndex;
    try { candidateIndex=validateDocument(candidate); }
    catch(const std::invalid_argument&) { return false; }
    Command command=LayersChange{layers(),candidate.presentation.userLayers};
    commands_.reserve(cursor_+1);
    commands_.resize(cursor_); commands_.push_back(std::move(command));
    document_.presentation.userLayers.swap(candidate.presentation.userLayers);
    index_=std::move(candidateIndex); ++cursor_; return true;
}
bool Project::setColor(const std::string& id,std::uint32_t color)
{
    auto c=country(id); if(!c) return false;
    auto next=properties(*c); next.color=color; return changeCountry(id,next);
}
bool Project::renameCountry(const std::string& id,const std::string& name)
{
    auto c=country(id); if(!c) return false;
    auto next=properties(*c); next.name=normalizeName(name); return changeCountry(id,next);
}
bool Project::setMemo(const std::string& id,const std::string& memo)
{
    auto c=country(id); if(!c) return false;
    auto next=properties(*c); next.memo=memo; return changeCountry(id,next);
}
bool Project::setCountryOpacity(const std::string& id,double opacity)
{
    auto c=country(id); if(!c) return false;
    auto next=properties(*c); next.opacity=opacity; return changeCountry(id,next);
}
bool Project::moveCountry(const std::string& id,const std::string& layerId)
{
    auto c=country(id); if(!c) return false;
    auto next=properties(*c); next.layerId=layerId; return changeCountry(id,next);
}
bool Project::addLayer(const std::string& id,const std::string& name)
{
    const auto clean=normalizeName(name);
    if(id.empty() || clean.empty() || layer(id)) return false;
    auto next=layers(); next.push_back({id,clean}); return changeLayers(std::move(next));
}
bool Project::removeLayer(const std::string& id)
{
    if(layers().size()<=1 || !layer(id)) return false;
    const auto dependents=index_.dependents.find({"userLayer",id});
    if(dependents!=index_.dependents.end() && !dependents->second.empty()) return false;
    if(!effectAllowed(document_,{"userLayer",id},"delete")) return false;
    auto next=layers();
    next.erase(std::remove_if(next.begin(),next.end(),[&](const auto& l){return l.id==id;}),next.end());
    return changeLayers(std::move(next));
}
bool Project::renameLayer(const std::string& id,const std::string& name)
{
    const auto clean=normalizeName(name); if(clean.empty()) return false;
    auto next=layers(); for(auto& l:next) if(l.id==id) l.name=clean;
    return changeLayers(std::move(next));
}
bool Project::setLayerVisible(const std::string& id,bool visible)
{
    auto next=layers(); for(auto& l:next) if(l.id==id) l.visible=visible;
    return changeLayers(std::move(next));
}
bool Project::setLayerLocked(const std::string& id,bool locked)
{
    auto next=layers(); for(auto& l:next) if(l.id==id) l.locked=locked;
    return changeLayers(std::move(next));
}
bool Project::setLayerOpacity(const std::string& id,double opacity)
{
    if(!validOpacity(opacity)) return false;
    auto next=layers(); for(auto& l:next) if(l.id==id) l.opacity=opacity;
    return changeLayers(std::move(next));
}
bool Project::moveLayer(const std::string& id,int delta)
{
    if(delta!=-1 && delta!=1) return false;
    auto next=layers();
    for(std::size_t i=0;i<next.size();++i) if(next[i].id==id) {
        const auto destination=static_cast<int>(i)+delta;
        if(destination<0 || destination>=static_cast<int>(next.size())) return false;
        std::swap(next[i],next[static_cast<std::size_t>(destination)]);
        return changeLayers(std::move(next));
    }
    return false;
}
bool Project::undo() { if(!canUndo()) return false; apply(commands_[--cursor_],false); return true; }
bool Project::redo() { if(!canRedo()) return false; apply(commands_[cursor_++],true); return true; }
void Project::markSaved()
{
    savedCountries_.clear();
    for(const auto& c:countries()) savedCountries_.push_back(properties(c));
    savedLayers_=layers();
}
bool Project::dirty() const
{
    if(!sameLayers(layers(),savedLayers_) || savedCountries_.size()!=countries().size()) return true;
    for(std::size_t i=0;i<countries().size();++i) if(!(properties(countries()[i])==savedCountries_[i])) return true;
    return false;
}
}

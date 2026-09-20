#include <pandoeditor/project.h>
#include "documentstate.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cctype>
#include <limits>
#include <stdexcept>

namespace pandoeditor {
namespace {
std::string nextInstanceId()
{
    // Process-local session identity, deliberately not persisted in a document.
    static std::atomic<std::uint64_t> counter{0};
    auto value = counter.load(std::memory_order_relaxed);
    do {
        if (value == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("PROJECT_INSTANCE_OVERFLOW");
    } while (!counter.compare_exchange_weak(value, value + 1, std::memory_order_relaxed));
    return "project-session-" + std::to_string(value + 1);
}
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
// The pinned web history restore prunes country overrides. Build a validated
// replacement before touching cursor/revision, retaining immutable geometry.
std::shared_ptr<const detail::DocumentState> restoredHistoryState(
    const std::shared_ptr<const detail::DocumentState>& target)
{
    if(!target->needsHistoryPruning)return target;
    auto document=target->document;
    for(auto& u:document.units)if(u.kind==UnitKind::Country) {
        u.notes=trimWebText(u.notes);
        if(u.name.empty())u.nameExplicit=false;
    }
    return std::make_shared<const detail::DocumentState>(std::move(document));
}
CountryProperties properties(const CountryView& c) { return {c.name,c.memo,c.color,c.opacity,c.layerId}; }
}
Project::Project() : state_(std::make_shared<const detail::DocumentState>()),
                     saved_(state_), instanceId_(nextInstanceId()) {}
std::string Project::normalizeName(const std::string& name)
{
    auto first=std::find_if_not(name.begin(),name.end(),[](unsigned char c){return std::isspace(c);});
    auto last=std::find_if_not(name.rbegin(),name.rend(),[](unsigned char c){return std::isspace(c);}).base();
    return first<last ? std::string(first,last) : std::string();
}
void Project::validate(const std::vector<Country>& countries) { validate(ProjectDocument{countries,{{"countries","국가"}}}); }
void Project::validate(const ProjectDocument& document) { (void)validateDocument(document); }
void Project::replace(std::vector<Country> countries) { replace(ProjectDocument{std::move(countries),{{"countries","국가"}}}); }
void Project::replace(ProjectDocument document)
{
    auto next=std::make_shared<const detail::DocumentState>(std::move(document));
    auto identity=nextInstanceId();
    // No allocation or validation after this point: replace is all-or-nothing.
    state_=std::move(next); saved_=state_; instanceId_.swap(identity);
    commands_.clear(); cursor_=0; revision_=0; presentationRevision_=0; checkpoint_=savedCheckpoint_=checkpointSequence_=0;
}
const ProjectDocument& Project::document() const noexcept { return state_->document; }
const std::vector<CountryView>& Project::countries() const noexcept { return state_->countries; }
const std::vector<Layer>& Project::layers() const noexcept { return state_->document.presentation.userLayers; }
const DocumentIndex& Project::index() const noexcept { return state_->index; }
const CountryView* Project::country(const std::string& id) const
{
    auto it=state_->countryIndex.find(id);
    return it==state_->countryIndex.end()?nullptr:&countries()[it->second];
}
const Layer* Project::layer(const std::string& id) const
{
    auto it=index().layers.find(id);
    return it==index().layers.end()?nullptr:&layers()[it->second];
}
const ObjectPropertyView* Project::propertyView(const ObjectRef& ref) const {
    auto it=state_->properties.find(ref);return it==state_->properties.end()?nullptr:&it->second;
}
bool Project::editable(const std::string& id) const
{
    const auto c=country(id); const auto l=c ? layer(c->layerId) : nullptr;
    return c && (!l || !l->locked) && !c->locked;
}
std::string Project::pick(Point point) const
{
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) return {};
    auto renderLayers=layers();renderLayers.insert(renderLayers.begin(),Layer{"",""});
    for(auto it=renderLayers.rbegin();it!=renderLayers.rend();++it) {
        if(!it->visible || it->locked || it->opacity==0) continue;
        for(const auto& c:countries())
            if(!c.locked && c.layerId==it->id && c.opacity>0 && effectiveMapVisibility(document(),territorialRef(c.id)) && contains(point,c)) return c.id;
    }
    return {};
}
void Project::apply(const ChangeSet& change)
{
    auto rebased=change.after_;
    if(!(document().presentation.webPresentation==change.before_->document.presentation.webPresentation)) {
        auto candidate=change.after_->document;
        candidate.presentation.webPresentation=rebasePresentation(document(),change.before_->document,candidate);
        if(const auto mutation=std::get_if<ApplyTerritorialMutation>(&change.request_.args.action))
            if(const auto conversion=std::get_if<ConvertTerritorialTypeIntent>(&mutation->plan.intent)) {
                const auto& source=document().units.at(index().objects.at(conversion->source));
                const auto id=conversion->targetKind==UnitKind::Country?source.id:conversion->generatedId;
                const auto fromGroup=territorialGroup(source.kind),toGroup=territorialGroup(conversion->targetKind);
                auto& out=candidate.presentation.webPresentation;
                if(itemVisible(document().presentation.webPresentation,fromGroup,source.id))out.hiddenItems[toGroup].erase(id);
                else out.hiddenItems[toGroup].insert(id);
                const auto fromKey=territorialPresentationKey(source.kind,source.id),toKey=territorialPresentationKey(conversion->targetKind,id);
                const auto latest=document().presentation.webPresentation.objectStyles.find(fromKey);
                if(latest!=document().presentation.webPresentation.objectStyles.end())out.objectStyles[toKey]=latest->second;
                normalizePresentation(candidate);
            }
        rebased=std::make_shared<const detail::DocumentState>(std::move(candidate));
    }
    // Stage the entire prospective history, including metadata allocations.
    // Erasing the redo branch before this succeeds would break failure atomicity.
    std::vector<ChangeSet> next;
    next.reserve(cursor_+1);
    next.insert(next.end(),commands_.begin(),commands_.begin()+cursor_);
    next.push_back(change);
    next.back().before_=state_;
    next.back().after_=rebased;
    auto& staged=next.back();staged.checkpointBefore_=checkpoint_;staged.checkpointAfter_=checkpoint_;
    const bool checkpoint=change.historyCheckpoint_;
    if(checkpoint) {
        if(checkpointSequence_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("CHECKPOINT_OVERFLOW");
        staged.checkpointAfter_=checkpointSequence_+1;
    }
    checkpoint_=staged.checkpointAfter_;if(checkpoint)++checkpointSequence_;
    commands_.swap(next);
    state_=std::move(rebased);
    ++presentationRevision_;
    ++cursor_; ++revision_;
}
bool Project::execute(std::string commandId, CommandArguments args)
{
    auto result=CommandProcessor::prepare(*this,CommandProcessor::makeRequest(*this,std::move(commandId),std::move(args)));
    return result.preview && CommandProcessor::confirm(*this,*result.preview).changed();
}
bool Project::changeCountry(const std::string& id,CountryProperties next)
{
    CommandArguments args; args.properties.countries.push_back({id,std::move(next)});
    return execute("edit.properties",std::move(args));
}
bool Project::changeLayer(Layer next)
{
    CommandArguments args; args.properties.layers.push_back(std::move(next));
    return execute("edit.properties",std::move(args));
}
bool Project::setColor(const std::string& id,std::uint32_t color)
{
    CommandArguments args; args.action=SetCountryColor{id,color}; return execute("country.color",std::move(args));
}
bool Project::renameCountry(const std::string& id,const std::string& name)
{
    if(!country(id))return false;
    CommandArguments args;args.action=TerritorialFieldEdit{territorialRef(id),TerritorialField::Name,name};
    return execute("territorial.field",std::move(args));
}
bool Project::setMemo(const std::string& id,const std::string& memo)
{
    if(!country(id))return false;
    CommandArguments args;args.action=TerritorialFieldEdit{territorialRef(id),TerritorialField::Notes,memo};
    return execute("territorial.field",std::move(args));
}
bool Project::setCountryOpacity(const std::string& id,double opacity)
{
    auto c=country(id); if(!c) return false;
    auto next=properties(*c); next.opacity=opacity; return changeCountry(id,std::move(next));
}
bool Project::moveCountry(const std::string& id,const std::string& layerId)
{
    CommandArguments args; args.action=MoveCountry{id,layerId}; return execute("country.move",std::move(args));
}
bool Project::addLayer(const std::string& id,const std::string& name)
{
    CommandArguments args; args.action=AddLayer{id,name}; return execute("layer.add",std::move(args));
}
bool Project::removeLayer(const std::string& id)
{
    CommandArguments args; args.action=RemoveLayer{id}; return execute("layer.remove",std::move(args));
}
bool Project::renameLayer(const std::string& id,const std::string& name)
{
    auto l=layer(id); if(!l) return false; auto next=*l; next.name=name; return changeLayer(std::move(next));
}
bool Project::setLayerVisible(const std::string& id,bool visible)
{
    CommandArguments args; args.action=SetLayerVisible{id,visible}; return execute("layer.visibility",std::move(args));
}
bool Project::setLayerLocked(const std::string& id,bool locked)
{
    CommandArguments args; args.action=SetLayerLocked{id,locked}; return execute("layer.lock",std::move(args));
}
bool Project::setLayerOpacity(const std::string& id,double opacity)
{
    auto l=layer(id); if(!l) return false; auto next=*l; next.opacity=opacity; return changeLayer(std::move(next));
}
bool Project::moveLayer(const std::string& id,int delta)
{
    CommandArguments args; args.action=MoveLayer{id,delta}; return execute("layer.move",std::move(args));
}
bool Project::undo()
{
    if(!canUndo() || revision_==std::numeric_limits<std::uint64_t>::max()) return false;
    const auto& target=commands_[cursor_-1].before_;
    auto next=restoredHistoryState(target);
    if(!(document().presentation.webPresentation==commands_[cursor_-1].after_->document.presentation.webPresentation)) {
        auto candidate=next->document;
        candidate.presentation.webPresentation=rebasePresentation(document(),commands_[cursor_-1].after_->document,candidate);
        next=std::make_shared<const detail::DocumentState>(std::move(candidate));
    }
    const bool savedTarget=target==saved_ || semanticallyEqual(target->document,saved_->document);
    // The saved bytes were already pruned by the codec; preserve that baseline.
    if(savedTarget)saved_=restoredHistoryState(target);
    state_=std::move(next);checkpoint_=commands_[cursor_-1].checkpointBefore_;--cursor_;++revision_;++presentationRevision_;return true;
}
bool Project::redo()
{
    if(!canRedo() || revision_==std::numeric_limits<std::uint64_t>::max()) return false;
    const auto& target=commands_[cursor_].after_;
    auto next=restoredHistoryState(target);
    if(!(document().presentation.webPresentation==commands_[cursor_].before_->document.presentation.webPresentation)) {
        auto candidate=next->document;
        candidate.presentation.webPresentation=rebasePresentation(document(),commands_[cursor_].before_->document,candidate);
        next=std::make_shared<const detail::DocumentState>(std::move(candidate));
    }
    const bool savedTarget=target==saved_ || semanticallyEqual(target->document,saved_->document);
    if(savedTarget)saved_=restoredHistoryState(target);
    state_=std::move(next);checkpoint_=commands_[cursor_].checkpointAfter_;++cursor_;++revision_;++presentationRevision_;return true;
}
void Project::markSaved() noexcept { saved_=state_; savedCheckpoint_=checkpoint_; }
bool Project::dirty() const { return checkpoint_!=savedCheckpoint_ || (state_!=saved_ && !semanticallyEqual(document(),saved_->document)); }
} // namespace pandoeditor

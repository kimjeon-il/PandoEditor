#include <pandoeditor/commands.h>
#include <pandoeditor/project.h>
#include <pandoeditor/objectproperties.h>
#include "documentstate.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace pandoeditor {
namespace {
struct Rejection { CommandError error; const char* detail; };
void require(bool condition,CommandError error,const char* detail)
{
    if(!condition) throw Rejection{error,detail};
}
bool opacity(double value) { return std::isfinite(value) && value>=0 && value<=1; }

std::vector<ObjectRef> targetsFor(const CommandArguments& args)
{
    std::set<ObjectRef> refs;
    for(const auto& edit:args.properties.countries) {
        refs.insert(territorialRef(edit.id));
        refs.insert({"userLayer",edit.properties.layerId});
    }
    for(const auto& layer:args.properties.layers) refs.insert({"userLayer",layer.id});
    for(const auto& field:args.properties.fields)refs.insert(field.target);
    std::visit([&](const auto& action) {
        using T=std::decay_t<decltype(action)>;
        if constexpr(std::is_same_v<T,SetCountryColor> || std::is_same_v<T,MoveCountry>) {
            refs.insert(territorialRef(action.id));
            if constexpr(std::is_same_v<T,MoveCountry>) refs.insert({"userLayer",action.layerId});
        } else if constexpr(std::is_same_v<T,TerritorialFieldEdit>) refs.insert(action.target);
        else if constexpr(std::is_same_v<T,TerritorialColorEdit> || std::is_same_v<T,TerritorialLockEdit>) refs.insert(action.targets.begin(),action.targets.end());
        else if constexpr(!std::is_same_v<T,std::monostate>) refs.insert({"userLayer",action.id});
    },args.action);
    return {refs.begin(),refs.end()};
}
void validateRequest(const ProjectSnapshot& project,const CommandRequest& request)
{
    require(request.projectInstanceId==project.instanceId(),CommandError::ProjectMismatch,"project instance changed");
    require(request.documentId==project.document().documentId,CommandError::DocumentMismatch,"document changed");
    require(request.revision==project.revision(),CommandError::StaleRevision,"revision changed");
    const std::pair<const char*,std::size_t> commands[]={
        {"edit.properties",0},{"country.color",1},{"layer.add",2},{"layer.remove",3},
        {"layer.move",4},{"layer.visibility",5},{"layer.lock",6},{"country.move",7},
        {"territorial.field",8},{"territorial.color",9},{"territorial.color.reset",9},
        {"territorial.batch-color",9},{"territorial.lock",10}};
    auto found=std::find_if(std::begin(commands),std::end(commands),[&](const auto& c){return request.commandId==c.first;});
    require(found!=std::end(commands),CommandError::InvalidCommand,"unknown commandId");
    require(request.args.action.index()==found->second,CommandError::InvalidArguments,"commandId/action mismatch");
    if(request.args.action.index()>=8) {
        require(request.args.properties.countries.empty() && request.args.properties.layers.empty() && request.args.properties.fields.empty(),CommandError::InvalidArguments,"property commands cannot collect unrelated drafts");
        if(const auto field=std::get_if<TerritorialFieldEdit>(&request.args.action)) {
            require(field->field>=TerritorialField::Name && field->field<=TerritorialField::ValidTo,CommandError::InvalidArguments,"unknown field");
        }
        std::visit([&](const auto& action){using T=std::decay_t<decltype(action)>;
            if constexpr(std::is_same_v<T,TerritorialColorEdit> || std::is_same_v<T,TerritorialLockEdit>) {
                std::set<ObjectRef> unique(action.targets.begin(),action.targets.end());
                require(!action.targets.empty() && unique.size()==action.targets.size(),CommandError::InvalidTargets,"empty/duplicate targets");
                if constexpr(std::is_same_v<T,TerritorialColorEdit>) {
                    require(request.commandId=="territorial.batch-color"?action.targets.size()>=2:action.targets.size()==1,CommandError::InvalidTargets,"color selection arity");
                    require(request.commandId=="territorial.color.reset"?!action.color:bool(action.color),CommandError::InvalidArguments,"explicit/reset color contract");
                    require(!action.color || *action.color<=0xffffff,CommandError::InvalidArguments,"invalid RGB");
                }
            }
        },request.args.action);
    }
    std::set<std::pair<ObjectRef,TerritorialField>> fields;
    for(const auto& f:request.args.properties.fields) {
        require(f.target.domain=="territorial" && f.field>=TerritorialField::Name && f.field<=TerritorialField::ValidTo && fields.emplace(f.target,f.field).second,CommandError::InvalidArguments,"invalid/duplicate field edit");
    }
    std::set<std::string> countries,layers;
    for(const auto& edit:request.args.properties.countries) {
        require(countries.insert(edit.id).second,CommandError::InvalidArguments,"duplicate country edit");
        const auto& p=edit.properties;
        require(!Project::normalizeName(p.name).empty() && p.color<=0xffffff && opacity(p.opacity),
                CommandError::InvalidArguments,"invalid country name/color/opacity");
    }
    for(const auto& layer:request.args.properties.layers) {
        require(layers.insert(layer.id).second,CommandError::InvalidArguments,"duplicate layer edit");
        require(!Project::normalizeName(layer.name).empty() && opacity(layer.opacity),
                CommandError::InvalidArguments,"invalid layer name/opacity");
    }
    std::visit([&](const auto& action) {
        using T=std::decay_t<decltype(action)>;
        if constexpr(std::is_same_v<T,SetCountryColor>)
            require(action.color<=0xffffff,CommandError::InvalidArguments,"invalid RGB color");
        else if constexpr(std::is_same_v<T,AddLayer>)
            require(!action.id.empty() && !Project::normalizeName(action.name).empty() && !project.layer(action.id),
                    CommandError::InvalidArguments,"invalid or duplicate new layer");
        else if constexpr(std::is_same_v<T,MoveLayer>)
            require(action.delta==-1 || action.delta==1,CommandError::InvalidArguments,"layer delta must be -1 or 1");
    },request.args.action);
    auto refs=request.targets;
    std::sort(refs.begin(),refs.end());
    require(std::adjacent_find(refs.begin(),refs.end())==refs.end() && refs==targetsFor(request.args),
            CommandError::InvalidTargets,"targets do not match arguments");
    if(request.args.action.index()>=8)for(const auto& ref:refs)require(ref.domain=="territorial",CommandError::InvalidTargets,"non-territorial target");
    const auto* added=std::get_if<AddLayer>(&request.args.action);
    for(const auto& ref:refs) {
        if(ref.domain=="territorial")
            require((request.args.action.index()>=8 || std::any_of(request.args.properties.fields.begin(),request.args.properties.fields.end(),[&](const auto& f){return f.target==ref;}))?project.index().objects.count(ref)!=0:project.country(ref.id)!=nullptr,CommandError::InvalidTargets,"territorial target not found");
        else
            require(ref.domain=="userLayer" && (project.layer(ref.id) || (added && ref.id==added->id)),
                    CommandError::InvalidTargets,"layer target not found");
    }
    // A property edit must refer to an existing layer, not the layer an action
    // will create later in this request.
    for(const auto& layer:request.args.properties.layers)
        require(project.layer(layer.id)!=nullptr,CommandError::InvalidTargets,"layer edit target not found");
}
void applyField(ProjectDocument& candidate,const DocumentIndex& index,const TerritorialFieldEdit& action) {
            auto& u=candidate.units.at(index.objects.at(action.target));
            const auto value=trimWebText(action.value);
            switch(action.field) {
            case TerritorialField::Name:
                if(u.kind!=UnitKind::Country || trimWebText(u.nameExplicit&&!u.name.empty()?u.name:!u.baseName.empty()?u.baseName:u.id)!=value){u.name=value;u.nameExplicit=true;}
                break;
            case TerritorialField::Notes:u.notes=u.kind==UnitKind::Country?action.value:value;break;
            case TerritorialField::ValidFrom:case TerritorialField::ValidTo:
                require(u.kind==UnitKind::Region,CommandError::InvalidArguments,"validity fields are region-only");
                (action.field==TerritorialField::ValidFrom?u.validity.from:u.validity.to)=value.empty()?std::nullopt:std::optional<std::string>(value);
                (void)temporalBounds(u.validity);break;
            }
}
void applyArguments(ProjectDocument& candidate,const DocumentIndex& index,const CommandArguments& args)
{
    for(const auto& edit:args.properties.countries) {
        const auto ref=territorialRef(edit.id);
        auto& u=candidate.units.at(index.objects.at(ref));
        auto& style=candidate.presentation.objectStyles.at(ref);
        if(objectDisplayName(u)!=Project::normalizeName(edit.properties.name)){u.name=Project::normalizeName(edit.properties.name);u.nameExplicit=true;}
        u.notes=edit.properties.memo;
        if(effectiveObjectColor(candidate,ref)!=edit.properties.color){style.color=edit.properties.color;style.explicitColor=true;}
        style.opacity=edit.properties.opacity;
        candidate.presentation.membership.at(ref)=edit.properties.layerId;
    }
    auto& layers=candidate.presentation.userLayers;
    for(auto layer:args.properties.layers) {
        layer.name=Project::normalizeName(layer.name);
        layers.at(index.layers.at(layer.id))=std::move(layer);
    }
    std::visit([&](const auto& action) {
        using T=std::decay_t<decltype(action)>;
        if constexpr(std::is_same_v<T,SetCountryColor>)
            {auto& s=candidate.presentation.objectStyles.at(territorialRef(action.id));s.color=action.color;s.explicitColor=true;}
        else if constexpr(std::is_same_v<T,AddLayer>)
            layers.push_back({action.id,Project::normalizeName(action.name)});
        else if constexpr(std::is_same_v<T,RemoveLayer>)
            layers.erase(layers.begin()+index.layers.at(action.id));
        else if constexpr(std::is_same_v<T,MoveLayer>) {
            const auto from=index.layers.at(action.id);
            if((action.delta==-1 && from>0) || (action.delta==1 && from+1<layers.size())) {
                const auto to=action.delta==-1 ? from-1 : from+1;
                std::swap(layers[from],layers[to]);
            }
        } else if constexpr(std::is_same_v<T,SetLayerVisible>)
            layers.at(index.layers.at(action.id)).visible=action.value;
        else if constexpr(std::is_same_v<T,SetLayerLocked>)
            layers.at(index.layers.at(action.id)).locked=action.value;
        else if constexpr(std::is_same_v<T,MoveCountry>)
            candidate.presentation.membership.at(territorialRef(action.id))=action.layerId;
        else if constexpr(std::is_same_v<T,TerritorialFieldEdit>) {
            applyField(candidate,index,action);
        } else if constexpr(std::is_same_v<T,TerritorialColorEdit>) {
            for(const auto& ref:action.targets){auto& s=candidate.presentation.objectStyles.at(ref);s.explicitColor=action.color.has_value();s.color=action.color.value_or(0);}
        } else if constexpr(std::is_same_v<T,TerritorialLockEdit>) {
            for(const auto& ref:action.targets)candidate.units.at(index.objects.at(ref)).locked=action.locked;
        }
    },args.action);
    for(const auto& field:args.properties.fields)applyField(candidate,index,field);
}
void checkEffects(const ProjectSnapshot& project,const ProjectDocument& after,const CommandRequest& request)
{
    const auto& before=project.document();
    auto allow=[&](bool changed,const ObjectRef& ref,const char* effect) {
        require(!changed || effectAllowed(before,ref,effect),CommandError::UnsupportedDependency,effect);
    };
    // New commands validate their intended effects even for equal-value requests.
    if(request.args.action.index()>=8)for(const auto& ref:request.targets) {
        const auto& u=before.units.at(project.index().objects.at(ref));
        const auto& layer=before.presentation.membership.at(ref);
        require(!project.layer(layer)->locked,CommandError::Locked,"source layer locked");
        const bool unlock=std::holds_alternative<TerritorialLockEdit>(request.args.action);
        require(unlock || request.commandId=="territorial.batch-color" || !u.locked,CommandError::Locked,"object locked");
        const char* effect=unlock?"locked":"color";
        if(const auto f=std::get_if<TerritorialFieldEdit>(&request.args.action))
            effect=f->field==TerritorialField::Name?"name":f->field==TerritorialField::Notes?"notes":"validity";
        allow(true,ref,effect);
    }
    for(std::size_t i=0;i<before.units.size();++i) {
        const auto& old=before.units[i]; const auto& next=after.units[i]; const auto ref=territorialRef(old.id);
        const auto& oldStyle=before.presentation.objectStyles.at(ref);
        const auto& nextStyle=after.presentation.objectStyles.at(ref);
        const auto& source=before.presentation.membership.at(ref);
        const auto& destination=after.presentation.membership.at(ref);
        const bool colorChanged=oldStyle.explicitColor!=nextStyle.explicitColor || (oldStyle.explicitColor&&oldStyle.color!=nextStyle.color);
        const bool dateChanged=old.validity.from!=next.validity.from || old.validity.to!=next.validity.to;
        bool changed=old.name!=next.name || old.nameExplicit!=next.nameExplicit || old.notes!=next.notes || colorChanged || dateChanged ||
                     oldStyle.opacity!=nextStyle.opacity || source!=destination;
        // Check pre-transaction locks; a compound unlock cannot bypass protection.
        require(!changed || ((!old.locked || request.commandId=="territorial.batch-color") && !project.layer(source)->locked),CommandError::Locked,"country/source layer locked");
        if(source!=destination) {
            const auto target=project.layer(destination);
            require(!target || !target->locked,CommandError::Locked,"destination layer locked");
            allow(true,{"userLayer",source},"membership"); allow(true,{"userLayer",destination},"membership");
        }
        allow(old.name!=next.name,ref,"name"); allow(old.notes!=next.notes,ref,"notes");
        allow(colorChanged,ref,"color");allow(dateChanged,ref,"validity");allow(old.locked!=next.locked,ref,"locked"); allow(oldStyle.opacity!=nextStyle.opacity,ref,"opacity");
        allow(source!=destination,ref,"membership");
    }
    const auto& layers=after.presentation.userLayers;
    for(std::size_t i=0;i<project.layers().size();++i) {
        const auto& old=project.layers()[i]; const ObjectRef ref{"userLayer",old.id};
        auto next=std::find_if(layers.begin(),layers.end(),[&](const Layer& layer){return layer.id==old.id;});
        if(next==layers.end()) { allow(true,ref,"delete"); continue; }
        // Layer management remains possible while locked (in particular unlock).
        // Lock protects member objects, as in the pre-M1.3 editor.
        allow(old.name!=next->name,ref,"name"); allow(old.opacity!=next->opacity,ref,"opacity");
        allow(old.visible!=next->visible,ref,"visibility"); allow(old.locked!=next->locked,ref,"locked");
        allow(static_cast<std::size_t>(next-layers.begin())!=i,ref,"order");
    }
    for(const auto& layer:layers) if(!project.layer(layer.id)) allow(true,{"userLayer",layer.id},"add");
}
template<class Range,class Equal> bool same(const Range& a,const Range& b,Equal equal)
{
    return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),equal);
}
bool sameValidity(const Validity& a,const Validity& b) { return a.from==b.from && a.to==b.to; }
bool sameGeometry(const Geometry& a,const Geometry& b)
{
    auto point=[](Point a,Point b){return a.x==b.x && a.y==b.y;};
    auto ring=[&](const Ring& a,const Ring& b){return same(a,b,point);};
    auto polygon=[&](const Polygon& a,const Polygon& b){return same(a,b,ring);};
    return a.type==b.type && same(a.points,b.points,point) && same(a.lines,b.lines,ring) && same(a.polygons,b.polygons,polygon);
}
} // namespace

bool CountryProperties::operator==(const CountryProperties& b) const
{
    return name==b.name && memo==b.memo && color==b.color && opacity==b.opacity && layerId==b.layerId;
}
const char* commandErrorCode(CommandError error) noexcept
{
    switch(error) {
    case CommandError::None: return "NONE";
    case CommandError::ProjectMismatch: return "PROJECT_MISMATCH";
    case CommandError::DocumentMismatch: return "DOCUMENT_MISMATCH";
    case CommandError::StaleRevision: return "STALE_REVISION";
    case CommandError::InvalidCommand: return "INVALID_COMMAND";
    case CommandError::InvalidTargets: return "INVALID_TARGETS";
    case CommandError::InvalidArguments: return "INVALID_ARGUMENTS";
    case CommandError::Locked: return "LOCKED";
    case CommandError::UnsupportedDependency: return "UNSUPPORTED_DEPENDENCY";
    case CommandError::ValidationFailed: return "VALIDATION_FAILED";
    case CommandError::PrepareFailed: return "PREPARE_FAILED";
    case CommandError::CommitFailed: return "COMMIT_FAILED";
    case CommandError::PreviewConsumed: return "PREVIEW_CONSUMED";
    case CommandError::RevisionOverflow: return "REVISION_OVERFLOW";
    }
    return "UNKNOWN_ERROR";
}
ChangeSet::ChangeSet(std::shared_ptr<const detail::DocumentState> before,
                     std::shared_ptr<const detail::DocumentState> after,CommandRequest request)
    : before_(std::move(before)),after_(std::move(after)),request_(std::move(request)) {}
const ProjectDocument& ChangeSet::before() const { return before_->document; }
const ProjectDocument& ChangeSet::after() const { return after_->document; }
const ChangeSet& CommandPreview::change() const
{
    if(!change_) throw std::logic_error("PREVIEW_CONSUMED");
    return *change_;
}
CommandRequest CommandProcessor::makeRequest(const Project& project,std::string commandId,CommandArguments args)
{
    auto refs=targetsFor(args);
    return {std::move(commandId),project.instanceId(),project.document().documentId,project.revision(),std::move(refs),std::move(args)};
}
PrepareResult CommandProcessor::prepare(const ProjectSnapshot& project,const CommandRequest& request)
{
    PrepareResult result;
    try {
        validateRequest(project,request);
        auto candidate=project.document();
        try { applyArguments(candidate,project.index(),request.args); }
        catch(const std::invalid_argument& e){ result.error=CommandError::ValidationFailed;result.detail=e.what();return result; }
        checkEffects(project,candidate,request);
        // Full candidate validation and all derived allocations precede preview.
        std::shared_ptr<const detail::DocumentState> after;
        try { after=std::make_shared<const detail::DocumentState>(std::move(candidate)); }
        catch(const std::invalid_argument& e) {
            result.error=CommandError::ValidationFailed; result.detail=e.what(); return result;
        }
        bool checkpoint=request.commandId=="territorial.batch-color";
        if(const auto field=std::get_if<TerritorialFieldEdit>(&request.args.action)) {
            const auto& unit=project.document().units.at(project.index().objects.at(field->target));
            // Clearing a displayed fallback name again invokes the pinned web's
            // country-metadata command even when the stored empty override agrees.
            checkpoint=unit.kind==UnitKind::Country && field->field==TerritorialField::Name &&
                unit.nameExplicit && unit.name.empty() && trimWebText(field->value).empty();
        }
        if(!checkpoint && semanticallyEqual(project.document(),after->document)) {
            result.status=CommandStatus::NoOp; return result;
        }
        auto change=std::unique_ptr<ChangeSet>(new ChangeSet(project.state_,std::move(after),request));
        change->historyCheckpoint_=checkpoint;
        result.preview=CommandPreview(std::move(change)); result.status=CommandStatus::Prepared;
    } catch(const Rejection& e) {
        result.error=e.error;
        // Diagnostic allocation must not turn a handled failure into mutation.
        try { result.detail=e.detail; } catch(const std::bad_alloc&) {}
    } catch(const std::exception&) {
        result.error=CommandError::PrepareFailed; result.detail.clear();
    }
    return result;
}
PrepareResult CommandProcessor::prepare(const Project& project,const CommandRequest& request)
{
    try { return prepare(project.snapshot(),request); }
    catch(const std::exception&) { PrepareResult r; r.error=CommandError::PrepareFailed; return r; }
}
CommandResult CommandProcessor::confirm(Project& project,CommandPreview& preview)
{
    // Every attempt consumes the token, including stale, foreign or failed commits.
    auto change=std::move(preview.change_);
    if(!change) return {CommandStatus::Rejected,CommandError::PreviewConsumed,{}};
    const auto& request=change->request_;
    if(request.projectInstanceId!=project.instanceId()) return {CommandStatus::Rejected,CommandError::ProjectMismatch,{}};
    if(request.documentId!=project.document().documentId) return {CommandStatus::Rejected,CommandError::DocumentMismatch,{}};
    if(request.revision!=project.revision() || change->before_!=project.state_)
        return {CommandStatus::Rejected,CommandError::StaleRevision,{}};
    if(project.revision()==std::numeric_limits<std::uint64_t>::max())
        return {CommandStatus::Rejected,CommandError::RevisionOverflow,{}};
    try { project.apply(*change); }
    catch(const std::exception&) { return {CommandStatus::Rejected,CommandError::CommitFailed,{}}; }
    return {CommandStatus::Applied,CommandError::None,{}};
}
bool semanticallyEqual(const ProjectDocument& a,const ProjectDocument& b)
{
    if(&a==&b) return true;
    return a.documentId==b.documentId &&
        same(a.units,b.units,[](const auto& x,const auto& y) {
            return x.id==y.id && x.name==y.name && x.baseName==y.baseName && x.nameExplicit==y.nameExplicit && x.notes==y.notes && x.kind==y.kind &&
                x.geometry==y.geometry && x.locked==y.locked && sameValidity(x.validity,y.validity) && x.coverageMode==y.coverageMode;
        }) && same(a.relations,b.relations,[](const auto& x,const auto& y) {
            return x.id==y.id && x.unit==y.unit && x.parent==y.parent && x.sovereign==y.sovereign &&
                x.dated==y.dated && sameValidity(x.validity,y.validity);
        }) && same(a.geometries.versions(),b.geometries.versions(),[](const auto& x,const auto& y) {
            return x.first==y.first && (x.second==y.second || sameGeometry(*x.second,*y.second));
        }) && same(a.presentation.userLayers,b.presentation.userLayers,[](const Layer& x,const Layer& y) {
            return x.id==y.id && x.name==y.name && x.visible==y.visible && x.locked==y.locked && x.opacity==y.opacity;
        }) && a.presentation.membership==b.presentation.membership &&
        same(a.presentation.objectStyles,b.presentation.objectStyles,[](const auto& x,const auto& y) {
            return x.first==y.first && x.second.explicitColor==y.second.explicitColor && (!x.second.explicitColor || x.second.color==y.second.color) && x.second.opacity==y.second.opacity;
        }) && same(a.extensions,b.extensions,[](const PreservedExtension& x,const PreservedExtension& y) {
            return x.id==y.id && x.sourceFormat==y.sourceFormat && x.sourceSchema==y.sourceSchema &&
                x.jsonPointer==y.jsonPointer && x.payload==y.payload && x.status==y.status &&
                x.dependencyKnowledge==y.dependencyKnowledge && x.dependencies==y.dependencies &&
                x.forbiddenEffects==y.forbiddenEffects && x.envelopeExtras==y.envelopeExtras;
        });
}
} // namespace pandoeditor

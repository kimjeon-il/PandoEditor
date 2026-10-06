#include <pandoeditor/commands.h>
#include <pandoeditor/project.h>
#include <pandoeditor/objectproperties.h>
#include <pandoeditor/geometrypredicates.h>
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
        if(!edit.properties.layerId.empty())refs.insert({"userLayer",edit.properties.layerId});
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
        else if constexpr(std::is_same_v<T,ApplyTerritorialMutation>) refs.insert(action.plan.affectedObjects.begin(),action.plan.affectedObjects.end());
        else if constexpr(std::is_same_v<T,HistoricalInstantiationPlan>) {
            for(const auto& addition:action.additions) {
                refs.insert(territorialRef(addition.selection.libraryId));
                if(addition.parent)refs.insert(*addition.parent);
                if(addition.sovereign)refs.insert(*addition.sovereign);
            }
            for(const auto& patch:action.territoryReplacements)refs.insert(patch.owner);
            for(const auto& [donor,target]:action.territoryTransfers)refs.insert(donor);
            for(const auto& [id,name]:action.countryNameUpdates)refs.insert(territorialRef(id));
        }
        else if constexpr(std::is_same_v<T,GisGenericImportPlan>)
            for(const auto& feature:action.features)refs.insert({"generic",feature.id});
        else if constexpr(std::is_same_v<T,GisTerritorialImportPlan>) {
            for(const auto& unit:action.units) {
                refs.insert(territorialRef(unit.id));
                if(unit.parent)refs.insert(*unit.parent);
                if(unit.sovereign)refs.insert(*unit.sovereign);
            }
            for(const auto& patch:action.countryReplacements)refs.insert(patch.owner);
        }
        else if constexpr(std::is_same_v<T,GisDistributionImportPlan>) {
            for(const auto& layer:action.layers)refs.insert({"distributionLayer",layer.id});
            for(const auto& row:action.entries) {
                refs.insert({"distributionEntry",row.entry.id});
                refs.insert({"distributionLayer",row.entry.layerId});
                if(row.entry.territory)refs.insert(*row.entry.territory);
            }
        }
        else if constexpr(std::is_same_v<T,ContentEdit>) refs.insert(action.target);
        else if constexpr(std::is_same_v<T,SetPhysicalData>) {}
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
        {"territorial.batch-color",9},{"territorial.lock",10},{"territorial.create",11},
        {"territorial.relation.parent",11},{"territorial.relation.sovereign",11},
        {"territorial.delete",11},{"territorial.geometry.commit",11},{"territorial.geometry.replace",11},
        {"content.edit",12},{"physical-data.configure",13},{"historical.instantiate",14},
        {"gis.import.generic",15},{"gis.import.territorial",16},
        {"gis.import.distribution",17}};
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
            else if constexpr(std::is_same_v<T,ApplyTerritorialMutation>) {
                require(action.plan.projectInstanceId==project.instanceId()&&action.plan.documentId==project.document().documentId&&action.plan.baseRevision==project.revision(),CommandError::StaleRevision,"territorial plan stale");
                require(!action.plan.affectedObjects.empty(),CommandError::InvalidTargets,"empty territorial plan");
            } else if constexpr(std::is_same_v<T,HistoricalInstantiationPlan>) {
                require(action.projectInstanceId==project.instanceId(),CommandError::ProjectMismatch,"historical plan project changed");
                require(action.documentId==project.document().documentId,CommandError::DocumentMismatch,"historical plan document changed");
                require(action.baseRevision==project.revision(),CommandError::StaleRevision,"historical plan stale");
                require(!action.additions.empty(),CommandError::InvalidTargets,"empty historical plan");
                std::set<std::string> unique;
                bool replacementMode=false;
                std::map<std::string,std::string> expectedNames;
                for(const auto& addition:action.additions) {
                    const auto& selection=addition.selection;
                    require(!selection.libraryId.empty()&&unique.insert(selection.libraryId).second,
                            CommandError::InvalidTargets,"duplicate historical selection");
                    require(selection.instantiation.mode=="independent"||selection.instantiation.mode=="territory-replacement",CommandError::InvalidArguments,"invalid historical mode");
                    replacementMode|=selection.instantiation.mode=="territory-replacement";
                    for(const auto& [id,name]:selection.instantiation.countryNameUpdates) {
                        const auto found=expectedNames.find(id);
                        require(found==expectedNames.end()||found->second==name,CommandError::InvalidArguments,"conflicting country updates");
                        expectedNames[id]=name;
                    }
                    require(!selection.partial||addition.partialApproved,CommandError::InvalidArguments,
                            "partial historical source not approved");
                    require(!addition.sovereign,CommandError::InvalidArguments,"UNSUPPORTED_POLITICAL_RELATION");
                    require(!addition.asIndependentCountry||(selection.type==UnitKind::General&&!addition.parent&&!addition.sovereign&&!addition.countryName.empty()),
                            CommandError::InvalidArguments,"invalid independent country choice");
                }
                require(action.countryNameUpdates==expectedNames,CommandError::InvalidArguments,"country updates differ from library");
                require(replacementMode||(action.territoryReplacements.empty()&&action.territoryTransfers.empty()),CommandError::InvalidArguments,"unexpected historical geometry patch");
                for(const auto& patch:action.territoryReplacements)
                    require(patch.owner.domain=="territorial" && project.index().objects.count(patch.owner) &&
                            (patch.geometry.type=="Polygon"||patch.geometry.type=="MultiPolygon"),
                            CommandError::InvalidArguments,"invalid historical geometry patch");
                for(const auto& [donor,target]:action.territoryTransfers) {
                    const auto found=project.index().objects.find(donor);
                    require(found!=project.index().objects.end()&&donor.domain=="territorial"&&
                            project.document().units.at(found->second).kind==UnitKind::General&&
                            std::any_of(action.additions.begin(),action.additions.end(),[&](const auto& item){
                                return territorialRef(item.selection.libraryId)==target&&
                                    item.selection.instantiation.mode=="territory-replacement";
                            }),CommandError::InvalidArguments,"invalid historical country transfer");
                }
                for(const auto& [id,name]:action.countryNameUpdates)
                    require(!name.empty()&&project.index().objects.count(territorialRef(id))&&
                            project.document().units.at(project.index().objects.at(territorialRef(id))).kind==UnitKind::General,
                            CommandError::InvalidArguments,"invalid country name update");
            } else if constexpr(std::is_same_v<T,GisGenericImportPlan>) {
                require(action.info.projectInstanceId==project.instanceId(),CommandError::ProjectMismatch,"GIS plan project changed");
                require(action.info.documentId==project.document().documentId,CommandError::DocumentMismatch,"GIS plan document changed");
                require(action.info.revision==project.revision(),CommandError::StaleRevision,"GIS plan stale");
                require(action.info.version==1 && !action.info.id.empty() &&
                        action.info.kind==GisImportKind::Generic && action.info.target==GisExchangeTarget::Generic &&
                        !action.features.empty() && action.features.size()==action.info.affectedIds.size(),
                        CommandError::InvalidArguments,"invalid generic GIS plan");
                std::set<std::string> unique;
                for(std::size_t i=0;i<action.features.size();++i)
                    require(!action.features[i].id.empty() &&
                            action.features[i].id==action.info.affectedIds[i] &&
                            unique.insert(action.features[i].id).second,
                            CommandError::InvalidTargets,"duplicate generic GIS target");
            } else if constexpr(std::is_same_v<T,GisTerritorialImportPlan>) {
                require(action.info.projectInstanceId==project.instanceId(),CommandError::ProjectMismatch,"GIS plan project changed");
                require(action.info.documentId==project.document().documentId,CommandError::DocumentMismatch,"GIS plan document changed");
                require(action.info.revision==project.revision(),CommandError::StaleRevision,"GIS plan stale");
                require(action.info.version==1&&!action.info.id.empty()&&!action.units.empty()&&
                    action.info.affectedIds.size()==action.units.size(),CommandError::InvalidArguments,
                    "invalid territorial GIS plan");
                std::set<std::string> unique;
                for(std::size_t i=0;i<action.units.size();++i)
                    require(action.info.affectedIds[i]==action.units[i].id&&
                        unique.insert(action.units[i].id).second,CommandError::InvalidTargets,
                        "territorial GIS IDs changed");
            } else if constexpr(std::is_same_v<T,GisDistributionImportPlan>) {
                require(action.info.projectInstanceId==project.instanceId(),CommandError::ProjectMismatch,"GIS plan project changed");
                require(action.info.documentId==project.document().documentId,CommandError::DocumentMismatch,"GIS plan document changed");
                require(action.info.revision==project.revision(),CommandError::StaleRevision,"GIS plan stale");
                require(action.info.version==1&&!action.info.id.empty()&&
                    action.info.kind==GisImportKind::Distribution&&
                    action.info.target==GisExchangeTarget::Distribution&&
                    !action.entries.empty()&&action.entries.size()==action.info.affectedIds.size(),
                    CommandError::InvalidArguments,"invalid distribution GIS plan");
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
    if(request.args.action.index()>=8 && request.args.action.index()<12)for(const auto& ref:refs)require(ref.domain=="territorial",CommandError::InvalidTargets,"non-territorial target");
    const auto* added=std::get_if<AddLayer>(&request.args.action);
    for(const auto& ref:refs) {
        if(std::holds_alternative<ContentEdit>(request.args.action)) {
            require(!ref.id.empty(),CommandError::InvalidTargets,"empty content target");
            continue; // The typed payload and target are checked together below.
        }
        if(const auto historical=std::get_if<HistoricalInstantiationPlan>(&request.args.action)) {
            const auto created=std::any_of(historical->additions.begin(),historical->additions.end(),
                [&](const auto& item){return territorialRef(item.selection.libraryId)==ref;});
            require(ref.domain=="territorial" && (created? !project.index().objects.count(ref)
                :project.index().objects.count(ref)!=0),CommandError::InvalidTargets,"historical target invalid");
            continue;
        }
        if(std::holds_alternative<GisGenericImportPlan>(request.args.action)) {
            require(ref.domain=="generic" && !project.index().objects.count(ref),
                    CommandError::InvalidTargets,"generic GIS target exists");
            continue;
        }
        if(const auto gis=std::get_if<GisTerritorialImportPlan>(&request.args.action)) {
            const auto imported=std::find_if(gis->units.begin(),gis->units.end(),
                [&](const auto& row){return territorialRef(row.id)==ref;});
            require(ref.domain=="territorial"&&
                (imported==gis->units.end()||imported->replaceExisting||!project.index().objects.count(ref))&&
                (imported!=gis->units.end()||project.index().objects.count(ref)),
                CommandError::InvalidTargets,"territorial GIS target invalid");
            continue;
        }
        if(const auto gis=std::get_if<GisDistributionImportPlan>(&request.args.action)) {
            const bool newLayer=std::any_of(gis->layers.begin(),gis->layers.end(),
                [&](const auto& row){return ref.domain=="distributionLayer"&&row.id==ref.id;});
            const bool newEntry=std::any_of(gis->entries.begin(),gis->entries.end(),
                [&](const auto& row){return ref.domain=="distributionEntry"&&row.entry.id==ref.id;});
            require((newLayer||newEntry)?!project.index().objects.count(ref):
                project.index().objects.count(ref)!=0,CommandError::InvalidTargets,
                "distribution GIS target invalid");
            continue;
        }
        if(ref.domain=="territorial") {
            const auto create=std::get_if<ApplyTerritorialMutation>(&request.args.action);
            const bool newCreate=create && (create->plan.kind==TerritorialMutationKind::CreateCountry||create->plan.kind==TerritorialMutationKind::CreateSubunit||create->plan.kind==TerritorialMutationKind::CreateRegion) && ref==create->plan.targets.front();
            require(newCreate || ((request.args.action.index()>=8 || std::any_of(request.args.properties.fields.begin(),request.args.properties.fields.end(),[&](const auto& f){return f.target==ref;}))?project.index().objects.count(ref)!=0:project.country(ref.id)!=nullptr),CommandError::InvalidTargets,"territorial target not found");
        }
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
                if(u.kind!=UnitKind::General || trimWebText(u.nameExplicit&&!u.name.empty()?u.name:!u.baseName.empty()?u.baseName:u.id)!=value){u.name=value;u.nameExplicit=true;}
                break;
            case TerritorialField::Notes:u.notes=u.kind==UnitKind::General?action.value:value;break;
            case TerritorialField::ValidFrom:case TerritorialField::ValidTo:
                require(value.empty(),CommandError::InvalidArguments,"TIMELINE_ACTIVATION: dated edits require T4");
                (void)staticLifetime(candidate,u.id);break;
            }
}
bool sameGeometry(const Geometry& a,const Geometry& b);
bool sameValidity(const Validity& a,const Validity& b);
void applyGeometryPatch(ProjectDocument& d,const TerritorialMutationPlan& plan,const GeometryPatch& patch) {
    if(patch.sourceRevision!=plan.baseRevision) throw Rejection{CommandError::StaleRevision,"geometry patch stale"};
    const auto& mappings=plan.geometry.replacements;
    std::set<ObjectRef> expected, seen;
    for(const auto& mapping:mappings)require(expected.insert(mapping.result).second,CommandError::InvalidArguments,"duplicate planned owner");
    if(expected.empty()) throw Rejection{CommandError::InvalidArguments,"geometry patch has no owners"};
    require(patch.creations.size()==plan.geometry.createOwners.size(),CommandError::InvalidArguments,"geometry creation owner set mismatch");
    std::set<ObjectRef> created;
    for(const auto& creation:patch.creations) {
        require(std::find(plan.geometry.createOwners.begin(),plan.geometry.createOwners.end(),creation.owner)!=plan.geometry.createOwners.end()&&created.insert(creation.owner).second,CommandError::InvalidArguments,"unexpected geometry creation owner");
        require(std::none_of(d.units.begin(),d.units.end(),[&](const auto& u){return territorialRef(u.id)==creation.owner;}),CommandError::InvalidArguments,"geometry creation owner already exists");
        require(creation.geometry.type=="Polygon"||creation.geometry.type=="MultiPolygon",CommandError::InvalidArguments,"polygon required");
    }
    for(const auto& replacement:patch.replacements) {
        if(!expected.count(replacement.owner)||!seen.insert(replacement.owner).second)
            throw Rejection{CommandError::InvalidArguments,"geometry patch owner mismatch"};
    }
    for(const auto& owner:patch.removedGeometryOwners) {
        if(!expected.count(owner)||!seen.insert(owner).second||
           std::find(plan.geometry.removableOwners.begin(),plan.geometry.removableOwners.end(),owner)==plan.geometry.removableOwners.end())
            throw Rejection{CommandError::InvalidArguments,"geometry patch removed owner mismatch"};
        const auto removed=std::find_if(d.units.begin(),d.units.end(),[&](const auto& u){return territorialRef(u.id)==owner;});
        const auto targetPatch=std::find_if(patch.replacements.begin(),patch.replacements.end(),[&](const auto& r){return !plan.geometry.replacements.empty()&&r.owner==plan.geometry.replacements.front().result;});
        const auto target=std::find_if(d.units.begin(),d.units.end(),[&](const auto& u){return !plan.geometry.readOwners.empty()&&territorialRef(u.id)==plan.geometry.readOwners.front();});
        require(plan.geometry.operation=="coast"||(target!=d.units.end()&&removed!=d.units.end()&&targetPatch!=patch.replacements.end()&&geometryContains(targetPatch->geometry,*d.geometries.get(staticGeometryBinding(d,removed->id).geometryRef))),CommandError::InvalidArguments,"removed owner is not covered by result");
    }
    if(seen!=expected) throw Rejection{CommandError::InvalidArguments,"geometry patch owner set incomplete"};
    for(const auto& replacement:patch.replacements) {
        const auto mapping=std::find_if(mappings.begin(),mappings.end(),[&](const auto& m){return m.result==replacement.owner;});
        require(mapping!=mappings.end(),CommandError::InvalidArguments,"geometry owner mapping missing");
        const auto& input=mapping->source;
        auto unit=std::find_if(d.units.begin(),d.units.end(),[&](const auto& u){return territorialRef(u.id)==input;});
        if(unit==d.units.end()) throw Rejection{CommandError::InvalidArguments,"geometry patch owner missing"};
        require(replacement.geometry.type=="Polygon"||replacement.geometry.type=="MultiPolygon",CommandError::InvalidArguments,"polygon required");
        if(sameGeometry(*d.geometries.get(staticGeometryBinding(d,unit->id).geometryRef),replacement.geometry))continue;
        auto& binding=staticGeometryBinding(d,unit->id); GeometryRef next=binding.geometryRef;
        do {require(next.version<std::numeric_limits<std::uint32_t>::max(),CommandError::RevisionOverflow,"geometry version overflow");++next.version;}while(d.geometries.get(next));
        d.geometries.insert(next,replacement.geometry); binding.geometryRef=next;
    }
    for(const auto& ref:patch.removedGeometryOwners) {
        d.presentation.membership.erase(ref);d.presentation.objectStyles.erase(ref);
        d.units.erase(std::remove_if(d.units.begin(),d.units.end(),[&](const auto& u){return territorialRef(u.id)==ref;}),d.units.end());
    }
}
void applyTerritorial(ProjectDocument& d,const ApplyTerritorialMutation& action) {
    const auto& plan=action.plan;
    const auto contentBefore=d;
    auto shape=[&](const std::string& id){return d.geometries.get(staticGeometryBinding(d,id).geometryRef);};
    auto patch=[&](){require(action.geometry.has_value(),CommandError::InvalidArguments,"geometry patch required");applyGeometryPatch(d,plan,*action.geometry);};
    std::visit([&](const auto& in) {
        using T=std::decay_t<decltype(in)>;
        if constexpr(std::is_same_v<T,ChangeParentIntent>) {
            staticParentRelation(d,in.target.id).parentId=in.parent.id;
        } else if constexpr(std::is_same_v<T,ChangeRegionSovereignIntent>) {
            throw Rejection{CommandError::InvalidArguments,"UNSUPPORTED_POLITICAL_RELATION: sovereignty is not an administrative parent"};
        } else if constexpr(std::is_same_v<T,CreateTerritorialIntent>) {
            require(!in.sovereign,CommandError::InvalidArguments,"UNSUPPORTED_POLITICAL_RELATION");
            require(!in.validity.from&&!in.validity.to,CommandError::InvalidArguments,"TIMELINE_ACTIVATION: dated creation requires T4");
            GeometryRef geometry{"geometry-"+in.id,1};unsigned n=1;while(d.geometries.get(geometry))geometry={"geometry-"+in.id+"-"+std::to_string(n++),1};d.geometries.insert(geometry,in.geometry);
            TerritorialUnit unit;unit.id=in.id;unit.name=in.name;unit.baseName=in.kind==UnitKind::General?in.name:"";unit.nameExplicit=true;unit.notes=in.notes;unit.kind=in.kind;d.units.push_back(unit);
            addStaticTerritorialRecords(d,in.id,geometry,in.parent?in.parent->id:"",in.coverageMode.empty()?"explicit":in.coverageMode);
            const auto ref=territorialRef(in.id);if(!d.presentation.userLayers.empty())d.presentation.membership.emplace(ref,d.presentation.userLayers.front().id);d.presentation.objectStyles.emplace(ref,ObjectStyle{in.explicitColor.value_or(0),1,in.explicitColor.has_value()});
        } else if constexpr(std::is_same_v<T,DeleteTerritorialIntent>) {
            std::set<ObjectRef> removed(in.targets.begin(),in.targets.end());std::vector<std::string> ids;
            for(const auto& ref:removed){ids.push_back(ref.id);d.presentation.membership.erase(ref);d.presentation.objectStyles.erase(ref);}
            removeTerritorialRecords(d,ids);d.units.erase(std::remove_if(d.units.begin(),d.units.end(),[&](const auto& u){return removed.count(territorialRef(u.id));}),d.units.end());
        } else if constexpr(std::is_same_v<T,TransferSubunitIntent>) {
            patch();staticParentRelation(d,in.target.id).parentId=in.destinationCountry.id;
        } else if constexpr(std::is_same_v<T,ConvertTerritorialTypeIntent>) {
            require(!in.sovereign,CommandError::InvalidArguments,"UNSUPPORTED_POLITICAL_RELATION");
            auto unit=std::find_if(d.units.begin(),d.units.end(),[&](const auto& u){return territorialRef(u.id)==in.source;});
            require(unit!=d.units.end()&&unit->kind==in.targetKind,CommandError::InvalidArguments,"kind changes require a new entity identity");
            const auto fromGroup=territorialGroup(d,unit->id);
            if(action.geometry)patch();
            auto& parent=staticParentRelation(d,in.source.id);parent.parentId=in.parent?in.parent->id:"";parent.coverageMode="explicit";
            const auto toGroup=territorialGroup(d,in.source.id);auto& web=d.presentation.webPresentation;
            if(web.hiddenItems[fromGroup].erase(in.source.id))web.hiddenItems[toGroup].insert(in.source.id);
        } else if constexpr(std::is_same_v<T,ReplaceGeometryIntent>||std::is_same_v<T,CoastlineIntent>) {
            patch();
        } else if constexpr(std::is_same_v<T,MergeTerritorialIntent>) {
            patch();const std::set<ObjectRef> donors(in.donors.begin(),in.donors.end());
            for(auto& relation:d.timelineRecords.parentRelations)if(donors.count(territorialRef(relation.parentId)))relation.parentId=in.target.id;
            auto& web=d.presentation.webPresentation;
            for(const auto& donor:in.donors) {
                for(auto& [group,hidden]:web.hiddenItems)hidden.erase(donor.id);
                web.objectStyles.erase(territorialPresentationKey(donor.id));
                web.objectOrder.erase(std::remove(web.objectOrder.begin(),web.objectOrder.end(),territorialPresentationKey(donor.id)),web.objectOrder.end());
            }
        } else if constexpr(std::is_same_v<T,AnnexTerritoryIntent>) {
            patch();const std::set<ObjectRef> donors(in.donors.begin(),in.donors.end());
            for(auto& relation:d.timelineRecords.parentRelations)if(donors.count(territorialRef(relation.parentId))) {
                const auto child=std::find_if(d.units.begin(),d.units.end(),[&](const auto& u){return u.id==relation.entityId;});
                if(child!=d.units.end()&&geometryContains(*shape(in.target.id),*shape(child->id)))relation.parentId=in.target.id;
            }
        } else if constexpr(std::is_same_v<T,SplitTerritorialIntent>) {
            require(action.geometry&&action.geometry->creations.size()==1,CommandError::InvalidArguments,"split geometry patch required");
            const auto source=std::find_if(d.units.begin(),d.units.end(),[&](const auto& u){return territorialRef(u.id)==in.source;});
            require(source!=d.units.end(),CommandError::ValidationFailed,"split source missing");
            auto createdUnit=*source;const auto parent=staticParentRelation(d,in.source.id);
            patch();GeometryRef geometry{"geometry-"+in.createdId,1};unsigned suffix=1;while(d.geometries.get(geometry))geometry={"geometry-"+in.createdId+"-"+std::to_string(suffix++),1};d.geometries.insert(geometry,action.geometry->creations.front().geometry);
            createdUnit.id=in.createdId;createdUnit.name=in.createdName;createdUnit.nameExplicit=true;d.units.push_back(createdUnit);
            addStaticTerritorialRecords(d,in.createdId,geometry,parent.parentId,parent.coverageMode);const auto createdRef=territorialRef(in.createdId);
            if(auto membership=d.presentation.membership.find(in.source);membership!=d.presentation.membership.end())d.presentation.membership[createdRef]=membership->second;
            if(auto style=d.presentation.objectStyles.find(in.source);style!=d.presentation.objectStyles.end())d.presentation.objectStyles[createdRef]=style->second;
        } else if constexpr(std::is_same_v<T,SharedBoundaryIntent>) {
            patch();std::set<std::string> owners;for(const auto& draft:in.drafts)owners.insert(draft.owner.id);
            for(auto& relation:d.timelineRecords.parentRelations)if(owners.count(relation.parentId)){
                const auto child=std::find_if(d.units.begin(),d.units.end(),[&](const auto& u){return u.id==relation.entityId;});if(child==d.units.end())continue;
                for(const auto& owner:owners)if(geometryContains(*shape(owner),*shape(child->id))){relation.parentId=owner;break;}
            }
        }
    },plan.intent);
    std::map<ObjectRef,ObjectRef> redirected;
    if(const auto merge=std::get_if<MergeTerritorialIntent>(&plan.intent))for(const auto& ref:merge->donors)redirected[ref]=merge->target;

    auto survives=[&](const ObjectRef& ref){return std::any_of(d.units.begin(),d.units.end(),[&](const auto& u){return territorialRef(u.id)==ref;});};
    for(auto& label:d.labels)if(label.territory&&!survives(*label.territory)) {
        require(effectAllowed(contentBefore,{"label",label.id},"relation"),CommandError::UnsupportedDependency,"label territorial dependency");
        const auto replacement=redirected.find(*label.territory);
        label.territory=replacement==redirected.end()?std::nullopt:std::optional<ObjectRef>(replacement->second);
    }
    for(auto it=d.distributionEntries.begin();it!=d.distributionEntries.end();) {
        if(!it->territory||survives(*it->territory)){++it;continue;}
        const auto ref=ObjectRef{"distributionEntry",it->id};const auto replacement=redirected.find(*it->territory);
        const auto layer=std::find_if(d.distributionLayers.begin(),d.distributionLayers.end(),[&](const auto& l){return l.id==it->layerId;});
        require(layer!=d.distributionLayers.end()&&!layer->locked,CommandError::Locked,"distribution reference locked");
        require(effectAllowed(contentBefore,ref,replacement==redirected.end()?"delete":"relation"),CommandError::UnsupportedDependency,"distribution territorial dependency");
        if(replacement!=redirected.end()){it->territory=replacement->second;++it;}
        else {d.presentation.membership.erase(ref);d.presentation.objectStyles.erase(ref);it=d.distributionEntries.erase(it);}
    }
    for(auto it=d.symbols.begin();it!=d.symbols.end();) {
        if(survives(it->first)){++it;continue;}const auto replacement=redirected.find(it->first);
        if(replacement!=redirected.end()) {
            require(!d.symbols.count(replacement->second),CommandError::UnsupportedDependency,"conflicting territorial symbols");
            d.symbols.emplace(replacement->second,it->second);
        }
        it=d.symbols.erase(it);
    }
    for(auto it=d.countryDetails.begin();it!=d.countryDetails.end();) {
        const auto unit=std::find_if(d.units.begin(),d.units.end(),[&](const auto& u){return territorialRef(u.id)==it->first;});
        if(unit!=d.units.end()){++it;continue;}
        require(it->second.capital.empty()||!redirected.count(it->first),CommandError::UnsupportedDependency,"capital requires explicit resolution before merge/conversion");
        it=d.countryDetails.erase(it);
    }
    if(action.geometry){std::vector<std::string> removed;for(const auto& ref:action.geometry->removedGeometryOwners)removed.push_back(ref.id);removeTerritorialRecords(d,removed);}
    if(action.geometry)for(const auto& r:d.timelineRecords.parentRelations)if(!r.parentId.empty()) {
        const auto child=std::find_if(d.units.begin(),d.units.end(),[&](const auto& u){return u.id==r.entityId;});
        const auto parent=std::find_if(d.units.begin(),d.units.end(),[&](const auto& u){return u.id==r.parentId;});
        if(child!=d.units.end()&&parent!=d.units.end()&&child->kind==UnitKind::General)
            require(geometryContains(*shape(parent->id),*shape(child->id)),CommandError::ValidationFailed,"final geometry outside parent");
    }
}
template<class Rows,class Value> void replaceContentRow(Rows& rows,const Value& value) {
    auto found=std::find_if(rows.begin(),rows.end(),[&](const auto& row){return row.id==value.id;});
    if(found==rows.end()) rows.push_back(value); else *found=value;
}
void applyContent(ProjectDocument& d,const DocumentIndex& before,const ContentEdit& edit) {
    require(!edit.target.id.empty(),CommandError::InvalidTargets,"empty content ID");
    const bool exists=before.objects.count(edit.target)!=0;
    require(edit.create?!exists:exists,CommandError::InvalidTargets,"content creation/update existence mismatch");
    require(!edit.create || (!std::holds_alternative<std::monostate>(edit.value) && edit.target.domain!="territorial"),
        CommandError::InvalidArguments,"invalid content creation");
    if(edit.geometry) {
        require(!d.geometries.get(edit.geometry->first),CommandError::InvalidArguments,"geometry version already exists");
        d.geometries.insert(edit.geometry->first,edit.geometry->second);
    }
    std::visit([&](const auto& value) {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,std::monostate>) {
            require(exists && edit.target.domain!="territorial",CommandError::InvalidTargets,"content deletion target missing");
            auto erase=[&](auto& rows){rows.erase(std::remove_if(rows.begin(),rows.end(),[&](const auto& row){return row.id==edit.target.id;}),rows.end());};
            if(edit.target.domain=="label") erase(d.labels);
            else if(edit.target.domain=="hydro") erase(d.hydro);
            else if(edit.target.domain=="generic") erase(d.genericFeatures);
            else if(edit.target.domain=="distributionEntry") erase(d.distributionEntries);
            else if(edit.target.domain=="distributionLayer") {
                erase(d.distributionLayers);
                for(const auto& entry:d.distributionEntries) if(entry.layerId==edit.target.id) {
                    d.presentation.membership.erase({"distributionEntry",entry.id});
                    d.presentation.objectStyles.erase({"distributionEntry",entry.id});
                }
                d.distributionEntries.erase(std::remove_if(d.distributionEntries.begin(),d.distributionEntries.end(),
                    [&](const auto& row){return row.layerId==edit.target.id;}),d.distributionEntries.end());
                for(auto& child:d.distributionLayers) if(child.parentId==edit.target.id) child.parentId.reset();
            } else require(false,CommandError::InvalidTargets,"invalid content domain");
            d.presentation.membership.erase(edit.target); d.presentation.objectStyles.erase(edit.target);
        } else if constexpr(std::is_same_v<T,CountryDetails>) {
            require(edit.target.domain=="territorial" && exists,CommandError::InvalidTargets,"country missing");
            d.countryDetails[edit.target]=value;
        } else if constexpr(std::is_same_v<T,TerritorialSymbolStyle>) {
            require(edit.target.domain=="territorial" && exists,CommandError::InvalidTargets,"symbol owner missing");
            d.symbols[edit.target]=value;
        } else {
            const char* domain=std::is_same_v<T,PlaceLabel>?"label":std::is_same_v<T,HydroFeature>?"hydro":
                std::is_same_v<T,DistributionLayer>?"distributionLayer":std::is_same_v<T,DistributionEntry>?"distributionEntry":"generic";
            require(edit.target.domain==domain && edit.target.id==value.id,CommandError::InvalidTargets,"content ID/domain mismatch");
            if constexpr(std::is_same_v<T,PlaceLabel>) replaceContentRow(d.labels,value);
            else if constexpr(std::is_same_v<T,HydroFeature>) {
                if(edit.create&&value.sourceFeatureId) {
                    const auto original=std::find_if(d.hydro.begin(),d.hydro.end(),[&](const auto& h){return h.id==*value.sourceFeatureId&&h.source.kind=="builtin";});
                    require(original!=d.hydro.end()||(!d.physicalData.source.empty()&&value.source.dataset==d.physicalData.dataset&&
                        value.source.version==d.physicalData.version&&value.source.kind=="user"),
                        CommandError::InvalidArguments,"source hydro dataset feature unavailable");
                    auto& hidden=d.physicalData.hiddenHydroIds;
                    require(std::find(hidden.begin(),hidden.end(),*value.sourceFeatureId)==hidden.end(),
                        CommandError::InvalidArguments,"source hydro already hidden");
                    hidden.push_back(*value.sourceFeatureId);
                }
                replaceContentRow(d.hydro,value);
            }
            else if constexpr(std::is_same_v<T,DistributionLayer>) replaceContentRow(d.distributionLayers,value);
            else if constexpr(std::is_same_v<T,DistributionEntry>) replaceContentRow(d.distributionEntries,value);
            else {
                require(exists,CommandError::InvalidArguments,"generic creation is import-only");
                replaceContentRow(d.genericFeatures,value);
            }
            if(edit.geometry) {
                if constexpr(std::is_same_v<T,PlaceLabel> || std::is_same_v<T,HydroFeature> || std::is_same_v<T,GenericFeature>)
                    require(value.geometry==edit.geometry->first,CommandError::InvalidArguments,"unused content geometry");
                else if constexpr(std::is_same_v<T,DistributionEntry>)
                    require(value.geometry && *value.geometry==edit.geometry->first,CommandError::InvalidArguments,"unused distribution geometry");
                else require(false,CommandError::InvalidArguments,"layer cannot own geometry");
            }
        }
    },edit.value);
    require(!edit.geometry || (edit.value.index()>=1 && edit.value.index()<=5),CommandError::InvalidArguments,"unexpected geometry payload");
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
        if(edit.properties.layerId.empty())candidate.presentation.membership.erase(ref);else candidate.presentation.membership[ref]=edit.properties.layerId;
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
            candidate.presentation.membership[territorialRef(action.id)]=action.layerId;
        else if constexpr(std::is_same_v<T,SetPhysicalData>)
            candidate.physicalData=action.settings;
        else if constexpr(std::is_same_v<T,TerritorialFieldEdit>) {
            applyField(candidate,index,action);
        } else if constexpr(std::is_same_v<T,TerritorialColorEdit>) {
            for(const auto& ref:action.targets){auto& s=candidate.presentation.objectStyles.at(ref);s.explicitColor=action.color.has_value();s.color=action.color.value_or(0);}
        } else if constexpr(std::is_same_v<T,TerritorialLockEdit>) {
            for(const auto& ref:action.targets)candidate.units.at(index.objects.at(ref)).locked=action.locked;
        } else if constexpr(std::is_same_v<T,ApplyTerritorialMutation>) {
            applyTerritorial(candidate,action);
        } else if constexpr(std::is_same_v<T,ContentEdit>) {
            applyContent(candidate,index,action);
        } else if constexpr(std::is_same_v<T,HistoricalInstantiationPlan>) {
            applyHistoricalInstantiation(candidate,action);
        } else if constexpr(std::is_same_v<T,GisGenericImportPlan>) {
            applyGenericGisImport(candidate,action);
        } else if constexpr(std::is_same_v<T,GisTerritorialImportPlan>) {
            applyTerritorialGisImport(candidate,action);
        } else if constexpr(std::is_same_v<T,GisDistributionImportPlan>) {
            applyDistributionGisImport(candidate,action);
        }
    },args.action);
    for(const auto& field:args.properties.fields)applyField(candidate,index,field);
}
void checkEffects(const ProjectSnapshot& project,const ProjectDocument& after,const CommandRequest& request)
{
    const auto& before=project.document();
    if(const auto edit=std::get_if<ContentEdit>(&request.args.action)) {
        const auto& ref=edit->target;
        const auto found=project.index().objects.find(ref);
        const bool exists=found!=project.index().objects.end();
        const auto layer=project.layer(nativeLayerId(before,ref));
        require(!layer || !layer->locked,CommandError::Locked,"content layer locked");
        bool locked=false;
        if(exists) {
            const auto i=found->second;
            if(ref.domain=="territorial") locked=before.units.at(i).locked;
            else if(ref.domain=="hydro") {
                require(before.hydro.at(i).source.kind!="builtin",CommandError::Locked,"builtin hydro requires copy-on-edit");
                locked=before.hydro.at(i).locked;
            }
            else if(ref.domain=="generic") locked=before.genericFeatures.at(i).locked;
            else if(ref.domain=="distributionLayer") locked=before.distributionLayers.at(i).locked;
            else if(ref.domain=="distributionEntry") {
                const auto& entry=before.distributionEntries.at(i);
                locked=before.distributionLayers.at(project.index().objects.at({"distributionLayer",entry.layerId})).locked;
            }
        }
        bool unlockOnly=false;
        if(locked && exists && !edit->geometry) {
            auto unlocked=before;
            const auto i=found->second;
            if(ref.domain=="hydro") unlocked.hydro.at(i).locked=false;
            else if(ref.domain=="generic") unlocked.genericFeatures.at(i).locked=false;
            else if(ref.domain=="distributionLayer") unlocked.distributionLayers.at(i).locked=false;
            unlockOnly=sameContent(unlocked,after);
        }
        require(!locked || unlockOnly,CommandError::Locked,"content object locked");
        // Derive effects from the candidate, not from UI claims. An opaque
        // geometry dependency must not prevent a safe name-only correction.
        const bool deleting=std::holds_alternative<std::monostate>(edit->value);
        if(!exists) require(effectAllowed(before,ref,"add"),CommandError::UnsupportedDependency,"content creation dependency");
        else if(deleting) require(effectAllowed(before,ref,"delete"),CommandError::UnsupportedDependency,"content deletion dependency");
        else if(unlockOnly) require(effectAllowed(before,ref,"locked"),CommandError::UnsupportedDependency,"content lock dependency");
        else {
            auto allow=[&](bool changed,const char* effect){require(!changed||effectAllowed(before,ref,effect),CommandError::UnsupportedDependency,effect);};
            auto fields=[&](const auto& old,const auto& next){
                using T=std::decay_t<decltype(old)>;
                if constexpr(!std::is_same_v<T,DistributionEntry>)allow(old.name!=next.name,"name");
                if constexpr(std::is_same_v<T,PlaceLabel>||std::is_same_v<T,HydroFeature>||std::is_same_v<T,GenericFeature>) {
                    allow(old.notes!=next.notes,"notes");allow(!(old.geometry==next.geometry),"geometry");
                    const auto sourceKey=[](const SourceProvenance& s){return std::tie(s.kind,s.dataset,s.version,s.sourceId,s.sourceFormat,s.sourceType,s.importedAt,s.details);};
                    allow(sourceKey(old.source)!=sourceKey(next.source),"source");
                }
                if constexpr(std::is_same_v<T,PlaceLabel>||std::is_same_v<T,HydroFeature>)allow(old.kind!=next.kind,"kind");
                if constexpr(std::is_same_v<T,PlaceLabel>)allow(!(old.territory==next.territory),"relation");
                if constexpr(std::is_same_v<T,GenericFeature>)allow(old.fallbackOnly!=next.fallbackOnly,"source");
                if constexpr(std::is_same_v<T,HydroFeature>)allow(old.sourceFeatureId!=next.sourceFeatureId,"source");
                if constexpr(std::is_same_v<T,HydroFeature>||std::is_same_v<T,GenericFeature>||std::is_same_v<T,DistributionLayer>){allow(old.color!=next.color,"color");allow(old.locked!=next.locked,"locked");}
                if constexpr(std::is_same_v<T,DistributionLayer>){allow(old.parentId!=next.parentId,"relation");allow(old.unit!=next.unit||!(old.valueScale==next.valueScale)||old.groups!=next.groups||old.metadata!=next.metadata,"metadata");}
                if constexpr(std::is_same_v<T,DistributionEntry>){allow(!(old.territory==next.territory)||old.layerId!=next.layerId,"relation");allow(!(old.geometry==next.geometry),"geometry");allow(old.value!=next.value||old.certainty!=next.certainty||old.metadata!=next.metadata,"metadata");}
                if constexpr(std::is_same_v<T,DistributionLayer>||std::is_same_v<T,DistributionEntry>)allow(old.validity.from!=next.validity.from||old.validity.to!=next.validity.to,"validity");
            };
            const auto i=found->second;
            if(ref.domain=="label")fields(before.labels.at(i),after.labels.at(i));
            else if(ref.domain=="hydro")fields(before.hydro.at(i),after.hydro.at(i));
            else if(ref.domain=="generic")fields(before.genericFeatures.at(i),after.genericFeatures.at(i));
            else if(ref.domain=="distributionLayer")fields(before.distributionLayers.at(i),after.distributionLayers.at(i));
            else if(ref.domain=="distributionEntry")fields(before.distributionEntries.at(i),after.distributionEntries.at(i));
            else allow(!sameContent(before,after),std::holds_alternative<CountryDetails>(edit->value)?"capital":"flag");
        }
        if(const auto entry=std::get_if<DistributionEntry>(&edit->value)) {
            const auto destination=project.index().objects.find({"distributionLayer",entry->layerId});
            require(destination!=project.index().objects.end() && !before.distributionLayers.at(destination->second).locked,
                CommandError::Locked,"destination distribution layer locked");
        }
        if(deleting && ref.domain=="distributionLayer") {
            for(const auto& child:before.distributionLayers) if(child.parentId==ref.id)
                require(!child.locked && effectAllowed(before,{"distributionLayer",child.id},"relation"),CommandError::Locked,"child layer is protected");
            for(const auto& entry:before.distributionEntries) if(entry.layerId==ref.id)
                require(effectAllowed(before,{"distributionEntry",entry.id},"delete"),CommandError::UnsupportedDependency,"entry is protected");
        }
        return;
    }
    if(const auto structural=std::get_if<ApplyTerritorialMutation>(&request.args.action)) {
        for(const auto& ref:structural->plan.affectedObjects) {
            if(project.index().objects.count(ref)) {
                const auto& u=before.units.at(project.index().objects.at(ref));
                require(!u.locked,CommandError::Locked,"object locked");
                const auto layer=project.layer(nativeLayerId(before,ref));
                require(!layer||!layer->locked,CommandError::Locked,"source layer locked");
            }
        }
        return;
    }
    if(const auto historical=std::get_if<HistoricalInstantiationPlan>(&request.args.action)) {
        for(const auto& addition:historical->additions) {
            require(effectAllowed(before,territorialRef(addition.selection.libraryId),"add"),
                    CommandError::UnsupportedDependency,"historical creation dependency");
            for(const auto& ref:{addition.parent,addition.sovereign})if(ref&&project.index().objects.count(*ref)) {
                const auto& owner=before.units.at(project.index().objects.at(*ref));
                require(!owner.locked,CommandError::Locked,"historical owner locked");
                const auto layer=project.layer(nativeLayerId(before,*ref));
                require(!layer||!layer->locked,CommandError::Locked,"historical owner layer locked");
            }
        }
        for(const auto& patch:historical->territoryReplacements) {
            const auto& owner=before.units.at(project.index().objects.at(patch.owner));
            require(!owner.locked&&effectAllowed(before,patch.owner,"geometry"),CommandError::Locked,"historical geometry owner locked");
            const auto layer=project.layer(nativeLayerId(before,patch.owner));
            require(!layer||!layer->locked,CommandError::Locked,"historical geometry layer locked");
        }
        for(const auto& [donor,target]:historical->territoryTransfers) {
            const auto& owner=before.units.at(project.index().objects.at(donor));
            require(!owner.locked&&effectAllowed(before,donor,"delete"),CommandError::Locked,"historical donor locked");
            const auto layer=project.layer(nativeLayerId(before,donor));
            require(!layer||!layer->locked,CommandError::Locked,"historical donor layer locked");
            for(const auto& label:before.labels)if(label.territory==donor)
                require(effectAllowed(before,{"label",label.id},"relation"),CommandError::UnsupportedDependency,"historical label dependency");
            for(const auto& entry:before.distributionEntries)if(entry.territory==donor) {
                const auto layer=std::find_if(before.distributionLayers.begin(),before.distributionLayers.end(),
                    [&](const auto& row){return row.id==entry.layerId;});
                require(layer!=before.distributionLayers.end()&&!layer->locked&&
                        effectAllowed(before,{"distributionEntry",entry.id},"relation"),
                        CommandError::UnsupportedDependency,"historical distribution dependency");
            }
        }
        for(const auto& [id,name]:historical->countryNameUpdates) {
            const auto ref=territorialRef(id);
            const auto& owner=before.units.at(project.index().objects.at(ref));
            require(!owner.locked&&effectAllowed(before,ref,"name"),CommandError::Locked,"historical country name locked");
        }
        return;
    }
    if(const auto generic=std::get_if<GisGenericImportPlan>(&request.args.action)) {
        for(const auto& feature:generic->features)
            require(effectAllowed(before,{"generic",feature.id},"add"),
                    CommandError::UnsupportedDependency,"generic GIS creation dependency");
        return;
    }
    if(const auto gis=std::get_if<GisTerritorialImportPlan>(&request.args.action)) {
        for(const auto& row:gis->units) {
            const auto ref=territorialRef(row.id);
            require(effectAllowed(before,ref,row.replaceExisting?"geometry":"add"),
                CommandError::UnsupportedDependency,"territorial GIS dependency");
            for(const auto& owner:{row.parent,row.sovereign})if(owner) {
                const auto found=project.index().objects.find(*owner);
                if(found!=project.index().objects.end()) {
                    const auto& unit=before.units.at(found->second);
                    const auto layer=project.layer(nativeLayerId(before,*owner));
                    require(!unit.locked&&(!layer||!layer->locked),CommandError::Locked,
                        "territorial GIS owner locked");
                }
            }
            if(row.replaceExisting) {
                const auto& unit=before.units.at(project.index().objects.at(ref));
                const auto layer=project.layer(nativeLayerId(before,ref));
                require(!unit.locked&&(!layer||!layer->locked),CommandError::Locked,
                    "territorial GIS replacement locked");
            }
        }
        for(const auto& patch:gis->countryReplacements) {
            require(effectAllowed(before,patch.owner,"geometry"),
                CommandError::UnsupportedDependency,"territorial GIS patch dependency");
            const auto& unit=before.units.at(project.index().objects.at(patch.owner));
            const auto layer=project.layer(nativeLayerId(before,patch.owner));
            require(!unit.locked&&(!layer||!layer->locked),CommandError::Locked,
                "territorial GIS donor locked");
        }
        return;
    }
    if(const auto gis=std::get_if<GisDistributionImportPlan>(&request.args.action)) {
        for(const auto& layer:gis->layers)
            require(effectAllowed(before,{"distributionLayer",layer.id},"add"),
                CommandError::UnsupportedDependency,"GIS distribution layer dependency");
        for(const auto& row:gis->entries) {
            require(effectAllowed(before,{"distributionEntry",row.entry.id},"add"),
                CommandError::UnsupportedDependency,"GIS distribution entry dependency");
            if(row.entry.territory)
                require(effectAllowed(before,*row.entry.territory,"relation"),
                    CommandError::UnsupportedDependency,"GIS distribution territory dependency");
            auto found=project.index().objects.find({"distributionLayer",row.entry.layerId});
            if(found!=project.index().objects.end())
                require(!before.distributionLayers.at(found->second).locked,CommandError::Locked,
                    "GIS distribution layer locked");
        }
        return;
    }
    auto allow=[&](bool changed,const ObjectRef& ref,const char* effect) {
        require(!changed || effectAllowed(before,ref,effect),CommandError::UnsupportedDependency,effect);
    };
    // New commands validate their intended effects even for equal-value requests.
    if(request.args.action.index()>=8)for(const auto& ref:request.targets) {
        const auto& u=before.units.at(project.index().objects.at(ref));
        const auto layer=project.layer(nativeLayerId(before,ref));
        require(!layer||!layer->locked,CommandError::Locked,"source layer locked");
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
        const auto& source=nativeLayerId(before,ref);
        const auto& destination=nativeLayerId(after,ref);
        const bool colorChanged=oldStyle.explicitColor!=nextStyle.explicitColor || (oldStyle.explicitColor&&oldStyle.color!=nextStyle.color);
        const bool dateChanged=!sameValidity(staticLifetime(before,old.id).validity,staticLifetime(after,next.id).validity);
        bool changed=old.name!=next.name || old.nameExplicit!=next.nameExplicit || old.notes!=next.notes || colorChanged || dateChanged ||
                     oldStyle.opacity!=nextStyle.opacity || source!=destination;
        // Check pre-transaction locks; a compound unlock cannot bypass protection.
        require(!changed || ((!old.locked || request.commandId=="territorial.batch-color") && (!project.layer(source)||!project.layer(source)->locked)),CommandError::Locked,"country/source layer locked");
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

ChangeImpact calculateChangeImpact(const ProjectDocument& before,const ProjectDocument& after,
                                   const std::vector<ObjectRef>& targets) {
    ChangeImpact impact;
    const auto beforeIndex=validateDocument(before),afterIndex=validateDocument(after);
    std::set<ObjectRef> objects,affected(targets.begin(),targets.end()),geometryObjects;
    std::set<GeometryRef> geometries;
    for(const auto& entry:beforeIndex.objects)objects.insert(entry.first);
    for(const auto& entry:afterIndex.objects)objects.insert(entry.first);
    for(const auto& object:objects) {
        const auto oldRef=objectGeometry(before,beforeIndex,object);
        const auto newRef=objectGeometry(after,afterIndex,object);
        const bool lifetime=beforeIndex.objects.count(object)!=afterIndex.objects.count(object);
        const auto oldShape=oldRef?before.geometries.get(*oldRef):nullptr;
        const auto newShape=newRef?after.geometries.get(*newRef):nullptr;
        // Fallback eligibility changes the effective LOD/projection preparation
        // even when the immutable geographic shape and binding remain identical.
        const bool fallbackPolicy=object.domain=="generic"&&!lifetime&&
            before.genericFeatures.at(beforeIndex.objects.at(object)).fallbackOnly!=
            after.genericFeatures.at(afterIndex.objects.at(object)).fallbackOnly;
        const bool geometry=!(oldRef==newRef)||oldShape!=newShape||fallbackPolicy;
        if(lifetime||geometry)affected.insert(object);
        if(geometry) {
            geometryObjects.insert(object);
            // Both endpoints are necessary when deleting or undoing a replacement.
            if(oldRef)geometries.insert(*oldRef);
            if(newRef)geometries.insert(*newRef);
        }
    }
    const auto sourceKey=[](const SourceProvenance& s) {
        return std::tie(s.kind,s.dataset,s.version,s.sourceId,s.sourceFormat,s.sourceType,s.importedAt,s.details);
    };
    const auto& a=before.presentation;const auto& b=after.presentation;
    const bool globalPresentation=!(a.webPresentation==b.webPresentation)||a.membership!=b.membership||
        !same(a.userLayers,b.userLayers,[](const auto& x,const auto& y) {
            return std::tie(x.id,x.name,x.visible,x.locked,x.opacity)==std::tie(y.id,y.name,y.visible,y.locked,y.opacity);
        })||!same(a.objectStyles,b.objectStyles,[](const auto& x,const auto& y) {
            return x.first==y.first&&std::tie(x.second.color,x.second.opacity,x.second.explicitColor)==
                std::tie(y.second.color,y.second.opacity,y.second.explicitColor);
        });
    const bool relationsChanged=
        !same(before.timelineRecords.lifetimes,after.timelineRecords.lifetimes,[](const auto& x,const auto& y) {
            return x.id==y.id&&x.entityId==y.entityId&&sameValidity(x.validity,y.validity);
        })||!same(before.timelineRecords.geometryBindings,after.timelineRecords.geometryBindings,[](const auto& x,const auto& y) {
            return x.id==y.id&&x.entityId==y.entityId&&sameValidity(x.validity,y.validity)&&x.geometryRef==y.geometryRef;
        })||!same(before.timelineRecords.parentRelations,after.timelineRecords.parentRelations,[](const auto& x,const auto& y) {
            return x.id==y.id&&x.entityId==y.entityId&&sameValidity(x.validity,y.validity)&&x.parentId==y.parentId&&x.coverageMode==y.coverageMode;
        });
    const bool unitPresentation=!same(before.units,after.units,[](const auto& x,const auto& y) {
        return std::tie(x.id,x.name,x.notes,x.kind,x.locked,x.baseName,x.nameExplicit,x.libraryOrigin,
                        x.metadata,x.sourceFolderId,x.sourceEntityId,x.sourceGeometryVersion)==
            std::tie(y.id,y.name,y.notes,y.kind,y.locked,y.baseName,y.nameExplicit,y.libraryOrigin,
                     y.metadata,y.sourceFolderId,y.sourceEntityId,y.sourceGeometryVersion);
    });
    const bool contentPresentation=
        !same(before.countryDetails,after.countryDetails,[](const auto& x,const auto& y) {
            return x.first==y.first&&x.second.capital==y.second.capital;
        })||!same(before.symbols,after.symbols,[](const auto& x,const auto& y) {
            return x.first==y.first&&std::tie(x.second.policy,x.second.embeddedDataUrl,x.second.defaultCountryId,x.second.defaultFlagDataUrl)==
                std::tie(y.second.policy,y.second.embeddedDataUrl,y.second.defaultCountryId,y.second.defaultFlagDataUrl);
        })||!same(before.labels,after.labels,[&](const auto& x,const auto& y) {
            return std::tie(x.id,x.name,x.kind,x.notes,x.territory)==std::tie(y.id,y.name,y.kind,y.notes,y.territory)&&sourceKey(x.source)==sourceKey(y.source);
        })||!same(before.hydro,after.hydro,[&](const auto& x,const auto& y) {
            return std::tie(x.id,x.name,x.kind,x.notes,x.color,x.locked,x.sourceFeatureId)==
                std::tie(y.id,y.name,y.kind,y.notes,y.color,y.locked,y.sourceFeatureId)&&sourceKey(x.source)==sourceKey(y.source);
        })||!same(before.genericFeatures,after.genericFeatures,[&](const auto& x,const auto& y) {
            return std::tie(x.id,x.name,x.notes,x.color,x.locked,x.fallbackOnly)==
                std::tie(y.id,y.name,y.notes,y.color,y.locked,y.fallbackOnly)&&sourceKey(x.source)==sourceKey(y.source);
        })||!same(before.distributionLayers,after.distributionLayers,[](const auto& x,const auto& y) {
            return std::tie(x.id,x.name,x.unit,x.color,x.locked,x.parentId,x.groups,x.validity.from,x.validity.to,x.metadata,x.valueScale.manual,x.valueScale.min,x.valueScale.max)==
                std::tie(y.id,y.name,y.unit,y.color,y.locked,y.parentId,y.groups,y.validity.from,y.validity.to,y.metadata,y.valueScale.manual,y.valueScale.min,y.valueScale.max);
        })||!same(before.distributionEntries,after.distributionEntries,[](const auto& x,const auto& y) {
            return std::tie(x.id,x.layerId,x.territory,x.value,x.certainty,x.metadata,x.validity.from,x.validity.to)==
                std::tie(y.id,y.layerId,y.territory,y.value,y.certainty,y.metadata,y.validity.from,y.validity.to);
        });
    const bool extensionsChanged=!same(before.extensions,after.extensions,[](const auto& x,const auto& y) {
        return std::tie(x.id,x.sourceFormat,x.sourceSchema,x.jsonPointer,x.payload,x.status,x.dependencyKnowledge,x.dependencies,x.forbiddenEffects,x.envelopeExtras)==
            std::tie(y.id,y.sourceFormat,y.sourceSchema,y.jsonPointer,y.payload,y.status,y.dependencyKnowledge,y.dependencies,y.forbiddenEffects,y.envelopeExtras);
    });
    auto& dirty=impact.sceneDirty;
    dirty.geometry=!geometryObjects.empty();
    dirty.datasetResource=before.documentId!=after.documentId||
        std::tie(before.physicalData.dataset,before.physicalData.version,before.physicalData.source)!=
        std::tie(after.physicalData.dataset,after.physicalData.version,after.physicalData.source);
    dirty.presentation=globalPresentation||relationsChanged||unitPresentation||contentPresentation||extensionsChanged||
        before.physicalData.hiddenHydroIds!=after.physicalData.hiddenHydroIds;
    // Broad display and reference changes can affect objects outside command targets.
    if(dirty.presentation||dirty.datasetResource)affected.insert(objects.begin(),objects.end());
    dirty.affectedObjects.assign(affected.begin(),affected.end());
    dirty.geometryObjects.assign(geometryObjects.begin(),geometryObjects.end());
    // Lifetime and known reference changes are covered by the complete affected
    // set and effective geometry diff; opaque extension effects remain global.
    dirty.fullRebuild=dirty.datasetResource||extensionsChanged;
    impact.changedObjects=dirty.affectedObjects;
    impact.changedGeometries.assign(geometries.begin(),geometries.end());
    if(dirty.presentation)impact.presentationInvalidations=dirty.affectedObjects;
    impact.requiresFullSpatialRebuild=dirty.datasetResource;
    impact.requiresFullSceneRebuild=dirty.fullRebuild;
    for(const auto& [ref,shape]:after.geometries.versions()) {
        const auto old=before.geometries.versions().find(ref);
        if(old!=before.geometries.versions().end()&&old->second==shape) {
            ++impact.retainedGeometryCount;continue;
        }
        impact.estimatedNewGeometryBytes+=sizeof(Geometry)+shape->points.size()*sizeof(Point);
        for(const auto& line:shape->lines)impact.estimatedNewGeometryBytes+=line.size()*sizeof(Point);
        for(const auto& polygon:shape->polygons)for(const auto& ring:polygon)
            impact.estimatedNewGeometryBytes+=ring.size()*sizeof(Point);
    }
    return impact;
}

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
{ return prepare(project,request,{}); }
PrepareResult CommandProcessor::prepare(const ProjectSnapshot& project,const CommandRequest& request,const ExtensionRewriter& rewriter)
{
    PrepareResult result;
    try {
        requireStaticTimeline(project.document());
        validateRequest(project,request);
        const auto structural=std::get_if<ApplyTerritorialMutation>(&request.args.action);
        if(structural) {
            auto rederived=planTerritorial(project,structural->plan.intent);
            // Planning is intentionally repeated against the current snapshot.  An
            // allocation failure while doing so is not evidence that a valid plan
            // has gone stale; preserve the candidate and report the preparation
            // failure to callers instead.
            if(!rederived.ok()) {
                require(false,rederived.error==CommandError::PrepareFailed
                              ?CommandError::PrepareFailed:rederived.error,
                        "territorial plan preparation failed");
            }
            require(rederived.plan&&rederived.plan->kind==structural->plan.kind&&rederived.plan->affectedObjects==structural->plan.affectedObjects,CommandError::ValidationFailed,"territorial plan changed");
            require(rederived.plan->geometry.readOwners==structural->plan.geometry.readOwners &&
                    rederived.plan->geometry.replacements==structural->plan.geometry.replacements &&
                    rederived.plan->geometry.createOwners==structural->plan.geometry.createOwners &&
                    rederived.plan->geometry.removableOwners==structural->plan.geometry.removableOwners,
                    CommandError::ValidationFailed,"geometry owner contract changed");
            require(rederived.plan->retainedGuards==structural->plan.retainedGuards&&rederived.plan->rewrites==structural->plan.rewrites,CommandError::UnsupportedDependency,"retained guards changed");
            for(const auto& guard:structural->plan.retainedGuards) {
                const auto found=std::find_if(project.document().extensions.begin(),project.document().extensions.end(),[&](const auto& e){return e.id==guard.id&&e.payload==guard.payload;});
                require(found!=project.document().extensions.end(),CommandError::UnsupportedDependency,"retained extension changed");
            }
            require(structural->plan.retainedGuards.empty()||bool(rewriter),CommandError::UnsupportedDependency,"retained references require a rewriter");
        }
        if(const auto gis=std::get_if<GisTerritorialImportPlan>(&request.args.action)) {
            try {
                (void)planTerritorialGisImport(project,gis->info.id,gis->info.source,
                    gis->info.target,gis->units,gis->countryReplacements);
            } catch(const std::invalid_argument& error) {
                result.error=CommandError::ValidationFailed;result.detail=error.what();return result;
            }
        }
        if(const auto gis=std::get_if<GisDistributionImportPlan>(&request.args.action)) {
            try {
                (void)planDistributionGisImport(project,gis->info.id,gis->info.source,
                    gis->layers,gis->entries);
            } catch(const std::invalid_argument& error) {
                result.error=CommandError::ValidationFailed;result.detail=error.what();return result;
            }
        }
        auto candidate=project.document();
        try { applyArguments(candidate,project.index(),request.args); }
        catch(const std::invalid_argument& e){ result.error=CommandError::ValidationFailed;result.detail=e.what();return result; }
        if(structural && (!structural->plan.retainedGuards.empty() || !structural->plan.rewrites.empty() ||
           (structural->geometry&&!structural->geometry->removedGeometryOwners.empty()))) {
            auto rewritePlan=structural->plan;
            if(structural->geometry)for(const auto& ref:structural->geometry->removedGeometryOwners)
                for(const auto& extension:project.document().extensions) {
                    if(extension.jsonPointer=="/distributionEntries"||extension.jsonPointer=="/genericFeatures")
                        rewritePlan.rewrites.push_back({extension.jsonPointer,ReferenceRewriteOperation::RemoveEntry,ref.id,{}});
                    else if(extension.jsonPointer=="/itemVisibility"||extension.jsonPointer=="/labelSettings")
                        rewritePlan.rewrites.push_back({extension.jsonPointer,ReferenceRewriteOperation::DeleteKey,ref.id,{}});
                }
            require(bool(rewriter)||(rewritePlan.retainedGuards.empty()&&rewritePlan.rewrites.empty()),CommandError::UnsupportedDependency,"retained rewriter required");
            const auto rewritten=rewriter?rewriter(project.document(),rewritePlan,candidate.extensions):ExtensionRewriteResult{};
            require(rewritten.ok,CommandError::UnsupportedDependency,"retained reference rewrite failed");
            for(const auto& guard:structural->plan.retainedGuards)
                require(std::find(rewritten.handledExtensionIds.begin(),rewritten.handledExtensionIds.end(),guard.id)!=rewritten.handledExtensionIds.end(),CommandError::UnsupportedDependency,"retained reference not handled");
        }
        checkEffects(project,candidate,request);
        normalizePresentation(candidate);
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
            checkpoint=unit.kind==UnitKind::General && field->field==TerritorialField::Name &&
                unit.nameExplicit && unit.name.empty() && trimWebText(field->value).empty();
        }
        if(!checkpoint && semanticallyEqual(project.document(),after->document)) {
            result.status=CommandStatus::NoOp; return result;
        }
        auto change=std::unique_ptr<ChangeSet>(new ChangeSet(project.state_,std::move(after),request));
        change->impact_=calculateChangeImpact(change->before(),change->after(),request.targets);
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
PrepareResult CommandProcessor::prepare(const Project& project,const CommandRequest& request,const ExtensionRewriter& rewriter)
{ return prepare(project.snapshot(),request,rewriter); }
TerritorialPlanResult CommandProcessor::planTerritorial(const ProjectSnapshot& project,const TerritorialMutationIntent& intent)
{
    TerritorialPlanResult result;
    try {
        std::visit([&](const auto& value){using T=std::decay_t<decltype(value)>;
            if constexpr(std::is_same_v<T,ChangeParentIntent>)result.plan=planChangeParent(project,value);
            else if constexpr(std::is_same_v<T,ChangeRegionSovereignIntent>)result.plan=planRegionSovereign(project,value);
            else if constexpr(std::is_same_v<T,CreateTerritorialIntent>)result.plan=planCreate(project,value);
            else if constexpr(std::is_same_v<T,DeleteTerritorialIntent>)result.plan=planDelete(project,value);
            else if constexpr(std::is_same_v<T,TransferSubunitIntent>)result.plan=planTransfer(project,value);
            else if constexpr(std::is_same_v<T,ConvertTerritorialTypeIntent>)result.plan=planConversion(project,value);
            else if constexpr(std::is_same_v<T,ReplaceGeometryIntent>)result.plan=planReplaceGeometry(project,value);
            else if constexpr(std::is_same_v<T,MergeTerritorialIntent>)result.plan=planMerge(project,value);
            else if constexpr(std::is_same_v<T,AnnexTerritoryIntent>)result.plan=planAnnex(project,value);
            else if constexpr(std::is_same_v<T,SplitTerritorialIntent>)result.plan=planSplit(project,value);
            else if constexpr(std::is_same_v<T,SharedBoundaryIntent>)result.plan=planSharedBoundary(project,value);
            else if constexpr(std::is_same_v<T,CoastlineIntent>)result.plan=planCoastline(project,value);
        },intent);result.status=CommandStatus::Prepared;
    } catch(const std::invalid_argument& e) { result.detail=e.what();result.error=result.detail=="LOCKED"?CommandError::Locked:result.detail=="INVALID_TARGETS"?CommandError::InvalidTargets:result.detail=="UNSUPPORTED_DEPENDENCY"?CommandError::UnsupportedDependency:CommandError::ValidationFailed; }
    catch(...) { result.error=CommandError::PrepareFailed; }
    return result;
}
TerritorialPlanResult CommandProcessor::planTerritorial(const Project& project,const TerritorialMutationIntent& intent)
{ return planTerritorial(project.snapshot(),intent); }
CommandResult CommandProcessor::confirm(Project& project,CommandPreview& preview)
{
    // Every attempt consumes the token, including stale, foreign or failed commits.
    auto change=std::move(preview.change_);
    if(!change) return {CommandStatus::Rejected,CommandError::PreviewConsumed,{}};
    const auto& request=change->request_;
    if(request.projectInstanceId!=project.instanceId()) return {CommandStatus::Rejected,CommandError::ProjectMismatch,{}};
    if(request.documentId!=project.document().documentId) return {CommandStatus::Rejected,CommandError::DocumentMismatch,{}};
    if(request.revision!=project.revision())
        return {CommandStatus::Rejected,CommandError::StaleRevision,{}};
    if(project.revision()==std::numeric_limits<std::uint64_t>::max())
        return {CommandStatus::Rejected,CommandError::RevisionOverflow,{}};
    const bool presentationRebased=!(project.document().presentation.webPresentation==
                                     change->before().presentation.webPresentation);
    // CommandPreview owns a const ChangeSet, so taking its impact is a copy,
    // not a move. Stage that allocation before mutating the project; otherwise
    // an allocation failure could escape after apply() has already committed.
    ChangeImpact impact;
    try {
        impact=change->impact_;
        if(presentationRebased) {
            // Stage conservative display invalidation before committing, preserving
            // the existing no-allocation-after-apply failure contract.
            impact.sceneDirty.presentation=true;
            impact.sceneDirty.fullRebuild=true;
            std::set<ObjectRef> affected(impact.sceneDirty.affectedObjects.begin(),impact.sceneDirty.affectedObjects.end());
            for(const auto& entry:project.index().objects)affected.insert(entry.first);
            for(const auto& entry:change->after_->index.objects)affected.insert(entry.first);
            impact.sceneDirty.affectedObjects.assign(affected.begin(),affected.end());
            impact.changedObjects=impact.sceneDirty.affectedObjects;
            impact.presentationInvalidations=impact.sceneDirty.affectedObjects;
            impact.requiresFullSceneRebuild=true;
        }
        project.apply(*change);
    }
    catch(const std::exception&) { return {CommandStatus::Rejected,CommandError::CommitFailed,{}}; }
    return {CommandStatus::Applied,CommandError::None,{},std::move(impact)};
}
bool semanticallyEqual(const ProjectDocument& a,const ProjectDocument& b)
{
    if(&a==&b) return true;
    return a.documentId==b.documentId && a.exchangeMetadata==b.exchangeMetadata && sameContent(a,b) &&
        same(a.units,b.units,[](const auto& x,const auto& y) {
            return x.id==y.id && x.name==y.name && x.baseName==y.baseName && x.nameExplicit==y.nameExplicit && x.notes==y.notes && x.kind==y.kind &&
                x.locked==y.locked && x.libraryOrigin==y.libraryOrigin && x.metadata==y.metadata &&
                x.sourceFolderId==y.sourceFolderId && x.sourceEntityId==y.sourceEntityId && x.sourceGeometryVersion==y.sourceGeometryVersion;
        }) && a.timelineRecords.schemaVersion==b.timelineRecords.schemaVersion &&
        same(a.timelineRecords.lifetimes,b.timelineRecords.lifetimes,[](const auto& x,const auto& y){return x.id==y.id&&x.entityId==y.entityId&&sameValidity(x.validity,y.validity);}) &&
        same(a.timelineRecords.geometryBindings,b.timelineRecords.geometryBindings,[](const auto& x,const auto& y){return x.id==y.id&&x.entityId==y.entityId&&sameValidity(x.validity,y.validity)&&x.geometryRef==y.geometryRef;}) &&
        same(a.timelineRecords.parentRelations,b.timelineRecords.parentRelations,[](const auto& x,const auto& y){return x.id==y.id&&x.entityId==y.entityId&&sameValidity(x.validity,y.validity)&&x.parentId==y.parentId&&x.coverageMode==y.coverageMode;}) && same(a.geometries.versions(),b.geometries.versions(),[](const auto& x,const auto& y) {
            return x.first==y.first && (x.second==y.second || sameGeometry(*x.second,*y.second));
        }) && same(a.presentation.userLayers,b.presentation.userLayers,[](const Layer& x,const Layer& y) {
            return x.id==y.id && x.name==y.name && x.visible==y.visible && x.locked==y.locked && x.opacity==y.opacity;
        }) && a.presentation.webPresentation==b.presentation.webPresentation && a.presentation.membership==b.presentation.membership &&
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

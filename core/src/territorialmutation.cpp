#include <pandoeditor/territorialmutation.h>
#include <pandoeditor/project.h>
#include <pandoeditor/geometrypredicates.h>
#include <algorithm>
#include <stdexcept>

namespace pandoeditor {
namespace {
const TerritorialUnit& unit(const ProjectSnapshot& s,const ObjectRef& ref) {
    if(ref.domain!="territorial") throw std::invalid_argument("INVALID_TARGETS");
    auto it=s.index().objects.find(ref); if(it==s.index().objects.end()) throw std::invalid_argument("INVALID_TARGETS");
    return s.document().units.at(it->second);
}
const TerritorialRelation* base(const ProjectDocument& d,const ObjectRef& ref) {
    for(const auto& r:d.relations) if(r.unit==ref&&!r.dated)return &r; return nullptr;
}
void unlocked(const TerritorialUnit& u){if(u.locked)throw std::invalid_argument("LOCKED");}
TerritorialMutationPlan initial(const ProjectSnapshot& s,TerritorialMutationKind kind,TerritorialMutationIntent intent) {
    TerritorialMutationPlan p; p.projectInstanceId=s.instanceId();p.documentId=s.document().documentId;p.baseRevision=s.revision();p.kind=kind;p.intent=std::move(intent);return p;
}
void guard(TerritorialMutationPlan& p,const ProjectDocument& d,const ObjectRef& ref,const std::string& effect) {
    for(const auto& id:blockingExtensions(d,ref,effect)) { auto it=std::find_if(d.extensions.begin(),d.extensions.end(),[&](const auto& e){return e.id==id;}); if(it!=d.extensions.end())p.retainedGuards.push_back({id,it->payload}); }
}
bool descendant(const ProjectDocument& d,const ObjectRef& child,const ObjectRef& ancestor) {
    auto current=child; for(std::size_t steps=0;steps<d.units.size();++steps){auto r=base(d,current);if(!r||!r->parent)return false;if(*r->parent==ancestor)return true;current=*r->parent;}return false;
}
}
TerritorialMutationPlan planChangeParent(const ProjectSnapshot& s,const ChangeParentIntent& in) {
    const auto& target=unit(s,in.target);const auto& parent=unit(s,in.parent);unlocked(target);unlocked(parent);
    if(target.kind!=UnitKind::Subunit||in.target==in.parent||descendant(s.document(),in.parent,in.target))throw std::invalid_argument("VALIDATION_FAILED");
    auto rel=base(s.document(),in.target); if(!rel||!rel->sovereign)throw std::invalid_argument("VALIDATION_FAILED");
    if(parent.kind==UnitKind::Country && !(*rel->sovereign==in.parent))throw std::invalid_argument("SOVEREIGN_MISMATCH");
    if(parent.kind==UnitKind::Subunit) { auto pr=base(s.document(),in.parent);if(!pr||!pr->sovereign||!(*pr->sovereign==*rel->sovereign))throw std::invalid_argument("SOVEREIGN_MISMATCH"); }
    if(!geometryContains(*s.document().geometries.get(parent.geometry),*s.document().geometries.get(target.geometry)))throw std::invalid_argument("GEOMETRY_OUTSIDE_PARENT");
    auto p=initial(s,TerritorialMutationKind::ChangeParent,in);p.targets={in.target};p.affectedObjects={in.target,in.parent};p.selectedAfter=in.target;p.impacts.push_back({"change-parent",in.target,"territorial.parent.changed"});guard(p,s.document(),in.target,"relation");return p;
}
TerritorialMutationPlan planRegionSovereign(const ProjectSnapshot& s,const ChangeRegionSovereignIntent& in) {
    const auto& target=unit(s,in.target);unlocked(target);if(target.kind!=UnitKind::Region)throw std::invalid_argument("VALIDATION_FAILED");if(in.sovereign){const auto& owner=unit(s,*in.sovereign);unlocked(owner);if(owner.kind!=UnitKind::Country)throw std::invalid_argument("SOVEREIGN_MISMATCH");}
    auto p=initial(s,TerritorialMutationKind::ChangeRegionSovereign,in);p.targets={in.target};p.affectedObjects={in.target};if(in.sovereign)p.affectedObjects.push_back(*in.sovereign);p.selectedAfter=in.target;p.impacts.push_back({"change-sovereign",in.target,"territorial.sovereign.changed"});guard(p,s.document(),in.target,"relation");return p;
}
TerritorialMutationPlan planCreate(const ProjectSnapshot& s,const CreateTerritorialIntent& in) {
    if(in.id.empty()||s.index().objects.count(territorialRef(in.id)))throw std::invalid_argument("DUPLICATE_ID");
    if(in.kind==UnitKind::Subunit) {if(!in.parent||!in.sovereign)throw std::invalid_argument("SOVEREIGN_MISMATCH");const auto& parent=unit(s,*in.parent);const auto& sovereign=unit(s,*in.sovereign);if(sovereign.kind!=UnitKind::Country)throw std::invalid_argument("SOVEREIGN_MISMATCH");if(parent.kind==UnitKind::Country&&!(*in.parent==*in.sovereign))throw std::invalid_argument("SOVEREIGN_MISMATCH");}
    auto kind=in.kind==UnitKind::Country?TerritorialMutationKind::CreateCountry:in.kind==UnitKind::Subunit?TerritorialMutationKind::CreateSubunit:TerritorialMutationKind::CreateRegion;
    auto p=initial(s,kind,in);p.targets={territorialRef(in.id)};p.affectedObjects=p.targets;p.selectedAfter=p.targets.front();p.geometry={GeometryRequirementKind::Prepared,"create",{}};p.impacts.push_back({"create",p.targets.front(),"territorial.create"});guard(p,s.document(),p.targets.front(),"add");return p;
}
TerritorialMutationPlan planDelete(const ProjectSnapshot& s,const DeleteTerritorialIntent& in) {
    if(in.targets.empty())throw std::invalid_argument("INVALID_TARGETS");auto p=initial(s,TerritorialMutationKind::DeleteUnits,in);p.targets=in.targets;p.affectedObjects=in.targets;p.requiresConfirmation=true;
    for(const auto& ref:in.targets){const auto& u=unit(s,ref);unlocked(u);for(const auto& r:s.document().relations)if(!r.dated&&r.parent&&*r.parent==ref&&std::find(in.targets.begin(),in.targets.end(),r.unit)==in.targets.end())throw std::invalid_argument("HAS_BASE_CHILD");guard(p,s.document(),ref,"delete");
        for(const auto& extension:s.document().extensions) {
            if(extension.status=="migrationArchive")continue;
            if(extension.jsonPointer=="/distributionEntries"||extension.jsonPointer=="/genericFeatures")p.rewrites.push_back({extension.jsonPointer,ReferenceRewriteOperation::RemoveEntry,ref.id,{}});
            else if(extension.jsonPointer=="/itemVisibility"||extension.jsonPointer=="/labelSettings")p.rewrites.push_back({extension.jsonPointer,ReferenceRewriteOperation::DeleteKey,ref.id,{}});
        }
        p.impacts.push_back({"delete",ref,"territorial.delete"});if(u.kind==UnitKind::Country)p.kind=TerritorialMutationKind::DeleteCountry;}
    return p;
}
TerritorialMutationPlan planTransfer(const ProjectSnapshot& s,const TransferSubunitIntent& in) {
    const auto& u=unit(s,in.target);const auto& d=unit(s,in.destinationCountry);unlocked(u);unlocked(d);if(u.kind!=UnitKind::Subunit||d.kind!=UnitKind::Country)throw std::invalid_argument("VALIDATION_FAILED");auto p=initial(s,TerritorialMutationKind::TransferSubunit,in);p.targets={in.target};p.affectedObjects={in.target,in.destinationCountry};p.geometry={GeometryRequirementKind::WorkerPatch,"transfer",{in.target,in.destinationCountry}};p.selectedAfter=in.target;p.impacts.push_back({"transfer",in.target,"territorial.transfer.requiresGeometry"});return p;
}
TerritorialMutationPlan planConversion(const ProjectSnapshot& s,const ConvertTerritorialTypeIntent& in) {
    const auto& u=unit(s,in.source);unlocked(u);if((u.kind==UnitKind::Subunit&&in.targetKind!=UnitKind::Country)||(u.kind==UnitKind::Country&&in.targetKind!=UnitKind::Subunit))throw std::invalid_argument("VALIDATION_FAILED");
    if(u.kind==UnitKind::Country&& (in.generatedId.empty()||in.generatedId==u.id||!in.sovereign||!in.parent))throw std::invalid_argument("INVALID_ARGUMENTS");
    std::vector<ObjectRef> sources{in.source};
    if(u.kind==UnitKind::Subunit) {const auto r=base(s.document(),in.source);if(!r||!r->sovereign)throw std::invalid_argument("SOVEREIGN_MISMATCH");sources.push_back(*r->sovereign);}
    else sources.push_back(*in.sovereign);
    auto p=initial(s,u.kind==UnitKind::Subunit?TerritorialMutationKind::PromoteSubunitToCountry:TerritorialMutationKind::ConvertCountryToSubunit,in);p.targets={in.source};p.affectedObjects=sources;p.selectedAfter=territorialRef(u.kind==UnitKind::Country?in.generatedId:u.id);p.geometry={GeometryRequirementKind::WorkerPatch,u.kind==UnitKind::Country?"country-to-subunit":"promote",sources};
    for(const auto& e:s.document().extensions)if(e.status!="migrationArchive") {
        if(e.jsonPointer=="/distributionEntries"||e.jsonPointer=="/genericFeatures")p.rewrites.push_back({e.jsonPointer,ReferenceRewriteOperation::ReplaceId,in.source.id,p.selectedAfter->id});
        else if(e.jsonPointer=="/itemVisibility"||e.jsonPointer=="/labelSettings")p.rewrites.push_back({e.jsonPointer,ReferenceRewriteOperation::ReplaceId,in.source.id,p.selectedAfter->id});
    }
    p.impacts.push_back({"convert",in.source,"territorial.convert.requiresGeometry"});return p;
}
}

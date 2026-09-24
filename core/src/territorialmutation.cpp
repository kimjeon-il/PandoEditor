#include <pandoeditor/territorialmutation.h>
#include <pandoeditor/project.h>
#include <pandoeditor/geometrypredicates.h>
#include <algorithm>
#include <cmath>
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
bool rewritable(const PreservedExtension& e) {
    if(e.status=="migrationArchive"||e.dependencyKnowledge!="known"||e.envelopeExtras!="{}")return false;
    return e.jsonPointer=="/distributionEntries"||e.jsonPointer=="/itemVisibility"||e.jsonPointer=="/labelSettings"||e.jsonPointer=="/genericFeatures";
}
void requireRewritableGuards(const TerritorialMutationPlan& p,const ProjectDocument& d) {
    for(const auto& g:p.retainedGuards) {auto it=std::find_if(d.extensions.begin(),d.extensions.end(),[&](const auto& e){return e.id==g.id;});if(it==d.extensions.end()||!rewritable(*it))throw std::invalid_argument("UNSUPPORTED_DEPENDENCY");}
}
bool descendant(const ProjectDocument& d,const ObjectRef& child,const ObjectRef& ancestor) {
    auto current=child; for(std::size_t steps=0;steps<d.units.size();++steps){auto r=base(d,current);if(!r||!r->parent)return false;if(*r->parent==ancestor)return true;current=*r->parent;}return false;
}
double cross(Point a,Point b,Point c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
bool adjacent(const Geometry& a,const Geometry& b) {
    constexpr double lineTolerance=1e-7,overlapTolerance=1e-8;
    auto edges=[](const Geometry& g){std::vector<std::pair<Point,Point>> out;for(const auto& p:g.polygons)for(const auto& r:p)for(std::size_t i=1;i<r.size();++i)out.push_back({r[i-1],r[i]});return out;};
    const auto left=edges(a),right=edges(b);
    for(const auto& [p,q]:left){const auto dx=q.x-p.x,dy=q.y-p.y,length=std::hypot(dx,dy);if(!length)continue;
        for(const auto& [u,v]:right){if(std::abs(cross(p,q,u))/length>lineTolerance||std::abs(cross(p,q,v))/length>lineTolerance)continue;
            const auto parameter=[&](Point r){return ((r.x-p.x)*dx+(r.y-p.y)*dy)/(length*length);};
            if(std::min(1.,std::max(parameter(u),parameter(v)))-std::max(0.,std::min(parameter(u),parameter(v)))>overlapTolerance)return true;
        }}
    return false;
}
void transferScope(const ProjectSnapshot& s,TerritorialMutationPlan& plan,
                   const ObjectRef& target,const ObjectRef& destination) {
    const auto& doc=s.document();const auto relation=base(doc,target);
    if(!relation||!relation->sovereign||*relation->sovereign==destination)
        throw std::invalid_argument("SOVEREIGN_MISMATCH");
    const auto old=*relation->sovereign;
    plan.geometry.readOwners={target,old};
    if(!(target==destination))plan.geometry.readOwners.push_back(destination);
    plan.affectedObjects=plan.geometry.readOwners;
    const auto shape=doc.geometries.get(unit(s,target).geometry);
    for(const auto& candidate:doc.units) {
        const auto ref=territorialRef(candidate.id);
        if(ref==target)continue;
        if(descendant(doc,ref,target)) {plan.affectedObjects.push_back(ref);continue;}
        const auto r=base(doc,ref);
        if(candidate.kind!=UnitKind::Subunit||!r||!r->sovereign||!(*r->sovereign==old))continue;
        if(geometrySignificantOverlap(*doc.geometries.get(candidate.geometry),*shape)) {
            plan.geometry.readOwners.push_back(ref);plan.geometry.removableOwners.push_back(ref);
            plan.affectedObjects.push_back(ref);
        }
    }
    for(const auto& ref:plan.geometry.readOwners)plan.geometry.replacements.push_back({ref,ref});
    for(const auto& ref:plan.affectedObjects) {
        unlocked(unit(s,ref));guard(plan,doc,ref,"geometry");guard(plan,doc,ref,"relation");
        const auto layer=s.layer(nativeLayerId(doc,ref));if(layer&&layer->locked)throw std::invalid_argument("LOCKED");
    }
    for(const auto& ref:plan.geometry.removableOwners)guard(plan,doc,ref,"delete");
    requireRewritableGuards(plan,doc);
    plan.requiresConfirmation=true;
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
    GeometryStore preflight;preflight.insert({"create-preflight",1},in.geometry);
    if(in.kind==UnitKind::Subunit) {if(!in.parent||!in.sovereign)throw std::invalid_argument("SOVEREIGN_MISMATCH");const auto& parent=unit(s,*in.parent);const auto& sovereign=unit(s,*in.sovereign);unlocked(parent);unlocked(sovereign);if(sovereign.kind!=UnitKind::Country)throw std::invalid_argument("SOVEREIGN_MISMATCH");if(parent.kind==UnitKind::Country&&!(*in.parent==*in.sovereign))throw std::invalid_argument("SOVEREIGN_MISMATCH");if(parent.kind==UnitKind::Subunit){const auto r=base(s.document(),*in.parent);if(!r||!r->sovereign||!(*r->sovereign==*in.sovereign))throw std::invalid_argument("SOVEREIGN_MISMATCH");}if(!geometryContains(*s.document().geometries.get(parent.geometry),in.geometry))throw std::invalid_argument("GEOMETRY_OUTSIDE_PARENT");}
    if(in.kind==UnitKind::Region&&in.sovereign){const auto& sovereign=unit(s,*in.sovereign);unlocked(sovereign);if(sovereign.kind!=UnitKind::Country)throw std::invalid_argument("SOVEREIGN_MISMATCH");}
    auto kind=in.kind==UnitKind::Country?TerritorialMutationKind::CreateCountry:in.kind==UnitKind::Subunit?TerritorialMutationKind::CreateSubunit:TerritorialMutationKind::CreateRegion;
    auto p=initial(s,kind,in);p.targets={territorialRef(in.id)};p.affectedObjects=p.targets;p.selectedAfter=p.targets.front();p.geometry.kind=GeometryRequirementKind::Prepared;p.geometry.operation="create";p.impacts.push_back({"create",p.targets.front(),"territorial.create"});guard(p,s.document(),p.targets.front(),"add");return p;
}
TerritorialMutationPlan planDelete(const ProjectSnapshot& s,const DeleteTerritorialIntent& in) {
    if(in.targets.empty())throw std::invalid_argument("INVALID_TARGETS");auto p=initial(s,TerritorialMutationKind::DeleteUnits,in);p.targets=in.targets;p.affectedObjects=in.targets;p.requiresConfirmation=true;
    for(const auto& ref:in.targets){const auto& u=unit(s,ref);unlocked(u);for(const auto& r:s.document().relations)if(!r.dated&&r.parent&&*r.parent==ref&&std::find(in.targets.begin(),in.targets.end(),r.unit)==in.targets.end())throw std::invalid_argument("HAS_BASE_CHILD");guard(p,s.document(),ref,"delete");requireRewritableGuards(p,s.document());
        for(const auto& extension:s.document().extensions) {
            if(extension.status=="migrationArchive")continue;
            if(extension.jsonPointer=="/distributionEntries"||extension.jsonPointer=="/genericFeatures")p.rewrites.push_back({extension.jsonPointer,ReferenceRewriteOperation::RemoveEntry,ref.id,{}});
            else if(extension.jsonPointer=="/itemVisibility"||extension.jsonPointer=="/labelSettings")p.rewrites.push_back({extension.jsonPointer,ReferenceRewriteOperation::DeleteKey,ref.id,{}});
        }
        p.impacts.push_back({"delete",ref,"territorial.delete"});if(u.kind==UnitKind::Country)p.kind=TerritorialMutationKind::DeleteCountry;}
    return p;
}
TerritorialMutationPlan planTransfer(const ProjectSnapshot& s,const TransferSubunitIntent& in) {
    const auto& u=unit(s,in.target);const auto& d=unit(s,in.destinationCountry);
    if(u.kind!=UnitKind::Subunit||d.kind!=UnitKind::Country)throw std::invalid_argument("VALIDATION_FAILED");
    auto p=initial(s,TerritorialMutationKind::TransferSubunit,in);p.targets={in.target};
    p.geometry.kind=GeometryRequirementKind::WorkerPatch;p.geometry.operation="transfer";
    transferScope(s,p,in.target,in.destinationCountry);p.selectedAfter=in.target;
    for(const auto& ref:p.affectedObjects)p.impacts.push_back({"transfer",ref,"territorial.transfer.geometry"});
    return p;
}
TerritorialMutationPlan planConversion(const ProjectSnapshot& s,const ConvertTerritorialTypeIntent& in) {
    const auto& u=unit(s,in.source);unlocked(u);if((u.kind==UnitKind::Subunit&&in.targetKind!=UnitKind::Country)||(u.kind==UnitKind::Country&&in.targetKind!=UnitKind::Subunit))throw std::invalid_argument("VALIDATION_FAILED");
    if(u.kind==UnitKind::Country) {
        if(in.generatedId.empty()||in.generatedId==u.id||s.index().objects.count(territorialRef(in.generatedId))||!in.sovereign||!in.parent)throw std::invalid_argument("INVALID_ARGUMENTS");
        const auto& sovereign=unit(s,*in.sovereign);const auto& parent=unit(s,*in.parent);unlocked(sovereign);unlocked(parent);
        if(*in.sovereign==in.source||sovereign.kind!=UnitKind::Country||!(parent.kind==UnitKind::Country||parent.kind==UnitKind::Subunit))throw std::invalid_argument("SOVEREIGN_MISMATCH");
        if(parent.kind==UnitKind::Country && !(*in.parent==*in.sovereign))throw std::invalid_argument("SOVEREIGN_MISMATCH");
        if(parent.kind==UnitKind::Subunit) {const auto r=base(s.document(),*in.parent);if(!r||!r->sovereign||!(*r->sovereign==*in.sovereign))throw std::invalid_argument("SOVEREIGN_MISMATCH");}
        if(parent.kind==UnitKind::Subunit&&!geometryContains(*s.document().geometries.get(parent.geometry),*s.document().geometries.get(u.geometry)))throw std::invalid_argument("GEOMETRY_OUTSIDE_PARENT");
    }
    std::vector<ObjectRef> sources{in.source};
    if(u.kind==UnitKind::Subunit) {const auto r=base(s.document(),in.source);if(!r||!r->sovereign)throw std::invalid_argument("SOVEREIGN_MISMATCH");sources.push_back(*r->sovereign);}
    else sources.push_back(*in.sovereign);
    const auto geometrySources=sources;
    for(const auto& candidate:s.document().units)if(descendant(s.document(),territorialRef(candidate.id),in.source)){unlocked(candidate);sources.push_back(territorialRef(candidate.id));}
    std::sort(sources.begin(),sources.end());sources.erase(std::unique(sources.begin(),sources.end()),sources.end());
    auto p=initial(s,u.kind==UnitKind::Subunit?TerritorialMutationKind::PromoteSubunitToCountry:TerritorialMutationKind::ConvertCountryToSubunit,in);p.targets={in.source};p.affectedObjects=sources;p.selectedAfter=territorialRef(u.kind==UnitKind::Country?in.generatedId:u.id);p.geometry.kind=GeometryRequirementKind::WorkerPatch;p.geometry.operation=u.kind==UnitKind::Country?"country-to-subunit":"promote";p.geometry.readOwners=geometrySources;
    if(u.kind==UnitKind::Subunit)transferScope(s,p,in.source,in.source);
    else {
        p.geometry.replacements={{in.source,*p.selectedAfter},{*in.sovereign,*in.sovereign}};
        for(const auto& ref:p.affectedObjects){unlocked(unit(s,ref));const auto layer=s.layer(nativeLayerId(s.document(),ref));if(layer&&layer->locked)throw std::invalid_argument("LOCKED");guard(p,s.document(),ref,"geometry");guard(p,s.document(),ref,"relation");}
        p.requiresConfirmation=true;
    }
    for(const auto& e:s.document().extensions)if(e.status!="migrationArchive") {
        if(e.jsonPointer=="/distributionEntries"||e.jsonPointer=="/genericFeatures")p.rewrites.push_back({e.jsonPointer,ReferenceRewriteOperation::ReplaceId,in.source.id,e.jsonPointer=="/genericFeatures"&&u.kind==UnitKind::Country?in.sovereign->id:p.selectedAfter->id});
        else if(e.jsonPointer=="/itemVisibility"||e.jsonPointer=="/labelSettings")p.rewrites.push_back({e.jsonPointer,ReferenceRewriteOperation::ReplaceId,in.source.id,p.selectedAfter->id});
    }
    guard(p,s.document(),in.source,"convert");requireRewritableGuards(p,s.document());
    p.impacts.push_back({"convert",in.source,"territorial.convert.requiresGeometry"});
    for(const auto& ref:p.affectedObjects)if(!(ref==in.source))p.impacts.push_back({"transfer",ref,"territorial.convert.geometry"});
    return p;
}
TerritorialMutationPlan planReplaceGeometry(const ProjectSnapshot& s,const ReplaceGeometryIntent& in) {
    const auto& target=unit(s,in.target);unlocked(target);
    const auto layer=s.layer(nativeLayerId(s.document(),in.target));if(layer&&layer->locked)throw std::invalid_argument("LOCKED");
    auto p=initial(s,TerritorialMutationKind::ReplaceGeometry,in);
    p.targets={in.target};p.affectedObjects={in.target};p.selectedAfter=in.target;
    p.geometry.kind=GeometryRequirementKind::WorkerPatch;p.geometry.operation="replace";
    p.geometry.readOwners={in.target};p.geometry.replacements={{in.target,in.target}};
    p.impacts.push_back({"geometry",in.target,"territorial.geometry.replace"});
    guard(p,s.document(),in.target,"geometry");requireRewritableGuards(p,s.document());
    p.requiresConfirmation=true;return p;
}
TerritorialMutationPlan planMerge(const ProjectSnapshot& s,const MergeTerritorialIntent& in) {
    if(in.donors.empty())throw std::invalid_argument("INVALID_TARGETS");
    const auto& target=unit(s,in.target);unlocked(target);
    std::vector<ObjectRef> owners{in.target};
    for(const auto& donor:in.donors)if(donor==in.target||std::find(owners.begin(),owners.end(),donor)!=owners.end())throw std::invalid_argument("INVALID_TARGETS");else owners.push_back(donor);
    const auto targetRelation=base(s.document(),in.target);
    for(const auto& donor:in.donors) {
        const auto& candidate=unit(s,donor);unlocked(candidate);
        if(candidate.kind!=target.kind)throw std::invalid_argument("VALIDATION_FAILED");
        const auto relation=base(s.document(),donor);
        if(target.kind!=UnitKind::Country) {
            if(!targetRelation||!relation||!(targetRelation->parent==relation->parent)||!(targetRelation->sovereign==relation->sovereign))
                throw std::invalid_argument("SOVEREIGN_MISMATCH");
        }
    }
    std::vector<ObjectRef> connected{in.target},pending=in.donors;
    while(!pending.empty()) {
        const auto found=std::find_if(pending.begin(),pending.end(),[&](const auto& candidate){const auto shape=s.document().geometries.get(unit(s,candidate).geometry);return std::any_of(connected.begin(),connected.end(),[&](const auto& current){return adjacent(*shape,*s.document().geometries.get(unit(s,current).geometry));});});
        if(found==pending.end())throw std::invalid_argument("GEOMETRY_NOT_CONNECTED");
        connected.push_back(*found);pending.erase(found);
    }
    auto p=initial(s,TerritorialMutationKind::MergeTerritorial,in);p.targets=owners;p.affectedObjects=owners;p.selectedAfter=in.target;p.requiresConfirmation=true;
    p.geometry.kind=GeometryRequirementKind::WorkerPatch;p.geometry.operation="merge";p.geometry.readOwners=owners;
    p.geometry.replacements.push_back({in.target,in.target});
    for(const auto& donor:in.donors){p.geometry.replacements.push_back({donor,donor});p.geometry.removableOwners.push_back(donor);guard(p,s.document(),donor,"delete");guard(p,s.document(),donor,"geometry");
        for(const auto& relation:s.document().relations)if(!relation.dated&&relation.parent&&*relation.parent==donor){p.affectedObjects.push_back(relation.unit);unlocked(unit(s,relation.unit));guard(p,s.document(),relation.unit,"relation");}
        for(const auto& extension:s.document().extensions)if(extension.status!="migrationArchive") {
            if(extension.jsonPointer=="/distributionEntries"||extension.jsonPointer=="/genericFeatures")p.rewrites.push_back({extension.jsonPointer,ReferenceRewriteOperation::ReplaceId,donor.id,in.target.id});
            else if(extension.jsonPointer=="/itemVisibility"||extension.jsonPointer=="/labelSettings")p.rewrites.push_back({extension.jsonPointer,ReferenceRewriteOperation::ReplaceId,donor.id,in.target.id});
        }
    }
    guard(p,s.document(),in.target,"geometry");requireRewritableGuards(p,s.document());
    std::sort(p.affectedObjects.begin(),p.affectedObjects.end());p.affectedObjects.erase(std::unique(p.affectedObjects.begin(),p.affectedObjects.end()),p.affectedObjects.end());
    p.impacts.push_back({"merge",in.target,"territorial.merge"});return p;
}
TerritorialMutationPlan planAnnex(const ProjectSnapshot& s,const AnnexTerritoryIntent& in) {
    if(in.donors.empty())throw std::invalid_argument("INVALID_TARGETS");GeometryStore preflight;preflight.insert({"annex-selection",1},in.selection);
    const auto& target=unit(s,in.target);unlocked(target);const auto targetRelation=base(s.document(),in.target);
    std::vector<ObjectRef> owners{in.target};
    for(const auto& donor:in.donors) {
        if(donor==in.target||std::find(owners.begin(),owners.end(),donor)!=owners.end())throw std::invalid_argument("INVALID_TARGETS");
        const auto& candidate=unit(s,donor);unlocked(candidate);if(candidate.kind!=target.kind)throw std::invalid_argument("VALIDATION_FAILED");
        const auto relation=base(s.document(),donor);if(target.kind!=UnitKind::Country&&(!targetRelation||!relation||!(targetRelation->parent==relation->parent)||!(targetRelation->sovereign==relation->sovereign)))throw std::invalid_argument("SOVEREIGN_MISMATCH");
        if(!geometrySignificantOverlap(*s.document().geometries.get(candidate.geometry),in.selection)&&!geometryContains(*s.document().geometries.get(candidate.geometry),in.selection))throw std::invalid_argument("SELECTION_OUTSIDE_DONOR");owners.push_back(donor);
    }
    auto p=initial(s,TerritorialMutationKind::AnnexTerritory,in);p.targets=owners;p.affectedObjects=owners;p.selectedAfter=in.target;p.requiresConfirmation=true;
    p.geometry.kind=GeometryRequirementKind::WorkerPatch;p.geometry.operation="annex";p.geometry.readOwners=owners;
    for(const auto& owner:owners)p.geometry.replacements.push_back({owner,owner});
    p.geometry.removableOwners=in.donors;
    for(const auto& candidate:s.document().units) {
        const auto ref=territorialRef(candidate.id);if(std::find(owners.begin(),owners.end(),ref)!=owners.end())continue;
        if(std::any_of(in.donors.begin(),in.donors.end(),[&](const auto& donor){return descendant(s.document(),ref,donor);})) {
            unlocked(candidate);p.geometry.readOwners.push_back(ref);p.geometry.replacements.push_back({ref,ref});p.geometry.removableOwners.push_back(ref);p.affectedObjects.push_back(ref);guard(p,s.document(),ref,"geometry");guard(p,s.document(),ref,"relation");
        }
    }
    for(const auto& owner:owners){guard(p,s.document(),owner,"geometry");const auto layer=s.layer(nativeLayerId(s.document(),owner));if(layer&&layer->locked)throw std::invalid_argument("LOCKED");}
    requireRewritableGuards(p,s.document());p.impacts.push_back({"annex",in.target,"territorial.annex"});return p;
}
TerritorialMutationPlan planSplit(const ProjectSnapshot& s,const SplitTerritorialIntent& in) {
    const auto& source=unit(s,in.source);unlocked(source);if(in.cutLine.size()<2||in.retainedPart<0||in.retainedPart>1||in.createdId.empty()||in.createdName.empty())throw std::invalid_argument("INVALID_ARGUMENTS");
    const auto created=territorialRef(in.createdId);if(created==in.source||s.index().objects.count(created))throw std::invalid_argument("DUPLICATE_ID");
    const auto layer=s.layer(nativeLayerId(s.document(),in.source));if(layer&&layer->locked)throw std::invalid_argument("LOCKED");
    auto p=initial(s,TerritorialMutationKind::SplitTerritorial,in);p.targets={in.source};p.affectedObjects={in.source};p.selectedAfter=in.source;p.requiresConfirmation=true;
    p.geometry.kind=GeometryRequirementKind::WorkerPatch;p.geometry.operation="split";p.geometry.readOwners={in.source};p.geometry.replacements={{in.source,in.source}};p.geometry.createOwners={created};
    guard(p,s.document(),in.source,"geometry");guard(p,s.document(),created,"add");requireRewritableGuards(p,s.document());p.impacts.push_back({"split",in.source,"territorial.split"});return p;
}
TerritorialMutationPlan planSharedBoundary(const ProjectSnapshot& s,const SharedBoundaryIntent& in) {
    if(in.drafts.size()<2)throw std::invalid_argument("BOUNDARY_REQUIRES_TWO_OWNERS");
    std::vector<ObjectRef> owners;const auto& first=unit(s,in.drafts.front().owner);const auto relation=base(s.document(),in.drafts.front().owner);
    for(const auto& draft:in.drafts){GeometryStore check;check.insert({"boundary-draft",1},draft.geometry);if(std::find(owners.begin(),owners.end(),draft.owner)!=owners.end())throw std::invalid_argument("DUPLICATE_OWNER");const auto& candidate=unit(s,draft.owner);unlocked(candidate);if(candidate.kind!=first.kind)throw std::invalid_argument("VALIDATION_FAILED");if(first.kind!=UnitKind::Country){const auto current=base(s.document(),draft.owner);if(!relation||!current||!(relation->parent==current->parent)||!(relation->sovereign==current->sovereign))throw std::invalid_argument("SOVEREIGN_MISMATCH");}owners.push_back(draft.owner);}
    auto p=initial(s,TerritorialMutationKind::ReconcileSharedBoundary,in);p.targets=owners;p.affectedObjects=owners;p.selectedAfter=owners.front();p.requiresConfirmation=true;p.geometry.kind=GeometryRequirementKind::WorkerPatch;p.geometry.operation="boundary";p.geometry.readOwners=owners;for(const auto& owner:owners){p.geometry.replacements.push_back({owner,owner});guard(p,s.document(),owner,"geometry");}
    for(const auto& candidate:s.document().units){const auto ref=territorialRef(candidate.id);const auto current=base(s.document(),ref);if(current&&current->parent&&std::find(owners.begin(),owners.end(),*current->parent)!=owners.end()){unlocked(candidate);p.affectedObjects.push_back(ref);p.geometry.readOwners.push_back(ref);p.geometry.replacements.push_back({ref,ref});guard(p,s.document(),ref,"geometry");guard(p,s.document(),ref,"relation");}}
    requireRewritableGuards(p,s.document());p.impacts.push_back({"boundary",owners.front(),"territorial.boundary.reconcile"});return p;
}
TerritorialMutationPlan planCoastline(const ProjectSnapshot& s,const CoastlineIntent& in) {
    GeometryStore check;check.insert({"coast-draft",1},in.draft);const auto& target=unit(s,in.target);unlocked(target);ObjectRef country=in.target;if(target.kind!=UnitKind::Country){const auto relation=base(s.document(),in.target);if(!relation||!relation->sovereign)throw std::invalid_argument("SOVEREIGN_REQUIRED");country=*relation->sovereign;unlocked(unit(s,country));}
    auto p=initial(s,TerritorialMutationKind::ReconcileCoastline,in);p.targets={in.target};p.affectedObjects={country};p.selectedAfter=in.target;p.requiresConfirmation=true;p.geometry.kind=GeometryRequirementKind::WorkerPatch;p.geometry.operation="coast";
    if(in.authority==CoastlineAuthority::Independent){p.geometry.readOwners={in.target};p.geometry.replacements={{in.target,in.target}};p.affectedObjects={in.target};guard(p,s.document(),in.target,"geometry");}
    else {p.geometry.readOwners={country};p.geometry.replacements={{country,country}};guard(p,s.document(),country,"geometry");for(const auto& candidate:s.document().units){const auto ref=territorialRef(candidate.id);const auto relation=base(s.document(),ref);if(candidate.kind==UnitKind::Subunit&&relation&&relation->sovereign&&*relation->sovereign==country){unlocked(candidate);p.affectedObjects.push_back(ref);p.geometry.readOwners.push_back(ref);p.geometry.replacements.push_back({ref,ref});p.geometry.removableOwners.push_back(ref);guard(p,s.document(),ref,"geometry");}}}
    requireRewritableGuards(p,s.document());p.impacts.push_back({"coast",country,"territorial.coast.reconcile"});return p;
}
}

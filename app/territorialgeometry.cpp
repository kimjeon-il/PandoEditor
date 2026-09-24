#include "territorialgeometry.h"
#include "geometrycalculator.h"
#include "retainedreferencerewriter.h"
#include <pandoeditor/project.h>
#include <pandoeditor/geometrypredicates.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace pandoeditor;
PrepareResult prepareTerritorialGeometry(const ProjectSnapshot& snapshot,
    const TerritorialMutationPlan& plan,const JobToken& token) {
    PrepareResult failure;
    try {
        if(token.cancelled()){failure.detail="CANCELLED";return failure;}
        if(plan.baseRevision!=snapshot.revision()||plan.projectInstanceId!=snapshot.instanceId()||plan.documentId!=snapshot.document().documentId)
            throw std::runtime_error("STALE_GEOMETRY_REQUEST");
        const auto& requirement=plan.geometry;
        if(requirement.kind!=GeometryRequirementKind::WorkerPatch||requirement.readOwners.empty())throw std::runtime_error("INVALID_GEOMETRY_REQUIREMENT");
        const auto geometry=[&](const ObjectRef& ref)->const Geometry& {
            const auto& u=snapshot.document().units.at(snapshot.index().objects.at(ref));
            return *snapshot.document().geometries.get(u.geometry);
        };
        const auto& target=geometry(requirement.readOwners.front());
        const auto preparePatch=[&](GeometryPatch patch){CommandArguments args;args.action=ApplyTerritorialMutation{plan,std::move(patch)};CommandRequest command{"territorial.geometry.commit",snapshot.instanceId(),snapshot.document().documentId,snapshot.revision(),plan.affectedObjects,std::move(args)};return CommandProcessor::prepare(snapshot,command,[](const ProjectDocument& before,const TerritorialMutationPlan& mutation,std::vector<PreservedExtension>& candidate){const auto rewrite=retainedrefs::rewrite(before,mutation,candidate);return ExtensionRewriteResult{rewrite.ok,rewrite.detail,rewrite.handledExtensionIds};});};
        if(requirement.operation=="boundary") {
            const auto& boundary=std::get<SharedBoundaryIntent>(plan.intent);if(boundary.drafts.size()<2||requirement.readOwners.size()<boundary.drafts.size())throw std::runtime_error("INVALID_GEOMETRY_REQUIREMENT");
            for(std::size_t left=0;left<boundary.drafts.size();++left)for(std::size_t right=left+1;right<boundary.drafts.size();++right){auto collision=calculateGeometry({GeometryOperation::Intersection,boundary.drafts[left].geometry,boundary.drafts[right].geometry},[&]{return token.cancelled();});if(collision.succeeded()&&collision.status!=GeometryOperationStatus::Empty&&significantArea(planarArea(collision.geometry),std::min(planarArea(boundary.drafts[left].geometry),planarArea(boundary.drafts[right].geometry))))throw std::runtime_error("BOUNDARY_OWNER_OVERLAP");}
            GeometryOperationRequest oldUnion;oldUnion.operation=GeometryOperation::Union;GeometryOperationRequest newUnion;newUnion.operation=GeometryOperation::Union;for(const auto& draft:boundary.drafts){oldUnion.operands.push_back(geometry(draft.owner));newUnion.operands.push_back(draft.geometry);}auto before=calculateGeometry(oldUnion,[&]{return token.cancelled();});auto after=calculateGeometry(newUnion,[&]{return token.cancelled();});if(!before.succeeded()||!after.succeeded())throw std::runtime_error("BOUNDARY_UNION_FAILED");auto overlap=calculateGeometry({GeometryOperation::Intersection,before.geometry,after.geometry},[&]{return token.cancelled();});const double beforeArea=planarArea(before.geometry),afterArea=planarArea(after.geometry),overlapArea=overlap.succeeded()?planarArea(overlap.geometry):0,epsilon=std::max(1e-9,beforeArea*1e-9);if(std::abs(beforeArea-afterArea)>epsilon||std::abs(beforeArea-overlapArea)>epsilon)throw std::runtime_error("BOUNDARY_OUTER_UNION_CHANGED");
            GeometryPatch patch;patch.sourceRevision=snapshot.revision();for(const auto& draft:boundary.drafts)patch.replacements.push_back({draft.owner,draft.geometry});
            for(std::size_t index=boundary.drafts.size();index<requirement.readOwners.size();++index){const auto owner=requirement.readOwners[index];const auto& child=geometry(owner);bool transferred=false;for(const auto& draft:boundary.drafts)if(geometryContains(draft.geometry,child)){patch.replacements.push_back({owner,child});transferred=true;break;}if(transferred)continue;const auto relation=std::find_if(snapshot.document().relations.begin(),snapshot.document().relations.end(),[&](const auto& value){return !value.dated&&value.unit==owner;});if(relation==snapshot.document().relations.end()||!relation->parent)throw std::runtime_error("BOUNDARY_CHILD_RELATION_MISSING");const auto parent=std::find_if(boundary.drafts.begin(),boundary.drafts.end(),[&](const auto& draft){return draft.owner==*relation->parent;});if(parent==boundary.drafts.end())throw std::runtime_error("BOUNDARY_CHILD_PARENT_MISSING");auto kept=calculateGeometry({GeometryOperation::Intersection,child,parent->geometry},[&]{return token.cancelled();});if(kept.status==GeometryOperationStatus::Empty)throw std::runtime_error("BOUNDARY_WOULD_REMOVE_CHILD");else if(!kept.succeeded())throw std::runtime_error(kept.detail);else patch.replacements.push_back({owner,std::move(kept.geometry)});}
            return preparePatch(std::move(patch));
        }
        if(requirement.operation=="coast") {
            const auto& coast=std::get<CoastlineIntent>(plan.intent);GeometryPatch patch;patch.sourceRevision=snapshot.revision();
            if(coast.authority==CoastlineAuthority::Independent){patch.replacements.push_back({coast.target,coast.draft});return preparePatch(std::move(patch));}
            const auto country=requirement.readOwners.front();Geometry countryResult=coast.draft;
            if(coast.authority==CoastlineAuthority::Subunit){GeometryOperationRequest unionRequest;unionRequest.operation=GeometryOperation::Union;for(std::size_t index=1;index<requirement.readOwners.size();++index){const auto owner=requirement.readOwners[index];unionRequest.operands.push_back(owner==coast.target?coast.draft:geometry(owner));patch.replacements.push_back({owner,owner==coast.target?coast.draft:geometry(owner)});}auto united=calculateGeometry(unionRequest,[&]{return token.cancelled();});if(!united.succeeded())throw std::runtime_error(united.detail);countryResult=std::move(united.geometry);}
            for(const auto& candidate:snapshot.document().units)if(candidate.kind==UnitKind::Country&&!(territorialRef(candidate.id)==country)){auto overlap=calculateGeometry({GeometryOperation::Intersection,countryResult,*snapshot.document().geometries.get(candidate.geometry)},[&]{return token.cancelled();});if(overlap.succeeded()&&significantArea(planarArea(overlap.geometry),planarArea(countryResult)))throw std::runtime_error("COAST_INVADES_OTHER_COUNTRY");}
            patch.replacements.push_back({country,std::move(countryResult)});if(coast.authority==CoastlineAuthority::Country)for(std::size_t index=1;index<requirement.readOwners.size();++index){const auto owner=requirement.readOwners[index];auto kept=calculateGeometry({GeometryOperation::Intersection,geometry(owner),coast.draft},[&]{return token.cancelled();});if(kept.status==GeometryOperationStatus::Empty)patch.removedGeometryOwners.push_back(owner);else if(!kept.succeeded())throw std::runtime_error(kept.detail);else patch.replacements.push_back({owner,std::move(kept.geometry)});}return preparePatch(std::move(patch));
        }
        if(requirement.operation=="split") {
            if(requirement.readOwners.size()!=1||requirement.replacements.size()!=1||requirement.createOwners.size()!=1)throw std::runtime_error("INVALID_GEOMETRY_REQUIREMENT");const auto& split=std::get<SplitTerritorialIntent>(plan.intent);
            auto result=splitGeometryByLine(target,split.cutLine,[&]{return token.cancelled();});if(result.status==GeometryOperationStatus::Cancelled){failure.detail="CANCELLED";return failure;}if(!result.succeeded())throw std::runtime_error(result.detail);
            Geometry retained=result.candidates[split.retainedPart],created=result.candidates[1-split.retainedPart];for(std::size_t p=0;p<target.polygons.size();++p)if(p!=result.componentIndex)retained.polygons.push_back(target.polygons[p]);
            GeometryPatch patch;patch.sourceRevision=snapshot.revision();patch.replacements.push_back({split.source,std::move(retained)});patch.creations.push_back({territorialRef(split.createdId),std::move(created)});
            CommandArguments args;args.action=ApplyTerritorialMutation{plan,std::move(patch)};CommandRequest requestCommand{"territorial.geometry.commit",snapshot.instanceId(),snapshot.document().documentId,snapshot.revision(),plan.affectedObjects,std::move(args)};
            return CommandProcessor::prepare(snapshot,requestCommand,[](const ProjectDocument& before,const TerritorialMutationPlan& mutation,std::vector<PreservedExtension>& candidate){const auto rewrite=retainedrefs::rewrite(before,mutation,candidate);return ExtensionRewriteResult{rewrite.ok,rewrite.detail,rewrite.handledExtensionIds};});
        }
        if(requirement.readOwners.size()<2||requirement.readOwners.size()!=requirement.replacements.size())throw std::runtime_error("INVALID_GEOMETRY_REQUIREMENT");
        const bool converting=requirement.operation=="country-to-subunit";
        if(requirement.operation=="merge") {
            GeometryOperationRequest request;request.operation=GeometryOperation::Union;
            for(const auto& owner:requirement.readOwners)request.operands.push_back(geometry(owner));
            auto result=calculateGeometry(request,[&]{return token.cancelled();});
            if(result.status==GeometryOperationStatus::Cancelled){failure.detail="CANCELLED";return failure;}
            if(!result.succeeded())throw std::runtime_error(result.detail);
            GeometryPatch patch;patch.sourceRevision=snapshot.revision();patch.replacements.push_back({requirement.replacements.front().result,std::move(result.geometry)});
            for(std::size_t i=1;i<requirement.replacements.size();++i)patch.removedGeometryOwners.push_back(requirement.replacements[i].result);
            CommandArguments args;args.action=ApplyTerritorialMutation{plan,std::move(patch)};
            CommandRequest requestCommand{"territorial.geometry.commit",snapshot.instanceId(),snapshot.document().documentId,snapshot.revision(),plan.affectedObjects,std::move(args)};
            return CommandProcessor::prepare(snapshot,requestCommand,[](const ProjectDocument& before,const TerritorialMutationPlan& mutation,std::vector<PreservedExtension>& candidate){const auto rewrite=retainedrefs::rewrite(before,mutation,candidate);return ExtensionRewriteResult{rewrite.ok,rewrite.detail,rewrite.handledExtensionIds};});
        }
        if(requirement.operation=="annex") {
            const auto& annex=std::get<AnnexTerritoryIntent>(plan.intent);GeometryPatch patch;patch.sourceRevision=snapshot.revision();
            Geometry donorCoverage=geometry(annex.donors.front());if(annex.donors.size()>1){GeometryOperationRequest unionRequest;unionRequest.operation=GeometryOperation::Union;for(const auto& donor:annex.donors)unionRequest.operands.push_back(geometry(donor));auto donorUnion=calculateGeometry(unionRequest,[&]{return token.cancelled();});if(!donorUnion.succeeded())throw std::runtime_error(donorUnion.detail);donorCoverage=std::move(donorUnion.geometry);}if(!geometryContains(donorCoverage,annex.selection))throw std::runtime_error("SELECTION_OUTSIDE_DONOR");
            auto target=calculateGeometry({GeometryOperation::Union,geometry(annex.target),annex.selection},[&]{return token.cancelled();});if(!target.succeeded())throw std::runtime_error(target.detail);patch.replacements.push_back({annex.target,std::move(target.geometry)});
            for(std::size_t i=1;i<requirement.readOwners.size();++i){const auto& owner=requirement.readOwners[i];const auto& original=geometry(owner);
                if(std::find(annex.donors.begin(),annex.donors.end(),owner)==annex.donors.end()&&geometryContains(annex.selection,original)){patch.replacements.push_back({owner,original});continue;}
                auto remaining=calculateGeometry({GeometryOperation::Difference,original,annex.selection},[&]{return token.cancelled();});if(remaining.status==GeometryOperationStatus::Cancelled){failure.detail="CANCELLED";return failure;}if(remaining.status==GeometryOperationStatus::Empty)patch.removedGeometryOwners.push_back(owner);else if(!remaining.succeeded())throw std::runtime_error(remaining.detail);else patch.replacements.push_back({owner,std::move(remaining.geometry)});
            }
            CommandArguments args;args.action=ApplyTerritorialMutation{plan,std::move(patch)};CommandRequest requestCommand{"territorial.geometry.commit",snapshot.instanceId(),snapshot.document().documentId,snapshot.revision(),plan.affectedObjects,std::move(args)};
            return CommandProcessor::prepare(snapshot,requestCommand,[](const ProjectDocument& before,const TerritorialMutationPlan& mutation,std::vector<PreservedExtension>& candidate){const auto rewrite=retainedrefs::rewrite(before,mutation,candidate);return ExtensionRewriteResult{rewrite.ok,rewrite.detail,rewrite.handledExtensionIds};});
        }
        if(!converting&&requirement.operation!="promote"&&requirement.operation!="transfer")
            throw std::runtime_error("UNSUPPORTED_GEOMETRY_OPERATION");
        GeometryPatch patch;patch.sourceRevision=snapshot.revision();
        patch.replacements.push_back({requirement.replacements.front().result,target});
        for(std::size_t i=1;i<requirement.readOwners.size();++i) {
            if(token.cancelled()){failure.detail="CANCELLED";return failure;}
            const bool destination=converting||(requirement.operation=="transfer"&&i==2);
            const auto& original=geometry(requirement.readOwners[i]);
            if(!converting&&!destination&&i>1) {
                const auto overlap=calculateGeometry({GeometryOperation::Intersection,original,target},[&]{return token.cancelled();});
                if(!overlap.succeeded())throw std::runtime_error(overlap.detail.empty()?"CANCELLED":overlap.detail);
                if(!significantArea(planarArea(overlap.geometry),planarArea(original))) {
                    patch.replacements.push_back({requirement.replacements[i].result,original});continue;
                }
            }
            auto result=calculateGeometry({destination?GeometryOperation::Union:GeometryOperation::Difference,original,target},[&]{return token.cancelled();});
            if(result.status==GeometryOperationStatus::Cancelled){failure.detail="CANCELLED";return failure;}
            if(!result.succeeded())throw std::runtime_error(result.detail);
            const auto& owner=requirement.replacements[i].result;
            if(!destination&&i==1&&!significantArea(planarArea(result.geometry),planarArea(original)))
                throw std::runtime_error("기존 국가의 영역 전체를 이전할 수 없습니다.");
            if(result.status==GeometryOperationStatus::Empty) {
                if(std::find(requirement.removableOwners.begin(),requirement.removableOwners.end(),owner)==requirement.removableOwners.end())
                    throw std::runtime_error("EMPTY_REQUIRED_GEOMETRY");
                patch.removedGeometryOwners.push_back(owner);
            } else patch.replacements.push_back({owner,std::move(result.geometry)});
            token.reportProgress(static_cast<int>(80*i/requirement.readOwners.size()));
        }
        if(token.cancelled()){failure.detail="CANCELLED";return failure;}
        CommandArguments args;args.action=ApplyTerritorialMutation{plan,std::move(patch)};
        CommandRequest request{"territorial.geometry.commit",snapshot.instanceId(),snapshot.document().documentId,
            snapshot.revision(),plan.affectedObjects,std::move(args)};
        auto prepared=CommandProcessor::prepare(snapshot,request,
            [](const ProjectDocument& before,const TerritorialMutationPlan& mutation,std::vector<PreservedExtension>& candidate){
                const auto result=retainedrefs::rewrite(before,mutation,candidate);
                return ExtensionRewriteResult{result.ok,result.detail,result.handledExtensionIds};
            });
        if(token.cancelled()){failure.detail="CANCELLED";return failure;}
        token.reportProgress(100);return prepared;
    }catch(const std::exception& error){failure.error=CommandError::PrepareFailed;failure.detail=error.what();return failure;}
}

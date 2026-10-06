#include "territoryselectionruntime.h"
#include "territorialgeometry.h"
#include "territorialpreviewruntime.h"
#include "geometrycalculator.h"
#include <pandoeditor/map/territoryselection.h>
#include "retainedreferencerewriter.h"
#include <pandoeditor/project.h>
#include <pandoeditor/geometrypredicates.h>
#include <algorithm>
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
            return *snapshot.document().geometries.get(pandoeditor::staticGeometryBinding(snapshot.document(),u.id).geometryRef);
        };
        const auto& target=geometry(requirement.readOwners.front());
        const auto preparePatch=[&](GeometryPatch patch){CommandArguments args;args.action=ApplyTerritorialMutation{plan,std::move(patch)};CommandRequest command{"territorial.geometry.commit",snapshot.instanceId(),snapshot.document().documentId,snapshot.revision(),plan.affectedObjects,std::move(args)};return CommandProcessor::prepare(snapshot,command,[](const ProjectDocument& before,const TerritorialMutationPlan& mutation,std::vector<PreservedExtension>& candidate){const auto rewrite=retainedrefs::rewrite(before,mutation,candidate);return ExtensionRewriteResult{rewrite.ok,rewrite.detail,rewrite.handledExtensionIds};});};
        if(requirement.operation=="boundary") {
            return prepareBoundaryGeometryCommit(snapshot,calculateBoundaryGeometryPreview(snapshot,std::get<SharedBoundaryIntent>(plan.intent),territorialPreviewCalculators(),token),token);
        }
        if(requirement.operation=="coast") {
            const auto& coast=std::get<CoastlineIntent>(plan.intent);GeometryPatch patch;patch.sourceRevision=snapshot.revision();
            if(coast.authority==CoastlineAuthority::Independent){patch.replacements.push_back({coast.target,coast.draft});return preparePatch(std::move(patch));}
            const auto country=requirement.readOwners.front();Geometry countryResult=coast.draft;
            if(coast.authority==CoastlineAuthority::Subunit){GeometryOperationRequest unionRequest;unionRequest.operation=GeometryOperation::Union;for(std::size_t index=1;index<requirement.readOwners.size();++index){const auto owner=requirement.readOwners[index];unionRequest.operands.push_back(owner==coast.target?coast.draft:geometry(owner));patch.replacements.push_back({owner,owner==coast.target?coast.draft:geometry(owner)});}auto united=calculateGeometry(unionRequest,[&]{return token.cancelled();});if(!united.succeeded())throw std::runtime_error(united.detail);countryResult=std::move(united.geometry);}
            for(const auto& candidate:snapshot.document().units)if(isRootGeneral(snapshot.document(),candidate)&&!(territorialRef(candidate.id)==country)){auto overlap=calculateGeometry({GeometryOperation::Intersection,countryResult,*snapshot.document().geometries.get(pandoeditor::staticGeometryBinding(snapshot.document(),candidate.id).geometryRef)},[&]{return token.cancelled();});if(overlap.succeeded()&&significantArea(planarArea(overlap.geometry),planarArea(countryResult)))throw std::runtime_error("COAST_INVADES_OTHER_COUNTRY");}
            patch.replacements.push_back({country,std::move(countryResult)});if(coast.authority==CoastlineAuthority::Country)for(std::size_t index=1;index<requirement.readOwners.size();++index){const auto owner=requirement.readOwners[index];auto kept=calculateGeometry({GeometryOperation::Intersection,geometry(owner),coast.draft},[&]{return token.cancelled();});if(kept.status==GeometryOperationStatus::Empty)patch.removedGeometryOwners.push_back(owner);else if(!kept.succeeded())throw std::runtime_error(kept.detail);else patch.replacements.push_back({owner,std::move(kept.geometry)});}return preparePatch(std::move(patch));
        }
        if(requirement.operation=="split") {
            return prepareSplitGeometryCommit(snapshot,calculateSplitGeometryPreview(snapshot,std::get<SplitTerritorialIntent>(plan.intent),territorialPreviewCalculators(),token),token);
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
            // Root-country commits use transferLandDependents in the web: a
            // wholly transferred child is removed, not reparented. Sibling
            // territorial annex deliberately retains its separate contract.
            const auto& targetUnit=snapshot.document().units.at(snapshot.index().objects.at(annex.target));
            const bool rootAnnex=isRootGeneral(snapshot.document(),targetUnit);
            if(rootAnnex)return prepareAnnexGeometryCommit(snapshot,
                calculateAnnexGeometryPreview(snapshot,{annex.target,annex.donors,annex.selection},territorialPreviewCalculators(),token),token);
            Geometry donorCoverage=geometry(annex.donors.front());if(annex.donors.size()>1){GeometryOperationRequest unionRequest;unionRequest.operation=GeometryOperation::Union;for(const auto& donor:annex.donors)unionRequest.operands.push_back(geometry(donor));auto donorUnion=calculateGeometry(unionRequest,[&]{return token.cancelled();});if(!donorUnion.succeeded())throw std::runtime_error(donorUnion.detail);donorCoverage=std::move(donorUnion.geometry);}if(!geometryContains(donorCoverage,annex.selection))throw std::runtime_error("SELECTION_OUTSIDE_DONOR");
            auto target=calculateGeometry({GeometryOperation::Union,geometry(annex.target),annex.selection},[&]{return token.cancelled();});if(!target.succeeded())throw std::runtime_error(target.detail);patch.replacements.push_back({annex.target,std::move(target.geometry)});
            for(std::size_t i=1;i<requirement.readOwners.size();++i){const auto& owner=requirement.readOwners[i];const auto& original=geometry(owner);
                if(!rootAnnex&&std::find(annex.donors.begin(),annex.donors.end(),owner)==annex.donors.end()&&geometryContains(annex.selection,original)){patch.replacements.push_back({owner,original});continue;}
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

PrepareResult prepareDrawnTerritoryAnnex(const ProjectSnapshot& snapshot,
    const AnnexTerritoryIntent& input,const JobToken& token) {
    PrepareResult failure;failure.error=CommandError::PrepareFailed;
    try {
        if(token.cancelled()){failure.detail="CANCELLED";return failure;}
        const auto geometry=[&](const ObjectRef& ref)->const Geometry& {
            const auto& unit=snapshot.document().units.at(snapshot.index().objects.at(ref));
            return *snapshot.document().geometries.get(staticGeometryBinding(snapshot.document(),unit.id).geometryRef);
        };
        GeometryOperationRequest unionRequest;unionRequest.operation=GeometryOperation::Union;
        for(const auto& donor:input.donors)unionRequest.operands.push_back(geometry(donor));
        if(unionRequest.operands.empty())throw std::runtime_error("INVALID_TARGETS");
        Geometry workingSource=unionRequest.operands.front();
        if(unionRequest.operands.size()>1) {
            const auto united=calculateGeometry(unionRequest,[&]{return token.cancelled();});
            if(!united.succeeded())throw std::runtime_error(token.cancelled()?"CANCELLED":united.detail);
            workingSource=united.geometry;
        }
        const auto candidates=prepareTerritoryPolygonCandidates(input.selection,workingSource,geometry(input.target),territorySelectionCalculators(),[&]{return token.cancelled();});
        if(!candidates.succeeded()||candidates.candidates.empty())
            throw std::runtime_error(token.cancelled()?"CANCELLED":candidates.detail.empty()?"NO_TRANSFERABLE_SELECTION":candidates.detail);
        auto annex=input;annex.selection=candidates.candidates.front().geometry;annex.donors.clear();
        // Source selection can include remote, untouched donors. The actual
        // command patch/affected refs contain only donors with positive area.
        for(const auto& donor:input.donors) {
            const auto overlap=calculateGeometry({GeometryOperation::Intersection,geometry(donor),annex.selection},[&]{return token.cancelled();});
            if(!overlap.succeeded())throw std::runtime_error(overlap.detail);
            if(overlap.status!=GeometryOperationStatus::Empty)annex.donors.push_back(donor);
        }
        if(token.cancelled()){failure.detail="CANCELLED";return failure;}
        auto plan=CommandProcessor::planTerritorial(snapshot,annex);
        if(!plan.ok()||!plan.plan){failure.error=plan.error;failure.detail=plan.detail;return failure;}
        return prepareTerritorialGeometry(snapshot,*plan.plan,token);
    }catch(const std::exception& error){failure.detail=error.what();return failure;}
}

PrepareResult prepareAnnexGeometryCommit(const ProjectSnapshot& snapshot,const AnnexGeometryPreviewResult& result,const JobToken& token) {
    PrepareResult failure;failure.error=CommandError::PrepareFailed;
    if(token.cancelled()){failure.detail="CANCELLED";return failure;}
    if(!result.ok()||!result.plan){failure.error=result.error;failure.detail=result.detail.empty()?"INVALID_ANNEX_GEOMETRY_RECEIPT":result.detail;return failure;}
    const auto& plan=*result.plan;
    if(plan.projectInstanceId!=snapshot.instanceId()){failure.error=CommandError::ProjectMismatch;failure.detail="PROJECT_MISMATCH";return failure;}
    if(plan.documentId!=snapshot.document().documentId){failure.error=CommandError::DocumentMismatch;failure.detail="DOCUMENT_MISMATCH";return failure;}
    if(plan.baseRevision!=snapshot.revision()){failure.error=CommandError::StaleRevision;failure.detail="STALE_GEOMETRY_REQUEST";return failure;}
    CommandArguments args;args.action=ApplyTerritorialMutation{plan,result.patch};
    CommandRequest command{"territorial.geometry.commit",snapshot.instanceId(),snapshot.document().documentId,snapshot.revision(),plan.affectedObjects,std::move(args)};
    auto prepared=CommandProcessor::prepare(snapshot,command,[](const ProjectDocument& before,const TerritorialMutationPlan& mutation,std::vector<PreservedExtension>& candidate){
        const auto rewritten=retainedrefs::rewrite(before,mutation,candidate);return ExtensionRewriteResult{rewritten.ok,rewritten.detail,rewritten.handledExtensionIds};
    });
    if(token.cancelled()){failure.detail="CANCELLED";return failure;}
    return prepared;
}

PrepareResult prepareSplitGeometryCommit(const ProjectSnapshot& snapshot,const SplitGeometryPreviewResult& result,const JobToken& token) {
    PrepareResult failure;failure.error=CommandError::PrepareFailed;
    if(token.cancelled()){failure.detail="CANCELLED";return failure;}
    if(!result.ok()||!result.plan){failure.error=result.error;failure.detail=result.detail.empty()?"INVALID_SPLIT_GEOMETRY_RECEIPT":result.detail;return failure;}
    const auto& plan=*result.plan;
    if(plan.projectInstanceId!=snapshot.instanceId()){failure.error=CommandError::ProjectMismatch;failure.detail="PROJECT_MISMATCH";return failure;}
    if(plan.documentId!=snapshot.document().documentId){failure.error=CommandError::DocumentMismatch;failure.detail="DOCUMENT_MISMATCH";return failure;}
    if(plan.baseRevision!=snapshot.revision()){failure.error=CommandError::StaleRevision;failure.detail="STALE_GEOMETRY_REQUEST";return failure;}
    // The root worker can display a complete transfer, but bounded creation
    // cannot archive/apply it. Never turn that inert preview into source deletion.
    if(result.remainingGeometry.polygons.empty()){failure.error=CommandError::InvalidArguments;failure.detail="SPLIT_SOURCE_EXHAUSTED";return failure;}
    CommandArguments args;args.action=ApplyTerritorialMutation{plan,result.patch};
    CommandRequest command{"territorial.geometry.commit",snapshot.instanceId(),snapshot.document().documentId,snapshot.revision(),plan.affectedObjects,std::move(args)};
    auto prepared=CommandProcessor::prepare(snapshot,command,[](const ProjectDocument& before,const TerritorialMutationPlan& mutation,std::vector<PreservedExtension>& candidate){
        const auto rewritten=retainedrefs::rewrite(before,mutation,candidate);return ExtensionRewriteResult{rewritten.ok,rewritten.detail,rewritten.handledExtensionIds};
    });
    if(token.cancelled()){failure.detail="CANCELLED";return failure;}return prepared;
}

PrepareResult prepareBoundaryGeometryCommit(const ProjectSnapshot& snapshot,const BoundaryGeometryPreviewResult& result,const JobToken& token) {
    PrepareResult failure;failure.error=CommandError::PrepareFailed;
    if(token.cancelled()){failure.detail="CANCELLED";return failure;}
    if(!result.ok()||!result.plan()){failure.error=result.error==CommandError::None?CommandError::PrepareFailed:result.error;failure.detail=result.detail.empty()?"INVALID_BOUNDARY_GEOMETRY_RECEIPT":result.detail;return failure;}
    const auto& plan=*result.plan();
    if(plan.projectInstanceId!=snapshot.instanceId()){failure.error=CommandError::ProjectMismatch;failure.detail="PROJECT_MISMATCH";return failure;}
    if(plan.documentId!=snapshot.document().documentId){failure.error=CommandError::DocumentMismatch;failure.detail="DOCUMENT_MISMATCH";return failure;}
    if(plan.baseRevision!=snapshot.revision()){failure.error=CommandError::StaleRevision;failure.detail="STALE_GEOMETRY_REQUEST";return failure;}
    CommandArguments args;args.action=ApplyTerritorialMutation{plan,result.patch()};
    CommandRequest command{"territorial.geometry.commit",snapshot.instanceId(),snapshot.document().documentId,snapshot.revision(),plan.affectedObjects,std::move(args)};
    auto prepared=CommandProcessor::prepare(snapshot,command,[](const ProjectDocument& before,const TerritorialMutationPlan& mutation,std::vector<PreservedExtension>& candidate){const auto rewritten=retainedrefs::rewrite(before,mutation,candidate);return ExtensionRewriteResult{rewritten.ok,rewritten.detail,rewritten.handledExtensionIds};});
    if(token.cancelled()){failure.detail="CANCELLED";return failure;}return prepared;
}

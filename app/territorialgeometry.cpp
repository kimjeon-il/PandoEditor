#include "territorialgeometry.h"
#include "geometrycalculator.h"
#include "splitgeometrynormalizer.h"
#include "geometryruntime_p.h"
#include "riverpartitioncalculator.h"
#include "riverareacalculator.h"
#include "territoryselection.h"
#include "retainedreferencerewriter.h"
#include <pandoeditor/project.h>
#include <pandoeditor/geometrypredicates.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <set>
#include <limits>

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
            const auto& boundary=std::get<SharedBoundaryIntent>(plan.intent);if(boundary.drafts.size()<2||requirement.readOwners.size()<boundary.drafts.size())throw std::runtime_error("INVALID_GEOMETRY_REQUIREMENT");
            for(std::size_t left=0;left<boundary.drafts.size();++left)for(std::size_t right=left+1;right<boundary.drafts.size();++right){auto collision=calculateGeometry({GeometryOperation::Intersection,boundary.drafts[left].geometry,boundary.drafts[right].geometry},[&]{return token.cancelled();});if(collision.succeeded()&&collision.status!=GeometryOperationStatus::Empty&&significantArea(planarArea(collision.geometry),std::min(planarArea(boundary.drafts[left].geometry),planarArea(boundary.drafts[right].geometry))))throw std::runtime_error("BOUNDARY_OWNER_OVERLAP");}
            GeometryOperationRequest oldUnion;oldUnion.operation=GeometryOperation::Union;GeometryOperationRequest newUnion;newUnion.operation=GeometryOperation::Union;for(const auto& draft:boundary.drafts){oldUnion.operands.push_back(geometry(draft.owner));newUnion.operands.push_back(draft.geometry);}auto before=calculateGeometry(oldUnion,[&]{return token.cancelled();});auto after=calculateGeometry(newUnion,[&]{return token.cancelled();});if(!before.succeeded()||!after.succeeded())throw std::runtime_error("BOUNDARY_UNION_FAILED");auto overlap=calculateGeometry({GeometryOperation::Intersection,before.geometry,after.geometry},[&]{return token.cancelled();});const double beforeArea=planarArea(before.geometry),afterArea=planarArea(after.geometry),overlapArea=overlap.succeeded()?planarArea(overlap.geometry):0,epsilon=std::max(1e-9,beforeArea*1e-9);if(std::abs(beforeArea-afterArea)>epsilon||std::abs(beforeArea-overlapArea)>epsilon)throw std::runtime_error("BOUNDARY_OUTER_UNION_CHANGED");
            GeometryPatch patch;patch.sourceRevision=snapshot.revision();for(const auto& draft:boundary.drafts)patch.replacements.push_back({draft.owner,draft.geometry});
            for(std::size_t index=boundary.drafts.size();index<requirement.readOwners.size();++index){const auto owner=requirement.readOwners[index];const auto& child=geometry(owner);bool transferred=false;for(const auto& draft:boundary.drafts)if(geometryContains(draft.geometry,child)){patch.replacements.push_back({owner,child});transferred=true;break;}if(transferred)continue;const auto& relation=staticParentRelation(snapshot.document(),owner.id);if(relation.parentId.empty())throw std::runtime_error("BOUNDARY_CHILD_RELATION_MISSING");const auto parent=std::find_if(boundary.drafts.begin(),boundary.drafts.end(),[&](const auto& draft){return draft.owner.id==relation.parentId;});if(parent==boundary.drafts.end())throw std::runtime_error("BOUNDARY_CHILD_PARENT_MISSING");auto kept=calculateGeometry({GeometryOperation::Intersection,child,parent->geometry},[&]{return token.cancelled();});if(kept.status==GeometryOperationStatus::Empty)throw std::runtime_error("BOUNDARY_WOULD_REMOVE_CHILD");else if(!kept.succeeded())throw std::runtime_error(kept.detail);else patch.replacements.push_back({owner,std::move(kept.geometry)});}
            return preparePatch(std::move(patch));
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
            return prepareSplitGeometryCommit(snapshot,calculateSplitGeometryPreview(snapshot,std::get<SplitTerritorialIntent>(plan.intent),token),token);
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
                calculateAnnexGeometryPreview(snapshot,{annex.target,annex.donors,annex.selection},token),token);
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
        const auto candidates=prepareTerritoryPolygonCandidates(input.selection,workingSource,geometry(input.target),[&]{return token.cancelled();});
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

namespace {
const Geometry& annexGeometry(const ProjectSnapshot& snapshot,const ObjectRef& owner) {
    if(owner.domain!="territorial"||!snapshot.index().objects.count(owner))throw std::invalid_argument("INVALID_TARGETS");
    const auto geometry=snapshot.document().geometries.get(staticGeometryBinding(snapshot.document(),owner.id).geometryRef);
    if(!geometry)throw std::invalid_argument("INVALID_GEOMETRY_REQUIREMENT");
    return *geometry;
}
void annexCheckCancelled(const JobToken& token) {
    if(token.cancelled())throw std::runtime_error("CANCELLED");
}
Geometry annexCalculate(GeometryOperation operation,const Geometry& left,const Geometry& right,const JobToken& token,bool river=false) {
    annexCheckCancelled(token);
    if(river) {
        auto calculated=calculateRiverGeometryIntermediate({operation,left,right},[&]{return token.cancelled();});
        annexCheckCancelled(token);
        if(!calculated.succeeded())throw std::runtime_error(calculated.detail);
        return std::move(calculated.geometry);
    }
    const auto calculated=calculateGeometry({operation,left,right},[&]{return token.cancelled();});
    annexCheckCancelled(token);
    if(!calculated.succeeded())throw std::runtime_error(calculated.detail);
    return calculated.geometry;
}
Geometry annexUnion(const std::vector<Geometry>& geometries,const JobToken& token,bool river=false) {
    annexCheckCancelled(token);
    if(geometries.empty())return {};
    if(geometries.size()==1)return geometries.front();
    GeometryOperationRequest request;request.operation=GeometryOperation::Union;request.operands=geometries;
    if(river) {
        auto calculated=calculateRiverGeometryIntermediate(request,[&]{return token.cancelled();});annexCheckCancelled(token);
        if(!calculated.succeeded())throw std::runtime_error(calculated.detail);
        return std::move(calculated.geometry);
    }
    const auto calculated=calculateGeometry(request,[&]{return token.cancelled();});annexCheckCancelled(token);
    if(!calculated.succeeded())throw std::runtime_error(calculated.detail);
    return calculated.geometry;
}
struct AnnexBounds { double minX=180,minY=90,maxX=-180,maxY=-90; };
AnnexBounds annexBounds(const Geometry& geometry) {
    AnnexBounds bounds;
    for(const auto& polygon:geometry.polygons)for(const auto& ring:polygon)for(const auto point:ring) {
        bounds.minX=std::min(bounds.minX,point.x);bounds.maxX=std::max(bounds.maxX,point.x);
        bounds.minY=std::min(bounds.minY,point.y);bounds.maxY=std::max(bounds.maxY,point.y);
    }
    return bounds;
}
bool annexBoundsOverlap(const AnnexBounds& left,const AnnexBounds& right) {
    return left.minX<=right.maxX&&left.maxX>=right.minX&&left.minY<=right.maxY&&left.maxY>=right.minY;
}
Geometry annexPolygon(const Polygon& polygon) { Geometry geometry;geometry.polygons={polygon};return geometry; }
// Exact pinned annex-slivers local-area and endpoint-dot overlap predicates.
// Translation is an existence test; it does not introduce an area tolerance.
double annexLocalRingArea(const Ring& ring,double scaleX=1,double scaleY=1) {
    if(ring.empty())return 0;const auto origin=ring.front();double sum=0;
    for(std::size_t i=1;i<ring.size();++i)
        sum+=(ring[i-1].x-origin.x)*(ring[i].y-origin.y)-(ring[i].x-origin.x)*(ring[i-1].y-origin.y);
    return std::abs(sum/2)*scaleX*scaleY;
}
double annexLocalPolygonArea(const Polygon& polygon,double scaleX=1,double scaleY=1) {
    if(polygon.empty())return 0;double holes=0;
    for(std::size_t i=1;i<polygon.size();++i)holes+=annexLocalRingArea(polygon[i],scaleX,scaleY);
    return annexLocalRingArea(polygon.front(),scaleX,scaleY)-holes;
}
bool annexPositiveArea(const Geometry& geometry) {
    return std::any_of(geometry.polygons.begin(),geometry.polygons.end(),[](const auto& polygon){return annexLocalPolygonArea(polygon)>0;});
}
double annexSliverAreaM2(const Polygon& polygon) {
    const auto bounds=annexBounds(annexPolygon(polygon));
    if(bounds.maxX-bounds.minX>1||bounds.maxY-bounds.minY>1||std::max(std::abs(bounds.minY),std::abs(bounds.maxY))>80)
        return std::numeric_limits<double>::infinity();
    const auto pi=std::acos(-1.),meters=6371008.8*pi/180;
    const auto x=meters*std::cos((bounds.minY+bounds.maxY)/2*pi/180);
    return std::max(0.,annexLocalPolygonArea(polygon,x,meters));
}
bool annexSharesBoundary(const Polygon& polygon,const Geometry& others) {
    constexpr double epsilon=1e-11;
    for(const auto& ring:polygon)for(std::size_t i=1;i<ring.size();++i) {
        const auto a=ring[i-1],b=ring[i];const auto dx=b.x-a.x,dy=b.y-a.y,length=std::hypot(dx,dy);
        if(length<=epsilon)continue;
        for(const auto& other:others.polygons)for(const auto& otherRing:other)for(std::size_t j=1;j<otherRing.size();++j) {
            const auto c=otherRing[j-1],d=otherRing[j];
            if(std::abs(dx*(c.y-a.y)-dy*(c.x-a.x))/length>epsilon||std::abs(dx*(d.y-a.y)-dy*(d.x-a.x))/length>epsilon)continue;
            const auto start=((c.x-a.x)*dx+(c.y-a.y)*dy)/length,end=((d.x-a.x)*dx+(d.y-a.y)*dy)/length;
            if(std::min(length,std::max(start,end))-std::max(0.,std::min(start,end))>epsilon)return true;
        }
    }
    return false;
}
Geometry annexNormalizeRiver(const Geometry& geometry,const JobToken& token) {
    if(geometry.polygons.empty())return geometry;
    auto normalized=normalizeRiverGeometry(geometry,[&]{return token.cancelled();});annexCheckCancelled(token);
    if(!normalized.succeeded())throw std::runtime_error(normalized.detail.toStdString());
    // A skipped microscopic remnant must not vanish at the presentation boundary.
    // Strict failure is safer than claiming a transfer which never included it.
    if(!normalized.geometry||normalized.geometry->polygons.size()!=geometry.polygons.size())
        throw std::runtime_error("RIVER_REMAINDER_NOT_REPRESENTABLE");
    for(std::size_t p=0;p<geometry.polygons.size();++p)
        if(normalized.geometry->polygons[p].size()!=geometry.polygons[p].size())throw std::runtime_error("RIVER_REMAINDER_NOT_REPRESENTABLE");
    GeometryStore validation;validation.insert({"river-annex-normalized",1},*normalized.geometry);
    return std::move(*normalized.geometry);
}
struct AnnexSlivers { Geometry geometry;double areaM2=0; };
// The web copies untouched polygons exactly and only sends affected pieces to
// clipping. In particular, remote island order/winding must not be normalized.
std::pair<bool,Geometry> annexSubtract(const Geometry& original,const Geometry& transfer,const JobToken& token,
    bool river=false,const std::vector<TerritorySelectionRiverSliverContext>& contexts={},
    const std::string& donorId={},AnnexSlivers* slivers=nullptr) {
    Geometry remaining;bool affected=false;const auto transferBounds=annexBounds(transfer);
    for(std::size_t polygonIndex=0;polygonIndex<original.polygons.size();++polygonIndex) {
        const auto& polygon=original.polygons[polygonIndex];
        annexCheckCancelled(token);const auto source=annexPolygon(polygon);
        if(!annexBoundsOverlap(annexBounds(source),transferBounds)) {
            remaining.polygons.push_back(polygon);continue;
        }
        const auto overlap=annexCalculate(GeometryOperation::Intersection,source,transfer,token,river);
        if(river?!annexPositiveArea(overlap):planarArea(overlap)<=0) {remaining.polygons.push_back(polygon);continue;}
        affected=true;const auto pieces=annexCalculate(GeometryOperation::Difference,source,transfer,token,river);
        const auto context=std::find_if(contexts.begin(),contexts.end(),[&](const auto& row){return row.donorId==donorId&&row.polygonIndex==polygonIndex;});
        Geometry unselected;
        if(context!=contexts.end())for(const auto& value:context->unselectedGeometries)
            unselected.polygons.insert(unselected.polygons.end(),value.polygons.begin(),value.polygons.end());
        for(const auto& piece:pieces.polygons) {
            const auto size=slivers&&context!=contexts.end()?annexSliverAreaM2(piece):std::numeric_limits<double>::infinity();
            if(slivers&&context!=contexts.end()&&size>0&&size<=1&&slivers->areaM2+size<=10
                &&!annexPositiveArea(annexCalculate(GeometryOperation::Intersection,annexPolygon(piece),unselected,token,true))
                &&annexSharesBoundary(piece,transfer)&&!annexSharesBoundary(piece,unselected)) {
                slivers->geometry.polygons.push_back(piece);slivers->areaM2+=size;
            } else {
                const auto kept=river?annexNormalizeRiver(annexPolygon(piece),token):annexPolygon(piece);
                remaining.polygons.insert(remaining.polygons.end(),kept.polygons.begin(),kept.polygons.end());
            }
        }
    }
    if(river&&remaining.polygons.size()==1)remaining.type="Polygon";
    return {affected,std::move(remaining)};
}
Geometry annexAdd(const Geometry& original,const Geometry& transfer,const JobToken& token,bool river=false) {
    Geometry result;std::vector<Geometry> nearby;const auto transferBounds=annexBounds(transfer);
    for(const auto& polygon:original.polygons) {
        auto source=annexPolygon(polygon);
        if(annexBoundsOverlap(annexBounds(source),transferBounds))nearby.push_back(std::move(source));
        else result.polygons.push_back(polygon);
    }
    nearby.push_back(transfer);auto merged=annexUnion(nearby,token,river);
    if(river)merged=annexNormalizeRiver(merged,token);
    result.polygons.insert(result.polygons.end(),merged.polygons.begin(),merged.polygons.end());
    if(river&&result.polygons.size()==1)result.type="Polygon";return result;
}
double annexBoundaryLength(const Geometry& geometry) {
    double length=0;
    for(const auto& polygon:geometry.polygons)for(const auto& ring:polygon)for(std::size_t i=1;i<ring.size();++i) {
        auto dx=ring[i].x-ring[i-1].x;if(dx>180)dx-=360;if(dx< -180)dx+=360;
        length+=std::hypot(dx,ring[i].y-ring[i-1].y);
    }
    return length;
}
std::vector<std::string> annexShapeIssues(const Geometry& geometry,const JobToken& token) {
    std::vector<std::string> issues;
    try { GeometryStore validation;validation.insert({"annex-preview-shape",1},geometry); }
    catch(const std::exception&) { issues.push_back("invalid-geometry");return issues; }
    const auto close=[](Point a,Point b){return std::abs(a.x-b.x)<=1e-10&&std::abs(a.y-b.y)<=1e-10;};
    // Match the preview validator's issue-key contract. It filters existing
    // issues by kind + owner, not by the position of the offending vertex.
    for(const auto& polygon:geometry.polygons)for(const auto& ring:polygon) {
        for(std::size_t i=1;i<ring.size();++i)if(close(ring[i-1],ring[i]))issues.push_back("duplicate-vertex");
        const auto count=ring.size()-1;
        struct Segment {std::size_t index;Point a,b;double minX,maxX,minY,maxY;};
        std::vector<Segment> segments;segments.reserve(count);
        for(std::size_t i=0;i<count;++i) {
            const auto a=ring[i],b=ring[i+1];
            segments.push_back({i,a,b,std::min(a.x,b.x),std::max(a.x,b.x),std::min(a.y,b.y),std::max(a.y,b.y)});
        }
        std::sort(segments.begin(),segments.end(),[](const auto& left,const auto& right){return left.minX<right.minX||(left.minX==right.minX&&left.minY<right.minY);});
        std::vector<const Segment*> active;bool intersects=false;
        for(const auto& current:segments) {
            annexCheckCancelled(token);
            active.erase(std::remove_if(active.begin(),active.end(),[&](const auto* candidate){return candidate->maxX<current.minX-1e-12;}),active.end());
            for(const auto* candidate:active) {
                const auto i=candidate->index,j=current.index;
                if(i+1==j||j+1==i||(i==0&&j+1==count)||(j==0&&i+1==count)||
                   candidate->maxY<current.minY-1e-12||candidate->minY>current.maxY+1e-12)continue;
                const auto a=candidate->a,b=candidate->b,c=current.a,d=current.b;
                const double rx=b.x-a.x,ry=b.y-a.y,sx=d.x-c.x,sy=d.y-c.y;
                const double denominator=rx*sy-ry*sx;if(std::abs(denominator)<=1e-12)continue;
                const double t=((c.x-a.x)*sy-(c.y-a.y)*sx)/denominator;
                const double u=((c.x-a.x)*ry-(c.y-a.y)*rx)/denominator;
                if(t>=-1e-12&&t<=1+1e-12&&u>=-1e-12&&u<=1+1e-12){intersects=true;break;}
            }
            if(intersects){issues.push_back("self-intersection");break;}
            active.push_back(&current);
        }
    }
    std::sort(issues.begin(),issues.end());issues.erase(std::unique(issues.begin(),issues.end()),issues.end());return issues;
}
void annexIssue(AnnexGeometryPreviewResult& result,std::string kind,std::vector<ObjectRef> objects,std::string detail) {
    std::sort(objects.begin(),objects.end());
    const auto found=std::find_if(result.issues.begin(),result.issues.end(),[&](const auto& issue){return issue.kind==kind&&issue.objects==objects;});
    if(found==result.issues.end())result.issues.push_back({std::move(kind),std::move(objects),std::move(detail),true});
}
void validateAnnexRootGeometry(const ProjectSnapshot& snapshot,AnnexGeometryPreviewResult& result,const JobToken& token,bool river=false,
    const std::function<const Geometry&(const ObjectRef&)>& geographicGeometry={}) {
    const auto roots=1+result.affectedDonors.size();double perimeter=0;std::vector<Geometry> beforeUnion,afterUnion;
    for(std::size_t i=0;i<roots;++i) {
        const auto& row=result.rows[i];perimeter+=annexBoundaryLength(row.before);if(!row.before.polygons.empty())beforeUnion.push_back(row.before);
        if(!row.after)continue;
        afterUnion.push_back(*row.after);const auto oldIssues=annexShapeIssues(row.before,token);
        for(const auto& issue:annexShapeIssues(*row.after,token))if(std::find(oldIssues.begin(),oldIssues.end(),issue)==oldIssues.end())
            annexIssue(result,issue,{row.owner},"ANNEX_INVALID_RESULT_GEOMETRY");
    }
    const double tolerance=std::max(1e-8,perimeter*2e-7);
    std::set<std::pair<ObjectRef,ObjectRef>> tested;
    for(std::size_t i=0;i<roots;++i) {
        const auto& row=result.rows[i];if(!row.after)continue;
        for(const auto& unit:snapshot.document().units) {
            annexCheckCancelled(token);if(!isRootGeneral(snapshot.document(),unit))continue;
            const auto other=territorialRef(unit.id);if(other==row.owner)continue;
            const auto key=row.owner<other?std::make_pair(row.owner,other):std::make_pair(other,row.owner);
            if(!tested.insert(key).second)continue;
            const auto changed=std::find_if(result.rows.begin(),result.rows.begin()+roots,[&](const auto& candidate){return candidate.owner==other;});
            if(changed!=result.rows.begin()+roots&&!changed->after)continue;
            const auto& oldOther=geographicGeometry?geographicGeometry(other):annexGeometry(snapshot,other);const auto& newOther=changed==result.rows.begin()+roots?oldOther:*changed->after;
            if(!annexBoundsOverlap(annexBounds(*row.after),annexBounds(newOther)))continue;
            const double oldOverlap=annexBoundsOverlap(annexBounds(row.before),annexBounds(oldOther))?
                planarArea(annexCalculate(GeometryOperation::Intersection,row.before,oldOther,token,river)):0;
            const double newOverlap=planarArea(annexCalculate(GeometryOperation::Intersection,*row.after,newOther,token,river));
            // Country calculation blocks increases above the scale tolerance;
            // preview additionally blocks any newly introduced overlap issue.
            if(newOverlap>oldOverlap+tolerance||(newOverlap>1e-10&&oldOverlap<=1e-10))
                annexIssue(result,"overlap",{row.owner,other},"ANNEX_NEW_COUNTRY_OVERLAP");
        }
    }
    const auto before=annexUnion(beforeUnion,token,river),after=annexUnion(afterUnion,token,river);
    const double changed=planarArea(annexCalculate(GeometryOperation::Difference,before,after,token,river))+
                         planarArea(annexCalculate(GeometryOperation::Difference,after,before,token,river));
    if(changed>tolerance)annexIssue(result,"gap",result.plan->targets,"ANNEX_UNION_AREA_CHANGED");
}
}
bool AnnexGeometryPreviewResult::ok() const {
    return status==GeometryOperationStatus::Completed&&error==CommandError::None&&plan&&!blocking();
}
bool AnnexGeometryPreviewResult::blocking() const {
    return status!=GeometryOperationStatus::Completed||std::any_of(issues.begin(),issues.end(),[](const auto& issue){return issue.blocking;});
}
AnnexGeometryPreviewResult calculateAnnexGeometryPreview(const ProjectSnapshot& snapshot,const AnnexGeometryPreviewRequest& request,const JobToken& token) {
    AnnexGeometryPreviewResult result;
    try {
        annexCheckCancelled(token);const auto& target=annexGeometry(snapshot,request.target);
        const auto validateOwner=[&](const ObjectRef& owner) {
            (void)annexGeometry(snapshot,owner);const auto& unit=snapshot.document().units.at(snapshot.index().objects.at(owner));
            if(!isRootGeneral(snapshot.document(),unit))throw std::invalid_argument("ANNEX_REQUIRES_ROOT_GENERAL");
            const auto layer=snapshot.layer(nativeLayerId(snapshot.document(),owner));
            if(unit.locked||(layer&&layer->locked))throw std::invalid_argument("LOCKED");
        };
        validateOwner(request.target);
        GeometryStore selectionValidation;selectionValidation.insert({"annex-selection",1},request.selection);
        if(request.selection.type!="Polygon"&&request.selection.type!="MultiPolygon")throw std::invalid_argument("INVALID_GEOMETRY: polygon required");
        if(planarArea(request.selection)<=0)throw std::invalid_argument("NO_TRANSFERABLE_SELECTION");
        std::vector<Geometry> donorInputs;const auto selectionBounds=annexBounds(request.selection);
        for(const auto& donor:request.donors) {
            if(donor.id.empty()||donor==request.target||std::find(result.selectedDonors.begin(),result.selectedDonors.end(),donor)!=result.selectedDonors.end())continue;
            validateOwner(donor);result.selectedDonors.push_back(donor);
            for(const auto& polygon:annexGeometry(snapshot,donor).polygons) {
                auto shape=annexPolygon(polygon);if(annexBoundsOverlap(annexBounds(shape),selectionBounds))donorInputs.push_back(std::move(shape));
            }
        }
        if(result.selectedDonors.empty())throw std::invalid_argument("INVALID_TARGETS");
        if(donorInputs.empty())throw std::invalid_argument("SELECTION_OUTSIDE_DONOR");
        const bool river=std::any_of(request.riverSliverContext.begin(),request.riverSliverContext.end(),[&](const auto& context) {
            return std::any_of(result.selectedDonors.begin(),result.selectedDonors.end(),[&](const auto& donor){return donor.id==context.donorId;});
        });
        const auto coverage=annexUnion(donorInputs,token,river);
        if(!river) {
            const auto outside=annexCalculate(GeometryOperation::Difference,request.selection,coverage,token);
            if(planarArea(outside)>std::max(1e-8,annexBoundaryLength(request.selection)*2e-7))throw std::invalid_argument("SELECTION_OUTSIDE_DONOR");
        }
        result.transferredGeometry=annexCalculate(GeometryOperation::Intersection,request.selection,coverage,token,river);
        if(river?!annexPositiveArea(result.transferredGeometry):result.transferredGeometry.polygons.empty()||planarArea(result.transferredGeometry)<=0)
            throw std::invalid_argument("NO_TRANSFERABLE_SELECTION");
        // Reserve the target's existing row position; its final union must wait
        // until every admitted remnant has joined the authoritative transfer.
        result.rows.push_back({request.target,target,{}});AnnexSlivers slivers;
        for(const auto& donor:result.selectedDonors) {
            const auto& original=annexGeometry(snapshot,donor);
            auto [affected,remaining]=annexSubtract(original,result.transferredGeometry,token,river,request.riverSliverContext,donor.id,river?&slivers:nullptr);
            if(!affected)continue;
            result.affectedDonors.push_back(donor);
            if(remaining.polygons.empty()){result.removedRoots.push_back(donor);result.rows.push_back({donor,original,{}});}
            else result.rows.push_back({donor,original,std::move(remaining)});
        }
        if(result.affectedDonors.empty())throw std::invalid_argument("NO_TRANSFERABLE_SELECTION");
        if(!slivers.geometry.polygons.empty())
            result.transferredGeometry=annexCalculate(GeometryOperation::Union,result.transferredGeometry,slivers.geometry,token,true);
        result.autoIncludedSliverCount=slivers.geometry.polygons.size();result.autoIncludedSliverAreaM2=slivers.areaM2;
        result.rows.front().after=annexAdd(target,result.transferredGeometry,token,river);
        if(river) {
            result.transferredGeometry=annexNormalizeRiver(result.transferredGeometry,token);
            result.transferredGeometry.type="MultiPolygon";
        }
        auto planned=CommandProcessor::planTerritorial(snapshot,AnnexTerritoryIntent{request.target,result.affectedDonors,result.transferredGeometry});
        if(!planned.ok()||!planned.plan){result.error=planned.error;throw std::runtime_error(planned.detail);}
        result.plan=std::move(planned.plan);result.patch.sourceRevision=snapshot.revision();
        // Same root transferLandDependents semantics as the strict command path:
        // clip every nested general child; an empty child is removed, never moved.
        for(std::size_t i=1+result.affectedDonors.size();i<result.plan->geometry.readOwners.size();++i) {
            const auto owner=result.plan->geometry.readOwners[i];const auto& original=annexGeometry(snapshot,owner);
            auto remaining=annexSubtract(original,result.transferredGeometry,token,river).second;
            if(remaining.polygons.empty())result.rows.push_back({owner,original,{}});
            else result.rows.push_back({owner,original,std::move(remaining)});
        }
        for(const auto& row:result.rows) {
            if(row.after)result.patch.replacements.push_back({row.owner,*row.after});
            else result.patch.removedGeometryOwners.push_back(row.owner);
        }
        validateAnnexRootGeometry(snapshot,result,token,river);annexCheckCancelled(token);
        if(result.issues.empty()) {
            // Web receipts have passed the pinned normalizer before D3 measures
            // them. Normalize an observation copy; never rewrite the strict receipt.
            const auto observed=normalizeRiverGeometry(result.transferredGeometry,[&]{return token.cancelled();});
            annexCheckCancelled(token);
            if(!observed.succeeded()||!observed.geometry)throw std::runtime_error("TRANSFER_AREA_NORMALIZATION_FAILED");
            const auto area=calculateRiverAreaKm2(*observed.geometry,[&]{return token.cancelled();});
            annexCheckCancelled(token);
            if(!area.succeeded())throw std::runtime_error(area.detail.toStdString());
            result.transferAreaKm2=area.areaKm2;
        }
        result.status=GeometryOperationStatus::Completed;
        result.error=result.issues.empty()?CommandError::None:CommandError::ValidationFailed;
        if(!result.issues.empty())result.detail=result.issues.front().detail;
        token.reportProgress(100);return result;
    } catch(const std::exception& error) {
        result.detail=token.cancelled()?"CANCELLED":error.what();
        result.status=result.detail=="CANCELLED"?GeometryOperationStatus::Cancelled:GeometryOperationStatus::Failed;
        if(result.status==GeometryOperationStatus::Cancelled){result.plan.reset();result.patch={};}
        else annexIssue(result,"invalid-geometry",{request.target},result.detail);
        return result;
    }
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

bool SplitGeometryPreviewResult::ok() const { return status==GeometryOperationStatus::Completed&&error==CommandError::None&&plan&&!blocking(); }
bool SplitGeometryPreviewResult::blocking() const { return status!=GeometryOperationStatus::Completed||std::any_of(issues.begin(),issues.end(),[](const auto& issue){return issue.blocking;}); }
SplitGeometryPreviewResult calculateSplitGeometryPreview(const ProjectSnapshot& snapshot,const SplitTerritorialIntent& input,const JobToken& token) {
    SplitGeometryPreviewResult result;
    try {
        annexCheckCancelled(token);const auto& original=annexGeometry(snapshot,input.source);auto intent=input;
        const auto& document=snapshot.document();const auto& sourceUnit=document.units.at(snapshot.index().objects.at(intent.source));const bool root=isRootGeneral(document,sourceUnit);
        const auto normalized=[&](const Geometry& geometry) {
            const auto value=normalizeSplitClippedGeometry(geometry,[&]{return token.cancelled();});annexCheckCancelled(token);
            if(!value.succeeded())throw std::runtime_error(value.detail);
            if(value.status==GeometryOperationStatus::Empty)throw std::invalid_argument("EMPTY_SPLIT_RESULT");
            return value.geometry;
        };
        const auto normalizedRaw=[&](const Geometry& geometry) {
            const auto value=normalizeSplitRawGeometry(geometry,[&]{return token.cancelled();});annexCheckCancelled(token);
            if(!value.succeeded())throw std::runtime_error(value.detail);
            if(value.status==GeometryOperationStatus::Empty)throw std::invalid_argument("EMPTY_SPLIT_RESULT");
            return value.geometry;
        };
        const auto wrapped=[&](const Geometry& geometry) {
            const auto value=wrapSplitGeometry(geometry,[&]{return token.cancelled();});annexCheckCancelled(token);
            if(!value.succeeded())throw std::runtime_error(value.detail);
            return value.geometry;
        };
        std::map<ObjectRef,Geometry> geographic;
        const auto geographicOwner=[&](const ObjectRef& owner)->const Geometry& {
            const auto found=geographic.find(owner);if(found!=geographic.end())return found->second;
            return geographic.emplace(owner,wrapped(annexGeometry(snapshot,owner))).first->second;
        };
        const auto& sourceGeometry=geographicOwner(input.source);const auto selectedGeometry=wrapped(input.selection);
        if(sourceGeometry.polygons.empty())throw std::invalid_argument("EMPTY_SPLIT_SOURCE");
        GeometryStore validation;validation.insert({"split-selection",1},intent.selection);
        if(intent.selection.type!="Polygon"&&intent.selection.type!="MultiPolygon")throw std::invalid_argument("INVALID_GEOMETRY: polygon required");
        if(planarArea(intent.selection)<=0)throw std::invalid_argument("NO_SPLIT_SELECTION");
        const auto outside=annexCalculate(GeometryOperation::Difference,selectedGeometry,sourceGeometry,token);
        const auto outsideTolerance=root?std::max(1e-8,annexBoundaryLength(selectedGeometry)*2e-7):std::max(1e-10,planarArea(selectedGeometry)*1e-9);
        if(planarArea(outside)>outsideTolerance)throw std::invalid_argument("SELECTION_OUTSIDE_SOURCE");
        // Country commands authoritatively intersect their transfer with source
        // coverage; sibling creation preserves the accepted draft verbatim.
        result.transferredGeometry=root?annexCalculate(GeometryOperation::Intersection,selectedGeometry,sourceGeometry,token):selectedGeometry;
        if(result.transferredGeometry.polygons.empty())throw std::invalid_argument("NO_SPLIT_SELECTION");
        result.remainingGeometry=root?annexSubtract(sourceGeometry,result.transferredGeometry,token).second:annexCalculate(GeometryOperation::Difference,sourceGeometry,result.transferredGeometry,token);
        if(!root&&!significantArea(planarArea(result.remainingGeometry),planarArea(sourceGeometry)))throw std::invalid_argument("SPLIT_SOURCE_EXHAUSTED");
        result.transferredGeometry=normalized(result.transferredGeometry);
        if(!result.remainingGeometry.polygons.empty())result.remainingGeometry=normalized(result.remainingGeometry);
        intent.selection=result.transferredGeometry;
        auto planned=CommandProcessor::planTerritorial(snapshot,intent);
        if(!planned.ok()||!planned.plan){result.error=planned.error;throw std::invalid_argument(planned.detail);}
        result.plan=std::move(planned.plan);result.patch.sourceRevision=snapshot.revision();
        result.rows.push_back({intent.source,original,result.remainingGeometry.polygons.empty()?std::optional<Geometry>{}:std::optional<Geometry>{result.remainingGeometry}});
        std::set<ObjectRef> previewChanged{intent.source},calculatedOwners{intent.source};
        if(root) {
            for(std::size_t index=1;index<result.plan->geometry.readOwners.size();++index) {
                const auto owner=result.plan->geometry.readOwners[index];const auto& shape=annexGeometry(snapshot,owner);
                auto kept=annexCalculate(GeometryOperation::Difference,geographicOwner(owner),result.transferredGeometry,token);
                result.rows.push_back({owner,shape,kept.polygons.empty()?std::optional<Geometry>{}:std::optional<Geometry>{std::move(kept)}});
            }
        } else {
            const auto visit=[&](const auto& self,const std::string& parent,const std::optional<Geometry>& keptParent,bool movedAncestor)->void {
                for(const auto& relation:document.timelineRecords.parentRelations)if(relation.parentId==parent) {
                    annexCheckCancelled(token);const auto owner=territorialRef(relation.entityId);const auto& shape=annexGeometry(snapshot,owner);
                    if(movedAncestor||geometryContains(result.transferredGeometry,shape)) {
                        result.rows.push_back({owner,shape,shape});
                        if(!movedAncestor){previewChanged.insert(owner);result.issues.push_back({"reparent-child",{owner},"SPLIT_CHILD_REPARENTED",false});}
                        self(self,owner.id,std::optional<Geometry>{shape},true);continue;
                    }
                    const auto& calculationShape=geographicOwner(owner);
                    auto kept=keptParent?annexCalculate(GeometryOperation::Intersection,calculationShape,*keptParent,token):Geometry{};
                    const auto cut=kept.polygons.empty()?calculationShape:annexCalculate(GeometryOperation::Difference,calculationShape,kept,token);
                    if(!significantArea(planarArea(cut),planarArea(calculationShape))) {
                        result.rows.push_back({owner,shape,shape});
                        // The web does not descend into an unchanged child.
                        // Populate immutable reads without altering that subtree.
                        self(self,owner.id,std::optional<Geometry>{shape},true);continue;
                    }
                    previewChanged.insert(owner);calculatedOwners.insert(owner);std::optional<Geometry> remainder;if(!kept.polygons.empty())remainder=std::move(kept);
                    result.rows.push_back({owner,shape,remainder});self(self,owner.id,remainder,false);
                }
            };visit(visit,intent.source.id,std::optional<Geometry>{result.remainingGeometry},false);
        }
        for(auto& row:result.rows) {
            if(std::none_of(result.plan->geometry.replacements.begin(),result.plan->geometry.replacements.end(),[&](const auto& mapping){return mapping.result==row.owner;}))continue;
            if(row.after){if(root||calculatedOwners.count(row.owner))row.after=normalized(*row.after);else if(previewChanged.count(row.owner))row.after=normalizedRaw(*row.after);result.patch.replacements.push_back({row.owner,*row.after});}
            else result.patch.removedGeometryOwners.push_back(row.owner);
        }
        result.patch.creations.push_back({territorialRef(intent.createdId),result.transferredGeometry});
        result.rows.push_back({territorialRef(intent.createdId),{},result.transferredGeometry});
        if(root) {
            // Country creation uses the same post-edit country overlap/union
            // validator as annex. The fresh owner has no baseline geometry.
            AnnexGeometryPreviewResult validation;validation.plan=result.plan;validation.affectedDonors={intent.source};
            validation.rows={result.rows.back(),result.rows.front()};validation.rows[1].before=sourceGeometry;
            validateAnnexRootGeometry(snapshot,validation,token,false,geographicOwner);
            result.issues.insert(result.issues.end(),validation.issues.begin(),validation.issues.end());
        }
        // Root previews show the root calculator's owners only; dependent
        // changes remain in the full commit patch. Child previews put the fresh
        // sibling first, then only changed/reparented existing owners.
        if(root) {auto created=std::move(result.rows.back());result.rows.resize(1);result.rows.push_back(std::move(created));}
        else {std::vector<AnnexGeometryRow> shown;shown.push_back(std::move(result.rows.back()));for(std::size_t index=0;index+1<result.rows.size();++index)if(previewChanged.count(result.rows[index].owner))shown.push_back(std::move(result.rows[index]));result.rows=std::move(shown);}
        annexCheckCancelled(token);result.status=GeometryOperationStatus::Completed;
        result.error=result.blocking()?CommandError::ValidationFailed:CommandError::None;
        if(result.blocking())for(const auto& issue:result.issues)if(issue.blocking){result.detail=issue.detail;break;}
        token.reportProgress(100);return result;
    } catch(const std::exception& error) {
        result.detail=token.cancelled()?"CANCELLED":error.what();
        result.status=result.detail=="CANCELLED"?GeometryOperationStatus::Cancelled:GeometryOperationStatus::Failed;
        if(result.status==GeometryOperationStatus::Cancelled){result.plan.reset();result.patch={};}
        else result.issues.push_back({"invalid-geometry",{input.source},result.detail,true});
        return result;
    }
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

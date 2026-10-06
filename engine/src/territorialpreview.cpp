#include <pandoeditor/map/territorialpreview.h>
#include <pandoeditor/project.h>
#include <pandoeditor/geometrypredicates.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <set>
#include <limits>

using namespace pandoeditor;
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
Geometry annexCalculate(GeometryOperation operation,const Geometry& left,const Geometry& right,const TerritorialPreviewCalculators& calculators,const JobToken& token,bool river=false) {
    annexCheckCancelled(token);
    if(river) {
        auto calculated=calculators.geometry.clipRiverIntermediate({operation,left,right},[&]{return token.cancelled();});
        annexCheckCancelled(token);
        if(!calculated.succeeded())throw std::runtime_error(calculated.detail);
        return std::move(calculated.geometry);
    }
    const auto calculated=calculators.geometry.clip({operation,left,right},[&]{return token.cancelled();});
    annexCheckCancelled(token);
    if(!calculated.succeeded())throw std::runtime_error(calculated.detail);
    return calculated.geometry;
}
Geometry annexUnion(const std::vector<Geometry>& geometries,const TerritorialPreviewCalculators& calculators,const JobToken& token,bool river=false) {
    annexCheckCancelled(token);
    if(geometries.empty())return {};
    if(geometries.size()==1)return geometries.front();
    GeometryOperationRequest request;request.operation=GeometryOperation::Union;request.operands=geometries;
    if(river) {
        auto calculated=calculators.geometry.clipRiverIntermediate(request,[&]{return token.cancelled();});annexCheckCancelled(token);
        if(!calculated.succeeded())throw std::runtime_error(calculated.detail);
        return std::move(calculated.geometry);
    }
    const auto calculated=calculators.geometry.clip(request,[&]{return token.cancelled();});annexCheckCancelled(token);
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
Geometry annexNormalizeRiver(const Geometry& geometry,const TerritorialPreviewCalculators& calculators,const JobToken& token) {
    if(geometry.polygons.empty())return geometry;
    auto normalized=calculators.normalizeRiver(geometry,[&]{return token.cancelled();});annexCheckCancelled(token);
    if(!normalized.succeeded())throw std::runtime_error(normalized.detail);
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
std::pair<bool,Geometry> annexSubtract(const Geometry& original,const Geometry& transfer,const TerritorialPreviewCalculators& calculators,const JobToken& token,
    bool river=false,const std::vector<TerritorySelectionRiverSliverContext>& contexts={},
    const std::string& donorId={},AnnexSlivers* slivers=nullptr) {
    Geometry remaining;bool affected=false;const auto transferBounds=annexBounds(transfer);
    for(std::size_t polygonIndex=0;polygonIndex<original.polygons.size();++polygonIndex) {
        const auto& polygon=original.polygons[polygonIndex];
        annexCheckCancelled(token);const auto source=annexPolygon(polygon);
        if(!annexBoundsOverlap(annexBounds(source),transferBounds)) {
            remaining.polygons.push_back(polygon);continue;
        }
        const auto overlap=annexCalculate(GeometryOperation::Intersection,source,transfer,calculators,token,river);
        if(river?!annexPositiveArea(overlap):planarArea(overlap)<=0) {remaining.polygons.push_back(polygon);continue;}
        affected=true;const auto pieces=annexCalculate(GeometryOperation::Difference,source,transfer,calculators,token,river);
        const auto context=std::find_if(contexts.begin(),contexts.end(),[&](const auto& row){return row.donorId==donorId&&row.polygonIndex==polygonIndex;});
        Geometry unselected;
        if(context!=contexts.end())for(const auto& value:context->unselectedGeometries)
            unselected.polygons.insert(unselected.polygons.end(),value.polygons.begin(),value.polygons.end());
        for(const auto& piece:pieces.polygons) {
            const auto size=slivers&&context!=contexts.end()?annexSliverAreaM2(piece):std::numeric_limits<double>::infinity();
            if(slivers&&context!=contexts.end()&&size>0&&size<=1&&slivers->areaM2+size<=10
                &&!annexPositiveArea(annexCalculate(GeometryOperation::Intersection,annexPolygon(piece),unselected,calculators,token,true))
                &&annexSharesBoundary(piece,transfer)&&!annexSharesBoundary(piece,unselected)) {
                slivers->geometry.polygons.push_back(piece);slivers->areaM2+=size;
            } else {
                const auto kept=river?annexNormalizeRiver(annexPolygon(piece),calculators,token):annexPolygon(piece);
                remaining.polygons.insert(remaining.polygons.end(),kept.polygons.begin(),kept.polygons.end());
            }
        }
    }
    if(river&&remaining.polygons.size()==1)remaining.type="Polygon";
    return {affected,std::move(remaining)};
}
Geometry annexAdd(const Geometry& original,const Geometry& transfer,const TerritorialPreviewCalculators& calculators,const JobToken& token,bool river=false) {
    Geometry result;std::vector<Geometry> nearby;const auto transferBounds=annexBounds(transfer);
    for(const auto& polygon:original.polygons) {
        auto source=annexPolygon(polygon);
        if(annexBoundsOverlap(annexBounds(source),transferBounds))nearby.push_back(std::move(source));
        else result.polygons.push_back(polygon);
    }
    nearby.push_back(transfer);auto merged=annexUnion(nearby,calculators,token,river);
    if(river)merged=annexNormalizeRiver(merged,calculators,token);
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
void validateAnnexRootGeometry(const ProjectSnapshot& snapshot,AnnexGeometryPreviewResult& result,const TerritorialPreviewCalculators& calculators,const JobToken& token,bool river=false,
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
                planarArea(annexCalculate(GeometryOperation::Intersection,row.before,oldOther,calculators,token,river)):0;
            const double newOverlap=planarArea(annexCalculate(GeometryOperation::Intersection,*row.after,newOther,calculators,token,river));
            // Country calculation blocks increases above the scale tolerance;
            // preview additionally blocks any newly introduced overlap issue.
            if(newOverlap>oldOverlap+tolerance||(newOverlap>1e-10&&oldOverlap<=1e-10))
                annexIssue(result,"overlap",{row.owner,other},"ANNEX_NEW_COUNTRY_OVERLAP");
        }
    }
    const auto before=annexUnion(beforeUnion,calculators,token,river),after=annexUnion(afterUnion,calculators,token,river);
    const double changed=planarArea(annexCalculate(GeometryOperation::Difference,before,after,calculators,token,river))+
                         planarArea(annexCalculate(GeometryOperation::Difference,after,before,calculators,token,river));
    if(changed>tolerance)annexIssue(result,"gap",result.plan->targets,"ANNEX_UNION_AREA_CHANGED");
}
}
bool AnnexGeometryPreviewResult::ok() const {
    return status==GeometryOperationStatus::Completed&&error==CommandError::None&&plan&&!blocking();
}
bool AnnexGeometryPreviewResult::blocking() const {
    return status!=GeometryOperationStatus::Completed||std::any_of(issues.begin(),issues.end(),[](const auto& issue){return issue.blocking;});
}
AnnexGeometryPreviewResult calculateAnnexGeometryPreview(const ProjectSnapshot& snapshot,const AnnexGeometryPreviewRequest& request,const TerritorialPreviewCalculators& calculators,const JobToken& token) {
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
        const auto coverage=annexUnion(donorInputs,calculators,token,river);
        if(!river) {
            const auto outside=annexCalculate(GeometryOperation::Difference,request.selection,coverage,calculators,token);
            if(planarArea(outside)>std::max(1e-8,annexBoundaryLength(request.selection)*2e-7))throw std::invalid_argument("SELECTION_OUTSIDE_DONOR");
        }
        result.transferredGeometry=annexCalculate(GeometryOperation::Intersection,request.selection,coverage,calculators,token,river);
        if(river?!annexPositiveArea(result.transferredGeometry):result.transferredGeometry.polygons.empty()||planarArea(result.transferredGeometry)<=0)
            throw std::invalid_argument("NO_TRANSFERABLE_SELECTION");
        // Reserve the target's existing row position; its final union must wait
        // until every admitted remnant has joined the authoritative transfer.
        result.rows.push_back({request.target,target,{}});AnnexSlivers slivers;
        for(const auto& donor:result.selectedDonors) {
            const auto& original=annexGeometry(snapshot,donor);
            auto [affected,remaining]=annexSubtract(original,result.transferredGeometry,calculators,token,river,request.riverSliverContext,donor.id,river?&slivers:nullptr);
            if(!affected)continue;
            result.affectedDonors.push_back(donor);
            if(remaining.polygons.empty()){result.removedRoots.push_back(donor);result.rows.push_back({donor,original,{}});}
            else result.rows.push_back({donor,original,std::move(remaining)});
        }
        if(result.affectedDonors.empty())throw std::invalid_argument("NO_TRANSFERABLE_SELECTION");
        if(!slivers.geometry.polygons.empty())
            result.transferredGeometry=annexCalculate(GeometryOperation::Union,result.transferredGeometry,slivers.geometry,calculators,token,true);
        result.autoIncludedSliverCount=slivers.geometry.polygons.size();result.autoIncludedSliverAreaM2=slivers.areaM2;
        result.rows.front().after=annexAdd(target,result.transferredGeometry,calculators,token,river);
        if(river) {
            result.transferredGeometry=annexNormalizeRiver(result.transferredGeometry,calculators,token);
            result.transferredGeometry.type="MultiPolygon";
        }
        auto planned=CommandProcessor::planTerritorial(snapshot,AnnexTerritoryIntent{request.target,result.affectedDonors,result.transferredGeometry});
        if(!planned.ok()||!planned.plan){result.error=planned.error;throw std::runtime_error(planned.detail);}
        result.plan=std::move(planned.plan);result.patch.sourceRevision=snapshot.revision();
        // Same root transferLandDependents semantics as the strict command path:
        // clip every nested general child; an empty child is removed, never moved.
        for(std::size_t i=1+result.affectedDonors.size();i<result.plan->geometry.readOwners.size();++i) {
            const auto owner=result.plan->geometry.readOwners[i];const auto& original=annexGeometry(snapshot,owner);
            auto remaining=annexSubtract(original,result.transferredGeometry,calculators,token,river).second;
            if(remaining.polygons.empty())result.rows.push_back({owner,original,{}});
            else result.rows.push_back({owner,original,std::move(remaining)});
        }
        for(const auto& row:result.rows) {
            if(row.after)result.patch.replacements.push_back({row.owner,*row.after});
            else result.patch.removedGeometryOwners.push_back(row.owner);
        }
        validateAnnexRootGeometry(snapshot,result,calculators,token,river);annexCheckCancelled(token);
        if(result.issues.empty()) {
            // Web receipts have passed the pinned normalizer before D3 measures
            // them. Normalize an observation copy; never rewrite the strict receipt.
            const auto observed=calculators.normalizeRiver(result.transferredGeometry,[&]{return token.cancelled();});
            annexCheckCancelled(token);
            if(!observed.succeeded()||!observed.geometry)throw std::runtime_error("TRANSFER_AREA_NORMALIZATION_FAILED");
            const auto area=calculators.areaKm2(*observed.geometry,[&]{return token.cancelled();});
            annexCheckCancelled(token);
            if(!area.succeeded())throw std::runtime_error(area.detail);
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


bool SplitGeometryPreviewResult::ok() const { return status==GeometryOperationStatus::Completed&&error==CommandError::None&&plan&&!blocking(); }
bool SplitGeometryPreviewResult::blocking() const { return status!=GeometryOperationStatus::Completed||std::any_of(issues.begin(),issues.end(),[](const auto& issue){return issue.blocking;}); }
SplitGeometryPreviewResult calculateSplitGeometryPreview(const ProjectSnapshot& snapshot,const SplitTerritorialIntent& input,const TerritorialPreviewCalculators& calculators,const JobToken& token) {
    SplitGeometryPreviewResult result;
    try {
        annexCheckCancelled(token);const auto& original=annexGeometry(snapshot,input.source);auto intent=input;
        const auto& document=snapshot.document();const auto& sourceUnit=document.units.at(snapshot.index().objects.at(intent.source));const bool root=isRootGeneral(document,sourceUnit);
        const auto normalized=[&](const Geometry& geometry) {
            const auto value=calculators.geometry.normalizeClipped(geometry,[&]{return token.cancelled();});annexCheckCancelled(token);
            if(!value.succeeded())throw std::runtime_error(value.detail);
            if(value.status==GeometryOperationStatus::Empty)throw std::invalid_argument("EMPTY_SPLIT_RESULT");
            return value.geometry;
        };
        const auto normalizedRaw=[&](const Geometry& geometry) {
            const auto value=calculators.normalizeRaw(geometry,[&]{return token.cancelled();});annexCheckCancelled(token);
            if(!value.succeeded())throw std::runtime_error(value.detail);
            if(value.status==GeometryOperationStatus::Empty)throw std::invalid_argument("EMPTY_SPLIT_RESULT");
            return value.geometry;
        };
        const auto wrapped=[&](const Geometry& geometry) {
            const auto value=calculators.geometry.wrap(geometry,[&]{return token.cancelled();});annexCheckCancelled(token);
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
        const auto outside=annexCalculate(GeometryOperation::Difference,selectedGeometry,sourceGeometry,calculators,token);
        const auto outsideTolerance=root?std::max(1e-8,annexBoundaryLength(selectedGeometry)*2e-7):std::max(1e-10,planarArea(selectedGeometry)*1e-9);
        if(planarArea(outside)>outsideTolerance)throw std::invalid_argument("SELECTION_OUTSIDE_SOURCE");
        // Country commands authoritatively intersect their transfer with source
        // coverage; sibling creation preserves the accepted draft verbatim.
        result.transferredGeometry=root?annexCalculate(GeometryOperation::Intersection,selectedGeometry,sourceGeometry,calculators,token):selectedGeometry;
        if(result.transferredGeometry.polygons.empty())throw std::invalid_argument("NO_SPLIT_SELECTION");
        result.remainingGeometry=root?annexSubtract(sourceGeometry,result.transferredGeometry,calculators,token).second:annexCalculate(GeometryOperation::Difference,sourceGeometry,result.transferredGeometry,calculators,token);
        if(!root&&!significantArea(planarArea(result.remainingGeometry),planarArea(sourceGeometry)))throw std::invalid_argument("SPLIT_SOURCE_EXHAUSTED");
        result.transferredGeometry=normalized(result.transferredGeometry);
        if(!result.remainingGeometry.polygons.empty())result.remainingGeometry=normalized(result.remainingGeometry);
        intent.selection=result.transferredGeometry;
        auto planned=CommandProcessor::planTerritorial(snapshot,intent);
        if(!planned.ok()||!planned.plan){result.error=planned.error;throw std::invalid_argument(planned.detail);}
        result.plan=std::move(planned.plan);result.patch.sourceRevision=snapshot.revision();
        result.rows.push_back({intent.source,original,result.remainingGeometry.polygons.empty()?std::optional<Geometry>{}:std::optional<Geometry>{result.remainingGeometry}});
        const auto sourceParentId=staticParentRelation(document,intent.source.id).parentId;
        if(result.rows.back().after)result.parentIds.emplace(intent.source,sourceParentId);
        std::set<ObjectRef> previewChanged{intent.source},calculatedOwners{intent.source};
        if(root) {
            for(std::size_t index=1;index<result.plan->geometry.readOwners.size();++index) {
                const auto owner=result.plan->geometry.readOwners[index];const auto& shape=annexGeometry(snapshot,owner);
                auto kept=annexCalculate(GeometryOperation::Difference,geographicOwner(owner),result.transferredGeometry,calculators,token);
                result.rows.push_back({owner,shape,kept.polygons.empty()?std::optional<Geometry>{}:std::optional<Geometry>{std::move(kept)}});
                if(result.rows.back().after)result.parentIds.emplace(owner,staticParentRelation(document,owner.id).parentId);
            }
        } else {
            const auto visit=[&](const auto& self,const std::string& parent,const std::optional<Geometry>& keptParent,bool movedAncestor)->void {
                for(const auto& relation:document.timelineRecords.parentRelations)if(relation.parentId==parent) {
                    annexCheckCancelled(token);const auto owner=territorialRef(relation.entityId);const auto& shape=annexGeometry(snapshot,owner);
                    if(movedAncestor||geometryContains(result.transferredGeometry,shape)) {
                        result.rows.push_back({owner,shape,shape});
                        result.parentIds.emplace(owner,movedAncestor?relation.parentId:intent.createdId);
                        if(!movedAncestor){previewChanged.insert(owner);result.issues.push_back({"reparent-child",{owner},"SPLIT_CHILD_REPARENTED",false});}
                        self(self,owner.id,std::optional<Geometry>{shape},true);continue;
                    }
                    const auto& calculationShape=geographicOwner(owner);
                    auto kept=keptParent?annexCalculate(GeometryOperation::Intersection,calculationShape,*keptParent,calculators,token):Geometry{};
                    const auto cut=kept.polygons.empty()?calculationShape:annexCalculate(GeometryOperation::Difference,calculationShape,kept,calculators,token);
                    if(!significantArea(planarArea(cut),planarArea(calculationShape))) {
                        result.rows.push_back({owner,shape,shape});
                        result.parentIds.emplace(owner,relation.parentId);
                        // The web does not descend into an unchanged child.
                        // Populate immutable reads without altering that subtree.
                        self(self,owner.id,std::optional<Geometry>{shape},true);continue;
                    }
                    previewChanged.insert(owner);calculatedOwners.insert(owner);std::optional<Geometry> remainder;if(!kept.polygons.empty())remainder=std::move(kept);
                    result.rows.push_back({owner,shape,remainder});if(remainder)result.parentIds.emplace(owner,relation.parentId);self(self,owner.id,remainder,false);
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
        result.parentIds.emplace(territorialRef(intent.createdId),sourceParentId);
        if(root) {
            // Country creation uses the same post-edit country overlap/union
            // validator as annex. The fresh owner has no baseline geometry.
            AnnexGeometryPreviewResult validation;validation.plan=result.plan;validation.affectedDonors={intent.source};
            validation.rows={result.rows.back(),result.rows.front()};validation.rows[1].before=sourceGeometry;
            validateAnnexRootGeometry(snapshot,validation,calculators,token,false,geographicOwner);
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


bool BoundaryGeometryPreviewResult::ok() const {return validated_;}
bool BoundaryGeometryPreviewResult::blocking() const {return !validated_;}
BoundaryGeometryPreviewResult calculateBoundaryGeometryPreview(const ProjectSnapshot& snapshot,const SharedBoundaryIntent& intent,const TerritorialPreviewCalculators& calculators,const JobToken& token) {
    BoundaryGeometryPreviewResult result;
    try {
        annexCheckCancelled(token);result.plan_=planSharedBoundary(snapshot,intent);auto& plan=*result.plan_;
        const auto& document=snapshot.document();const bool root=staticParentRelation(document,intent.drafts.front().owner.id).parentId.empty();
        const auto same=[](const Geometry& a,const Geometry& b) {
            if(a.type!=b.type||a.polygons.size()!=b.polygons.size())return false;
            for(std::size_t p=0;p<a.polygons.size();++p){if(a.polygons[p].size()!=b.polygons[p].size())return false;for(std::size_t r=0;r<a.polygons[p].size();++r){if(a.polygons[p][r].size()!=b.polygons[p][r].size())return false;for(std::size_t n=0;n<a.polygons[p][r].size();++n)if(a.polygons[p][r][n].x!=b.polygons[p][r][n].x||a.polygons[p][r][n].y!=b.polygons[p][r][n].y)return false;}}
            return true;
        };
        const auto wrapped=[&](const Geometry& geometry) {auto value=calculators.geometry.wrap(geometry,[&]{return token.cancelled();});annexCheckCancelled(token);if(!value.succeeded())throw std::runtime_error(value.detail);return value.geometry;};
        std::map<ObjectRef,Geometry> original,proposed;
        for(const auto& owner:plan.geometry.readOwners)original.emplace(owner,wrapped(annexGeometry(snapshot,owner)));
        std::vector<Geometry> beforeShapes,afterShapes;
        for(const auto& draft:intent.drafts) {beforeShapes.push_back(original.at(draft.owner));auto next=wrapped(draft.geometry);if(next.polygons.empty()||planarArea(next)<=0)throw std::runtime_error("EMPTY_REQUIRED_GEOMETRY");afterShapes.push_back(next);proposed.emplace(draft.owner,std::move(next));}
        const auto before=annexUnion(beforeShapes,calculators,token),after=annexUnion(afterShapes,calculators,token);
        if(significantArea(planarArea(annexCalculate(GeometryOperation::Difference,before,after,calculators,token)),planarArea(before))||significantArea(planarArea(annexCalculate(GeometryOperation::Difference,after,before,calculators,token)),planarArea(after)))throw std::runtime_error("BOUNDARY_OUTER_UNION_CHANGED");
        for(const auto& draft:intent.drafts) {
            if(root) {
                const auto gained=annexCalculate(GeometryOperation::Difference,proposed.at(draft.owner),original.at(draft.owner),calculators,token);
                if(gained.polygons.empty())continue;
                for(const auto& other:document.units)if(isRootGeneral(document,other)&&other.id!=draft.owner.id) {
                    const auto ref=territorialRef(other.id);const auto changed=proposed.find(ref);const auto& otherGeometry=changed==proposed.end()?original.at(ref):changed->second;
                    if(significantArea(planarArea(annexCalculate(GeometryOperation::Intersection,gained,otherGeometry,calculators,token)),planarArea(gained)))throw std::runtime_error("BOUNDARY_OWNER_OVERLAP");
                }
            } else for(const auto& other:intent.drafts)if(other.owner<draft.owner) {
                const auto overlap=annexCalculate(GeometryOperation::Intersection,proposed.at(draft.owner),proposed.at(other.owner),calculators,token);
                if(significantArea(planarArea(overlap),std::min(planarArea(proposed.at(draft.owner)),planarArea(proposed.at(other.owner)))))throw std::runtime_error("BOUNDARY_OWNER_OVERLAP");
            }
        }
        std::map<ObjectRef,std::optional<Geometry>> next;
        std::map<std::string,std::string> parents;
        std::set<ObjectRef> shown;
        for(const auto& mapping:plan.geometry.replacements)next.emplace(mapping.source,annexGeometry(snapshot,mapping.source));
        for(const auto& draft:intent.drafts){next[draft.owner]=draft.geometry;if(!same(annexGeometry(snapshot,draft.owner),draft.geometry))shown.insert(draft.owner);}
        const auto children=[&](const std::string& parent){std::vector<ObjectRef> out;for(const auto& unit:document.units)if(unit.kind==UnitKind::General&&staticParentRelation(document,unit.id).parentId==parent)out.push_back(territorialRef(unit.id));return out;};
        const auto move=[&](const ObjectRef& owner,const ObjectRef& destination) {
            const auto old=territorialRef(staticParentRelation(document,owner.id).parentId);parents[owner.id]=destination.id;
            result.reparented.push_back({owner,old,destination});shown.insert(owner);
            plan.impacts.push_back({"ownership-change",owner,"territorial.boundary.parent:"+destination.id});
        };
        const auto moveHierarchy=[&](const auto& self,const ObjectRef& owner,const ObjectRef& destination)->void {
            move(owner,destination);for(const auto& child:children(owner.id))self(self,child,owner);
        };
        const auto clipped=[&](const ObjectRef& owner,const Geometry& parentGeometry) {
            const auto& source=original.at(owner);const auto kept=parentGeometry.polygons.empty()?Geometry{}:annexCalculate(GeometryOperation::Intersection,source,parentGeometry,calculators,token);
            const auto cut=kept.polygons.empty()?source:annexCalculate(GeometryOperation::Difference,source,kept,calculators,token);
            const bool changes=significantArea(planarArea(cut),planarArea(source));
            if(changes) {
                shown.insert(owner);plan.impacts.push_back({kept.polygons.empty()?"remove-child":"clip-child",owner,kept.polygons.empty()?"territorial.boundary.remove-child":"territorial.boundary.clip-child"});
                if(kept.polygons.empty())next[owner]=std::nullopt;
                else {auto normalized=calculators.geometry.normalizeClipped(kept,[&]{return token.cancelled();});annexCheckCancelled(token);if(!normalized.succeeded())throw std::runtime_error(normalized.detail);next[owner]=std::move(normalized.geometry);}
            }
            return std::make_pair(kept,changes);
        };
        if(root) {
            const auto reconcile=[&](const auto& self,const std::string& parent,const Geometry& parentGeometry,const ObjectRef& sourceRoot)->void {
                for(const auto& child:children(parent)) {
                    annexCheckCancelled(token);
                    const auto receiver=std::find_if(intent.drafts.begin(),intent.drafts.end(),[&](const auto& draft){return !(draft.owner==sourceRoot)&&geometryContains(draft.geometry,annexGeometry(snapshot,child));});
                    if(receiver!=intent.drafts.end()){moveHierarchy(moveHierarchy,child,receiver->owner);continue;}
                    const auto kept=clipped(child,parentGeometry);self(self,child.id,kept.first,sourceRoot);
                }
            };
            for(const auto& draft:intent.drafts)reconcile(reconcile,draft.owner.id,proposed.at(draft.owner),draft.owner);
        } else {
            const auto reconcile=[&](const auto& self,const std::string& parent,const Geometry& parentGeometry)->void {
                for(const auto& child:children(parent)){const auto kept=clipped(child,parentGeometry);if(kept.second)self(self,child.id,kept.first);}
            };
            for(const auto& draft:intent.drafts)for(const auto& child:children(draft.owner.id)) {
                const auto receiver=std::find_if(intent.drafts.begin(),intent.drafts.end(),[&](const auto& other){return !(other.owner==draft.owner)&&geometryContains(other.geometry,annexGeometry(snapshot,child));});
                if(receiver!=intent.drafts.end()){move(child,receiver->owner);continue;}
                const auto kept=clipped(child,proposed.at(draft.owner));if(kept.second)reconcile(reconcile,child.id,kept.first);
            }
        }
        // The web finalizer validates affected partition siblings after all
        // ownership changes, including residents of a receiving parent.
        const auto survives=[&](const ObjectRef& owner){const auto found=next.find(owner);return found==next.end()||found->second.has_value();};
        const auto parentOf=[&](const std::string& id){const auto found=parents.find(id);return found==parents.end()?staticParentRelation(document,id).parentId:found->second;};
        std::map<ObjectRef,Geometry> finalGeometry;
        const auto finalShape=[&](const ObjectRef& owner)->const Geometry& {
            const auto cached=finalGeometry.find(owner);if(cached!=finalGeometry.end())return cached->second;
            const auto value=next.find(owner);
            return finalGeometry.emplace(owner,value==next.end()||same(annexGeometry(snapshot,owner),*value->second)?original.at(owner):wrapped(*value->second)).first->second;
        };
        for(const auto& unit:document.units) {
            if(unit.kind!=UnitKind::General)continue;const auto owner=territorialRef(unit.id);const auto parent=parentOf(unit.id);
            if(parent.empty()||!survives(owner)||staticParentRelation(document,unit.id).coverageMode!="partition"||(!shown.count(owner)&&!shown.count(territorialRef(parent))))continue;
            for(const auto& sibling:document.units) {
                const auto other=territorialRef(sibling.id);
                if(other==owner||sibling.kind!=UnitKind::General||!survives(other)||parentOf(sibling.id)!=parent||staticParentRelation(document,sibling.id).coverageMode!="partition")continue;
                const auto& left=finalShape(owner);const auto& right=finalShape(other);
                if(annexBoundsOverlap(annexBounds(left),annexBounds(right))&&significantArea(planarArea(annexCalculate(GeometryOperation::Intersection,left,right,calculators,token)),planarArea(left)))throw std::runtime_error("BOUNDARY_PARTITION_OVERLAP");
            }
        }
        const auto rootOf=[&](std::string id,bool changed){std::set<std::string> seen;while(seen.insert(id).second){const auto found=parents.find(id);const auto parent=changed&&found!=parents.end()?found->second:staticParentRelation(document,id).parentId;if(parent.empty())return id;id=parent;}throw std::runtime_error("PARENT_CYCLE");};
        result.patch_.sourceRevision=snapshot.revision();
        for(const auto& mapping:plan.geometry.replacements) {
            const auto owner=mapping.source;const auto& old=annexGeometry(snapshot,owner);const auto& value=next.at(owner);
            const bool changed=!value||!same(old,*value)||(parents.count(owner.id)&&parents.at(owner.id)!=staticParentRelation(document,owner.id).parentId)||rootOf(owner.id,false)!=rootOf(owner.id,true);
            const auto& unit=document.units.at(snapshot.index().objects.at(owner));const auto layer=snapshot.layer(nativeLayerId(document,owner));
            if(changed&&(unit.locked||(layer&&layer->locked)))throw std::runtime_error("LOCKED");
            if(value)result.patch_.replacements.push_back({owner,*value});else result.patch_.removedGeometryOwners.push_back(owner);
            if(shown.count(owner))result.rows.push_back({owner,old,value});
        }
        annexCheckCancelled(token);result.status=GeometryOperationStatus::Completed;result.error=CommandError::None;result.validated_=true;token.reportProgress(100);return result;
    } catch(const std::exception& error) {
        result.detail=token.cancelled()?"CANCELLED":error.what();result.error=result.detail=="LOCKED"?CommandError::Locked:CommandError::ValidationFailed;
        result.status=result.detail=="CANCELLED"?GeometryOperationStatus::Cancelled:GeometryOperationStatus::Failed;
        if(result.status==GeometryOperationStatus::Cancelled){result.plan_.reset();result.patch_={};}
        else result.issues.push_back({"boundary",{},result.detail,true});return result;
    }
}

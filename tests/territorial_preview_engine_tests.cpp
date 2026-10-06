#include <pandoeditor/map/territorialpreview.h>
#include "territorial_fixture.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <type_traits>

using namespace pandoeditor;
namespace {
using Status=GeometryOperationStatus;
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
bool exact(const Geometry& a,const Geometry& b) {
    const auto ring=[](const Ring& a,const Ring& b) {
        if(a.size()!=b.size())return false;
        for(std::size_t i=0;i<a.size();++i)if(std::memcmp(&a[i].x,&b[i].x,sizeof(double))||std::memcmp(&a[i].y,&b[i].y,sizeof(double)))return false;
        return true;
    };
    if(a.type!=b.type||!ring(a.points,b.points)||a.lines.size()!=b.lines.size()||a.polygons.size()!=b.polygons.size())return false;
    for(std::size_t i=0;i<a.lines.size();++i)if(!ring(a.lines[i],b.lines[i]))return false;
    for(std::size_t p=0;p<a.polygons.size();++p) {
        if(a.polygons[p].size()!=b.polygons[p].size())return false;
        for(std::size_t r=0;r<a.polygons[p].size();++r)if(!ring(a.polygons[p][r],b.polygons[p][r]))return false;
    }
    return true;
}
Geometry box(double left,double right,double bottom=0,double top=10) {
    Geometry g;g.polygons={{{{left,bottom},{right,bottom},{right,top},{left,top},{left,bottom}}}};return g;
}
GeometryOperationResult value(Geometry g) {return {g.polygons.empty()?Status::Empty:Status::Completed,std::move(g),{}};}
ProjectDocument roots() {return ProjectDocument({{"A","A",box(0,10).polygons,0},{"B","B",box(10,20).polygons,0}},{{"countries","Countries"}});}
void child(ProjectDocument& d,const std::string& id,const std::string& parent,Geometry g,bool locked=false) {
    GeometryRef ref{id,1};d.geometries.insert(ref,std::move(g));appendTerritory(d,{id,id,"",UnitKind::General,locked},ref,parent);
    d.presentation.objectStyles[territorialRef(id)]={};
}
struct Work {
    Project project;
    JobScheduler jobs;
    JobTicket ticket;
    explicit Work(ProjectDocument d=roots()):ticket(setup(std::move(d))) {}
    JobTicket setup(ProjectDocument d) {project.replace(std::move(d));auto t=jobs.enqueue(project.snapshot(),"preview-engine");jobs.takeNext();return t;}
};
TerritorialPreviewCalculators forbidden() {
    TerritorialPreviewCalculators c;
    c.geometry.clip=[](const GeometryOperationRequest&,const GeometryCancellation&)->GeometryOperationResult {throw std::runtime_error("unused clip");};
    c.geometry.clipRiverIntermediate=[](const GeometryOperationRequest&,const GeometryCancellation&)->GeometryOperationResult {throw std::runtime_error("unused river clip");};
    c.geometry.wrap=[](const Geometry&,const GeometryCancellation&)->GeometryOperationResult {throw std::runtime_error("unused wrap");};
    c.geometry.normalizeClipped=[](const Geometry&,const GeometryCancellation&)->GeometryOperationResult {throw std::runtime_error("unused clipped normalization");};
    c.normalizeRaw=[](const Geometry&,const GeometryCancellation&)->GeometryOperationResult {throw std::runtime_error("unused raw normalization");};
    c.normalizeRiver=[](const Geometry&,const GeometryCancellation&)->PreviewRiverNormalizationResult {throw std::runtime_error("unused river normalization");};
    c.areaKm2=[](const Geometry&,const GeometryCancellation&)->PreviewAreaResult {throw std::runtime_error("unused area");};
    return c;
}
struct Step {GeometryOperation operation;Geometry left,right;std::vector<Geometry> operands;Geometry output;};
// Scripted responses test callback wiring and receipt state, never geometry parity.
std::vector<Step> annexSteps() {
    const auto A=box(0,10),B=box(10,20),S=box(10,12),R=box(12,20),T=box(0,12),U=box(0,20);
    return {{GeometryOperation::Difference,S,B,{},{}},{GeometryOperation::Intersection,S,B,{},S},
        {GeometryOperation::Intersection,B,S,{},S},{GeometryOperation::Difference,B,S,{},R},
        {GeometryOperation::Union,{},{},{A,S},T},{GeometryOperation::Intersection,A,B,{},{}},
        {GeometryOperation::Intersection,T,R,{},{}},{GeometryOperation::Union,{},{},{A,B},U},
        {GeometryOperation::Union,{},{},{T,R},U},{GeometryOperation::Difference,U,U,{},{}},
        {GeometryOperation::Difference,U,U,{},{}}};
}
GeometryOperationResult runStep(const GeometryOperationRequest& r,const Step& s) {
    require(r.operation==s.operation&&exact(r.left,s.left)&&exact(r.right,s.right),"binary operation/left/right changed");
    require(r.operands.size()==s.operands.size(),"union operands shape changed");
    for(std::size_t i=0;i<r.operands.size();++i)require(exact(r.operands[i],s.operands[i]),"ordered union operand changed");
    return value(s.output);
}
AnnexGeometryPreviewRequest annexRequest() {return {territorialRef("A"),{territorialRef("B"),territorialRef("B"),territorialRef("A"),territorialRef("")},box(10,12)};}
void snapshotUnchanged(const Work& w,const ProjectSnapshot& before) {
    require(w.project.revision()==before.revision()&&w.project.snapshot().instanceId()==before.instanceId(),"preview mutated project identity/revision");
    for(const auto& u:before.document().units)require(exact(*w.project.document().geometries.get(staticGeometryBinding(w.project.document(),u.id).geometryRef),*before.document().geometries.get(staticGeometryBinding(before.document(),u.id).geometryRef)),"preview changed canonical geometry");
    require(w.project.document().timelineRecords.parentRelations.size()==before.document().timelineRecords.parentRelations.size(),"preview changed hierarchy");
}
void authorityAndInitialCancellation() {
    static_assert(std::is_same_v<decltype(std::declval<BoundaryGeometryPreviewResult&>().plan()),const std::optional<TerritorialMutationPlan>&>);
    static_assert(std::is_same_v<decltype(std::declval<BoundaryGeometryPreviewResult&>().patch()),const GeometryPatch&>);
    BoundaryGeometryPreviewResult receipt;require(!receipt.ok()&&receipt.blocking()&&!receipt.plan(),"default receipt authorizes");
    receipt.status=Status::Completed;receipt.error=CommandError::None;receipt.issues.clear();require(!receipt.ok(),"diagnostics forged receipt");
    Work w;w.jobs.cancel(w.ticket.id());const auto callbacks=forbidden();
    const auto a=calculateAnnexGeometryPreview(w.project.snapshot(),annexRequest(),callbacks,w.ticket.token());
    const auto s=calculateSplitGeometryPreview(w.project.snapshot(),{territorialRef("A"),box(0,4),"C","Created"},callbacks,w.ticket.token());
    const auto b=calculateBoundaryGeometryPreview(w.project.snapshot(),{{{territorialRef("A"),box(0,8)},{territorialRef("B"),box(8,20)}}},callbacks,w.ticket.token());
    require(a.status==Status::Cancelled&&a.detail=="CANCELLED"&&!a.plan&&a.patch.replacements.empty()&&a.rows.empty(),"initial annex cancellation");
    require(s.status==Status::Cancelled&&s.detail=="CANCELLED"&&!s.plan&&s.patch.replacements.empty()&&s.rows.empty(),"initial split cancellation");
    require(b.status==Status::Cancelled&&b.detail=="CANCELLED"&&!b.plan()&&b.patch().replacements.empty()&&b.rows.empty(),"initial boundary cancellation");
    require(w.ticket.token().progress()==-1,"cancelled preview reported progress");
}
void annexOrderedRoutesAndObservationCopy() {
    Work w;const auto before=w.project.snapshot();auto callbacks=forbidden();const auto steps=annexSteps();std::size_t calls=0;int normalize=0,area=0;
    callbacks.geometry.clip=[&](const GeometryOperationRequest& r,const GeometryCancellation& cancellation) {require(cancellation&&!cancellation(),"token cancellation adapter");require(calls<steps.size(),"extra annex call");return runStep(r,steps[calls++]);};
    const auto observed=box(30,31);
    callbacks.normalizeRiver=[&](const Geometry& g,const GeometryCancellation& cancellation) {++normalize;require(!cancellation()&&exact(g,box(10,12)),"area observation input changed");return PreviewRiverNormalizationResult{Status::Completed,"normalizer detail",observed};};
    callbacks.areaKm2=[&](const Geometry& g,const GeometryCancellation& cancellation) {++area;require(!cancellation()&&exact(g,observed),"area did not observe returned normalization");return PreviewAreaResult{Status::Completed,"area detail",0x1.23456789abcdep+4};};
    const auto r=calculateAnnexGeometryPreview(before,annexRequest(),callbacks,w.ticket.token());
    require(r.ok()&&r.status==Status::Completed&&r.detail.empty()&&r.issues.empty(),"scripted annex did not complete");
    require(calls==steps.size()&&normalize==1&&area==1,"annex callback count/identity union changed");
    require(r.selectedDonors==std::vector<ObjectRef>{territorialRef("B")}&&r.affectedDonors==r.selectedDonors,"donor dedup order changed");
    require(exact(r.transferredGeometry,box(10,12))&&r.rows.size()==2&&r.patch.replacements.size()==2,"observation normalization rewrote receipt");
    require(r.rows[0].owner==territorialRef("A")&&r.rows[1].owner==territorialRef("B")&&r.transferAreaKm2==0x1.23456789abcdep+4,"ordered rows/area changed");
    require(w.ticket.token().progress()==100,"completed annex progress");snapshotUnchanged(w,before);
}
void annexFailureAndCancellationBoundaries() {
    for(const auto status:{Status::Failed,Status::Cancelled})for(const std::string detail:{"callback detail","CANCELLED"}) {
        Work w;auto callbacks=forbidden();callbacks.geometry.clip=[&](const GeometryOperationRequest&,const GeometryCancellation&) {return GeometryOperationResult{status,{},detail};};
        const auto r=calculateAnnexGeometryPreview(w.project.snapshot(),annexRequest(),callbacks,w.ticket.token());
        require(r.detail==detail&&r.status==(detail=="CANCELLED"?Status::Cancelled:Status::Failed),"live-token callback status/detail was normalized");
        require(r.error==CommandError::PrepareFailed&&w.ticket.token().progress()==-1,"annex error/progress changed");
    }
    const auto steps=annexSteps();
    for(std::size_t cancelAt=1;cancelAt<=steps.size();++cancelAt) {
        Work w;auto callbacks=forbidden();std::size_t calls=0;
        callbacks.geometry.clip=[&](const GeometryOperationRequest& r,const GeometryCancellation&) {auto output=runStep(r,steps[calls++]);if(calls==cancelAt)w.jobs.cancel(w.ticket.id());return output;};
        const auto r=calculateAnnexGeometryPreview(w.project.snapshot(),annexRequest(),callbacks,w.ticket.token());
        require(r.status==Status::Cancelled&&r.detail=="CANCELLED"&&calls==cancelAt&&!r.plan&&r.patch.replacements.empty(),"after-callback annex cancellation did not clear authority");
        if(cancelAt>=3)require(!r.transferredGeometry.polygons.empty()&&!r.rows.empty(),"annex partial rows/transfer cleared");
        require(w.ticket.token().progress()==-1,"cancelled annex progress changed");
    }
    Work w;auto callbacks=forbidden();std::size_t calls=0;
    callbacks.geometry.clip=[&](const GeometryOperationRequest& r,const GeometryCancellation&) {auto output=runStep(r,steps[calls++]);if(calls==7)output=value(box(10,11));if(calls==10)w.jobs.cancel(w.ticket.id());return output;};
    const auto r=calculateAnnexGeometryPreview(w.project.snapshot(),annexRequest(),callbacks,w.ticket.token());
    require(r.status==Status::Cancelled&&!r.plan&&r.patch.replacements.empty()&&r.rows.size()==2&&r.issues.size()==1&&r.issues[0].detail=="ANNEX_NEW_COUNTRY_OVERLAP","cancelled annex erased partial diagnostics");
}
void annexBlockingProgressAndOptionalObservation() {
    for(const bool present:{false,true}) {
        Work w;auto callbacks=forbidden();const auto steps=annexSteps();std::size_t calls=0;int area=0;
        callbacks.geometry.clip=[&](const GeometryOperationRequest& r,const GeometryCancellation&) {return runStep(r,steps.at(calls++));};
        callbacks.normalizeRiver=[&](const Geometry&,const GeometryCancellation&) {return PreviewRiverNormalizationResult{Status::Completed,{},present?std::optional<Geometry>{Geometry{}}:std::nullopt};};
        callbacks.areaKm2=[&](const Geometry& g,const GeometryCancellation&) {++area;require(g.polygons.empty(),"present empty observation changed");return PreviewAreaResult{Status::Completed,{},7};};
        const auto r=calculateAnnexGeometryPreview(w.project.snapshot(),annexRequest(),callbacks,w.ticket.token());
        require(area==(present?1:0),"optional observation collapsed absent/empty");
        require(present?r.ok():r.status==Status::Failed&&r.detail=="TRANSFER_AREA_NORMALIZATION_FAILED","optional observation policy changed");
    }
    Work w;auto callbacks=forbidden();const auto steps=annexSteps();std::size_t calls=0;
    callbacks.geometry.clip=[&](const GeometryOperationRequest& r,const GeometryCancellation&) {auto output=runStep(r,steps.at(calls++));if(calls==7)output=value(box(10,11));return output;};
    const auto r=calculateAnnexGeometryPreview(w.project.snapshot(),annexRequest(),callbacks,w.ticket.token());
    require(r.status==Status::Completed&&r.error==CommandError::ValidationFailed&&!r.ok()&&r.blocking()&&r.issues.size()==1&&w.ticket.token().progress()==100,"blocking annex transport/progress changed");
}
void riverAbsentAndEmptyRemnantsStayProtected() {
    for(const bool present:{false,true}) {
        Work w;auto callbacks=forbidden();const auto steps=annexSteps();std::size_t calls=0;int normalization=0;
        callbacks.geometry.clipRiverIntermediate=[&](const GeometryOperationRequest& r,const GeometryCancellation&) {return runStep(r,steps.at(1+calls++));};
        callbacks.normalizeRiver=[&](const Geometry&,const GeometryCancellation&) {++normalization;return PreviewRiverNormalizationResult{Status::Completed,{},present?std::optional<Geometry>{Geometry{}}:std::nullopt};};
        auto request=annexRequest();request.riverSliverContext={{"B",0,{box(12,20)}}};
        const auto r=calculateAnnexGeometryPreview(w.project.snapshot(),request,callbacks,w.ticket.token());
        require(r.status==Status::Failed&&r.detail=="RIVER_REMAINDER_NOT_REPRESENTABLE"&&calls==3&&normalization==1&&!r.plan,"protected river remnant dropped");
    }
}
void childSplitUsesRawOnlyForReparentedOwner() {
    ProjectDocument d({{"P","P",box(0,20).polygons,0}},{{"countries","Countries"}});child(d,"A","P",box(0,10));child(d,"carried","A",box(1,2,1,2));child(d,"fixed","carried",box(1.2,1.4,1.2,1.4),true);
    Work w(d);const auto before=w.project.snapshot();auto callbacks=forbidden();int wrap=0,clip=0,normalized=0,raw=0;
    callbacks.geometry.wrap=[&](const Geometry& g,const GeometryCancellation&) {++wrap;return value(g);};
    callbacks.geometry.clip=[&](const GeometryOperationRequest& r,const GeometryCancellation&) {++clip;require(r.operation==GeometryOperation::Difference&&r.operands.empty(),"child split binary route");if(clip==1){require(exact(r.left,box(0,4))&&exact(r.right,box(0,10)),"child outside inputs");return value({});}require(clip==2&&exact(r.left,box(0,10))&&exact(r.right,box(0,4)),"child difference inputs");return value(box(4,10));};
    callbacks.geometry.normalizeClipped=[&](const Geometry& g,const GeometryCancellation&) {++normalized;return value(g);};
    callbacks.normalizeRaw=[&](const Geometry& g,const GeometryCancellation&) {++raw;require(exact(g,box(1,2,1,2)),"raw normalized untouched descendant");return value(g);};
    const auto r=calculateSplitGeometryPreview(before,{territorialRef("A"),box(0,4),"C","Created"},callbacks,w.ticket.token());
    if(!r.ok()||wrap!=2||clip!=2||normalized!=3||raw!=1)throw std::runtime_error("child split dependency/count contract: "+r.detail+" wraps="+std::to_string(wrap)+" clips="+std::to_string(clip)+" normalized="+std::to_string(normalized)+" raw="+std::to_string(raw));
    require(r.rows.size()==3&&r.rows[0].owner==territorialRef("C")&&r.rows[1].owner==territorialRef("A")&&r.rows[2].owner==territorialRef("carried"),"child split row order changed");
    require(r.patch.replacements.size()==2&&r.patch.creations.size()==1&&r.issues.size()==1&&!r.issues[0].blocking,"child split full patch/presentation contract");
    require(w.ticket.token().progress()==100,"split progress");snapshotUnchanged(w,before);
}
void rootExhaustionKeepsIdentityUnionAndInertReceipt() {
    ProjectDocument d({{"A","A",box(0,10).polygons,0}},{{"countries","Countries"}});Work w(d);auto callbacks=forbidden();int wraps=0,clips=0,normalizes=0;
    callbacks.geometry.wrap=[&](const Geometry& g,const GeometryCancellation&) {++wraps;return value(g);};
    callbacks.geometry.normalizeClipped=[&](const Geometry& g,const GeometryCancellation&) {++normalizes;return value(g);};
    callbacks.geometry.clip=[&](const GeometryOperationRequest& r,const GeometryCancellation&) {++clips;require(r.operands.empty()&&r.operation!=GeometryOperation::Union,"one-operand union called runtime");return value(r.operation==GeometryOperation::Intersection?box(0,10):Geometry{});};
    const auto r=calculateSplitGeometryPreview(w.project.snapshot(),{territorialRef("A"),box(0,10),"C","Created"},callbacks,w.ticket.token());
    require(r.ok()&&r.remainingGeometry.polygons.empty()&&wraps==2&&clips==6&&normalizes==1,"inert root exhaustion policy/count changed");
    require(r.rows.size()==2&&!r.rows[0].after&&r.patch.removedGeometryOwners==std::vector<ObjectRef>{territorialRef("A")},"root exhausted preview shape changed");
}
void boundarySealDependenciesAndErrors() {
    const SharedBoundaryIntent intent{{{territorialRef("A"),box(0,8)},{territorialRef("B"),box(8,20)}}};
    Work w;const auto before=w.project.snapshot();auto callbacks=forbidden();int wraps=0,clips=0;
    callbacks.geometry.wrap=[&](const Geometry& g,const GeometryCancellation&) {++wraps;return value(g);};
    callbacks.geometry.clip=[&](const GeometryOperationRequest& r,const GeometryCancellation&) {++clips;if(r.operation==GeometryOperation::Union){require(r.operands.size()==2&&exact(r.left,{})&&exact(r.right,{}),"boundary union transport");return value(box(0,20));}require(r.operands.empty()&&r.operation==GeometryOperation::Difference,"boundary binary transport");return value({});};
    auto r=calculateBoundaryGeometryPreview(before,intent,callbacks,w.ticket.token());
    require(r.ok()&&r.plan()&&r.patch().replacements.size()==2&&r.rows.size()==2&&wraps==4&&clips==6&&w.ticket.token().progress()==100,"boundary sealed receipt/dependencies");
    const auto sealed=r.patch().replacements;r.status=Status::Failed;r.error=CommandError::Locked;r.issues.push_back({"diagnostic",{},"untrusted",true});r.rows.clear();
    require(r.ok()&&!r.blocking()&&r.patch().replacements.size()==sealed.size()&&exact(r.patch().replacements[0].geometry,sealed[0].geometry),"diagnostics changed sealed authority");snapshotUnchanged(w,before);
    for(const std::string detail:{"LOCKED","callback detail","CANCELLED"}) {
        Work failed;auto c=forbidden();c.geometry.wrap=[&](const Geometry&,const GeometryCancellation&) {return GeometryOperationResult{Status::Cancelled,{},detail};};
        auto receipt=calculateBoundaryGeometryPreview(failed.project.snapshot(),intent,c,failed.ticket.token());
        require(receipt.detail==detail&&receipt.status==(detail=="CANCELLED"?Status::Cancelled:Status::Failed)&&receipt.error==(detail=="LOCKED"?CommandError::Locked:CommandError::ValidationFailed),"boundary status/error detail mapping changed");
        receipt.status=Status::Completed;receipt.error=CommandError::None;receipt.issues.clear();require(!receipt.ok(),"failed boundary receipt promoted");
        if(detail=="CANCELLED")require(!receipt.plan()&&receipt.patch().replacements.empty(),"boundary cancellation retained plan");
    }
    for(int cancelAt=1;cancelAt<=10;++cancelAt) {
        Work cancelled;auto c=forbidden();int calls=0;
        const auto cancel=[&] {if(++calls==cancelAt)cancelled.jobs.cancel(cancelled.ticket.id());};
        c.geometry.wrap=[&](const Geometry& g,const GeometryCancellation&) {auto out=value(g);cancel();return out;};
        c.geometry.clip=[&](const GeometryOperationRequest& request,const GeometryCancellation&) {auto out=value(request.operation==GeometryOperation::Union?box(0,20):Geometry{});cancel();return out;};
        const auto receipt=calculateBoundaryGeometryPreview(cancelled.project.snapshot(),intent,c,cancelled.ticket.token());
        require(receipt.status==Status::Cancelled&&!receipt.ok()&&!receipt.plan()&&receipt.patch().replacements.empty()&&calls==cancelAt&&cancelled.ticket.token().progress()==-1,"boundary callback cancellation boundary");
    }
}
void observationFailuresAndCancellationKeepPartialReceipt() {
    for(const int stage:{0,1})for(const bool cancelled:{false,true}) {
        Work w;auto callbacks=forbidden();const auto steps=annexSteps();std::size_t calls=0;
        callbacks.geometry.clip=[&](const GeometryOperationRequest& r,const GeometryCancellation&) {return runStep(r,steps.at(calls++));};
        callbacks.normalizeRiver=[&](const Geometry& g,const GeometryCancellation&) {
            if(stage==0&&cancelled)w.jobs.cancel(w.ticket.id());
            return PreviewRiverNormalizationResult{stage==0&&!cancelled?Status::Failed:Status::Completed,"normalizer-specific detail",g};
        };
        callbacks.areaKm2=[&](const Geometry&,const GeometryCancellation&) {
            require(stage==1,"failed observation reached area");if(cancelled)w.jobs.cancel(w.ticket.id());
            return PreviewAreaResult{Status::Failed,"area-specific detail",0};
        };
        const auto r=calculateAnnexGeometryPreview(w.project.snapshot(),annexRequest(),callbacks,w.ticket.token());
        require(r.rows.size()==2&&!r.transferredGeometry.polygons.empty(),"late observation failure erased diagnostics");
        if(cancelled)require(r.status==Status::Cancelled&&r.detail=="CANCELLED"&&!r.plan&&r.patch.replacements.empty(),"late observation cancellation retained authority");
        else require(r.status==Status::Failed&&r.detail==(stage==0?"TRANSFER_AREA_NORMALIZATION_FAILED":"area-specific detail")&&r.plan&&r.patch.replacements.size()==2,"observation failure detail/partial receipt changed");
        require(w.ticket.token().progress()==-1,"failed observation reported completion");
    }
}
void splitFailuresAndCancellationPreservePolicy() {
    ProjectDocument d({{"P","P",box(0,20).polygons,0}},{{"countries","Countries"}});child(d,"A","P",box(0,10));
    for(const std::string detail:{"split callback detail","CANCELLED"}) {
        Work w(d);auto callbacks=forbidden();callbacks.geometry.wrap=[&](const Geometry&,const GeometryCancellation&) {return GeometryOperationResult{Status::Cancelled,{},detail};};
        const auto r=calculateSplitGeometryPreview(w.project.snapshot(),{territorialRef("A"),box(0,4),"C","Created"},callbacks,w.ticket.token());
        require(r.status==(detail=="CANCELLED"?Status::Cancelled:Status::Failed)&&r.detail==detail&&r.error==CommandError::PrepareFailed,"split wrap cancellation/error detail was normalized");
    }
    for(int cancelAt=0;cancelAt<=7;++cancelAt) {
        Work w(d);auto callbacks=forbidden();int calls=0,clips=0;
        const auto checkpoint=[&] {if(++calls==cancelAt)w.jobs.cancel(w.ticket.id());};
        callbacks.geometry.wrap=[&](const Geometry& g,const GeometryCancellation&) {checkpoint();return value(g);};
        callbacks.geometry.clip=[&](const GeometryOperationRequest&,const GeometryCancellation&) {checkpoint();return value(++clips==1?Geometry{}:box(4,10));};
        callbacks.geometry.normalizeClipped=[&](const Geometry& g,const GeometryCancellation&) {checkpoint();return cancelAt==0?GeometryOperationResult{Status::Empty,{},{}}:value(g);};
        const auto r=calculateSplitGeometryPreview(w.project.snapshot(),{territorialRef("A"),box(0,4),"C","Created"},callbacks,w.ticket.token());
        if(cancelAt==0)require(r.status==Status::Failed&&r.detail=="EMPTY_SPLIT_RESULT","Empty clipped normalization did not reject split");
        else {
            require(r.status==Status::Cancelled&&r.detail=="CANCELLED"&&!r.plan&&r.patch.replacements.empty()&&calls==cancelAt,"split after-callback cancellation boundary");
            if(cancelAt>=5)require(exact(r.transferredGeometry,box(0,4))&&exact(r.remainingGeometry,box(4,10)),"split cancellation cleared partial transfer/remainder");
        }
        require(w.ticket.token().progress()==-1,"failed split progress changed");
    }
}
void boundaryClippingUsesReturnedNormalizationOnly() {
    auto d=roots();child(d,"partial","A",box(7,9,1,2));Work w(d);auto callbacks=forbidden();int normalization=0;
    const auto kept=box(7,8,1,2),returned=box(7.1,7.9,1,2);
    callbacks.geometry.wrap=[](const Geometry& g,const GeometryCancellation&) {return value(g);};
    callbacks.geometry.clip=[&](const GeometryOperationRequest& r,const GeometryCancellation&) {
        if(r.operation==GeometryOperation::Union)return value(box(0,20));
        if(r.operation==GeometryOperation::Intersection) {require(exact(r.left,box(7,9,1,2))&&exact(r.right,box(0,8)),"boundary child clipping operands");return value(kept);}
        if(exact(r.left,box(7,9,1,2)))return value(box(8,9,1,2));
        return value({});
    };
    callbacks.geometry.normalizeClipped=[&](const Geometry& g,const GeometryCancellation& cancellation) {++normalization;require(exact(g,kept)&&!cancellation(),"boundary clipped normalizer forwarding");return value(returned);};
    const SharedBoundaryIntent intent{{{territorialRef("A"),box(0,8)},{territorialRef("B"),box(8,20)}}};
    const auto r=calculateBoundaryGeometryPreview(w.project.snapshot(),intent,callbacks,w.ticket.token());
    require(r.ok()&&normalization==1&&r.rows.size()==3&&r.patch().replacements.size()==3,"boundary child clipping dependency contract");
    require(r.rows.back().owner==territorialRef("partial")&&r.rows.back().after&&exact(*r.rows.back().after,returned)&&exact(r.patch().replacements.back().geometry,returned),"boundary ignored returned clipped normalization");
    require(exact(r.patch().replacements[0].geometry,intent.drafts[0].geometry)&&exact(r.patch().replacements[1].geometry,intent.drafts[1].geometry),"boundary replaced canonical drafts with wrapped views");
}
}
int main() {
    try {
        authorityAndInitialCancellation();annexOrderedRoutesAndObservationCopy();annexFailureAndCancellationBoundaries();
        observationFailuresAndCancellationKeepPartialReceipt();splitFailuresAndCancellationPreservePolicy();boundaryClippingUsesReturnedNormalizationOnly();
        annexBlockingProgressAndOptionalObservation();riverAbsentAndEmptyRemnantsStayProtected();
        childSplitUsesRawOnlyForReparentedOwner();rootExhaustionKeepsIdentityUnionAndInertReceipt();boundarySealDependenciesAndErrors();
        std::cout<<"11 territorial preview engine contract groups passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}

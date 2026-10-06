#include <pandoeditor/map/territoryselection.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace pandoeditor;
namespace {
using Status=GeometryOperationStatus;
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
bool exact(double a,double b) {return std::memcmp(&a,&b,sizeof(double))==0;}
bool exact(const Point& a,const Point& b) {return exact(a.x,b.x)&&exact(a.y,b.y);}
bool exact(const Ring&,const Ring&);
bool exact(const Polygon&,const Polygon&);
template<class T> bool sequence(const std::vector<T>& a,const std::vector<T>& b) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)if(!exact(a[i],b[i]))return false;
    return true;
}
bool exact(const Ring& a,const Ring& b) {return sequence(a,b);}
bool exact(const Polygon& a,const Polygon& b) {return sequence(a,b);}
bool exact(const Geometry& a,const Geometry& b) {
    return a.type==b.type&&sequence(a.points,b.points)&&sequence(a.lines,b.lines)&&sequence(a.polygons,b.polygons);
}
bool exact(const std::optional<Geometry>& a,const Geometry& b) {return a&&exact(*a,b);}
Geometry box(double x,double y=0,double size=2) {
    Geometry g;g.type="Polygon";g.polygons={{{{x,y},{x,y+size},{x+size,y+size},{x+size,y},{x,y}}}};return g;
}
TerritorySelectionSource source(std::string id,Geometry g) {
    TerritorySelectionSource s;s.ref=territorialRef(id);s.geometry=std::move(g);s.geometryRef={"geometry-"+id,3};
    s.name="name-"+id;s.propertiesJson="{\"kept\":true}";return s;
}
GeometryOperationResult completed(Geometry g) {return {Status::Completed,std::move(g),{}};}
void noPublished(const TerritorySelectionDerivedResult& r) {
    require(r.componentFeatures.empty()&&r.components.empty()&&r.riverSliverContext.empty(),"partial derived collections published");
    require(!r.baseSourceGeometry&&!r.workingSourceGeometry&&!r.archivedGeometry&&!r.currentGeometry&&!r.combinedGeometry&&!r.remainingGeometry,"partial derived geometry published");
}
struct Calls {
    int wrap=0,clip=0,river=0,normalize=0;
    int total() const {return wrap+clip+river+normalize;}
};
TerritorySelectionCalculators forbidden(Calls& calls) {
    return {
        [&](const GeometryOperationRequest&,const GeometryCancellation&)->GeometryOperationResult {++calls.clip;throw std::runtime_error("forbidden clip");},
        [&](const GeometryOperationRequest&,const GeometryCancellation&)->GeometryOperationResult {++calls.river;throw std::runtime_error("forbidden river clip");},
        [&](const Geometry&,const GeometryCancellation&)->GeometryOperationResult {++calls.wrap;throw std::runtime_error("forbidden wrap");},
        [&](const Geometry&,const GeometryCancellation&)->GeometryOperationResult {++calls.normalize;throw std::runtime_error("forbidden normalization");}
    };
}
// These callbacks deliberately perform no geometric computation. They only
// expose the seam, ordered operands and state publication boundaries.
TerritorySelectionCalculators inert(Calls& calls,const Geometry& sentinel) {
    return {
        [&](const GeometryOperationRequest&,const GeometryCancellation&) {++calls.clip;return completed(sentinel);},
        [&](const GeometryOperationRequest&,const GeometryCancellation&) {++calls.river;return completed(sentinel);},
        [&](const Geometry& g,const GeometryCancellation&) {++calls.wrap;return completed(g);},
        [&](const Geometry& g,const GeometryCancellation&) {++calls.normalize;return completed(g);}
    };
}
TerritorySelectionState twoSources() {
    TerritorySelectionState s;s.revision=91;s.sources={source("A",box(0)),source("B",box(4))};return s;
}
void emptyAndOneSourceAreExact() {
    Calls calls;const auto runtime=forbidden(calls);
    TerritorySelectionState empty;empty.revision=17;
    const auto result=rebuildTerritorySelection(empty,runtime);
    require(result.status==Status::Completed&&result.inputRevision==17,"empty rebuild status/revision");noPublished(result);
    auto g=box(-0.0);g.type="MultiPolygon";g.polygons.push_back(box(8,3).polygons.front());
    TerritorySelectionState one;one.revision=19;one.sources={source("sentinel",g)};
    const auto single=rebuildTerritorySelection(one,runtime);
    require(single.status==Status::Completed&&single.inputRevision==19,"one source rebuild status/revision");
    require(exact(single.baseSourceGeometry,g)&&exact(single.workingSourceGeometry,g)&&exact(single.remainingGeometry,g),"one source was normalized or reordered");
    require(!single.archivedGeometry&&!single.currentGeometry&&!single.combinedGeometry,"one source spuriously selected");
    require(single.components.size()==2&&single.componentFeatures.size()==1,"one source component shape");
    const auto& feature=single.componentFeatures[0];
    require(exact(feature.source.geometry,g)&&feature.source.ref.id=="sentinel"&&feature.source.geometryRef==one.sources[0].geometryRef&&feature.source.propertiesJson==one.sources[0].propertiesJson,"component source provenance/coordinates changed");
    require(feature.sourcePolygonIndices==std::vector<std::size_t>{0,1},"source polygon order changed");
    for(std::size_t i=0;i<2;++i) {
        const auto& c=single.components[i];Geometry p;p.type="Polygon";p.polygons={g.polygons[i]};
        require(c.key=="component:sentinel:"+std::to_string(i)+":0"&&c.componentKey=="sentinel:"+std::to_string(i)&&c.polygonIndex==i&&c.sourcePolygonIndex==i&&exact(c.geometry,p),"component order or signed-zero identity changed");
    }
    require(exact(one.sources[0].geometry,g),"one source input changed");
    for(const auto& state:{empty,one}) {const auto cancelled=rebuildTerritorySelection(state,runtime,[]{return true;});
        require(cancelled.status==Status::Cancelled&&cancelled.detail.empty()&&cancelled.inputRevision==state.revision,"shortcut ignored initial cancellation");noPublished(cancelled);}
    auto cached=one;cached.baseSourceGeometry=g;cached.workingSourceGeometry=g;cached.archivedGeometry=g;
    cached.currentGeometry=g;cached.combinedGeometry=g;cached.remainingGeometry=g;cached.components=single.components;
    cached.componentFeatures=single.componentFeatures;cached.derivedRiverSliverContext={{"sentinel",0,{g}}};
    const auto cancelledCached=rebuildTerritorySelection(cached,runtime,[]{return true;});
    require(cancelledCached.status==Status::Cancelled&&cancelledCached.detail.empty()&&cancelledCached.inputRevision==one.revision,"cached initial cancellation status");noPublished(cancelledCached);
    require(calls.total()==0,"shortcut called runtime");
}
void orderedPolygonPipeline() {
    const auto A=box(0),B=box(4),C=box(8),I=box(12);
    auto D=box(16);D.type="MultiPolygon";D.polygons.push_back(box(20).polygons.front());
    const auto beforeA=A,beforeB=B,beforeC=C;
    std::vector<std::string> order;Calls calls;
    TerritorySelectionCalculators runtime;
    runtime.wrap=[&](const Geometry& g,const GeometryCancellation&) {
        ++calls.wrap;
        if(exact(g,A))order.push_back("wrap(A)");else if(exact(g,B))order.push_back("wrap(B)");
        else if(exact(g,I))order.push_back("wrap(I)");else if(exact(g,C))order.push_back("wrap(C)");else require(false,"unexpected wrapped operand");
        return completed(g);
    };
    runtime.clip=[&](const GeometryOperationRequest& r,const GeometryCancellation&) {
        ++calls.clip;require(exact(r.left,Geometry{})&&exact(r.right,Geometry{}),"legacy left/right populated");
        require(r.operands.size()==2,"full ordered operand vector lost");
        if(calls.clip==1) {require(r.operation==GeometryOperation::Intersection&&exact(r.operands[0],A)&&exact(r.operands[1],B),"intersection operand order");order.push_back("clip(Intersection,[A,B])");return completed(I);}
        require(calls.clip==2&&r.operation==GeometryOperation::Difference&&exact(r.operands[0],I)&&exact(r.operands[1],C),"difference operand order");order.push_back("clip(Difference,[I,C])");return completed(D);
    };
    runtime.normalizeClipped=[&](const Geometry& g,const GeometryCancellation&) {
        ++calls.normalize;require(exact(g,calls.normalize==1?I:D),"normalization result order");order.push_back(calls.normalize==1?"normalize(I)":"normalize(D)");return completed(g);
    };
    runtime.clipRiverIntermediate=[&](const GeometryOperationRequest&,const GeometryCancellation&)->GeometryOperationResult {++calls.river;throw std::runtime_error("polygon used river");};
    const auto r=prepareTerritoryPolygonCandidates(A,B,C,runtime);
    require(order==std::vector<std::string>{"wrap(A)","wrap(B)","clip(Intersection,[A,B])","normalize(I)","wrap(I)","wrap(C)","clip(Difference,[I,C])","normalize(D)"},"pipeline call order");
    require(r.status==Status::Completed&&r.detail.empty()&&r.candidates.size()==1&&r.candidates[0].id.empty()&&!r.candidates[0].area&&exact(r.candidates[0].geometry,D),"disconnected result candidate shape changed");
    require(exact(A,beforeA)&&exact(B,beforeB)&&exact(C,beforeC),"polygon inputs changed");
    // A three-source call rules out reducing the full vector through binary
    // unions, and distinct wrapped results rule out discarding transforms.
    Calls unionCalls;auto unionRuntime=forbidden(unionCalls);
    const std::vector<Geometry> original{A,B,C},wrapped{box(24),box(28),box(32)};
    unionRuntime.wrap=[&](const Geometry& g,const GeometryCancellation&) {
        require(unionCalls.wrap<3&&exact(g,original[unionCalls.wrap]),"source wrap order");
        return completed(wrapped[unionCalls.wrap++]);
    };
    unionRuntime.clip=[&](const GeometryOperationRequest& request,const GeometryCancellation&) {
        ++unionCalls.clip;require(request.operation==GeometryOperation::Union&&request.operands.size()==3,"full source union was reduced");
        require(exact(request.left,Geometry{})&&exact(request.right,Geometry{}),"source union legacy operands populated");
        for(std::size_t i=0;i<3;++i)require(exact(request.operands[i],wrapped[i]),"source union operand order or wrapped value lost");
        return completed(I);
    };
    unionRuntime.normalizeClipped=[&](const Geometry& g,const GeometryCancellation&) {++unionCalls.normalize;require(exact(g,I),"source union normalized wrong result");return completed(D);};
    TerritorySelectionState state;state.sources={source("A",A),source("B",B),source("C",C)};
    const auto derived=rebuildTerritorySelection(state,unionRuntime);
    require(derived.status==Status::Completed&&exact(derived.baseSourceGeometry,D)&&derived.components.size()==3,"ordered full source union output");
    require(unionCalls.wrap==3&&unionCalls.clip==1&&unionCalls.normalize==1&&unionCalls.river==0,"source union made extra callbacks");
}
void emptyStagesPreservePolicy() {
    const auto A=box(0),B=box(4),C=box(8),sentinel=box(12);
    for(const int stage:{0,1,2,3}) {
        Calls calls;auto runtime=inert(calls,sentinel);
        runtime.clip=[&](const GeometryOperationRequest&,const GeometryCancellation&) {++calls.clip;return calls.clip==(stage<2?1:2)&&stage%2==0?GeometryOperationResult{Status::Empty,{},{}}:completed(sentinel);};
        runtime.normalizeClipped=[&](const Geometry& g,const GeometryCancellation&) {++calls.normalize;return calls.normalize==(stage<2?1:2)&&stage%2==1?GeometryOperationResult{Status::Empty,{},{}}:completed(g);};
        const auto r=prepareTerritoryPolygonCandidates(A,B,C,runtime);
        require(r.status==Status::Empty&&r.succeeded()&&r.candidates.empty()&&r.detail.empty(),"polygon empty stage policy");
        require(calls.clip==(stage<2?1:2)&&calls.wrap==(stage<2?2:4)&&calls.normalize==(stage==0?0:stage==1||stage==2?1:2),"empty result entered a later stage");
    }
    for(const bool normalizeEmpty:{false,true}) {
        Calls calls;auto runtime=inert(calls,sentinel);
        if(normalizeEmpty)runtime.normalizeClipped=[&](const Geometry&,const GeometryCancellation&) {++calls.normalize;return GeometryOperationResult{Status::Empty,{},{}};};
        else runtime.clip=[&](const GeometryOperationRequest&,const GeometryCancellation&) {++calls.clip;return GeometryOperationResult{Status::Empty,{},{}};};
        const auto r=rebuildTerritorySelection(twoSources(),runtime);
        require(r.status==Status::Completed&&r.succeeded()&&!r.baseSourceGeometry&&!r.workingSourceGeometry&&!r.remainingGeometry,"empty rebuild must still complete");
        require(r.components.size()==2&&r.componentFeatures.size()==2,"source identity components lost after empty union");
        require(calls.wrap==2&&calls.clip==1&&calls.river==0&&calls.normalize==(normalizeEmpty?1:0),"empty rebuild made extra calls");
    }
    Calls calls;auto runtime=inert(calls,sentinel);bool emptyOperandObserved=false;
    runtime.wrap=[&](const Geometry& g,const GeometryCancellation&) {++calls.wrap;return calls.wrap==1?GeometryOperationResult{Status::Empty,{},{}}:completed(g);};
    runtime.clip=[&](const GeometryOperationRequest& r,const GeometryCancellation&) {++calls.clip;if(calls.clip==1)emptyOperandObserved=exact(r.operands[0],Geometry{})&&exact(r.operands[1],B);return completed(sentinel);};
    const auto r=prepareTerritoryPolygonCandidates(A,B,C,runtime);
    require(r.status==Status::Completed&&emptyOperandObserved&&calls.clip==2&&calls.normalize==2,"wrap Empty was short-circuited");
}
void riverRoutingAndArchivedSlivers() {
    const auto sentinel=box(12);
    for(const bool enabled:{false,true}) {
        Calls calls;const auto runtime=inert(calls,sentinel);auto state=twoSources();state.useRiverBoundaries=enabled;
        const auto r=rebuildTerritorySelection(state,runtime);
        require(r.status==Status::Completed&&calls.clip==(enabled?0:1)&&calls.river==(enabled?1:0),"active river routing changed");
    }
    for(const bool river:{false,true}) {
        Calls calls;const auto runtime=inert(calls,sentinel);TerritorySelectionState state;state.revision=7;state.sources={source("donor",box(0))};
        TerritorySelectionComponent chosen;chosen.key="chosen";chosen.countryId="donor";chosen.componentKey="donor:0";chosen.sourcePolygonIndex=0;chosen.usesRiverBoundary=river;chosen.snapshotId="archive";chosen.geometry=box(4);
        auto other=chosen;other.key="unselected";other.geometry=box(8);
        state.parts={{"part",TerritorySelectionMethod::Components,chosen.geometry,chosen}};
        state.componentSnapshots={{"archive",{chosen,other}}};
        const auto r=rebuildTerritorySelection(state,runtime);
        require(r.status==Status::Completed&&!state.useRiverBoundaries,"archived rebuild failed");
        require(calls.clip==(river?0:2)&&calls.river==(river?3:0),"archived river/sliver did not route independently of active flag");
        if(river)require(r.riverSliverContext.size()==1&&r.riverSliverContext[0].donorId=="donor"&&r.riverSliverContext[0].polygonIndex==0&&r.riverSliverContext[0].unselectedGeometries.size()==1&&exact(r.riverSliverContext[0].unselectedGeometries[0],sentinel),"archived snapshot sliver context changed");
        else require(r.riverSliverContext.empty(),"ordinary archive produced river context");
        require(state.parts[0].component->snapshotId=="archive"&&exact(state.componentSnapshots[0].items[1].geometry,other.geometry),"snapshot inputs changed");
    }

}
void failureForwardingAndAtomicPublication() {
    const auto A=box(0),B=box(4),C=box(8),sentinel=box(12);
    for(const int stage:{0,1,2,3})for(const std::string& detail:{std::string{},std::string{"exact-stage-error"}}) {
        Calls calls;auto runtime=inert(calls,sentinel);
        const auto failed=GeometryOperationResult{Status::Failed,sentinel,detail};
        if(stage==0)runtime.wrap=[&](const Geometry&,const GeometryCancellation&) {++calls.wrap;return failed;};
        if(stage==1)runtime.clip=[&](const GeometryOperationRequest&,const GeometryCancellation&) {++calls.clip;return failed;};
        if(stage==2)runtime.normalizeClipped=[&](const Geometry&,const GeometryCancellation&) {++calls.normalize;return failed;};
        if(stage==3)runtime.clipRiverIntermediate=[&](const GeometryOperationRequest&,const GeometryCancellation&) {++calls.river;return failed;};
        const std::string expected=(stage==1||stage==3)&&detail.empty()?"TERRITORY_SELECTION_CALCULATION_FAILED":detail;
        if(stage!=3) {const auto r=prepareTerritoryPolygonCandidates(A,B,C,runtime);
            require(r.status==Status::Failed&&r.detail==expected&&r.candidates.empty(),"polygon failure detail/publication");}
        auto state=twoSources();state.useRiverBoundaries=stage==3;const auto r=rebuildTerritorySelection(state,runtime);
        require(r.status==Status::Failed&&r.detail==expected&&r.inputRevision==state.revision,"rebuild failure detail/revision");noPublished(r);
    }
    Calls calls;auto runtime=inert(calls,sentinel);
    runtime.clip=[&](const GeometryOperationRequest&,const GeometryCancellation&)->GeometryOperationResult {++calls.clip;throw std::runtime_error("thrown sentinel");};
    const auto draft=prepareTerritoryPolygonCandidates(A,B,C,runtime);require(draft.status==Status::Failed&&draft.detail=="thrown sentinel"&&draft.candidates.empty(),"thrown error relabeled");
    const auto r=rebuildTerritorySelection(twoSources(),runtime);require(r.status==Status::Failed&&r.detail=="thrown sentinel"&&r.inputRevision==91,"thrown rebuild error relabeled");noPublished(r);
}
void cancellationCheckpoints() {
    const auto A=box(0),B=box(4),C=box(8),sentinel=box(12);
    for(const bool flip:{false,true})for(const int stage:{0,1,2,3}) {
        for(const bool derived:{false,true}) {
            if(stage==3&&!derived)continue;
            Calls calls;bool cancelled=false;auto runtime=inert(calls,sentinel);
            const auto stop=[&](Geometry g) {if(flip){cancelled=true;return completed(std::move(g));}return GeometryOperationResult{Status::Cancelled,sentinel,"must not publish"};};
            if(stage==0)runtime.wrap=[&](const Geometry& g,const GeometryCancellation&) {++calls.wrap;if(calls.wrap==1)return stop(g);return completed(g);};
            if(stage==1)runtime.clip=[&](const GeometryOperationRequest&,const GeometryCancellation&) {++calls.clip;return stop(sentinel);};
            if(stage==2)runtime.normalizeClipped=[&](const Geometry& g,const GeometryCancellation&) {++calls.normalize;return stop(g);};
            if(stage==3)runtime.clipRiverIntermediate=[&](const GeometryOperationRequest&,const GeometryCancellation&) {++calls.river;return stop(sentinel);};
            const auto cancellation=[&]{return cancelled;};
            if(derived) {auto state=twoSources();state.useRiverBoundaries=stage==3;const auto r=rebuildTerritorySelection(state,runtime,cancellation);
                require(r.status==Status::Cancelled&&r.detail.empty()&&r.inputRevision==91,"derived cancellation result");noPublished(r);}
            else {const auto r=prepareTerritoryPolygonCandidates(A,B,C,runtime,cancellation);require(r.status==Status::Cancelled&&r.detail.empty()&&r.candidates.empty(),"draft cancellation result");}
            require(calls.wrap==(stage==0&&!flip?1:2)&&calls.clip==(stage==3||(stage==0&&!flip)?0:1)&&calls.river==(stage==3?1:0)&&calls.normalize==(stage==2?1:0),"cancellation moved an existing call boundary");
        }
    }
    Calls immediateCalls;auto immediate=inert(immediateCalls,sentinel);
    const auto initial=prepareTerritoryPolygonCandidates(A,B,C,immediate,[]{return true;});
    require(initial.status==Status::Cancelled&&initial.candidates.empty()&&initial.detail.empty()&&immediateCalls.total()==0,"initial polygon cancellation");
    for(const bool derived:{false,true}) {
        int checkpoints=0;Calls calls;auto runtime=inert(calls,sentinel);
        if(derived)require(rebuildTerritorySelection(twoSources(),runtime,[&]{++checkpoints;return false;}).status==Status::Completed,"checkpoint reference rebuild");
        else require(prepareTerritoryPolygonCandidates(A,B,C,runtime,[&]{++checkpoints;return false;}).status==Status::Completed,"checkpoint reference polygon");
        require(checkpoints==(derived?20:9),"existing engine checkpoint count changed");
        for(int stop=1;stop<=checkpoints;++stop) {int polls=0;Calls stoppedCalls;auto stopped=inert(stoppedCalls,sentinel);
            if(derived) {const auto r=rebuildTerritorySelection(twoSources(),stopped,[&]{return ++polls>=stop;});require(r.status==Status::Cancelled&&r.detail.empty()&&r.inputRevision==91,"checkpoint rebuild cancellation");noPublished(r);}
            else {const auto r=prepareTerritoryPolygonCandidates(A,B,C,stopped,[&]{return ++polls>=stop;});require(r.status==Status::Cancelled&&r.detail.empty()&&r.candidates.empty(),"checkpoint polygon cancellation");}
            require(polls==stop,"cancellation polled after stop boundary");
        }
    }
}
void valueOwnershipNeedsNoRuntime() {
    TerritorySelection selection;const auto donor=source("donor",box(-0.0));
    require(selection.resetSources({donor}),"pure source reset");
    require(selection.requestMethod(TerritorySelectionMethod::Polygon)==TerritoryMethodChange::Activated,"pure method request");
    require(selection.setCandidates({{"ignored",box(4),{}}}),"pure candidate install");
    const auto ids=selection.state().selectedCandidateIds;const auto revision=selection.state().revision;
    const auto method=selection.state().activeMethod;const auto originalRequested=selection.state().requestedMethod;const auto phase=selection.state().activePhase;
    TerritorySelectionDerivedResult stale;stale.inputRevision=revision-1;stale.status=Status::Completed;stale.baseSourceGeometry=box(8);
    require(!selection.installDerived(stale)&&!selection.derivedReady(),"stale success installed");
    stale.status=Status::Failed;stale.detail="stale failure";require(!selection.installDerived(stale)&&selection.lastError().empty(),"stale failure changed error");
    TerritorySelectionDerivedResult failed;failed.inputRevision=revision;failed.status=Status::Failed;failed.detail="current failure";failed.components.push_back({});failed.currentGeometry=box(10);
    require(!selection.installDerived(failed)&&selection.lastError()=="current failure"&&!selection.state().currentGeometry&&selection.state().components.empty(),"failed result partially installed");
    failed.status=Status::Cancelled;failed.detail="not an error";require(!selection.installDerived(failed)&&selection.lastError()=="current failure","cancelled result changed error");
    require(selection.state().selectedCandidateIds==ids&&selection.state().activeMethod==method&&selection.state().requestedMethod==originalRequested&&selection.state().activePhase==phase&&selection.state().revision==revision,"failed/cancelled result erased intent");
    TerritorySelectionDerivedResult success;success.inputRevision=revision;success.status=Status::Completed;success.baseSourceGeometry=donor.geometry;success.workingSourceGeometry=donor.geometry;success.currentGeometry=box(4);success.combinedGeometry=box(4);success.remainingGeometry=donor.geometry;
    require(selection.installDerived(success)&&selection.derivedReady()&&selection.lastError().empty()&&exact(selection.state().currentGeometry,box(4)),"derived success not installed");
    require(selection.state().sources[0].ref.id=="donor"&&selection.state().selectedCandidateIds==ids&&selection.state().candidates.size()==1&&selection.state().activeMethod==method&&selection.state().activePhase==phase&&selection.state().revision==revision,"success overwrote semantic intent");
    require(selection.requestMethod(TerritorySelectionMethod::Line)==TerritoryMethodChange::NeedsConfirmation,"pure confirmation request");
    const auto confirmation=selection.state().methodChangeConfirmation;const auto requested=selection.state().requestedMethod;
    failed.inputRevision=selection.state().revision;failed.status=Status::Failed;require(!selection.installDerived(failed)&&selection.state().methodChangeConfirmation==confirmation&&selection.state().requestedMethod==requested&&selection.state().selectedCandidateIds==ids,"failed result erased confirmation intent");
    require(selection.cancelMethodChange()&&!selection.state().methodChangeConfirmation,"pure confirmation cancellation");
    require(selection.toggleCandidate(ids.front())&&selection.state().selectedCandidateIds.empty(),"pure candidate toggle");
}
// Scripted callbacks expose the legacy split-draft branch's transport and
// failure boundaries. They are not substitutes for actual clipping parity.
void splitDraftUsesOrderedBinaryPipeline() {
    const auto drawn=box(-0.0),source=box(4),wrappedDrawn=box(8),wrappedSource=box(12),clipped=box(16);
    auto normalized=box(20);normalized.type="MultiPolygon";normalized.polygons.push_back(box(24).polygons.front());
    const auto beforeDrawn=drawn,beforeSource=source;
    Calls calls;auto runtime=forbidden(calls);std::vector<std::string> order;int polls=0;
    const GeometryCancellation cancelled=[&]{++polls;return true;};
    runtime.wrap=[&](const Geometry& g,const GeometryCancellation& c) {
        require(bool(c),"split cancellation callback lost");++calls.wrap;
        require(exact(g,calls.wrap==1?drawn:source),"split wrap input order");
        order.push_back(calls.wrap==1?"drawn":"source");return completed(calls.wrap==1?wrappedDrawn:wrappedSource);
    };
    runtime.clip=[&](const GeometryOperationRequest& r,const GeometryCancellation& c) {
        ++calls.clip;require(bool(c),"split clip cancellation callback lost");
        require(r.operation==GeometryOperation::Intersection&&r.operands.empty()&&exact(r.left,wrappedDrawn)&&exact(r.right,wrappedSource),"split binary wrapped intersection changed");
        order.push_back("clip");return completed(clipped);
    };
    runtime.normalizeClipped=[&](const Geometry& g,const GeometryCancellation& c) {
        ++calls.normalize;require(bool(c)&&exact(g,clipped),"split normalization input changed");order.push_back("normalize");return completed(normalized);
    };
    const auto result=prepareSplitPolygonCandidates(drawn,source,runtime,cancelled);
    require(order==std::vector<std::string>{"drawn","source","clip","normalize"},"split draft order changed");
    require(result.status==Status::Completed&&result.detail.empty()&&result.candidates.size()==1&&result.candidates.front().id.empty()&&!result.candidates.front().area&&exact(result.candidates.front().geometry,normalized),"split candidate shape changed");
    require(calls.wrap==2&&calls.clip==1&&calls.normalize==1&&calls.river==0&&polls==0,"split added a callback or cancellation checkpoint");
    require(exact(drawn,beforeDrawn)&&exact(source,beforeSource),"split draft mutated inputs");
    // The existing branch trusts runtime callbacks, with no GeometryStore check.
    Geometry invalid;invalid.type="Point";invalid.points={{0,0}};
    Calls invalidCalls;const auto trusted=inert(invalidCalls,normalized);
    const auto accepted=prepareSplitPolygonCandidates(invalid,Geometry{},trusted);
    require(accepted.status==Status::Completed&&invalidCalls.wrap==2&&invalidCalls.clip==1&&invalidCalls.normalize==1,"split introduced upfront validation");
}
void splitDraftWrapStatusesKeepFirstPrecedence() {
    const auto a=box(0),b=box(4),sentinel=box(8);
    for(const auto first:{Status::Completed,Status::Empty,Status::Cancelled,Status::Failed})
    for(const auto second:{Status::Completed,Status::Empty,Status::Cancelled,Status::Failed}) {
        Calls calls;auto runtime=inert(calls,sentinel);
        runtime.wrap=[&](const Geometry&,const GeometryCancellation&) {++calls.wrap;return GeometryOperationResult{calls.wrap==1?first:second,sentinel,calls.wrap==1?"first":"second"};};
        const auto result=prepareSplitPolygonCandidates(a,b,runtime);
        const bool firstFailed=first==Status::Cancelled||first==Status::Failed;
        const bool secondFailed=second==Status::Cancelled||second==Status::Failed;
        require(calls.wrap==2,"returned first wrap failure suppressed source wrap");
        if(firstFailed||secondFailed) {
            require(result.status==(firstFailed?first:second)&&result.detail==(firstFailed?"first":"second")&&result.candidates.empty(),"split first-failing wrap precedence/detail changed");
            require(calls.clip==0&&calls.normalize==0,"failed wrap entered clip");
        } else require(result.status==Status::Completed&&calls.clip==1&&calls.normalize==1&&result.candidates.size()==1,"wrap Empty became an early draft Empty");
        require(calls.river==0,"split draft entered river clip");
    }
}
void splitDraftClipAndNormalizeStatusesForwardExactly() {
    const auto a=box(0),b=box(4),sentinel=box(8);
    for(const bool normalizeStage:{false,true})
    for(const auto status:{Status::Completed,Status::Empty,Status::Cancelled,Status::Failed})
    for(const std::string& detail:{std::string{},std::string{"stage-detail"}}) {
        Calls calls;auto runtime=inert(calls,sentinel);
        if(normalizeStage)runtime.normalizeClipped=[&](const Geometry&,const GeometryCancellation&) {++calls.normalize;return GeometryOperationResult{status,sentinel,detail};};
        else runtime.clip=[&](const GeometryOperationRequest&,const GeometryCancellation&) {++calls.clip;return GeometryOperationResult{status,sentinel,detail};};
        const auto result=prepareSplitPolygonCandidates(a,b,runtime);
        const bool completedStatus=status==Status::Completed;
        require(result.status==status&&result.detail==(normalizeStage||!completedStatus?detail:std::string{})&&result.candidates.size()==(completedStatus?1u:0u),"split stage status/detail/candidate mapping changed");
        require(calls.wrap==2&&calls.clip==1&&calls.normalize==(normalizeStage||completedStatus?1:0)&&calls.river==0,"split Empty/failure entered later normalization");
    }
}
void splitDraftExceptionsEscapeImmediately() {
    const auto a=box(0),b=box(4),sentinel=box(8);
    for(const int stage:{0,1,2,3}) {
        Calls calls;auto runtime=inert(calls,sentinel);
        runtime.wrap=[&](const Geometry& g,const GeometryCancellation&) {++calls.wrap;if(calls.wrap==stage+1)throw std::runtime_error("split-exception");return completed(g);};
        if(stage==2)runtime.clip=[&](const GeometryOperationRequest&,const GeometryCancellation&)->GeometryOperationResult {++calls.clip;throw std::runtime_error("split-exception");};
        if(stage==3)runtime.normalizeClipped=[&](const Geometry&,const GeometryCancellation&)->GeometryOperationResult {++calls.normalize;throw std::runtime_error("split-exception");};
        bool threw=false;
        try {(void)prepareSplitPolygonCandidates(a,b,runtime);}catch(const std::runtime_error& error){threw=std::string(error.what())=="split-exception";}
        require(threw,"split swallowed adapter exception into draft result");
        require(calls.wrap==(stage==0?1:2)&&calls.clip==(stage>=2?1:0)&&calls.normalize==(stage==3?1:0),"exception unwinding called a later stage");
    }
    Calls calls;auto runtime=inert(calls,sentinel);
    runtime.wrap=[&](const Geometry& g,const GeometryCancellation&) {++calls.wrap;if(calls.wrap==2)throw std::runtime_error("source-exception");return GeometryOperationResult{Status::Failed,g,"first-failure"};};
    bool threw=false;try {(void)prepareSplitPolygonCandidates(a,b,runtime);}catch(const std::runtime_error& error){threw=std::string(error.what())=="source-exception";}
    require(threw&&calls.wrap==2&&calls.clip==0,"first returned failure hid second thrown exception");
}

}
int main() {
    const std::pair<const char*,void(*)()> cases[]={
        {"split draft ordered binary pipeline",splitDraftUsesOrderedBinaryPipeline},
        {"split draft wrap precedence",splitDraftWrapStatusesKeepFirstPrecedence},
        {"split draft stage status forwarding",splitDraftClipAndNormalizeStatusesForwardExactly},
        {"split draft exception escape",splitDraftExceptionsEscapeImmediately},
        {"empty and one source identity",emptyAndOneSourceAreExact},
        {"ordered polygon pipeline",orderedPolygonPipeline},
        {"empty stage policies",emptyStagesPreservePolicy},
        {"river and archived snapshot routing",riverRoutingAndArchivedSlivers},
        {"error forwarding and atomic publication",failureForwardingAndAtomicPublication},
        {"cancellation checkpoints",cancellationCheckpoints},
        {"value ownership without runtime",valueOwnershipNeedsNoRuntime}
    };
    for(const auto& item:cases)try {item.second();std::cout<<"PASS: "<<item.first<<'\n';}catch(const std::exception& error) {std::cerr<<"FAIL: "<<item.first<<": "<<error.what()<<'\n';return 1;}
    return 0;
}

#include "../app/terraindisplaystate.h"
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
TerrainDisplayScope scope(){return {7,9,11,13,17,19,23,29};}
TerrainDisplayResource tile(const char* key,int level,TerrainDisplayRect bounds) {
    return {QString::fromLatin1(key),QStringLiteral("bytes:")+QString::fromLatin1(key),level,bounds};
}
const auto west=tile("0/w",0,{-180,90,0,-90});
const auto east=tile("0/e",0,{0,90,180,-90});
const auto oldWest=tile("1/old-west",1,{-180,90,0,-90});
const auto coarseEast=tile("1/coarse-east",1,{0,90,180,-90});
const auto nw=tile("2/nw",2,{-180,90,0,0});
const auto sw=tile("2/sw",2,{-180,0,0,-90});
const auto ne=tile("2/ne",2,{0,90,180,0});
const auto se=tile("2/se",2,{0,0,180,-90});
TerrainDisplayDemand demand(std::vector<TerrainDisplayResource> targets) {
    return {scope(),{west,east},std::move(targets),{},{{-180,90,180,-90}},{0}};
}
std::vector<QString> keys(const std::vector<TerrainDisplayDraw>& draws) {
    std::vector<QString> result;for(const auto& draw:draws)result.push_back(draw.resource.key);return result;
}
bool contains(const std::vector<QString>& inventory,const QString& key) {
    return std::find(inventory.begin(),inventory.end(),key)!=inventory.end();
}
void upload(TerrainDisplayState& state,const TerrainDisplayScope& tag,const TerrainDisplayResource& resource) {
    require(state.cpuReady(tag,resource),"current CPU observation accepted");
    require(state.uploadSubmitted(tag,resource),"current upload submission accepted");
}
void present(TerrainDisplayState& state,std::uint64_t frame) {
    const auto candidate=state.buildCandidate();require(candidate.has_value(),"complete reserve enables candidate");
    require(state.submitCandidate(*candidate,frame,candidate->draws),"exact candidate submission accepted");
    require(state.acceptDisplayReceipt({candidate->scope,candidate->id,frame,candidate->draws}),"exact frame swap receipt accepted");
}
void coldDetailWaitsForSubmittedBase() {
    TerrainDisplayState state;const auto current=demand({nw,sw,ne,se});state.beginDemand(current);
    for(const auto& resource:current.target)require(state.cpuReady(current.scope,resource),"CPU readiness accepted");
    require(state.snapshot().uploadSubmittedKeys.empty()&&state.snapshot().displayed.empty(),"CPU-ready is not submitted or displayed");
    for(const auto& resource:current.target)require(state.uploadSubmitted(current.scope,resource),"detail submission accepted");
    require(!state.buildCandidate()&&state.snapshot().displayed.empty(),"detail alone cannot publish without world reserve");
    upload(state,current.scope,west);require(!state.buildCandidate(),"one base tile cannot bootstrap");
    upload(state,current.scope,east);require(state.snapshot().displayed.empty(),"base upload is not a display receipt");
    const auto candidate=state.buildCandidate();require(candidate&&keys(candidate->draws)==std::vector<QString>({nw.key,sw.key,ne.key,se.key}),"complete targets preserve semantic demand order and omit base draws");
    require(state.submitCandidate(*candidate,1,candidate->draws),"candidate submission accepted");
    require(state.snapshot().displayed.empty(),"submission alone does not authenticate an onscreen frame");
    require(state.acceptDisplayReceipt({candidate->scope,candidate->id,1,candidate->draws}),"current displayed receipt accepted");
    require(contains(state.snapshot().protectedKeys,west.key)&&contains(state.snapshot().protectedKeys,east.key),"base remains protected even when not drawn");
}
void partialReplacementRetiresOnlyAfterDisplay() {
    TerrainDisplayState state;auto initial=demand({oldWest,coarseEast});state.beginDemand(initial);
    upload(state,initial.scope,west);upload(state,initial.scope,east);upload(state,initial.scope,oldWest);present(state,1);
    auto next=demand({nw,sw,ne,se});++next.scope.candidateSequence;++next.scope.requestSequence;state.beginDemand(next);
    upload(state,next.scope,nw);
    const auto partial=state.buildCandidate();require(partial.has_value(),"partial candidate has base reserve");
    require(keys(partial->draws)==std::vector<QString>({west.key,east.key,oldWest.key,nw.key}),"base then retained detail then target");
    require(contains(state.snapshot().protectedKeys,oldWest.key),"upload cannot release old displayed detail");
    require(state.submitCandidate(*partial,2,partial->draws),"partial submitted");
    require(contains(state.snapshot().protectedKeys,oldWest.key),"submission cannot release old displayed detail");
    require(state.acceptDisplayReceipt({partial->scope,partial->id,2,partial->draws}),"partial receipt accepted");
    require(state.snapshot().releasedKeys.empty(),"northwest does not cover southwest fallback");
    upload(state,next.scope,sw);const auto completeWest=state.buildCandidate();require(completeWest.has_value(),"western replacement candidate");
    require(!contains(keys(completeWest->draws),oldWest.key),"union of two target interiors replaces old west");
    require(contains(state.snapshot().protectedKeys,oldWest.key),"candidate construction retains old lease");
    require(state.submitCandidate(*completeWest,3,completeWest->draws),"replacement submitted");
    require(state.snapshot().releasedKeys.empty(),"no retirement on upload/submission");
    require(state.acceptDisplayReceipt({completeWest->scope,completeWest->id,3,completeWest->draws}),"replacement displayed");
    require(state.snapshot().releasedKeys==std::vector<QString>({oldWest.key}),"accepted display releases only covered obsolete west");
}
void reverseZoomDrawsCoarseTargetLast() {
    TerrainDisplayState state;auto first=demand({nw,sw,ne,se});state.beginDemand(first);
    for(const auto& resource:{west,east,nw,sw,ne,se})upload(state,first.scope,resource);present(state,1);
    auto next=demand({oldWest,coarseEast});++next.scope.viewGeneration;++next.scope.requestSequence;state.beginDemand(next);
    upload(state,next.scope,oldWest);const auto candidate=state.buildCandidate();require(candidate.has_value(),"coarse partial candidate");
    require(keys(candidate->draws)==std::vector<QString>({west.key,east.key,ne.key,se.key,oldWest.key}),"eastern finer fallback precedes ready coarser target");
    require(state.submitCandidate(*candidate,2,candidate->draws),"coarse candidate submitted");
    require(state.acceptDisplayReceipt({candidate->scope,candidate->id,2,candidate->draws}),"coarse candidate displayed");
    require(state.snapshot().releasedKeys==std::vector<QString>({nw.key,sw.key}),"display retires western fine leases only");
}
void allEightStaleScopesReject() {
    std::uint64_t TerrainDisplayScope::* const fields[]={&TerrainDisplayScope::sourceEpoch,&TerrainDisplayScope::windowEpoch,
        &TerrainDisplayScope::contextEpoch,&TerrainDisplayScope::projectGeneration,&TerrainDisplayScope::viewGeneration,
        &TerrainDisplayScope::maskGeneration,&TerrainDisplayScope::candidateSequence,&TerrainDisplayScope::requestSequence};
    for(const auto field:fields) {
        TerrainDisplayState state;const auto current=demand({west,east});state.beginDemand(current);
        upload(state,current.scope,west);upload(state,current.scope,east);present(state,1);
        const auto before=state.snapshot();auto stale=current.scope;--(stale.*field);
        require(!state.cpuReady(stale,west)&&!state.uploadSubmitted(stale,west),"all stale asynchronous observations reject");
        const auto candidate=state.buildCandidate();require(candidate.has_value(),"current candidate");
        require(state.submitCandidate(*candidate,2,candidate->draws),"current submission");
        require(!state.acceptDisplayReceipt({stale,candidate->id,2,candidate->draws}),"all eight stale receipt identities reject");
        require(state.snapshot().displayed==before.displayed&&state.snapshot().protectedKeys==before.protectedKeys,
                "stale receipt cannot mutate displayed coverage or leases");
    }
}
void receiptInventoryAndFrameMustBeExact() {
    TerrainDisplayState state;const auto current=demand({west,east});state.beginDemand(current);
    upload(state,current.scope,west);upload(state,current.scope,east);const auto candidate=state.buildCandidate();require(candidate.has_value(),"candidate exists");
    auto wrong=candidate->draws;wrong[0].bounds.east-=0.000000001;
    require(!state.submitCandidate(*candidate,1,wrong),"submitted interior must match candidate exactly");
    require(state.submitCandidate(*candidate,1,candidate->draws),"exact submission accepted");
    require(!state.acceptDisplayReceipt({candidate->scope,candidate->id,2,candidate->draws}),"wrong frame receipt rejects");
    require(!state.acceptDisplayReceipt({candidate->scope,candidate->id+1,1,candidate->draws}),"wrong candidate receipt rejects");
    wrong=candidate->draws;wrong[0].resource.contentKey="different-bytes";
    require(!state.acceptDisplayReceipt({candidate->scope,candidate->id,1,wrong}),"content identity must match actual submitted resource");
    wrong=candidate->draws;std::reverse(wrong.begin(),wrong.end());
    require(!state.acceptDisplayReceipt({candidate->scope,candidate->id,1,wrong}),"draw order is part of authenticated inventory");
    require(state.snapshot().displayed.empty(),"rejected receipts cannot bootstrap");
}
void replicasShareOneProtectedResource() {
    TerrainDisplayState state;auto current=demand({west,east});current.domains={{170,90,190,-90}};current.worldOffsets={0,360};state.beginDemand(current);
    upload(state,current.scope,west);upload(state,current.scope,east);present(state,1);
    const auto snapshot=state.snapshot();require(snapshot.displayed.size()==2,"dateline draws two interiors");
    require(snapshot.displayed[0].resource.key==east.key&&snapshot.displayed[0].bounds==east.interior,"east uses original interior");
    require(snapshot.displayed[1].resource.key==west.key&&snapshot.displayed[1].bounds==TerrainDisplayRect{180,90,360,-90},"wrapped west has actual shifted bounds");
    require(snapshot.protectedKeys==std::vector<QString>({east.key,west.key}),"replicas never duplicate canonical leases");
    require(TerrainDisplayState::coverageGaps(snapshot.displayed,current.domains).empty(),"dateline domain has no geographic gap");
}
void exactArrangementDetectsNarrowGaps() {
    const std::vector<TerrainDisplayRect> domain={{-180,90,180,-90}};
    auto left=west;left.interior.east=-1e-10;
    std::vector<TerrainDisplayDraw> draws={{left,left.interior,0},{east,east.interior,0}};
    require(!TerrainDisplayState::coverageGaps(draws,domain).empty(),"narrow vertical gap cannot be hidden by epsilon");
    auto north=tile("north",1,{-180,90,180,1e-10}),south=tile("south",1,{-180,0,180,-90});
    draws={{north,north.interior,0},{south,south.interior,0}};
    require(!TerrainDisplayState::coverageGaps(draws,domain).empty(),"narrow horizontal gap cannot be hidden by sample points");
    draws={{west,west.interior,0},{east,east.interior,0}};
    require(TerrainDisplayState::coverageGaps(draws,domain).empty(),"exact poles and shared boundaries cover world");
}
void viewReuseAndOwnerResetAreDifferent() {
    TerrainDisplayState state;auto current=demand({west,east});state.beginDemand(current);
    upload(state,current.scope,west);upload(state,current.scope,east);present(state,1);
    auto moved=current;++moved.scope.viewGeneration;++moved.scope.maskGeneration;++moved.scope.candidateSequence;++moved.scope.requestSequence;
    state.beginDemand(moved);require(state.snapshot().uploadSubmittedKeys.size()==2,"immutable submitted textures survive a view change");
    require(state.snapshot().displayed.size()==2,"displayed fallback survives until current receipt");
    require(!state.cpuReady(current.scope,west),"late old-view decode cannot insert into current state");present(state,2);
    auto reset=moved;++reset.scope.contextEpoch;state.beginDemand(reset);
    require(state.snapshot().uploadSubmittedKeys.empty()&&state.snapshot().displayed.empty(),"context destruction drops old resources and display receipts");
    require(!state.buildCandidate(),"new context must rebuild its world reserve");
}
void retirementNeedsOwnerAcknowledgement() {
    TerrainDisplayState state;auto current=demand({oldWest,coarseEast});state.beginDemand(current);
    for(const auto& resource:{west,east,oldWest})upload(state,current.scope,resource);present(state,1);
    require(!state.retireResource(current.scope,oldWest.key,oldWest.contentKey),"displayed resource cannot retire");
    auto next=demand({nw,sw,ne,se});++next.scope.candidateSequence;++next.scope.requestSequence;state.beginDemand(next);
    for(const auto& resource:{nw,sw,ne,se})upload(state,next.scope,resource);present(state,2);
    require(contains(state.snapshot().uploadSubmittedKeys,oldWest.key),"lease release is not texture destruction");
    require(state.retireResource(next.scope,oldWest.key,oldWest.contentKey),"owner retirement acknowledgement removes unprotected resident");
    require(!contains(state.snapshot().uploadSubmittedKeys,oldWest.key),"retired texture no longer reusable");
}
void supersededCandidateAndDuplicateReceiptReject() {
    TerrainDisplayState state;const auto current=demand({west,east});state.beginDemand(current);
    upload(state,current.scope,west);upload(state,current.scope,east);const auto old=state.buildCandidate();const auto latest=state.buildCandidate();
    require(old&&latest&&latest->id>old->id,"progressive candidates have independent monotone identities");
    require(!state.submitCandidate(*old,1,old->draws),"superseded candidate cannot submit");
    require(state.submitCandidate(*latest,2,latest->draws),"latest candidate submits");
    const TerrainDisplayReceipt receipt{latest->scope,latest->id,2,latest->draws};
    require(state.acceptDisplayReceipt(receipt),"first matching receipt accepted");
    require(!state.acceptDisplayReceipt(receipt),"duplicate receipt cannot release leases twice");
}
void malformedWorldReserveRefuses() {
    TerrainDisplayState state;auto current=demand({west,east});current.base={west};bool threw=false;
    try{state.beginDemand(current);}catch(const std::invalid_argument&){threw=true;}
    require(threw,"declared base must actually cover the complete canonical world");
}
void rasterVariantsKeepIndependentDisplayLeases() {
    auto colorWest=west,colorEast=east,grayWest=west,grayEast=east;
    colorWest.key+="/raster-color";colorEast.key+="/raster-color";
    grayWest.key+="/raster-gray";grayEast.key+="/raster-gray";
    colorWest.contentKey="actual-color-west";colorEast.contentKey="actual-color-east";
    grayWest.contentKey="actual-gray-west";grayEast.contentKey="actual-gray-east";
    auto color=demand({colorWest,colorEast});color.base={colorWest,colorEast};
    TerrainDisplayState state;state.beginDemand(color);
    upload(state,color.scope,colorWest);upload(state,color.scope,colorEast);present(state,1);
    auto gray=demand({grayWest,grayEast});gray.base={grayWest,grayEast};
    ++gray.scope.candidateSequence;++gray.scope.requestSequence;state.beginDemand(gray);
    upload(state,gray.scope,grayWest);
    require(!state.buildCandidate(),"new style still needs its entire submitted base reserve");
    require(contains(state.snapshot().protectedKeys,colorWest.key)&&contains(state.snapshot().protectedKeys,grayWest.key),
            "old displayed and new variant backing have independent simultaneous leases");
    require(!state.cpuReady(color.scope,colorEast),"old variant demand completion rejects");
    upload(state,gray.scope,grayEast);const auto candidate=state.buildCandidate();require(candidate.has_value(),"new variant complete candidate");
    require(state.submitCandidate(*candidate,2,candidate->draws),"new variant submitted");
    require(contains(state.snapshot().protectedKeys,colorEast.key),"old backing remains pinned until display receipt");
    require(state.acceptDisplayReceipt({candidate->scope,candidate->id,2,candidate->draws}),"new variant actually displayed");
    require(state.snapshot().releasedKeys==std::vector<QString>({colorEast.key,colorWest.key}),"only old variant backing leases retire");
    require(state.retireResource(gray.scope,colorWest.key,colorWest.contentKey),"old color backing can retire separately from gray backing");
    require(contains(state.snapshot().uploadSubmittedKeys,grayWest.key),"current gray texture remains resident");
}
void compatibilityPublicationRemainsSeparate() {
    TerrainDisplayState state;QVariantList complete{QStringLiteral("old")},partial{QStringLiteral("new")};
    require(state.publish(complete,true)==complete,"legacy controller compatibility");
    require(state.publish(partial,false)==QVariantList({QStringLiteral("new"),QStringLiteral("old")}),"legacy complete-view fallback preserved during integration");
    require(state.snapshot().displayed.empty(),"legacy CPU publication never creates a typed displayed receipt");
    state.reset();require(state.publish({},false).empty(),"reset clears compatibility cache");
}
void cancellationRetainsDisplayedFallbackAndRejectsPendingEvents() {
    TerrainDisplayState state;const auto initial=demand({oldWest,coarseEast});state.beginDemand(initial);
    for(const auto& resource:{west,east,oldWest})upload(state,initial.scope,resource);present(state,1);
    auto next=demand({nw,sw,ne,se});++next.scope.candidateSequence;++next.scope.requestSequence;state.beginDemand(next);
    upload(state,next.scope,nw);const auto candidate=state.buildCandidate();require(candidate.has_value(),"pending partial candidate");
    require(state.submitCandidate(*candidate,2,candidate->draws),"partial candidate submitted before cancel");
    const auto before=state.snapshot();
    require(state.cancel(next.scope),"current hidden-input scheduling can cancel");
    require(state.snapshot().cancelled&&state.snapshot().displayed==before.displayed,"cancel preserves actually displayed fallback");
    require(contains(state.snapshot().protectedKeys,west.key)&&contains(state.snapshot().protectedKeys,east.key)&&
            contains(state.snapshot().protectedKeys,oldWest.key),"cancel preserves base reserve and old displayed detail leases");
    require(state.snapshot().releasedKeys.empty(),"cancel does not authenticate displayed fallback retirement");
    require(!state.cpuReady(next.scope,sw)&&!state.uploadSubmitted(next.scope,nw),"cancel rejects pending asynchronous readiness and uploads");
    require(!state.buildCandidate(),"cancel prevents new render candidates");
    require(!state.submitCandidate(*candidate,3,candidate->draws),"cancel rejects candidate submission");
    require(!state.acceptDisplayReceipt({candidate->scope,candidate->id,2,candidate->draws}),"cancel rejects queued onscreen receipt");
    bool reusedScope=false;try{state.beginDemand(next);}catch(const std::invalid_argument&){reusedScope=true;}
    require(reusedScope&&state.snapshot().cancelled,"cancelled async scope cannot be reauthenticated by reopening the same identity");
    auto resumed=next;++resumed.scope.candidateSequence;++resumed.scope.requestSequence;state.beginDemand(resumed);
    require(!state.snapshot().cancelled&&state.snapshot().displayed==before.displayed,"new demand resumes while preserving fallback");
    require(!state.cpuReady(next.scope,sw),"old cancelled demand remains stale after resumption");
    upload(state,resumed.scope,sw);present(state,3);
    require(!contains(state.snapshot().protectedKeys,oldWest.key),"current displayed replacement can retire old fallback after resume");
    state.reset();require(state.snapshot().protectedKeys.empty()&&state.snapshot().displayed.empty()&&
        state.snapshot().uploadSubmittedKeys.empty(),"window/context reset fully clears owner state");
}
void submittedPrefetchSuppliesFallbackAfterPan() {
    const auto prefetched=tile("1/prefetched-east",1,{0,90,180,-90});
    const auto far=tile("4/unrelated-west",4,{-180,90,0,-90});
    TerrainDisplayState state;auto first=demand({nw,sw});first.domains={{-180,90,0,-90}};
    first.prefetch={prefetched,far};state.beginDemand(first);
    for(const auto& resource:{west,east,nw,sw,prefetched,far})upload(state,first.scope,resource);
    present(state,1);
    require(!contains(keys(state.snapshot().displayed),prefetched.key),"offscreen prefetch was never displayed");
    const auto missing=tile("3/current-east",3,{0,90,180,-90});
    auto moved=demand({missing});moved.domains={{0,90,180,-90}};
    ++moved.scope.viewGeneration;++moved.scope.requestSequence;++moved.scope.candidateSequence;state.beginDemand(moved);
    const auto candidate=state.buildCandidate();require(candidate.has_value(),"pan reuses complete submitted reserve");
    require(keys(candidate->draws)==std::vector<QString>({east.key,prefetched.key}),
            "submitted cached prefetch is fallback only over overlapping missing targets; unrelated tiles are excluded");
    require(state.submitCandidate(*candidate,2,candidate->draws),"prefetch fallback can submit");
    require(state.acceptDisplayReceipt({candidate->scope,candidate->id,2,candidate->draws}),"prefetch fallback can display");
}
void replacementContentsShareKeyWithoutBlockingHandoff() {
    auto original=oldWest,replacement=oldWest;original.contentKey="immutable-A";replacement.contentKey="immutable-B";
    auto first=demand({original});first.domains={{-180,90,0,-90}};
    TerrainDisplayState state;state.beginDemand(first);
    for(const auto& resource:{west,east,original})upload(state,first.scope,resource);present(state,1);
    auto next=demand({replacement});next.domains=first.domains;
    ++next.scope.maskGeneration;++next.scope.requestSequence;++next.scope.candidateSequence;state.beginDemand(next);
    require(state.cpuReady(next.scope,replacement),"new immutable backing is CPU-ready while old same-key backing is displayed");
    auto waiting=state.buildCandidate();require(waiting.has_value(),"old same-key backing remains fallback during upload");
    require(waiting->draws.back().resource.contentKey==original.contentKey,"candidate fallback contains actual old backing");
    require(state.uploadSubmitted(next.scope,replacement),"new immutable backing uploads before old backing retires");
    const auto candidate=state.buildCandidate();require(candidate.has_value(),"replacement candidate");
    require(candidate->draws.size()==1&&candidate->draws.front().resource==replacement,"replacement selects exact new backing");
    const TerrainDisplayResourceId oldId{original.key,original.contentKey},newId{replacement.key,replacement.contentKey};
    auto protectedIds=state.snapshot().protectedResources;
    require(std::find(protectedIds.begin(),protectedIds.end(),oldId)!=protectedIds.end()&&
            std::find(protectedIds.begin(),protectedIds.end(),newId)!=protectedIds.end(),"old/new backing leases coexist");
    require(state.submitCandidate(*candidate,2,candidate->draws),"new same-key backing submitted");
    require(!state.retireResource(next.scope,original.key,original.contentKey),"old displayed backing still cannot retire on submission");
    require(state.acceptDisplayReceipt({candidate->scope,candidate->id,2,candidate->draws}),"new same-key backing displayed");
    require(state.snapshot().releasedResources==std::vector<TerrainDisplayResourceId>({oldId}),"only old immutable backing is released");
    require(!contains(state.snapshot().releasedKeys,original.key),"canonical-key compatibility cannot retire the current same-key backing");
    require(state.retireResource(first.scope,original.key,original.contentKey),"unchanged owner can acknowledge old-view backing retirement");
    const auto residentIds=state.snapshot().uploadSubmittedResources;
    require(std::find(residentIds.begin(),residentIds.end(),newId)!=residentIds.end(),"replacement backing remains resident after old retirement");
}
void demandSequenceFencePreventsCancelledScopeReactivation() {
    TerrainDisplayState state;const auto first=demand({west,east});state.beginDemand(first);
    upload(state,first.scope,west);upload(state,first.scope,east);present(state,1);
    require(state.cancel(first.scope),"first scope cancelled");
    auto next=first;++next.scope.requestSequence;++next.scope.candidateSequence;state.beginDemand(next);present(state,2);
    bool stale=false;try{state.beginDemand(first);}catch(const std::invalid_argument&){stale=true;}
    require(stale&&!state.cpuReady(first.scope,west),"A then B cannot reactivate A after cancellation");
    const auto before=state.snapshot();state.beginDemand(next);
    require(state.snapshot().submitted->frameSequence==before.submitted->frameSequence&&
        state.snapshot().displayed==before.displayed,"identical current beginDemand is idempotent and preserves receipt history");
    auto candidateRegression=next;++candidateRegression.scope.requestSequence;--candidateRegression.scope.candidateSequence;
    bool badCandidate=false;try{state.beginDemand(candidateRegression);}catch(const std::invalid_argument&){badCandidate=true;}
    require(badCandidate,"new requests cannot regress candidate scope sequence");
    auto requestRegression=next;++requestRegression.scope.candidateSequence;
    bool badRequest=false;try{state.beginDemand(requestRegression);}catch(const std::invalid_argument&){badRequest=true;}
    require(badRequest,"changed demands require a strictly newer request sequence");
}
void displayedFrameSequenceIsMonotoneAcrossViews() {
    TerrainDisplayState state;const auto first=demand({west,east});state.beginDemand(first);
    upload(state,first.scope,west);upload(state,first.scope,east);present(state,100);
    auto moved=first;++moved.scope.viewGeneration;++moved.scope.requestSequence;state.beginDemand(moved);
    const auto candidate=state.buildCandidate();require(candidate.has_value(),"new view candidate");
    require(!state.submitCandidate(*candidate,99,candidate->draws)&&!state.submitCandidate(*candidate,100,candidate->draws),
            "same-owner view changes cannot regress or reuse an accepted frame number");
    require(state.submitCandidate(*candidate,101,candidate->draws),"strictly newer render frame accepted");
    require(state.acceptDisplayReceipt({candidate->scope,candidate->id,101,candidate->draws}),"new view receipt accepted");
    auto rebuilt=moved;++rebuilt.scope.contextEpoch;state.beginDemand(rebuilt);
    upload(state,rebuilt.scope,west);upload(state,rebuilt.scope,east);present(state,1);
}
void cancelledUnshownResourcesCanRetireWithoutFalseReuse() {
    const auto prefetched=tile("3/prefetch",3,{-180,90,0,-90});
    TerrainDisplayState state;const auto first=demand({west,east});state.beginDemand(first);
    upload(state,first.scope,west);upload(state,first.scope,east);present(state,1);
    auto next=demand({nw,sw,ne,se});next.prefetch={prefetched};
    ++next.scope.requestSequence;++next.scope.candidateSequence;state.beginDemand(next);
    upload(state,next.scope,prefetched);upload(state,next.scope,nw);
    require(state.cancel(next.scope),"scheduling cancelled before new targets display");
    require(!contains(state.snapshot().protectedKeys,nw.key),"cancel drops undisplayed target residency protection");
    require(state.snapshot().releasedResources==std::vector<TerrainDisplayResourceId>({{nw.key,nw.contentKey}}),
            "cancel reports exact unshown candidate lease release without touching displayed/base resources");
    require(state.retireResource(next.scope,prefetched.key,prefetched.contentKey)&&
            state.retireResource(next.scope,nw.key,nw.contentKey),"cancelled owner retirement acknowledgements update resident inventory");
    auto resumed=demand({prefetched});resumed.domains={prefetched.interior};
    resumed.scope=next.scope;++resumed.scope.requestSequence;++resumed.scope.candidateSequence;state.beginDemand(resumed);
    const auto candidate=state.buildCandidate();require(candidate.has_value(),"world reserve still supplies resumed coverage");
    require(!contains(keys(candidate->draws),prefetched.key)&&!contains(state.snapshot().uploadSubmittedKeys,prefetched.key),
            "a destroyed backing cannot become falsely resident after same-owner resume");
}
void cancelledSubmittedBackingWaitsForExactOwnerAbandonment() {
    TerrainDisplayState state;const auto first=demand({west,east});state.beginDemand(first);
    upload(state,first.scope,west);upload(state,first.scope,east);present(state,1);
    auto next=demand({nw,sw,ne,se});++next.scope.requestSequence;++next.scope.candidateSequence;state.beginDemand(next);
    upload(state,next.scope,nw);const auto candidate=state.buildCandidate();require(candidate.has_value(),"pending render inventory");
    require(state.submitCandidate(*candidate,2,candidate->draws),"inventory actually submitted");
    require(state.cancel(next.scope),"submitted demand cancelled");
    require(!state.retireResource(next.scope,nw.key,nw.contentKey),"cancel preserves recorded actual in-flight leases");
    require(!state.discardSubmission(next.scope,candidate->id,3),"wrong frame cannot discard actual submission");
    require(state.discardSubmission(next.scope,candidate->id,2),"exact owner retirement can discard cancelled submission");
    require(state.retireResource(next.scope,nw.key,nw.contentKey),"backing retires after submitted lease acknowledgement");
    require(!state.retireResource(next.scope,west.key,west.contentKey),"base/displayed lease remains protected");
}
}
int main() {
    try {
        void(*const cases[])()={coldDetailWaitsForSubmittedBase,partialReplacementRetiresOnlyAfterDisplay,
            reverseZoomDrawsCoarseTargetLast,allEightStaleScopesReject,receiptInventoryAndFrameMustBeExact,
            replicasShareOneProtectedResource,exactArrangementDetectsNarrowGaps,viewReuseAndOwnerResetAreDifferent,
            retirementNeedsOwnerAcknowledgement,supersededCandidateAndDuplicateReceiptReject,
            malformedWorldReserveRefuses,rasterVariantsKeepIndependentDisplayLeases,compatibilityPublicationRemainsSeparate,
            cancellationRetainsDisplayedFallbackAndRejectsPendingEvents,submittedPrefetchSuppliesFallbackAfterPan,
            replacementContentsShareKeyWithoutBlockingHandoff,demandSequenceFencePreventsCancelledScopeReactivation,
            displayedFrameSequenceIsMonotoneAcrossViews,cancelledUnshownResourcesCanRetireWithoutFalseReuse,
            cancelledSubmittedBackingWaitsForExactOwnerAbandonment};
        std::size_t passed=0;for(const auto run:cases){run();++passed;}
        std::cout<<"terrain display state: processed="<<std::size(cases)<<" passed="<<passed<<" failed=0 skip=0\n";return 0;
    }catch(const std::exception& error){std::cerr<<"terrain display state FAIL: "<<error.what()<<'\n';return 1;}
}

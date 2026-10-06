#include <pandoeditor/project.h>
#include <pandoeditor/geometryprovenance.h>
#include <pandoeditor/gisexchange.h>
#include <pandoeditor/presentationcommands.h>
#include <algorithm>
#include <functional>
#include <cstdlib>
#include <new>
#include <iostream>
#include <stdexcept>
// Exercise actual allocation failures at candidate/evidence boundaries, without
// exposing test hooks in production provenance classes.
static thread_local long allocationFailure=-1;
void* operator new(std::size_t size) {
    if(allocationFailure==0)throw std::bad_alloc();
    if(allocationFailure>0)--allocationFailure;
    if(auto memory=std::malloc(size?size:1))return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) {return ::operator new(size);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
using namespace pandoeditor;
#define CHECK(c) do { if(!(c)) throw std::runtime_error(std::string(__func__)+":"+std::to_string(__LINE__)+": " #c); } while(false)
namespace {
const GeometryRef derived{"web-label:owner",1};
const std::string digest(64,'a');
Geometry point(double x=1) { return {"Point",{{x,2}},{},{}}; }
Geometry polygon() { return {"Polygon",{},{},{{{{0,0},{4,0},{4,4},{0,4},{0,0}}}}}; }
void seal(ProjectDocument& d) {
    for(const auto& [path,value]:geometryProvenanceOpaqueSlots(d))d.geometryProvenance.opaqueBaseline[path]=digest;
    GeometryProvenanceCodecAccess::sealVerifiedOpaqueBaseline(d);
}
ProjectDocument fixture() {
    ProjectDocument d;d.documentId="ownership-core";
    d.geometries.insert(derived,point());
    PlaceLabel label;label.id="owner";label.name="Owner";label.geometry=derived;
    label.source.details="{\"array\":[null,9007199254740993,1e400]}";
    d.labels.push_back(label);
    d.geometryProvenance.inlineAllocations.emplace(derived,InlineGeometryAllocation{{"label","owner"},digest,false});
    seal(d);return d;
}
bool promoted(const ProjectDocument& d) {return d.geometryProvenance.inlineAllocations.at(derived).promoted;}
template<class F> void rejects(F action,const std::string& reason) {
    bool rejected=false;
    try {action();}catch(const std::invalid_argument& e) {rejected=true;if(std::string(e.what()).find(reason)==std::string::npos)throw std::runtime_error(std::string("wrong rejection: ")+e.what()+"; expected "+reason);}
    CHECK(rejected);
}
PrepareResult prepare(Project& p,ContentEdit edit) {
    CommandArguments args;args.action=std::move(edit);
    auto result=CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,"content.edit",args));
    if(!result.preview)throw std::runtime_error("preparation failed: "+result.detail);
    return result;
}
void apply(Project& p,ContentEdit edit) {auto result=prepare(p,std::move(edit));CHECK(CommandProcessor::confirm(p,*result.preview).changed());}
void promotionIsPreviewLocalStickyAndUndoable() {
    Project p;p.replace(fixture());const auto initial=p.document();const auto snap=p.snapshot();
    PlaceLabel adopter;adopter.id="adopter";adopter.geometry=derived;
    auto preview=prepare(p,{{"label","adopter"},adopter,{},true});
    CHECK(promoted(preview.preview->change().after()));CHECK(!promoted(p.document()));CHECK(!promoted(snap.document()));
    CommandProcessor::cancel(*preview.preview);CHECK(!p.dirty()&&!p.canUndo());
    apply(p,{{"label","adopter"},adopter,{},true});CHECK(promoted(p.document()));
    apply(p,{{"label","adopter"},{},{},false});CHECK(promoted(p.document()));
    CHECK(p.undo()&&promoted(p.document()));CHECK(p.undo()&&!promoted(p.document()));CHECK(semanticallyEqual(initial,p.document()));
    CHECK(p.redo()&&promoted(p.document()));CHECK(p.redo()&&promoted(p.document()));
    p.replace(initial);CHECK(!promoted(p.document())); // load is not an edit transition
}
void deleteRecreateAndRebindPromoteOnlyReadoption() {
    Project p;p.replace(fixture());const auto original=p.document().labels.front();
    apply(p,{{"label","owner"},{},{},false});CHECK(!promoted(p.document()));CHECK(p.document().geometries.get(derived));
    CHECK(p.undo()&&!promoted(p.document()));CHECK(p.redo()&&!promoted(p.document()));
    auto recreated=original;recreated.source.details="{}";
    apply(p,{{"label","owner"},recreated,{},true});CHECK(promoted(p.document()));
    p.replace(fixture());auto rebound=original;GeometryRef newRef{derived.id,2};rebound.geometry=newRef;
    apply(p,{{"label","owner"},rebound,std::make_pair(newRef,point(3)),false});
    CHECK(!promoted(p.document()));CHECK(p.document().geometryProvenance.inlineAllocations.size()==1);
    CHECK(geometryProvenanceUsers(p.document()).count(derived)==0);
    CHECK(p.undo()&&!promoted(p.document()));CHECK(p.redo()&&!promoted(p.document()));
    apply(p,{{"label","owner"},original,{},false});CHECK(promoted(p.document()));
}
void equalShapeDoesNotPromoteOrInfer() {
    auto before=fixture(),after=before;
    GeometryRef other{"web-label:second",1};after.geometries.insert(other,point());
    PlaceLabel label;label.id="second";label.geometry=other;after.labels.push_back(label);
    reconcileGeometryProvenance(before,after);CHECK(!promoted(after));CHECK(after.geometryProvenance.inlineAllocations.size()==1);
    CHECK(after.labels.front().geometry==derived);CHECK(after.labels.back().geometry==other);Project::validate(after);
    auto native=after;native.geometryProvenance={};Project::validate(native);CHECK(native.geometryProvenance.inlineAllocations.empty());
}
void rawReplaceRejectsTypedAdoptionAtomically() {
    Project p;p.replace(fixture());auto renamed=p.document().labels.front();renamed.name="Changed";
    apply(p,{{"label","owner"},renamed,{},false});CHECK(p.undo());const auto before=p.document();const auto rev=p.revision();
    auto invalid=before;PlaceLabel adopter;adopter.id="adopter";adopter.geometry=derived;invalid.labels.push_back(adopter);
    rejects([&]{p.replace(invalid);},"unpromoted geometry user");
    CHECK(semanticallyEqual(before,p.document()));CHECK(p.revision()==rev&&!p.dirty()&&p.canRedo());
}
void opaqueMutationIsUncertaintyNotPromotionAndUndoRestoresEvidence() {
    Project p;p.replace(fixture());const auto initial=p.document();auto changed=p.document().labels.front();
    changed.source.details="{\"new\":true}";auto preview=prepare(p,{{"label","owner"},changed,{},false});
    CHECK(preview.preview->change().after().geometryProvenance.opaqueUncertain);CHECK(!promoted(preview.preview->change().after()));
    CHECK(!p.document().geometryProvenance.opaqueUncertain);CHECK(CommandProcessor::confirm(p,*preview.preview).changed());
    auto forged=p.document();forged.geometryProvenance.opaqueUncertain=false;
    rejects([&]{Project::validate(forged);},"missing clean evidence");
    changed.source.details=initial.labels.front().source.details;apply(p,{{"label","owner"},changed,{},false});
    CHECK(p.document().geometryProvenance.opaqueUncertain);CHECK(p.undo()&&p.document().geometryProvenance.opaqueUncertain);
    CHECK(p.undo()&&!p.document().geometryProvenance.opaqueUncertain);Project::validate(p.document());
    CHECK(semanticallyEqual(initial,p.document()));CHECK(p.redo()&&p.document().geometryProvenance.opaqueUncertain);
}
void staleOpaqueAndMissingEvidenceRejected() {
    auto valid=fixture();Project p;p.replace(valid);const auto revision=p.revision();
    auto invalid=valid;invalid.labels.front().source.details="{\"array\":[1e400,null,9007199254740993]}";
    rejects([&]{p.replace(invalid);},"opaque slot changed");CHECK(semanticallyEqual(valid,p.document())&&p.revision()==revision);
    invalid=valid;invalid.geometryProvenance.opaqueBaseline.begin()->second=std::string(64,'b');
    rejects([&]{Project::validate(invalid);},"opaque baseline changed");
    invalid=valid;GeometryProvenance unsealed;unsealed.inlineAllocations=valid.geometryProvenance.inlineAllocations;
    unsealed.opaqueBaseline=valid.geometryProvenance.opaqueBaseline;invalid.geometryProvenance=unsealed;
    rejects([&]{Project::validate(invalid);},"missing clean evidence");
    invalid=valid;invalid.geometryProvenance.opaqueUncertain=true;
    GeometryProvenanceCodecAccess::sealVerifiedOpaqueBaseline(invalid);invalid.geometryProvenance.opaqueUncertain=false;
    rejects([&]{Project::validate(invalid);},"missing clean evidence");
}
void removedOpaqueAndExactCopiesAtNewOwners() {
    auto before=fixture(),removed=before;removed.labels.front().source.details="{}";
    reconcileGeometryProvenance(before,removed);CHECK(!removed.geometryProvenance.opaqueUncertain);Project::validate(removed);
    auto reopened=removed;GeometryProvenanceCodecAccess::sealVerifiedOpaqueBaseline(reopened);
    CHECK(semanticallyEqual(removed,reopened));
    auto restored=reopened;restored.labels.front().source.details=before.labels.front().source.details;
    reconcileGeometryProvenance(reopened,restored);CHECK(restored.geometryProvenance.opaqueUncertain);
    auto copied=before;PlaceLabel second=before.labels.front();second.id="copy";second.geometry={"native",1};
    copied.geometries.insert(second.geometry,point(5));copied.labels.push_back(second);
    reconcileGeometryProvenance(before,copied);CHECK(copied.geometryProvenance.opaqueUncertain);CHECK(!promoted(copied));
}
void opaqueInventoryIsOwnerKeyedAndLossless() {
    auto d=fixture();d.labels.front().id="a~/b";
    auto other=d.labels.front();other.id="second";other.source.details="{\"second\":true}";d.labels.push_back(other);
    d.exchangeMetadata="{\"header\":null}";TerritorialUnit u;u.id="unit";u.metadata="{\"x\":[2,1]}";d.units.push_back(u);
    HydroFeature h;h.id="hydro";h.source.details="{\"h\":1}";d.hydro.push_back(h);
    GenericFeature g;g.id="generic";g.source.details="{\"g\":1}";d.genericFeatures.push_back(g);
    DistributionLayer l;l.id="layer";l.metadata="{\"l\":1}";d.distributionLayers.push_back(l);
    DistributionEntry e;e.id="entry";e.metadata="{\"e\":1}";d.distributionEntries.push_back(e);
    PreservedExtension x;x.id="extension";x.payload="[null,9007199254740993]";x.envelopeExtras="{\"extra\":1e400}";d.extensions.push_back(x);
    const auto slots=geometryProvenanceOpaqueSlots(d);CHECK(slots.size()==10);
    CHECK(slots.at("/labels/a~0~1b/source/details")==d.labels.front().source.details);
    CHECK(slots.at("/extensions/extension/payload")==x.payload);CHECK(slots.at("/extensions/extension/envelopeExtras")==x.envelopeExtras);
    auto moved=d;std::reverse(moved.labels.begin(),moved.labels.end());CHECK(geometryProvenanceOpaqueSlots(moved)==slots);
    auto empty=fixture();empty.labels.front().source.details="null";CHECK(geometryProvenanceOpaqueSlots(empty).empty());
}
void reorderAndEmptyCreationKeepOpaqueKnowledge() {
    auto before=fixture();GeometryRef other{"native",1};before.geometries.insert(other,point(6));
    PlaceLabel label;label.id="second";label.geometry=other;label.source.details="{\"source\":true}";before.labels.push_back(label);seal(before);
    auto after=before;std::reverse(after.labels.begin(),after.labels.end());
    reconcileGeometryProvenance(before,after);CHECK(!after.geometryProvenance.opaqueUncertain&&!promoted(after));Project::validate(after);
    PlaceLabel empty;empty.id="empty";empty.geometry=other;after.labels.push_back(empty);
    reconcileGeometryProvenance(before,after);CHECK(!after.geometryProvenance.opaqueUncertain&&!promoted(after));Project::validate(after);
}
void everyTypedContentAdopterPromotes() {
    ProjectDocument before;before.documentId="all-users";const GeometryRef ref{"web-genericFeatures:creator",1};before.geometries.insert(ref,polygon());
    GenericFeature creator;creator.id="creator";creator.geometry=ref;before.genericFeatures.push_back(creator);
    before.geometryProvenance.inlineAllocations.emplace(ref,InlineGeometryAllocation{{"generic","creator"},digest,false});seal(before);
    for(int kind=0;kind<3;++kind) {
        auto after=before;
        if(kind==0){HydroFeature h;h.id="lake";h.kind="lake";h.geometry=ref;after.hydro.push_back(h);}
        if(kind==1){GenericFeature g;g.id="generic";g.geometry=ref;after.genericFeatures.push_back(g);}
        if(kind==2){DistributionLayer l;l.id="layer";after.distributionLayers.push_back(l);DistributionEntry e;e.id="entry";e.layerId=l.id;e.geometry=ref;after.distributionEntries.push_back(e);}
        rejects([&]{Project::validate(after);},"unpromoted geometry user");reconcileGeometryProvenance(before,after);
        CHECK(after.geometryProvenance.inlineAllocations.at(ref).promoted);Project::validate(after);
    }
}
void historicalBindingsPromoteWithoutRelaxingActivation() {
    ProjectDocument before;before.documentId="history";GeometryRef ref{"web-genericFeatures:g",1},native{"native",1};
    before.geometries.insert(ref,polygon());before.geometries.insert(native,polygon());
    GenericFeature g;g.id="g";g.geometry=ref;before.genericFeatures.push_back(g);
    before.geometryProvenance.inlineAllocations.emplace(ref,InlineGeometryAllocation{{"generic","g"},digest,false});
    before.units.push_back({"u","Unit","",UnitKind::General});before.presentation.objectStyles[territorialRef("u")]={};
    addStaticTerritorialRecords(before,"u",native);seal(before);Project::validate(before);
    auto after=before;after.timelineRecords.geometryBindings={{"past","u",{{},{"1999-12"}},ref},{"now","u",{{"2000-01"},{}},native}};
    rejects([&]{Project::validate(after);},"unpromoted geometry user");
    reconcileGeometryProvenance(before,after);CHECK(after.geometryProvenance.inlineAllocations.at(ref).promoted);Project::validate(after);
    Project p;p.replace(after);CommandArguments args;args.action=AddLayer{"extra","Extra"};
    CHECK(!CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,"layer.add",args)).ok());
}
void planningReconcilesNewOpaqueSlots() {
    Project p;p.replace(fixture());
    auto plan=planGenericGisImport(p.snapshot(),"gis",{"test.geojson","geojson"},{{"new","New",point(4),"{\"external\":true}"}});
    CommandArguments args;args.action=plan;auto result=CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,"gis.import.generic",args));
    // The plan itself must succeed before common preparation is reached.
    if(!result.preview)throw std::runtime_error("GIS preparation failed: "+result.detail);
    CHECK(result.preview->change().after().geometryProvenance.opaqueUncertain);CHECK(!promoted(p.document()));
    CHECK(CommandProcessor::confirm(p,*result.preview).changed());CHECK(p.document().geometryProvenance.opaqueUncertain);
}
void distributionCascadeKeepsCleanDetachedRows() {
    auto d=fixture();GeometryRef allocation{"web-distributionEntry:entry",1};d.geometries.insert(allocation,polygon());
    DistributionLayer layer;layer.id="layer";layer.name="Layer";layer.metadata="{\"scaleSource\":true}";d.distributionLayers.push_back(layer);
    DistributionEntry entry;entry.id="entry";entry.layerId=layer.id;entry.geometry=allocation;entry.metadata="{\"entrySource\":true}";d.distributionEntries.push_back(entry);
    d.geometryProvenance.inlineAllocations.emplace(allocation,InlineGeometryAllocation{{"distributionEntry","entry"},digest,false});seal(d);
    Project p;p.replace(d);apply(p,{{"distributionLayer","layer"},{},{},false});
    CHECK(p.document().distributionEntries.empty()&&p.document().distributionLayers.empty());
    CHECK(!p.document().geometryProvenance.opaqueUncertain);CHECK(p.document().geometries.get(allocation));
    CHECK(geometryProvenanceUsers(p.document()).count(allocation)==0);
    CHECK(p.undo()&&semanticallyEqual(p.document(),d));CHECK(p.redo()&&!p.document().geometryProvenance.opaqueUncertain);
}
void presentationRebaseAndStalePreviewDoNotMutateOwnership() {
    Project p;p.replace(fixture());const auto before=p.document();PlaceLabel adopter;adopter.id="adopter";adopter.geometry=derived;
    auto preview=prepare(p,{{"label","adopter"},adopter,{},true});
    CHECK(PresentationCommandProcessor::apply(p,SetBatchVisibility{{{"label","owner"}},false})==PresentationResult::Applied);
    CHECK(!promoted(p.document())&&!p.document().geometryProvenance.opaqueUncertain);
    CHECK(CommandProcessor::confirm(p,*preview.preview).changed());CHECK(promoted(p.document()));
    CHECK(p.undo()&&!promoted(p.document()));CHECK(p.redo()&&promoted(p.document()));
    p.replace(before);preview=prepare(p,{{"label","adopter"},adopter,{},true});
    auto renamed=before.labels.front();renamed.name="Rename";apply(p,{{"label","owner"},renamed,{},false});
    CHECK(!CommandProcessor::confirm(p,*preview.preview).ok());CHECK(!promoted(p.document()));
    auto invalid=renamed;invalid.kind="invalid-kind";CommandArguments args;args.action=ContentEdit{{"label","owner"},invalid,{},false};
    CHECK(!CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,"content.edit",args)).ok());CHECK(!promoted(p.document()));
}
void splitUsesEmptySiblingOpaqueDefaults() {
    auto d=fixture();d.geometries.insert({"country",1},polygon());d.units.push_back({"country","Country","",UnitKind::General});
    d.units.back().metadata="{\"ownerRelative\":9007199254740993}";d.units.back().sourceFolderId="folder";
    d.units.back().sourceLibraryId="library";d.units.back().sourceGeometryVersion="version";
    d.presentation.objectStyles[territorialRef("country")]={};addStaticTerritorialRecords(d,"country",{"country",1});seal(d);
    Project p;p.replace(d);const Geometry left{"Polygon",{},{},{{{{0,0},{2,0},{2,4},{0,4},{0,0}}}}};
    const Geometry right{"Polygon",{},{},{{{{2,0},{4,0},{4,4},{2,4},{2,0}}}}};
    auto planned=CommandProcessor::planTerritorial(p,SplitTerritorialIntent{territorialRef("country"),right,"sibling","Sibling"});
    CHECK(planned.plan);CommandArguments args;args.action=ApplyTerritorialMutation{*planned.plan,GeometryPatch{p.revision(),{{territorialRef("country"),left}},{{territorialRef("sibling"),right}},{}}};
    auto preview=CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,"territorial.geometry.commit",args));
    if(!preview.preview)throw std::runtime_error("split failed: "+preview.detail);
    CHECK(CommandProcessor::confirm(p,*preview.preview).changed());const auto& sibling=p.document().units.back();
    CHECK(sibling.id=="sibling"&&sibling.metadata=="{}"&&sibling.sourceFolderId.empty()&&sibling.sourceLibraryId.empty()&&sibling.sourceGeometryVersion.empty());
    CHECK(p.document().units.front().metadata==d.units.front().metadata);CHECK(!p.document().geometryProvenance.opaqueUncertain);
    CHECK(!promoted(p.document()));CHECK(p.document().geometryProvenance.inlineAllocations.size()==1);
}
void distributionPlanningAllowsOpaqueAdditions() {
    Project p;p.replace(fixture());DistributionLayer layer;layer.id="new-layer";layer.metadata="{\"source\":true}";
    DistributionEntry entry;entry.id="new-entry";entry.layerId=layer.id;entry.metadata="{\"source\":null}";
    auto plan=planDistributionGisImport(p.snapshot(),"distribution",{"test.geojson","geojson"},{layer},{{entry,polygon()}});
    CommandArguments args;args.action=plan;auto prepared=CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,"gis.import.distribution",args));
    if(!prepared.preview)throw std::runtime_error("distribution planning failed: "+prepared.detail);
    CHECK(prepared.preview->change().after().geometryProvenance.opaqueUncertain);CHECK(CommandProcessor::confirm(p,*prepared.preview).changed());
    CHECK(!promoted(p.document()));CHECK(p.undo()&&!p.document().geometryProvenance.opaqueUncertain);
}
void ownershipPreparationAllocationFailuresAreAtomic() {
    for(bool opaque:{false,true}) {
        bool reachedSuccess=false;int failures=0;
        for(long position=0;position<2000;++position) {
            Project p;p.replace(fixture());auto value=p.document().labels.front();
            if(opaque)value.source.details="{\"changed\":true}";else {value.id="adopter";value.source.details="{}";}
            CommandArguments args;args.action=ContentEdit{{"label",value.id},value,{},!opaque};
            const auto request=CommandProcessor::makeRequest(p,"content.edit",args);const auto before=p.snapshot();
            allocationFailure=position;auto result=CommandProcessor::prepare(p,request);allocationFailure=-1;
            CHECK(&p.document()==&before.document()&&!p.dirty()&&!p.canUndo());
            CHECK(!promoted(p.document())&&!p.document().geometryProvenance.opaqueUncertain);Project::validate(p.document());
            if(result.preview) {
                CHECK(CommandProcessor::confirm(p,*result.preview).changed());
                CHECK(opaque?p.document().geometryProvenance.opaqueUncertain:promoted(p.document()));
                CHECK(p.undo()&&semanticallyEqual(p.document(),before.document()));reachedSuccess=true;break;
            }
            CHECK(result.error==CommandError::PrepareFailed);++failures;
        }
        CHECK(reachedSuccess&&failures>0);
    }
}
void serializedLedgerParticipatesInEquality() {
    auto a=fixture(),b=a;b.geometryProvenance.inlineAllocations.at(derived).promoted=true;CHECK(!semanticallyEqual(a,b));
    b=a;b.geometryProvenance.opaqueUncertain=true;CHECK(!semanticallyEqual(a,b));
    b=a;b.geometryProvenance.opaqueBaseline.clear();CHECK(!semanticallyEqual(a,b));
    b=a;GeometryProvenanceCodecAccess::sealVerifiedOpaqueBaseline(b);CHECK(semanticallyEqual(a,b));
}
void malformedOpaquePathsRejected() {
    for(const auto* path:{"/unknown/owner/metadata","/labels/a~2b/source/details","/labels//source/details","/labels/a/b/source/details","/exchangeMetadata/extra","/units/u/details"}) {
        auto d=fixture();d.geometryProvenance.opaqueBaseline[path]=digest;
        GeometryProvenanceCodecAccess::sealVerifiedOpaqueBaseline(d);
        rejects([&]{Project::validate(d);},"invalid opaque path");
    }
}
void incompatibleAllocationShapeRejected() {
    auto d=fixture();d.labels.clear();GeometryStore replacement;replacement.insert(derived,polygon());d.geometries=std::move(replacement);
    rejects([&]{Project::validate(d);},"invalid allocation geometry");
}
void malformedLedgerRejectedWithoutPrefixInference() {
    auto d=fixture();d.geometryProvenance.originalArchive.insert(derived);
    rejects([&]{Project::validate(d);},"overlapping original allocation");
    d=fixture();d.geometryProvenance.inlineAllocations.at(derived).createdFor.domain="territorial";
    rejects([&]{Project::validate(d);},"invalid allocation creator");
    d=fixture();d.geometryProvenance.inlineAllocations.at(derived).geometrySha256="bad";
    rejects([&]{Project::validate(d);},"invalid geometry digest");
    d=fixture();d.geometryProvenance.originalArchive.insert({"missing",1});
    rejects([&]{Project::validate(d);},"missing original geometry");
}
}
int main() {
    const std::pair<const char*,void(*)()> cases[]={
        {"promotion preview sticky UndoRedo",promotionIsPreviewLocalStickyAndUndoable},
        {"delete recreate and rebind",deleteRecreateAndRebindPromoteOnlyReadoption},
        {"equal shape no inferred provenance",equalShapeDoesNotPromoteOrInfer},
        {"raw replace adoption atomicity",rawReplaceRejectsTypedAdoptionAtomically},
        {"opaque mutation uncertainty and Undo",opaqueMutationIsUncertaintyNotPromotionAndUndoRestoresEvidence},
        {"stale opaque missing evidence",staleOpaqueAndMissingEvidenceRejected},
        {"removed opaque owner scoped evidence",removedOpaqueAndExactCopiesAtNewOwners},
        {"opaque inventory lossless owner keys",opaqueInventoryIsOwnerKeyedAndLossless},
        {"historical adoption static gate",historicalBindingsPromoteWithoutRelaxingActivation},
        {"reorder and empty creation",reorderAndEmptyCreationKeepOpaqueKnowledge},
        {"all typed content adopters",everyTypedContentAdopterPromotes},
        {"GIS intermediate reconciliation",planningReconcilesNewOpaqueSlots},
        {"serialized ledger equality",serializedLedgerParticipatesInEquality},
        {"ownership allocation failure atomicity",ownershipPreparationAllocationFailuresAreAtomic},
        {"distribution cascade clean",distributionCascadeKeepsCleanDetachedRows},
        {"presentation rebase stale preview",presentationRebaseAndStalePreviewDoNotMutateOwnership},
        {"split empty opaque defaults",splitUsesEmptySiblingOpaqueDefaults},
        {"distribution planning opaque",distributionPlanningAllowsOpaqueAdditions},
        {"malformed ledger validation",malformedLedgerRejectedWithoutPrefixInference},
        {"opaque path validation",malformedOpaquePathsRejected},
        {"allocation shape validation",incompatibleAllocationShapeRejected}
    };
    int failures=0;for(const auto& [name,test]:cases)try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
    return failures?1:0;
}

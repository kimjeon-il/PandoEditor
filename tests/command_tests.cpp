#include <pandoeditor/project.h>
#include <pandoeditor/commands.h>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

using namespace pandoeditor;
#define CHECK(condition) do { if(!(condition)) throw std::runtime_error(std::string(__func__)+":"+std::to_string(__LINE__)+": " #condition); } while(false)
static_assert(!std::is_copy_constructible<CommandPreview>::value,"preview must be single owner");
static_assert(std::is_nothrow_move_constructible<CommandPreview>::value,"preview move must not allocate");
ProjectDocument fixture() {
    ProjectDocument d({{"A","Alpha",{{{{0,0},{10,0},{10,10},{0,10},{0,0}}}},0x123456,"original"},
                       {"B","Beta",{{{{20,0},{25,0},{25,5},{20,5},{20,0}}}},0xabcdef}},
                      {{"countries","Countries"},{"other","Other"},{"empty","Empty"}});
    d.documentId="command-test-document";
    return d;
}
CountryProperties properties(const Project& p,const std::string& id="A") {
    const auto& c=*p.country(id); return {c.name,c.memo,c.color,c.opacity,c.layerId};
}
CommandArguments renamed(const Project& p,const std::string& name="Changed") {
    auto next=properties(p); next.name=name;
    CommandArguments args; args.properties.countries.push_back({"A",next}); return args;
}
CommandRequest request(const Project& p,CommandArguments args,std::string id="edit.properties") {
    return CommandProcessor::makeRequest(p,std::move(id),std::move(args));
}
CommandPreview preview(const Project& p,const CommandRequest& r) {
    auto prepared=CommandProcessor::prepare(p,r);
    CHECK(prepared.status==CommandStatus::Prepared);
    CHECK(prepared.preview && prepared.preview->pending());
    return std::move(*prepared.preview);
}
void preparedIsPureAndCompoundIsOneUndo() {
    Project p; p.replace(fixture()); const auto before=p.document();
    auto geometry=p.document().geometries.get(p.document().units[0].geometry);
    auto args=renamed(p,"  New name  ");
    auto& props=args.properties.countries[0].properties; props.memo="new memo"; props.color=0xff0000; props.opacity=0.4;
    auto layer=*p.layer("countries"); layer.name="Renamed layer"; layer.opacity=0.7; args.properties.layers.push_back(layer);
    const auto r=request(p,args); auto v=preview(p,r);
    CHECK(p.revision()==0 && !p.dirty() && !p.canUndo() && !p.canRedo());
    CHECK(semanticallyEqual(p.document(),before));
    CHECK(v.change().request().revision==0);
    CHECK(v.change().before().units[0].name=="Alpha");
    CHECK(v.change().after().units[0].name=="New name");
    CHECK(v.change().after().geometries.get(before.units[0].geometry)==geometry);
    CHECK(CommandProcessor::confirm(p,v).changed());
    CHECK(!v.pending() && p.revision()==1 && p.dirty());
    CHECK(p.country("A")->name=="New name" && p.country("A")->memo=="new memo");
    CHECK(p.country("A")->color==0xff0000 && p.country("A")->opacity==0.4);
    CHECK(p.layer("countries")->name=="Renamed layer" && p.layer("countries")->opacity==0.7);
    CHECK(&p.country("A")->name==&p.document().units[0].name);
    CHECK(p.document().geometries.get(before.units[0].geometry)==geometry);
    CHECK(p.undo() && p.revision()==2 && !p.canUndo() && !p.dirty());
    CHECK(semanticallyEqual(p.document(),before));
    CHECK(&p.country("A")->name==&p.document().units[0].name);
    CHECK(p.redo() && p.revision()==3 && p.country("A")->memo=="new memo");
    CHECK(&p.country("A")->name==&p.document().units[0].name);
}
void rejectIdentifiersTargetsAndArguments() {
    Project p; p.replace(fixture()); const auto valid=request(p,renamed(p));
    auto reject=[&](CommandRequest r,CommandError error) {
        const auto out=CommandProcessor::prepare(p,r); CHECK(out.error==error && !out.ok() && !out.preview);
        CHECK(!p.dirty() && p.revision()==0 && !p.canUndo());
    };
    auto r=valid; r.projectInstanceId="wrong"; reject(r,CommandError::ProjectMismatch);
    r=valid; r.documentId="wrong"; reject(r,CommandError::DocumentMismatch);
    r=valid; ++r.revision; reject(r,CommandError::StaleRevision);
    r=valid; r.commandId="not-a-command"; reject(r,CommandError::InvalidCommand);
    r=valid; r.targets.clear(); reject(r,CommandError::InvalidTargets);
    r=valid; r.targets.push_back(r.targets.front()); reject(r,CommandError::InvalidTargets);
    r=valid; r.args.action=RemoveLayer{"empty"}; reject(r,CommandError::InvalidArguments);
    auto args=renamed(p); args.properties.countries[0].properties.opacity=std::numeric_limits<double>::quiet_NaN();
    reject(request(p,args),CommandError::InvalidArguments);
    args=renamed(p," \t "); reject(request(p,args),CommandError::InvalidArguments);
    args=renamed(p); args.properties.countries[0].properties.color=0x1000000; reject(request(p,args),CommandError::InvalidArguments);
    args=renamed(p); args.properties.countries[0].id="missing"; reject(request(p,args),CommandError::InvalidTargets);
    args=renamed(p); args.properties.countries.push_back(args.properties.countries[0]); reject(request(p,args),CommandError::InvalidArguments);
}
void failedValidationKeepsDocumentAndRedo() {
    Project p; p.replace(fixture()); CHECK(p.setMemo("A","redo entry") && p.undo());
    const auto before=p.document(); const auto rev=p.revision();
    auto args=renamed(p); args.action=RemoveLayer{"countries"};
    auto out=CommandProcessor::prepare(p,request(p,args,"layer.remove"));
    CHECK(out.error==CommandError::ValidationFailed && !out.preview);
    CHECK(semanticallyEqual(p.document(),before) && p.revision()==rev && !p.dirty() && p.canRedo());
    CHECK(p.redo() && p.country("A")->memo=="redo entry");
}
void noOpCancelAndFailuresPreserveRedo() {
    Project p; p.replace(fixture()); CHECK(p.setMemo("A","redo") && p.undo()); const auto rev=p.revision();
    auto out=CommandProcessor::prepare(p,request(p,renamed(p,"  Alpha  ")));
    CHECK(out.status==CommandStatus::NoOp && !out.preview);
    CHECK(p.canRedo() && p.revision()==rev && !p.dirty());
    auto v=preview(p,request(p,renamed(p))); CommandProcessor::cancel(v);
    CHECK(!v.pending() && p.canRedo() && p.revision()==rev);
    CHECK(CommandProcessor::confirm(p,v).error==CommandError::PreviewConsumed);
    auto bad=request(p,renamed(p)); bad.projectInstanceId="other";
    CHECK(!CommandProcessor::prepare(p,bad).ok() && p.canRedo());
    auto change=preview(p,request(p,renamed(p))); CHECK(CommandProcessor::confirm(p,change).changed());
    CHECK(!p.canRedo() && p.revision()==rev+1);
    CHECK(CommandProcessor::confirm(p,change).error==CommandError::PreviewConsumed);
}
void stalePreviewIsConsumedAcrossUndoRedoAndReopen() {
    Project p; p.replace(fixture()); auto r=request(p,renamed(p)); auto v=preview(p,r);
    CHECK(p.setMemo("A","different") && p.undo());
    CHECK(p.country("A")->name=="Alpha" && p.country("A")->memo=="original");
    CHECK(CommandProcessor::confirm(p,v).error==CommandError::StaleRevision && !v.pending());
    CHECK(p.canRedo() && !p.dirty());
    auto afterUndo=preview(p,request(p,renamed(p))); CHECK(p.redo());
    CHECK(CommandProcessor::confirm(p,afterUndo).error==CommandError::StaleRevision);
    auto oldInstance=p.instanceId(); auto reopen=preview(p,request(p,renamed(p))); auto d=p.document(); p.replace(d);
    CHECK(p.instanceId()!=oldInstance && p.document().documentId==d.documentId && p.revision()==0);
    CHECK(CommandProcessor::confirm(p,reopen).error==CommandError::ProjectMismatch && !reopen.pending());
    CHECK(!p.canUndo() && !p.dirty());
    Project other; other.replace(d); auto foreign=preview(p,request(p,renamed(p)));
    CHECK(CommandProcessor::confirm(other,foreign).error==CommandError::ProjectMismatch);
}
void lockAndRetainedDataProtection() {
    Project p; auto d=fixture(); d.presentation.userLayers[0].locked=true; p.replace(d);
    CHECK(CommandProcessor::prepare(p,request(p,renamed(p))).error==CommandError::Locked);
    d=fixture(); d.presentation.userLayers[1].locked=true; p.replace(d);
    auto args=renamed(p); args.action=MoveCountry{"A","other"};
    CHECK(CommandProcessor::prepare(p,request(p,args,"country.move")).error==CommandError::Locked);
    CHECK(p.country("A")->name=="Alpha" && !p.canUndo());
    d=fixture(); PreservedExtension e; e.id="opaque"; e.payload="{\"keep\":[null,1,\"1\"]}";
    d.extensions.push_back(e); p.replace(d);
    args=renamed(p); args.properties.countries[0].properties.color=0xff0000;
    CHECK(CommandProcessor::prepare(p,request(p,args)).error==CommandError::UnsupportedDependency);
    CHECK(!p.canUndo() && p.country("A")->name=="Alpha");
    auto v=preview(p,request(p,renamed(p))); CHECK(CommandProcessor::confirm(p,v).changed());
    CHECK(p.document().extensions[0].payload==e.payload);
    CHECK(p.undo() && p.document().extensions[0].payload==e.payload);
    d=fixture(); e.dependencyKnowledge="known"; e.dependencies={{"userLayer","other"}}; e.forbiddenEffects={"order"};
    d.extensions.push_back(e); p.replace(d); args={}; args.action=MoveLayer{"countries",1};
    CHECK(CommandProcessor::prepare(p,request(p,args,"layer.move")).error==CommandError::UnsupportedDependency);
}
void savedBaselineIsIndependentOfRevision() {
    Project p; p.replace(fixture()); CHECK(p.renameCountry("A","saved")); p.markSaved(); const auto savedRev=p.revision();
    auto v=preview(p,request(p,renamed(p,"changed"))); p.markSaved();
    CHECK(p.revision()==savedRev && CommandProcessor::confirm(p,v).changed());
    CHECK(p.dirty() && p.undo() && !p.dirty() && p.revision()==savedRev+2);
    CHECK(p.redo() && p.dirty());
    CHECK(p.renameCountry("A","saved") && !p.dirty());
}
void compoundImmediateActionsAreAtomic() {
    Project p; p.replace(fixture()); auto args=renamed(p); args.action=MoveCountry{"A","other"};
    auto v=preview(p,request(p,args,"country.move")); CHECK(CommandProcessor::confirm(p,v).changed());
    CHECK(p.country("A")->name=="Changed" && p.country("A")->layerId=="other" && p.revision()==1);
    CHECK(p.undo() && !p.canUndo() && p.country("A")->name=="Alpha" && p.country("A")->layerId=="countries");
    args=renamed(p); args.action=MoveCountry{"A","missing"}; const auto rev=p.revision();
    CHECK(CommandProcessor::prepare(p,request(p,args,"country.move")).error==CommandError::InvalidTargets);
    CHECK(p.revision()==rev && p.canRedo() && p.country("A")->name=="Alpha");
    args=renamed(p); args.action=MoveLayer{"countries",-1};
    v=preview(p,request(p,args,"layer.move")); CHECK(CommandProcessor::confirm(p,v).changed());
    CHECK(p.country("A")->name=="Changed" && p.layers()[0].id=="countries");
}
void layerActionsAndCompatibilityUseSameHistory() {
    Project p; p.replace(fixture()); const auto before=p.document();
    CHECK(p.addLayer("top"," Top ") && p.revision()==1 && p.layer("top")->name=="Top");
    CHECK(p.removeLayer("top") && p.revision()==2 && semanticallyEqual(p.document(),before));
    CHECK(!p.dirty() && p.undo() && p.layer("top"));
    CHECK(p.undo() && !p.canUndo() && !p.layer("top"));
    CHECK(!p.addLayer("other","duplicate") && p.canRedo());
    CHECK(!p.moveLayer("missing",1) && p.canRedo());
    CHECK(!p.moveLayer("countries",-1) && p.canRedo());
    CHECK(p.setLayerLocked("other",true)); CHECK(p.setLayerLocked("other",false));
    CHECK(p.setLayerVisible("other",false)); CHECK(p.setLayerOpacity("other",0.3));
    CHECK(p.layer("other")->opacity==0.3 && !p.layer("other")->visible);
}
void semanticEqualityIncludesAllPreservedFields() {
    auto a=fixture(), b=a; CHECK(semanticallyEqual(a,b));
    b.documentId="different"; CHECK(!semanticallyEqual(a,b)); b=a;
    b.units[0].locked=true; CHECK(!semanticallyEqual(a,b)); b=a;
    b.units[0].validity.from="1900"; CHECK(!semanticallyEqual(a,b)); b=a;
    b.units[0].coverageMode="partition"; CHECK(!semanticallyEqual(a,b)); b=a;
    b.presentation.userLayers[0].visible=false; CHECK(!semanticallyEqual(a,b)); b=a;
    b.presentation.membership[territorialRef("A")]="other"; CHECK(!semanticallyEqual(a,b)); b=a;
    PreservedExtension e; e.id="retained"; e.payload="null"; a.extensions.push_back(e); b=a;
    CHECK(semanticallyEqual(a,b)); b.extensions[0].envelopeExtras="{\"future\":true}"; CHECK(!semanticallyEqual(a,b));
    b=a; b.extensions[0].payload="\"null\""; CHECK(!semanticallyEqual(a,b));
    // Same geometry data in a different immutable allocation is semantically equal.
    b=a; GeometryStore copied;
    for(const auto& pair:a.geometries.versions()) copied.insert(pair.first,*pair.second);
    b.geometries=std::move(copied); CHECK(semanticallyEqual(a,b));
}
int main() {
    const std::pair<const char*,std::function<void()>> tests[]={
        {"prepare pure / compound Undo",preparedIsPureAndCompoundIsOneUndo},
        {"identifiers / targets / arguments",rejectIdentifiersTargetsAndArguments},
        {"failed validation / Redo",failedValidationKeepsDocumentAndRedo},
        {"NoOp / cancel / failures",noOpCancelAndFailuresPreserveRedo},
        {"stale Undo / Redo / reopen",stalePreviewIsConsumedAcrossUndoRedoAndReopen},
        {"lock / retained protection",lockAndRetainedDataProtection},
        {"saved baseline / revision",savedBaselineIsIndependentOfRevision},
        {"compound immediate actions",compoundImmediateActionsAreAtomic},
        {"layer compatibility",layerActionsAndCompatibilityUseSameHistory},
        {"semantic equality",semanticEqualityIncludesAllPreservedFields}};
    int failed=0;
    for(const auto& test:tests) { try { test.second(); std::cout<<"PASS "<<test.first<<'\n'; }
        catch(const std::exception& e) { ++failed; std::cerr<<"FAIL "<<test.first<<": "<<e.what()<<'\n'; } }
    return failed?1:0;
}

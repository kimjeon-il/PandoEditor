#include <pandoeditor/project.h>
#include <pandoeditor/commands.h>
#include <pandoeditor/presentationcommands.h>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

// Deterministic failure injection at each actual allocation site, not mocked
// validators or public production backdoors. Single-threaded test executable.
static thread_local long failAfter=-1;
void* operator new(std::size_t size) {
    if(failAfter==0) throw std::bad_alloc();
    if(failAfter>0) --failAfter;
    if(auto p=std::malloc(size?size:1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }
using namespace pandoeditor;
void checkAt(bool value, const char* expression, int line) {
    if(!value) throw std::runtime_error(std::string("allocation atomicity check failed at line ")
                                       +std::to_string(line)+": "+expression);
}
#define check(value) checkAt((value), #value, __LINE__)
ProjectDocument fixture() {
    ProjectDocument d({{"A","Alpha",{{{{0,0},{10,0},{10,10},{0,10},{0,0}}}},0x123456,"original"}},{{"countries","Countries"}});
    d.documentId="allocation-test-document"; return d;
}
CommandRequest change(const Project& p) {
    const auto& c=*p.country("A"); CommandArguments args;
    args.properties.countries.push_back({"A",{std::string(80,'n'),std::string(80,'m'),0x102030,0.4,c.layerId}});
    return CommandProcessor::makeRequest(p,"edit.properties",std::move(args));
}
ProjectDocument deleteFixture() {
    auto d=fixture();
    for(const auto id:{"S","R"}) { Geometry g;g.type="Polygon";g.polygons.push_back(Polygon{Ring{{1,1},{2,1},{2,2},{1,2},{1,1}}});GeometryRef ref{std::string("geometry-")+id,1};d.geometries.insert(ref,g);d.units.push_back({id,id,"",UnitKind::Region,ref});d.presentation.membership[territorialRef(id)]="countries";d.presentation.objectStyles[territorialRef(id)]={}; }
    return d;
}
CommandRequest deletion(const Project& p) {
    const auto plan=CommandProcessor::planTerritorial(p,DeleteTerritorialIntent{{territorialRef("S"),territorialRef("R")}});check(plan.ok()&&plan.plan.has_value());CommandArguments args;args.action=ApplyTerritorialMutation{*plan.plan,{}};return CommandProcessor::makeRequest(p,"territorial.delete",std::move(args));
}
int main() {
    try {
        int geometryFailures=0;
        for(long position=0;position<5000;++position) {
            auto d=deleteFixture();d.units[1].kind=UnitKind::Subunit;d.units[1].coverageMode="partition";
            d.relations.push_back({"s-base",territorialRef("S"),territorialRef("A"),territorialRef("A")});
            Project p;p.replace(d);check(p.setMemo("A","redo")&&p.undo());
            const auto plan=CommandProcessor::planTerritorial(p,ConvertTerritorialTypeIntent{territorialRef("S"),UnitKind::Country,{},{},{}});check(plan.ok());
            const auto source=*d.geometries.get(d.units[1].geometry);auto remainder=*d.geometries.get(d.units[0].geometry);remainder.polygons[0].push_back(source.polygons[0][0]);
            CommandArguments args;args.action=ApplyTerritorialMutation{*plan.plan,GeometryPatch{p.revision(),{{territorialRef("S"),source},{territorialRef("A"),remainder}},{},{}}};
            const auto request=CommandProcessor::makeRequest(p,"territorial.geometry.commit",args);
            const auto before=&p.document();const auto rev=p.revision();
            failAfter=position;auto prepared=CommandProcessor::prepare(p,request);failAfter=-1;
            check(&p.document()==before&&p.revision()==rev&&p.canRedo()&&!p.dirty());
            if(prepared.preview){check(CommandProcessor::confirm(p,*prepared.preview).ok());check(p.undo());check(p.redo());break;}
            check(prepared.error==CommandError::PrepareFailed);++geometryFailures;check(position<4999);
        }
        check(geometryFailures>0);std::cout<<"Geometry patch preparation allocation failures verified: "<<geometryFailures<<"\n";
        int presentationFailures=0;
        for(long position=0;position<5000;++position) {
            Project p;p.replace(fixture());check(p.setMemo("A","redo")&&p.undo());
            auto before=&p.document();const auto rev=p.revision(),pr=p.presentationRevision();
            PresentationAction action=SetPresentationVisibility{"countries",false};
            failAfter=position;auto result=PresentationCommandProcessor::apply(p,action);failAfter=-1;
            if(result==PresentationResult::Applied){check(p.revision()==rev&&p.canRedo()&&p.dirty());break;}
            check(result==PresentationResult::Failed);check(&p.document()==before&&p.revision()==rev&&p.presentationRevision()==pr&&p.canRedo()&&!p.dirty());
            ++presentationFailures;check(position<4999);
        }
        check(presentationFailures>0);
        int prepareFailures=0,commitFailures=0,replaceFailures=0,historyFailures=0,deleteFailures=0,deleteCommitFailures=0;
        for(long position=0;position<5000;++position) {
            Project p; p.replace(fixture()); check(p.setMemo("A","redo") && p.undo());
            const auto* before=&p.document(); const auto* view=p.country("A"); const auto rev=p.revision(); auto r=change(p);
            failAfter=position; auto result=CommandProcessor::prepare(p,r); failAfter=-1;
            check(&p.document()==before && p.country("A")==view && p.revision()==rev && !p.dirty() && p.canRedo());
            if(result.preview) break;
            check(result.error==CommandError::PrepareFailed); ++prepareFailures;
            check(position<4999);
        }
        for(long position=0;position<5000;++position) {
            Project p; p.replace(fixture()); check(p.setMemo("A","first") && p.setMemo("A","redo") && p.undo());
            p.markSaved(); const auto* before=&p.document(); const auto* view=p.country("A"); const auto rev=p.revision();
            auto prepared=CommandProcessor::prepare(p,change(p)); check(prepared.preview.has_value());
            failAfter=position; auto result=CommandProcessor::confirm(p,*prepared.preview); failAfter=-1;
            check(!prepared.preview->pending());
            if(result.changed()) {
                check(p.revision()==rev+1 && p.dirty() && !p.canRedo());
                failAfter=0; bool undone=p.undo(),redone=p.redo(); failAfter=-1;
                check(undone && redone); break;
            }
            check(result.error==CommandError::CommitFailed);
            check(&p.document()==before && p.country("A")==view && p.revision()==rev && !p.dirty() && p.canRedo());
            check(p.redo() && p.country("A")->memo=="redo"); ++commitFailures; check(position<4999);
        }
        for(long position=0;position<5000;++position) {
            Project p; p.replace(fixture()); check(p.setMemo("A","first"));
            const auto* before=&p.document(); auto id=p.instanceId(); auto replacement=fixture(); bool failed=false;
            failAfter=position;
            try { p.replace(std::move(replacement)); } catch(const std::bad_alloc&) { failed=true; }
            failAfter=-1;
            if(!failed) { check(p.instanceId()!=id && p.revision()==0 && !p.dirty()); break; }
            check(&p.document()==before && p.instanceId()==id && p.revision()==1 && p.canUndo() && p.dirty());
            ++replaceFailures; check(position<4999);
        }
        for(long position=0;position<5000;++position) {
            Project p;p.replace(fixture());check(p.setMemo("A","  raw notes  "));p.markSaved();
            check(p.renameCountry("A","Next"));const auto* before=&p.document();const auto rev=p.revision();
            bool failed=false;failAfter=position;
            try{check(p.undo());}catch(const std::bad_alloc&){failed=true;}
            failAfter=-1;
            if(!failed){check(p.country("A")->memo=="raw notes" && !p.dirty() && p.canRedo());break;}
            check(&p.document()==before && p.revision()==rev && p.canUndo() && !p.canRedo() && p.dirty());
            ++historyFailures;check(position<4999);
        }
        for(long position=0;position<5000;++position) {
            Project p;p.replace(deleteFixture());const auto bytesBefore=p.document().units.size();const auto rev=p.revision();auto request=deletion(p);
            failAfter=position;auto prepared=CommandProcessor::prepare(p,request);failAfter=-1;
            check(p.document().units.size()==bytesBefore&&p.revision()==rev&&!p.dirty());
            if(prepared.preview) {check(CommandProcessor::confirm(p,*prepared.preview).ok());check(!p.index().objects.count(territorialRef("S"))&&!p.index().objects.count(territorialRef("R")));check(p.undo());check(p.index().objects.count(territorialRef("S"))&&p.index().objects.count(territorialRef("R")));break;}
            if(prepared.error!=CommandError::PrepareFailed) {
                throw std::runtime_error(std::string("unexpected delete preparation result at allocation ")
                                         +std::to_string(position)+": "+commandErrorCode(prepared.error)
                                         +" ("+prepared.detail+")");
            }
            ++deleteFailures;check(position<4999);
        }
        for(long position=0;position<5000;++position) {
            Project p;p.replace(deleteFixture());p.markSaved();const auto bytesBefore=p.document().units.size();const auto rev=p.revision();
            auto prepared=CommandProcessor::prepare(p,deletion(p));check(prepared.preview.has_value());
            failAfter=position;auto result=CommandProcessor::confirm(p,*prepared.preview);failAfter=-1;
            check(!prepared.preview->pending());
            if(result.changed()) {
                check(p.document().units.size()==bytesBefore-2&&p.revision()==rev+1&&p.dirty());
                check(p.undo());check(p.document().units.size()==bytesBefore&&p.index().objects.count(territorialRef("S")));
                check(p.redo());check(p.document().units.size()==bytesBefore-2&&!p.index().objects.count(territorialRef("R")));break;
            }
            check(result.error==CommandError::CommitFailed);
            check(p.document().units.size()==bytesBefore&&p.revision()==rev&&!p.dirty());
            ++deleteCommitFailures;check(position<4999);
        }
        check(prepareFailures>0 && commitFailures>0 && replaceFailures>0 && historyFailures>0 && deleteFailures>0 && deleteCommitFailures>0);
        std::cout<<"Allocation failure positions verified: prepare="<<prepareFailures
                 <<", confirm="<<commitFailures<<", replace="<<replaceFailures<<", history="<<historyFailures
                 <<", delete-prepare="<<deleteFailures<<", delete-confirm="<<deleteCommitFailures<<"\n";
    } catch(const std::exception& e) { failAfter=-1; std::cerr<<e.what()<<'\n'; return 1; }
}

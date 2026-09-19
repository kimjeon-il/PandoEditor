#include <pandoeditor/project.h>
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
void check(bool value) { if(!value) throw std::runtime_error("allocation atomicity check failed"); }
ProjectDocument fixture() {
    ProjectDocument d({{"A","Alpha",{{{{0,0},{10,0},{10,10},{0,10},{0,0}}}},0x123456,"original"}},{{"countries","Countries"}});
    d.documentId="allocation-test-document"; return d;
}
CommandRequest change(const Project& p) {
    const auto& c=*p.country("A"); CommandArguments args;
    args.properties.countries.push_back({"A",{std::string(80,'n'),std::string(80,'m'),0x102030,0.4,c.layerId}});
    return CommandProcessor::makeRequest(p,"edit.properties",std::move(args));
}
int main() {
    try {
        int prepareFailures=0,commitFailures=0,replaceFailures=0,historyFailures=0;
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
        check(prepareFailures>0 && commitFailures>0 && replaceFailures>0 && historyFailures>0);
        std::cout<<"Allocation failure positions verified: prepare="<<prepareFailures
                 <<", confirm="<<commitFailures<<", replace="<<replaceFailures<<", history="<<historyFailures<<"\n";
    } catch(const std::exception& e) { failAfter=-1; std::cerr<<e.what()<<'\n'; return 1; }
}

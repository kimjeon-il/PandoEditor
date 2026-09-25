#include <pandoeditor/gisexchange.h>
#include <pandoeditor/project.h>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

// Fail each real C++ allocation in the GIS plan / preview / commit path.
// This test is single threaded and leaves Qt worker allocation out of scope.
static thread_local long failAfter=-1;
void* operator new(std::size_t size) {
    if(failAfter==0)throw std::bad_alloc();
    if(failAfter>0)--failAfter;
    if(void* p=std::malloc(size?size:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) {return ::operator new(size);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}

using namespace pandoeditor;
namespace {
void check(bool value) {if(!value)throw std::runtime_error("GIS allocation atomicity failed");}
Project project() {
    Project p;p.replace(ProjectDocument({{"A","Alpha",{{{{0,0},{2,0},{2,2},{0,2},{0,0}}}},0x123456}},
        {{"countries","Countries"}}));
    check(p.setMemo("A","redo")&&p.undo());
    return p;
}
GisGenericInput input() {
    GisGenericInput row;row.id="gis:allocation";row.name="Marker";
    row.geometry.type="Point";row.geometry.points={{1,1}};
    row.propertiesJson="{\"source\":\"allocation test\"}";
    return row;
}
void unchanged(const Project& p,const ProjectDocument* before,std::uint64_t revision) {
    check(&p.document()==before&&p.revision()==revision&&p.canRedo()&&!p.dirty());
    check(p.document().genericFeatures.empty());
}
}
int main() {
    int planFailures=0,previewFailures=0,commitFailures=0;
    for(long index=0;index<5000;++index) {
        auto p=project();auto snapshot=p.snapshot();auto feature=input();
        const auto* before=&p.document();const auto revision=p.revision();
        bool failed=false;
        failAfter=index;
        try {auto plan=planGenericGisImport(snapshot,"gis:allocation",
            {"sample.geojson","geojson"},{feature});(void)plan;}
        catch(const std::bad_alloc&) {failed=true;}
        failAfter=-1;unchanged(p,before,revision);
        if(!failed)break;
        ++planFailures;check(index<4999);
    }
    for(long index=0;index<5000;++index) {
        auto p=project();auto plan=planGenericGisImport(p.snapshot(),"gis:allocation",
            {"sample.geojson","geojson"},{input()});
        CommandArguments args;args.action=std::move(plan);
        auto request=CommandProcessor::makeRequest(p,"gis.import.generic",std::move(args));
        const auto* before=&p.document();const auto revision=p.revision();
        failAfter=index;
        auto prepared=CommandProcessor::prepare(p,request);
        failAfter=-1;unchanged(p,before,revision);
        if(prepared.preview)break;
        check(prepared.error==CommandError::PrepareFailed);
        ++previewFailures;check(index<4999);
    }
    for(long index=0;index<5000;++index) {
        auto p=project();auto plan=planGenericGisImport(p.snapshot(),"gis:allocation",
            {"sample.geojson","geojson"},{input()});
        CommandArguments args;args.action=std::move(plan);
        auto request=CommandProcessor::makeRequest(p,"gis.import.generic",std::move(args));
        auto prepared=CommandProcessor::prepare(p,request);check(prepared.preview.has_value());
        const auto* before=&p.document();const auto revision=p.revision();
        failAfter=index;
        auto committed=CommandProcessor::confirm(p,*prepared.preview);
        failAfter=-1;
        if(committed.changed()) {
            check(p.revision()==revision+1&&p.document().genericFeatures.size()==1);
            check(p.undo()&&p.document().genericFeatures.empty());
            break;
        }
        check(committed.error==CommandError::CommitFailed);
        unchanged(p,before,revision);
        ++commitFailures;check(index<4999);
    }
    check(planFailures>0&&previewFailures>0&&commitFailures>0);
    std::cout<<"GIS allocation failures: plan "<<planFailures<<", preview "
             <<previewFailures<<", commit "<<commitFailures<<"\n";
}

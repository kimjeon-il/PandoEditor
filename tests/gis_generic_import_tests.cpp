#include <pandoeditor/gisexchange.h>
#include <pandoeditor/project.h>
#include <cassert>
#include <functional>
#include <stdexcept>

using namespace pandoeditor;
namespace {
Geometry square(double x) {
    Geometry shape;shape.type="Polygon";
    shape.polygons={Polygon{Ring{{x,0},{x+1,0},{x+1,1},{x,1},{x,0}}}};
    return shape;
}
Project project() {
    Project p;p.replace(ProjectDocument({{"A","Alpha",square(0).polygons,0x123456}},{{"countries","Countries"}}));
    return p;
}
bool rejected(const std::function<void()>& f) {
    try {f();}catch(const std::invalid_argument&){return true;}
    return false;
}
}
int main() {
    auto p=project();
    GisGenericInput one{"gis:one","One",square(3),"{\"foreign\":123}","Memo",0xabcdef};
    GisGenericInput two{"gis:two","Two",square(5),"{\"foreign\":456}"};
    auto plan=planGenericGisImport(p.snapshot(),"import:1",{"generic.geojson","geojson"},{one,two});
    assert(plan.info.affectedIds.size()==2 && p.document().genericFeatures.empty());
    CommandArguments args;args.action=plan;
    auto request=CommandProcessor::makeRequest(p,"gis.import.generic",args);
    auto prepared=CommandProcessor::prepare(p,request);
    assert(prepared.ok()&&prepared.preview && p.document().genericFeatures.empty());
    assert(prepared.preview->change().after().genericFeatures.size()==2);
    assert(CommandProcessor::confirm(p,*prepared.preview).changed());
    assert(p.index().objects.count({"generic","gis:one"}));
    assert(p.document().genericFeatures.front().source.details=="{\"foreign\":123}");
    assert(p.document().genericFeatures.front().notes=="Memo");
    assert(p.document().genericFeatures.front().color==0xabcdef);
    assert(p.undo() && p.document().genericFeatures.empty());
    assert(p.redo() && p.document().genericFeatures.size()==2);
    assert(CommandProcessor::prepare(p,request).error==CommandError::StaleRevision);
    auto other=project();
    assert(CommandProcessor::prepare(other,request).error==CommandError::ProjectMismatch);
    assert(rejected([&]{planGenericGisImport(p.snapshot(),"import:2",{"x","geojson"},{one,one});}));
    assert(rejected([&]{planGenericGisImport(p.snapshot(),"import:2",{"x","geojson"},{one});}));
    auto bad=one;bad.geometry.polygons.front().front().pop_back();
    assert(rejected([&]{planGenericGisImport(other.snapshot(),"import:3",{"x","geojson"},{bad});}));
    assert(other.document().genericFeatures.empty());
}

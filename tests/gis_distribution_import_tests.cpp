#include <pandoeditor/gisexchange.h>
#include <pandoeditor/project.h>
#include <cassert>
#include <functional>
#include <stdexcept>

using namespace pandoeditor;
namespace {
Geometry square() {Geometry g;g.type="Polygon";g.polygons={Polygon{Ring{{0,0},{1,0},{1,1},{0,1},{0,0}}}};return g;}
Project project() {Project p;p.replace(ProjectDocument({{"A","Alpha",square().polygons,0x123456}},{{"countries","Countries"}}));return p;}
bool rejects(const std::function<void()>& fn){try{fn();}catch(const std::invalid_argument&){return true;}return false;}
}
int main() {
    auto p=project();
    DistributionLayer layer;layer.id="lang:1";layer.name="Languages";layer.type="language";
    GisDistributionInput territorial;territorial.entry.id="entry:1";
    territorial.entry.layerId=layer.id;territorial.entry.territory=territorialRef("A");
    territorial.entry.share=60;
    GisDistributionInput free;free.entry.id="entry:2";
    free.entry.layerId=layer.id;free.entry.share=70;free.geometry=square();
    auto plan=planDistributionGisImport(p.snapshot(),"gis:distribution",{"source.geojson","geojson"},
        {layer},{territorial,free});
    assert(p.document().distributionEntries.empty());
    CommandArguments args;args.action=plan;
    auto request=CommandProcessor::makeRequest(p,"gis.import.distribution",args);
    auto prepared=CommandProcessor::prepare(p,request);
    assert(prepared.ok()&&prepared.preview&&p.document().distributionEntries.empty());
    assert(CommandProcessor::confirm(p,*prepared.preview).changed());
    assert(p.document().distributionEntries.size()==2);
    assert(p.document().distributionEntries.front().territory==territorialRef("A"));
    assert(!p.document().distributionEntries.front().geometry);
    assert(p.document().distributionEntries.back().geometry);
    assert(p.undo()&&p.document().distributionEntries.empty());
    assert(p.redo()&&p.document().distributionEntries.size()==2);
    assert(CommandProcessor::prepare(p,request).error==CommandError::StaleRevision);
    auto other=project();
    assert(CommandProcessor::prepare(other,request).error==CommandError::ProjectMismatch);
    assert(rejects([&]{planDistributionGisImport(other.snapshot(),"dup",{"x","geojson"},
        {layer},{territorial,territorial});}));
    auto dangling=territorial;dangling.entry.territory=territorialRef("missing");
    assert(rejects([&]{planDistributionGisImport(other.snapshot(),"missing",{"x","geojson"},
        {layer},{dangling});}));
    auto invalid=free;invalid.entry.territory=territorialRef("A");
    assert(rejects([&]{planDistributionGisImport(other.snapshot(),"ambiguous",{"x","geojson"},
        {layer},{invalid});}));
    assert(other.document().distributionEntries.empty());
}

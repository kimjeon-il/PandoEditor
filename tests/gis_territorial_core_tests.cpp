#include <pandoeditor/gisexchange.h>
#include <pandoeditor/project.h>
#include <cassert>
#include <functional>
#include <stdexcept>

using namespace pandoeditor;
namespace {
Geometry square(double x,double y=0) {
    Geometry shape;shape.type="Polygon";
    shape.polygons={Polygon{Ring{{x,y},{x+2,y},{x+2,y+2},{x,y+2},{x,y}}}};
    return shape;
}
Project project() {
    Project p;p.replace(ProjectDocument({{"A","Alpha",square(0).polygons,0x123456}},
        {{"countries","Countries"}}));return p;
}
bool rejected(const std::function<void()>& callback) {
    try {callback();}catch(const std::invalid_argument&){return true;}
    return false;
}
}
int main() {
    auto p=project();
    GisTerritorialInput region;region.id="R";region.name="Region";region.kind=UnitKind::Region;
    region.geometry=square(0);region.sovereign=territorialRef("A");
    auto plan=planTerritorialGisImport(p.snapshot(),"region",{"r.geojson","geojson"},
        GisExchangeTarget::Region,{region});
    CommandArguments args;args.action=plan;
    auto request=CommandProcessor::makeRequest(p,"gis.import.territorial",args);
    auto preview=CommandProcessor::prepare(p,request);
    assert(preview.ok()&&preview.preview&&p.document().units.size()==1);
    assert(CommandProcessor::confirm(p,*preview.preview).changed());
    assert(p.document().units.size()==2&&p.undo()&&p.document().units.size()==1);
    assert(p.redo()&&p.document().units.size()==2);
    assert(CommandProcessor::prepare(p,request).error==CommandError::StaleRevision);
    auto other=project();
    assert(CommandProcessor::prepare(other,request).error==CommandError::ProjectMismatch);
    assert(rejected([&]{planTerritorialGisImport(other.snapshot(),"duplicate",{"r","geojson"},
        GisExchangeTarget::Region,{region,region});}));
    GisTerritorialInput missing=region;missing.id="S";missing.sovereign=territorialRef("absent");
    assert(rejected([&]{planTerritorialGisImport(other.snapshot(),"missing",{"r","geojson"},
        GisExchangeTarget::Region,{missing});}));
    GisTerritorialInput country;country.id="C";country.name="Charlie";
    country.kind=UnitKind::Country;country.geometry=square(1);
    assert(rejected([&]{planTerritorialGisImport(other.snapshot(),"overlap",{"c","geojson"},
        GisExchangeTarget::Country,{country});}));
    auto invalid=plan;invalid.units.front().id="swapped";
    CommandArguments changed;changed.action=invalid;
    assert(CommandProcessor::prepare(other,CommandProcessor::makeRequest(other,
        "gis.import.territorial",changed)).error==CommandError::ProjectMismatch);
    auto valid=planTerritorialGisImport(other.snapshot(),"valid",{"r","geojson"},
        GisExchangeTarget::Region,{region});
    valid.units.front().id="swapped";
    CommandArguments malformed;malformed.action=valid;
    assert(CommandProcessor::prepare(other,CommandProcessor::makeRequest(other,
        "gis.import.territorial",malformed)).error==CommandError::InvalidTargets);
    assert(other.document().units.size()==1);
}

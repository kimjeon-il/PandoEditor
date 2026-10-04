#include "territorial_fixture.h"
#include <pandoeditor/map/mappicker.h>
#include <pandoeditor/objectproperties.h>
#include <cmath>
#include <stdexcept>
#include <string>

using namespace pandoeditor;

namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}

ProjectDocument fixture() {
    ProjectDocument d({
        {"A","알파",{{{{0,0},{10,0},{10,10},{0,10},{0,0}}}},0x123456}
    },{{"countries","국가"},{"other","영역"},{"hidden","숨김",false,false,1}});
    d.documentId="picker";
    auto add=[&](std::string id,std::string name,UnitKind kind,const std::string& layer,
                 double left,double right) {
        Geometry g;g.type="Polygon";
        g.polygons={{{{left,2},{right,2},{right,4},{left,4},{left,2}}}};
        GeometryRef gr{"picker-"+id,1};d.geometries.insert(gr,std::move(g));
        TerritorialUnit unit;unit.id=id;unit.name=name;unit.kind=kind;
        appendTerritory(d,std::move(unit),gr);
        d.presentation.membership[territorialRef(id)]=layer;
        d.presentation.objectStyles[territorialRef(id)]=ObjectStyle{};
    };
    add("S","하하",UnitKind::General,"other",1,4);
    add("T","가나다",UnitKind::General,"other",1,4);
    add("H","숨김",UnitKind::Regional,"hidden",40,42);
    d.presentation.webPresentation.objectOrder={
        "territorial:entity:T","territorial:entity:S"};
    setFixtureParent(d,territorialRef("S"),territorialRef("A"));
    setFixtureParent(d,territorialRef("T"),territorialRef("A"));
    validateDocument(d);
    return d;
}

MapCameraMetrics metrics(){return {50,10,1,0,10};}

void chooserOrderAndTopPaintOrder() {
    Project project;project.replace(fixture());
    MapPicker picker;MapPickContext context;
    const auto hits=picker.pickMap(project.snapshot(),metrics(),{2.5,7,10},context);
    require(hits.size()>=2,"overlap candidates");
    require(hits[0]==territorialRef("T"),"chooser is display-name ordered");
    require(hits[1]==territorialRef("S"),"second candidate");
    const auto top=picker.topCandidate(project.snapshot(),hits);
    require(top&&*top==territorialRef("S"),"paint order independently selects S");
    const auto hidden=picker.pickMap(project.snapshot(),metrics(),{41,7,10},context);
    require(hidden.empty(),"hidden layer excluded");
}

void screenScaleMatchesProjection() {
    MapViewState flat;flat.viewportWidth=800;flat.viewportHeight=600;
    flat.scale=180;flat.translateX=400;flat.translateY=300;
    const auto ppu=mapPickPixelsPerMapUnit(flat,metrics(),400,300);
    require(std::abs(ppu-180.0/(180.0/3.14159265358979323846))<1e-9,
            "flat screen scale");
    auto globe=flat;globe.mode=ProjectionMode::Globe;globe.scale=240;
    globe.centerLongitude=2.5;globe.centerLatitude=3;
    require(mapPickPixelsPerMapUnit(globe,metrics(),400,300)>0,
            "globe local screen scale");
}

void flatAndGlobeScreenPicking() {
    Project project;project.replace(fixture());
    MapPicker picker;MapPickContext context;
    MapViewState flat;flat.viewportWidth=800;flat.viewportHeight=600;
    flat.scale=200;flat.translateX=400;flat.translateY=300;
    flat.centerLongitude=2.5;flat.centerLatitude=3;
    auto hits=picker.pickScreen(project.snapshot(),flat,metrics(),{400,300},context);
    require(!hits.empty(),"flat screen pick");
    auto globe=flat;globe.mode=ProjectionMode::Globe;globe.scale=240;
    globe.centerLongitude=2.5;globe.centerLatitude=3;
    hits=picker.pickScreen(project.snapshot(),globe,metrics(),{400,300},context);
    require(!hits.empty(),"globe screen pick");
    require(picker.pickScreen(project.snapshot(),globe,metrics(),{799,599},context).empty(),
            "outside globe misses");
}

void labelAndDistributionFilters() {
    auto d=fixture();
    Geometry point;point.type="Point";point.points={{2,3}};
    d.geometries.insert({"label",1},point);
    d.labels.push_back({"L","라벨","city",{},{"label",1}});
    d.presentation.webPresentation.visibility["labels"]=true;
    d.presentation.membership[{"label","L"}]="other";

    d.distributionLayers.push_back({"D","분포","language"});
    Geometry area;area.type="Polygon";
    area.polygons={{{{1.5,2.5},{2.5,2.5},{2.5,3.5},{1.5,3.5},{1.5,2.5}}}};
    d.geometries.insert({"distribution",1},area);
    DistributionEntry entry;entry.id="E";entry.layerId="D";
    entry.geometry=GeometryRef{"distribution",1};
    d.distributionEntries.push_back(entry);
    d.presentation.membership[{"distributionEntry","E"}]="other";
    validateDocument(d);

    Project project;project.replace(std::move(d));
    MapPicker picker;MapPickContext context;
    auto hits=picker.pickMap(project.snapshot(),metrics(),{2,7,10},context);
    require(std::find(hits.begin(),hits.end(),ObjectRef{"label","L"})==hits.end(),
            "unplaced label excluded");
    const std::set<ObjectRef> placed{{"label","L"}};
    context.placedLabels=&placed;
    hits=picker.pickMap(project.snapshot(),metrics(),{2,7,10},context);
    require(std::find(hits.begin(),hits.end(),ObjectRef{"label","L"})!=hits.end(),
            "placed label included");
}

void externalHydroAndNormalization() {
    Project project;project.replace(fixture());
    HydroPhysicalFeature feature;feature.geometry.kind=1;
    feature.geometry.lines={{{2000000,3000000},{3000000,3000000}}};
    MapExternalHydroPickFeature external;
    external.ref={"hydroBuiltin","R"};
    external.category="river";
    external.bounds={2,3,3,3};
    external.feature=&feature;
    MapPickContext context;context.externalHydro.push_back(external);
    MapPicker picker;
    const auto hits=picker.pickMap(project.snapshot(),metrics(),{2.5,7,10},context);
    require(std::find(hits.begin(),hits.end(),external.ref)!=hits.end(),"external hydro hit");
}

void externalHydroFragmentsRemainPickable() {
    Project project;project.replace(fixture());
    HydroPhysicalFeature miss;miss.geometry.kind=1;
    miss.geometry.lines={{{20000000,3000000},{21000000,3000000}}};
    HydroPhysicalFeature hit;hit.geometry.kind=1;
    hit.geometry.lines={{{2000000,3000000},{3000000,3000000}}};
    MapExternalHydroPickFeature first;
    first.ref={"hydroBuiltin","fragmented"};first.category="river";
    first.bounds={0,0,50,10};first.feature=&miss;
    auto second=first;second.feature=&hit;
    MapPickContext context;context.externalHydro={first,second};
    MapPicker picker;
    const auto hits=picker.pickMap(project.snapshot(),metrics(),{2.5,7,10},context);
    require(std::find(hits.begin(),hits.end(),ObjectRef{"hydroBuiltin","fragmented"})!=hits.end(),
            "later hydro fragment can hit");
}

void incrementalIndexUpdate() {
    Project project;project.replace(fixture());
    MapPicker picker;MapPickContext context;
    picker.pickMap(project.snapshot(),metrics(),{2,7,10},context);
    require(picker.incrementalUpdateCount()==0,"initial index build");
}
}

int main() {
    chooserOrderAndTopPaintOrder();
    screenScaleMatchesProjection();
    flatAndGlobeScreenPicking();
    labelAndDistributionFilters();
    externalHydroAndNormalization();
    externalHydroFragmentsRemainPickable();
    incrementalIndexUpdate();
}

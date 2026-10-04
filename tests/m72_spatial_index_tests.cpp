#include "territorial_fixture.h"
#include <pandoeditor/spatialindex.h>
#include <pandoeditor/presentation.h>
#include <algorithm>
#include <stdexcept>

namespace {
using namespace pandoeditor;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
Geometry square(double west,double east,double south=0,double north=2) {
    return {"Polygon",{},{},{{{{west,south},{east,south},{east,north},{west,north},{west,south}}}}};
}
struct Corpus {ProjectDocument document;DocumentIndex index;};
Corpus makeCorpus() {
    Corpus c;
    auto add=[&](std::string id,Geometry geometry) {
        auto i=c.document.units.size();GeometryRef ref{id,1};
        c.document.geometries.insert(ref,std::move(geometry));
        appendTerritory(c.document,{id,id,{},UnitKind::General,false},ref);
        c.index.objects[{"territorial",id}]=i;
    };
    add("DEU",square(10,12,50,52));
    add("DATELINE",square(179,-179,70,80));
    add("POLAR",square(-180,180,89.8,89.9));
    add("HIDDEN",square(10,11,50,51));
    c.document.presentation.webPresentation.hiddenItems["countries"].insert("HIDDEN");
    return c;
}
bool has(const std::vector<ObjectRef>& refs,const std::string& id) {
    return std::find(refs.begin(),refs.end(),territorialRef(id))!=refs.end();
}
void ordinaryAndHidden() {
    auto c=makeCorpus();GeoSpatialIndex index;index.rebuild(c.document,c.index);
    auto refs=index.query({{9,49,13,53}});
    require(has(refs,"DEU")&&has(refs,"HIDDEN"),"ordinary and hidden indexed");
    require(!has(refs,"DATELINE"),"distant object filtered");
    refs.erase(std::remove_if(refs.begin(),refs.end(),[&](const auto& ref){
        return !effectiveMapVisibility(c.document,ref);
    }),refs.end());
    require(has(refs,"DEU")&&!has(refs,"HIDDEN"),"visibility filtered after query");
    require(index.query({{10,50,12,52},{10,50,11,51}}).size()==2,"duplicates returned once");
}
void datelineAndPolar() {
    auto c=makeCorpus();GeoSpatialIndex index;index.rebuild(c.document,c.index);
    require(has(index.query({{178,69,-178,81,true}}),"DATELINE"),"wrapped viewport");
    require(has(index.query({{-180,69,-178,81}}),"DATELINE"),"western world copy");
    require(has(index.query({{170,89.7,180,90}}),"POLAR"),"polar longitude cell");
    require(!has(index.query({{-1,74,1,76}}),"DATELINE"),"canonical wrapped window remains local");
    require(has(index.queryLegacyFlat({{-1,74,1,76}}),"DATELINE"),
        "legacy flat unsplit segment remains a conservative picking candidate");
}
void viewMoveDoesNotRebuildButGeometryDoes() {
    auto c=makeCorpus();GeoSpatialIndex index;index.rebuild(c.document,c.index);
    const auto revision=index.geometryRevision();
    index.query({{0,0,5,5}});index.rebuild(c.document,c.index);
    require(index.geometryRevision()==revision,"same geometry and view move do not rebuild");
    c.document.geometries.insert({"DEU",2},square(30,31,50,52));
    staticGeometryBinding(c.document,c.document.units[0].id).geometryRef={"DEU",2};index.rebuild(c.document,c.index);
    require(index.geometryRevision()==revision+1,"changed geometry version rebuilds");
    require(!has(index.query({{9,49,13,53}}),"DEU")&&
        has(index.query({{29,49,32,53}}),"DEU"),"new location replaces old cells");
    c.document.physicalData.dataset="new-data";index.rebuild(c.document,c.index);
    require(index.geometryRevision()==revision+2,"dataset replacement rebuilds");
}
}
int main(){ordinaryAndHidden();datelineAndPolar();viewMoveDoesNotRebuildButGeometryDoes();}

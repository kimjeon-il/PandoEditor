#include "territorial_fixture.h"
#include <pandoeditor/map/mapscenebuilder.h>
#include <algorithm>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
using namespace pandoeditor;
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
Geometry square(double west) {
    return {"Polygon",{},{},{{{{west,0},{west+1,0},{west+1,1},{west,1},{west,0}}}}};
}
ProjectDocument document() {
    ProjectDocument doc;
    appendTerritory(doc,{"ROOT","Root",{},UnitKind::General,false},{"root",1});
    appendTerritory(doc,{"CHILD","Child",{},UnitKind::General,false},{"child",1},"ROOT");
    appendTerritory(doc,{"REGION","Region",{},UnitKind::Regional,false},{"region",1});
    for(const auto& id:{"root","child","region"})doc.geometries.insert({id,1},square(id[0]));
    return doc;
}
std::shared_ptr<WorldBaseFrame> base() {
    auto frame=std::make_shared<WorldBaseFrame>();
    frame->mesh=std::make_shared<CountryBaseMesh>();
    frame->ranges={{"ROOT-A","ROOT","root"},{"ROOT-B","ROOT","root"},
                   {"REGION-A","REGION","region"},{"REMOVED","REMOVED","removed"}};
    while(frame->ranges.size()<258) {
        const auto id="absent-"+std::to_string(frame->ranges.size());
        frame->ranges.push_back({id,id,id});
    }
    return frame;
}
void hiddenGeneralGeometryRemainsLand() {
    auto doc=document();
    doc.presentation.webPresentation.visibility["countries"]=false;
    doc.presentation.webPresentation.visibility["subunits"]=false;
    doc.presentation.webPresentation.hiddenItems["countries"].insert("ROOT");
    doc.presentation.webPresentation.hiddenItems["subunits"].insert("CHILD");
    doc.presentation.objectStyles[territorialRef("ROOT")].opacity=0;
    doc.presentation.objectStyles[territorialRef("CHILD")].opacity=0;
    GeometryPacketCache cache;MapSceneBuilder builder(cache);builder.setWorldBase(base());
    const auto scene=builder.buildDocument(doc,1,{}, {},{});
    require(bool(scene->physicalLandMask),"physical geometry has an independent channel");
    require(scene->physicalLandMask->baseSlots==std::vector<std::size_t>({0,1}),
            "hidden root retains every owned base slot; Regional and deleted slots are excluded");
    const auto& polygons=scene->physicalLandMask->polygons;
    require(polygons.size()==1&&polygons.front().object==territorialRef("CHILD"),
            "hidden General descendants outside the root remain land; Regional is excluded");
    require(polygons.front().geometryPacket.triangleCount>0,"hidden General is actually triangulated");
}
void replacementRetiresAllOriginalBaseSlots() {
    auto doc=document();doc.geometries.insert({"root",2},square(80));
    staticGeometryBinding(doc,"ROOT").geometryRef.version=2;
    doc.presentation.webPresentation.visibility["countries"]=false;
    GeometryPacketCache cache;MapSceneBuilder builder(cache);builder.setWorldBase(base());
    const auto scene=builder.buildDocument(doc,2,{}, {},{});
    require(scene->physicalLandMask->baseSlots.empty(),"replacement does not expose old immutable land");
    const auto& polygons=scene->physicalLandMask->polygons;
    const auto root=std::find_if(polygons.begin(),polygons.end(),[](const auto& p){return p.object==territorialRef("ROOT");});
    require(root!=polygons.end()&&root->geometry==GeometryRef{"root",2},
            "hidden replacement uses the actual effective geometry version");
}
void presentationAndCameraRetainMaskIdentity() {
    auto doc=document();GeometryPacketCache cache;MapSceneBuilder builder(cache);builder.setWorldBase(base());
    MapViewState view;const auto first=builder.buildDocument(doc,1,view,{},{});
    doc.presentation.objectStyles[territorialRef("CHILD")].opacity=0;
    doc.presentation.webPresentation.visibility["countries"]=false;
    const auto styled=builder.buildDocument(doc,2,view,{},first);
    require(styled->physicalLandMask==first->physicalLandMask,"paint changes retain semantic packet identity");
    view.revision=1;view.translateX=100;view.centerLongitude=30;
    const auto moved=builder.buildDocument(doc,2,view,{},styled);
    require(moved->physicalLandMask==first->physicalLandMask,"camera changes retain geographic source identity");
    view.mode=ProjectionMode::Globe;view.revision=2;
    const auto globe=builder.buildDocument(doc,2,view,{},moved);
    require(globe->physicalLandMask==first->physicalLandMask,"projection uses retained geographic triangle buffers");
}
void hiddenGeometryChangePublishesNewIdentity() {
    auto doc=document();doc.presentation.webPresentation.visibility["countries"]=false;
    doc.presentation.webPresentation.visibility["subunits"]=false;
    GeometryPacketCache cache;MapSceneBuilder builder(cache);builder.setWorldBase(base());
    const auto first=builder.buildDocument(doc,1,{}, {},{});
    doc.geometries.insert({"child",2},square(120));staticGeometryBinding(doc,"CHILD").geometryRef.version=2;
    const auto next=builder.buildDocument(doc,2,{}, {},first);
    require(next->physicalLandMask!=first->physicalLandMask,"hidden geometry edit invalidates physical mask");
    require(next->geometrySignature!=first->geometrySignature,"hidden physical geometry participates in scene publication");
    require(next->physicalLandMask->polygons.front().geometry==GeometryRef{"child",2},"new geometry is published");
}
void emptyDocumentDoesNotRestoreDeletedLand() {
    GeometryPacketCache cache;MapSceneBuilder builder(cache);builder.setWorldBase(base());
    const auto scene=builder.buildDocument({},3,{}, {},{});
    require(scene->physicalLandMask&&scene->physicalLandMask->baseSlots.empty()&&scene->physicalLandMask->polygons.empty(),
            "a ready empty document has an authoritative empty land mask");
}
void hiddenLandAccountingSharesVisibleFillAllocation() {
    ProjectDocument doc;
    appendTerritory(doc,{"LAND","Land",{},UnitKind::General,false},{"land",1});
    doc.geometries.insert({"land",1},square(0));
    doc.presentation.webPresentation.visibility["countries"]=false;
    doc.presentation.webPresentation.styles["countries"].boundaryVisible=false;
    GeometryPacketCache cache;MapSceneBuilder builder(cache);
    const auto hidden=builder.buildDocument(doc,1,{}, {},{});
    const auto hiddenBytes=cache.resourceCacheSnapshot().activeBytes;
    require(hidden->polygons.empty()&&hidden->strokes.empty(),"fixture has no painted geometry");
    require(hiddenBytes>0&&hiddenBytes==cache.stats().residentBytes,
            "hidden physical-only packet has actual active resident bytes");
    doc.presentation.webPresentation.visibility["countries"]=true;
    const auto visible=builder.buildDocument(doc,2,{}, {},hidden);
    require(visible->polygons.size()==1&&visible->strokes.empty(),"fixture exposes only one fill");
    const auto& fill=visible->polygons.front().geometryPacket;
    const auto& mask=visible->physicalLandMask->polygons.front().geometryPacket;
    require(fill.positions==mask.positions&&fill.indices==mask.indices,
            "visible fill and physical mask share actual triangle allocations");
    require(cache.resourceCacheSnapshot().activeBytes==hiddenBytes&&cache.stats().residentBytes==hiddenBytes,
            "shared visible/mask geometry is counted exactly once");
    GeometryPacketCache independentCache;MapSceneBuilder independent(independentCache);
    const auto full=independent.buildDocument(doc,2,{}, {},{});
    require(visible->geometrySignature==full->geometrySignature,
            "independent full preparation has the same semantic geometry signature");
}
}
int main() {
    try {
        void (*const cases[])()={hiddenGeneralGeometryRemainsLand,replacementRetiresAllOriginalBaseSlots,
            presentationAndCameraRetainMaskIdentity,hiddenGeometryChangePublishesNewIdentity,
            emptyDocumentDoesNotRestoreDeletedLand,hiddenLandAccountingSharesVisibleFillAllocation};
        std::size_t passed=0;
        for(const auto run:cases){run();++passed;}
        std::cout<<"terrain land mask: processed="<<std::size(cases)<<" passed="<<passed<<" failed=0\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

#include <pandoeditor/maprenderorder.h>
#include <algorithm>
#include <array>

namespace pandoeditor {
namespace {
constexpr std::array<const char*,4> overlayGroups{
    "distributions","subunits","regions","genericFeatures"};
std::string groupFor(const ProjectDocument& document,const ObjectRef& ref) {
    if(ref.domain=="territorial")for(const auto& unit:document.units)if(unit.id==ref.id)
        return unit.kind==UnitKind::Country?"countries":unit.kind==UnitKind::Subunit?"subunits":"regions";
    return contentGroup(document,ref);
}
int overlayIndex(const std::string& group) {
    for(std::size_t i=0;i<overlayGroups.size();i++)if(group==overlayGroups[i])return static_cast<int>(i);
    return -1;
}
double territorialObjectOrder(const ProjectDocument& document,const ObjectRef& ref) {
    if(ref.domain!="territorial")return 0;
    const auto& order=document.presentation.webPresentation.objectOrder;
    for(const auto& unit:document.units)if(unit.id==ref.id&&unit.kind!=UnitKind::Country){
        const auto key=territorialPresentationKey(unit.kind,unit.id);
        const auto found=std::find(order.begin(),order.end(),key);
        if(found!=order.end())return double(found-order.begin())/double(order.size()+1);
    }
    return 0;
}
}
MapRenderOrder mapBuiltinHydroRenderOrder(const std::string& kind,
                                         RenderPrimitiveRole role,bool borderAligned) {
    if(kind=="lake")return {role==RenderPrimitiveRole::Boundary?31:30,0,0};
    return {borderAligned?33:32,0,0};
}
MapRenderOrder mapRenderOrder(const ProjectDocument& document,const ObjectRef& ref,
                             RenderPrimitiveRole role) {
    const auto group=groupFor(document,ref);
    // Web submits child fills first but reserves their pixels with stencil.
    // QPainter uses ordinary overpainting, so country must be behind children.
    if(group=="countries")return {role==RenderPrimitiveRole::Boundary?50:0,0,0};
    if(group=="subunits"||group=="regions")
        return {role==RenderPrimitiveRole::Fill?10:60,overlayIndex(group),territorialObjectOrder(document,ref)};
    if(group=="rivers"||group=="lakes")
        return mapBuiltinHydroRenderOrder(group=="lakes"?"lake":"river",role);
    if(group=="labels")return {role==RenderPrimitiveRole::Label?80:70,0,0};
    const auto overlay=overlayIndex(group);
    if(overlay>=0) {
        double layerOrder=0;
        if(ref.domain=="distributionEntry")for(const auto& entry:document.distributionEntries)if(entry.id==ref.id)
            for(std::size_t i=0;i<document.distributionLayers.size();++i)if(document.distributionLayers[i].id==entry.layerId)
                layerOrder=double(i)/double(document.distributionLayers.size()+1);
        return {role==RenderPrimitiveRole::Fill?20:60,overlay,layerOrder};
    }
    return {90,0,0};
}
int mapPickOrder(const ProjectDocument& document,const ObjectRef& ref) {
    const auto group=groupFor(document,ref);
    const auto overlay=overlayIndex(group);
    if(overlay>=0)return 1000-overlay;
    if(group=="labels")return 1300;
    if(group=="rivers"||group=="lakes")return 850;
    if(group=="countries")return 500;
    return 700;
}
int mapBuiltinHydroPickOrder() {return 850;}
}

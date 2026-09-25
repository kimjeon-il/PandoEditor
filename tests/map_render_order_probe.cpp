#include <pandoeditor/maprenderorder.h>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

int main() {
    using namespace pandoeditor;
    ProjectDocument document;
    for(const auto& [id,kind]:std::vector<std::pair<std::string,UnitKind>>{
        {"country",UnitKind::Country},{"subunit",UnitKind::Subunit},{"region",UnitKind::Region}}){
        TerritorialUnit unit;unit.id=id;unit.kind=kind;document.units.push_back(unit);
    }
    for(const auto& type:{"religion","ethnicity","language"}){
        DistributionLayer layer;layer.id=type;layer.type=type;document.distributionLayers.push_back(layer);
        DistributionEntry entry;entry.id=type;entry.layerId=type;document.distributionEntries.push_back(entry);
    }
    for(const auto& kind:{"river","lake"}){HydroFeature hydro;hydro.id=kind;hydro.kind=kind;document.hydro.push_back(hydro);}
    // The web stencil lets a territorial child own its sample before the
    // country fill. A native painter without that stencil must paint country first.
    assert(mapRenderOrder(document,{"territorial","country"},RenderPrimitiveRole::Fill)<
           mapRenderOrder(document,{"territorial","subunit"},RenderPrimitiveRole::Fill));
    struct Draw{std::string name;MapRenderOrder order;};
    std::vector<Draw> draw{
        {"terrain",{-1,0,0}},
        {"territory-fill",mapRenderOrder(document,{"territorial","subunit"},RenderPrimitiveRole::Fill)},
        {"country-fill",mapRenderOrder(document,{"territorial","country"},RenderPrimitiveRole::Fill)},
        {"polygon-overlay",mapRenderOrder(document,{"distributionEntry","religion"},RenderPrimitiveRole::Fill)},
        {"lake",mapBuiltinHydroRenderOrder("lake",RenderPrimitiveRole::Fill)},
        {"lake-boundary",mapBuiltinHydroRenderOrder("lake",RenderPrimitiveRole::Boundary)},
        {"river",mapBuiltinHydroRenderOrder("river",RenderPrimitiveRole::Line)},
        {"border-river",mapBuiltinHydroRenderOrder("river",RenderPrimitiveRole::Line,true)},
        {"country-boundary",mapRenderOrder(document,{"territorial","country"},RenderPrimitiveRole::Boundary)},
        {"stroke-overlay",mapRenderOrder(document,{"generic","generic"},RenderPrimitiveRole::Line)},
    };
    std::stable_sort(draw.begin(),draw.end(),[](const auto& a,const auto& b){return a.order<b.order;});
    struct Pick{std::string name;ObjectRef ref;int rank;};
    std::vector<Pick> pick{
        {"country",{"territorial","country"},0},{"river",{"hydro","river"},0},
        {"lake",{"hydro","lake"},0},
        {"religion",{"distributionEntry","religion"},0},
        {"ethnicity",{"distributionEntry","ethnicity"},0},
        {"language",{"distributionEntry","language"},0},
        {"subunit",{"territorial","subunit"},0},
        {"region",{"territorial","region"},0},
        {"generic",{"generic","generic"},0},
        {"label",{"label","label"},0},
    };
    for(auto& row:pick)row.rank=mapPickOrder(document,row.ref);
    std::stable_sort(pick.begin(),pick.end(),[](const auto& a,const auto& b){return a.rank>b.rank;});
    std::cout<<"{\"visualDraw\":[";
    for(std::size_t i=0;i<draw.size();i++)std::cout<<(i?",\"":"\"")<<draw[i].name<<'"';
    std::cout<<"],\"pick\":[";
    for(std::size_t i=0;i<pick.size();i++)std::cout<<(i?",\"":"\"")<<pick[i].name<<'"';
    std::cout<<"],\"pairs\":[";
    struct Pair{const char* left;const char* right;ObjectRef a,b;RenderPrimitiveRole ar,br;};
    const std::vector<Pair> pairs{
        {"country","lake",{"territorial","country"},{"hydro","lake"},RenderPrimitiveRole::Fill,RenderPrimitiveRole::Fill},
        {"country","river",{"territorial","country"},{"hydro","river"},RenderPrimitiveRole::Fill,RenderPrimitiveRole::Line},
        {"lake","religion",{"hydro","lake"},{"distributionEntry","religion"},RenderPrimitiveRole::Fill,RenderPrimitiveRole::Fill},
        {"religion","ethnicity",{"distributionEntry","religion"},{"distributionEntry","ethnicity"},RenderPrimitiveRole::Fill,RenderPrimitiveRole::Fill},
        {"ethnicity","language",{"distributionEntry","ethnicity"},{"distributionEntry","language"},RenderPrimitiveRole::Fill,RenderPrimitiveRole::Fill},
        {"language","subunit",{"distributionEntry","language"},{"territorial","subunit"},RenderPrimitiveRole::Fill,RenderPrimitiveRole::Fill},
        {"subunit","region",{"territorial","subunit"},{"territorial","region"},RenderPrimitiveRole::Fill,RenderPrimitiveRole::Fill},
        {"region","generic",{"territorial","region"},{"generic","generic"},RenderPrimitiveRole::Fill,RenderPrimitiveRole::Fill},
        {"generic","place",{"generic","generic"},{"label","label"},RenderPrimitiveRole::Fill,RenderPrimitiveRole::Point},
        {"place","label",{"label","label"},{"label","label"},RenderPrimitiveRole::Point,RenderPrimitiveRole::Label},
        {"country","subunit",{"territorial","country"},{"territorial","subunit"},RenderPrimitiveRole::Fill,RenderPrimitiveRole::Fill},
        {"lake","lake-boundary",{"hydro","lake"},{"hydro","lake"},RenderPrimitiveRole::Fill,RenderPrimitiveRole::Boundary},
        {"lake-boundary","river",{"hydro","lake"},{"hydro","river"},RenderPrimitiveRole::Boundary,RenderPrimitiveRole::Line},
        {"river","country-boundary",{"hydro","river"},{"territorial","country"},RenderPrimitiveRole::Line,RenderPrimitiveRole::Boundary},
        {"country-boundary","generic-line",{"territorial","country"},{"generic","generic"},RenderPrimitiveRole::Boundary,RenderPrimitiveRole::Line},
        {"generic-line","place",{"generic","generic"},{"label","label"},RenderPrimitiveRole::Line,RenderPrimitiveRole::Point},
    };
    for(std::size_t i=0;i<pairs.size();++i){
        const auto& p=pairs[i];
        auto order=[&](const ObjectRef& ref,RenderPrimitiveRole role){return ref.domain=="hydro"?
            mapBuiltinHydroRenderOrder(ref.id,role):mapRenderOrder(document,ref,role);};
        const auto top=order(p.a,p.ar)<order(p.b,p.br)?p.right:p.left;
        const auto chooser=p.a.domain==p.b.domain&&p.a.id==p.b.id?"same-ref":
            (p.a.domain=="hydro"?mapBuiltinHydroPickOrder():mapPickOrder(document,p.a))>=
            (p.b.domain=="hydro"?mapBuiltinHydroPickOrder():mapPickOrder(document,p.b))?p.left:p.right;
        std::cout<<(i?",":"")<<"{\"pair\":\""<<p.left<<"/"<<p.right
                 <<"\",\"top\":\""<<top<<"\",\"chooserFirst\":\""<<chooser<<"\"}";
    }
    std::cout<<"]}\n";
}

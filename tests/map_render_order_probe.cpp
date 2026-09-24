#include <pandoeditor/maprenderorder.h>
#include <algorithm>
#include <iostream>
#include <string>
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
    std::cout<<"{\"draw\":[";
    for(std::size_t i=0;i<draw.size();i++)std::cout<<(i?",\"":"\"")<<draw[i].name<<'"';
    std::cout<<"],\"pick\":[";
    for(std::size_t i=0;i<pick.size();i++)std::cout<<(i?",\"":"\"")<<pick[i].name<<'"';
    std::cout<<"]}\n";
}

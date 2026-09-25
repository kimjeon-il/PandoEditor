#include <pandoeditor/gisexchange.h>
#include <pandoeditor/project.h>
#include <iostream>

using namespace pandoeditor;
namespace {
const char* name(GisExchangeTarget target) {
    switch(target) {
    case GisExchangeTarget::Project:return "project";
    case GisExchangeTarget::Country:return "country";
    case GisExchangeTarget::Subunit:return "subunit";
    case GisExchangeTarget::Region:return "region";
    case GisExchangeTarget::Distribution:return "distribution";
    case GisExchangeTarget::Generic:return "generic";
    }
    return "";
}
}
int main() {
    for(const auto* raw:{"admin","territory","administrative","country","unknown"}) {
        const auto target=*normalizeExchangeTarget(raw);
        const auto descriptor=exchangeTargetDescriptor(target);
        std::cout<<"target|"<<raw<<"|"<<name(target)<<"|"<<descriptor.domain<<"|"
                 <<int(descriptor.replaceOnly)<<"|"<<int(descriptor.fallback)<<'\n';
    }
    Project p;p.replace(ProjectDocument({{"A","A",{{{{0,0},{1,0},{1,1},{0,1},{0,0}}}},0}},{{"countries","Countries"}}));
    auto plan=createGisImportPlan(p.snapshot(),"gis-import:1",GisImportKind::Territorial,
        {"test.geojson","geojson"},GisExchangeTarget::Subunit,{"A","A","B"});
    std::cout<<"plan|"<<plan.version<<"|territorial|"<<plan.source.fileName<<"|"
             <<plan.source.sourceKind<<"|";
    for(std::size_t i=0;i<plan.affectedIds.size();++i)
        std::cout<<(i?",":"")<<plan.affectedIds[i];
    std::cout<<'\n';
    bool stale=false;
    try {p.renameCountry("A","Changed");assertCurrentGisImportPlan(p.snapshot(),plan);}
    catch(const std::invalid_argument&){stale=true;}
    std::cout<<"stale|"<<int(stale)<<'\n';
}

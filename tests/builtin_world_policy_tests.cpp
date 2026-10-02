#include "worlddatasetloader.h"
#include "worlddataset.h"
#include "canonicalpacket.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
const pandoeditor::TerritorialUnit* unit(const pandoeditor::ProjectDocument& document,
                                         const std::string& id) {
    const auto found=std::find_if(document.units.begin(),document.units.end(),
        [&](const auto& candidate){return candidate.id==id;});
    return found==document.units.end()?nullptr:&*found;
}
const pandoeditor::TerritorialRelation* baseRelation(
    const pandoeditor::ProjectDocument& document,const std::string& id) {
    const auto ref=pandoeditor::territorialRef(id);
    const auto found=std::find_if(document.relations.begin(),document.relations.end(),
        [&](const auto& candidate){return !candidate.dated&&candidate.unit==ref;});
    return found==document.relations.end()?nullptr:&*found;
}
std::size_t polygonCount(const pandoeditor::ProjectDocument& document,const std::string& id) {
    const auto* item=unit(document,id);
    if(!item)return 0;
    const auto geometry=document.geometries.get(item->geometry);
    return geometry?geometry->polygons.size():0;
}
void check(bool valid,const char* message) {
    if(!valid)throw std::runtime_error(message);
}
}

int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    try {
        const auto loaded=WorldDatasetLoader::canonical(QStringLiteral(PANDOEDITOR_WORLD_ASSET_DIR));
        const auto& document=*loaded.document;
        check(std::count_if(document.units.begin(),document.units.end(),[](const auto& value){
            return value.kind==pandoeditor::UnitKind::Country;
        })==std::ptrdiff_t(207),"fresh default must contain 207 countries");
        check(std::count_if(document.units.begin(),document.units.end(),[](const auto& value){
            return value.kind==pandoeditor::UnitKind::Subunit;
        })==std::ptrdiff_t(47),"fresh default must contain 47 subunits");
        check(document.units.size()==std::size_t(254),"fresh default must contain 254 logical units");

        const std::string greenland="d34b00a1-9b13-8000-8000-00000047524c";
        const auto* item=unit(document,greenland);
        check(item,"Greenland subunit is missing");
        check(item->kind==pandoeditor::UnitKind::Subunit,"Greenland is not a subunit");
        check(item->baseName.empty()&&item->nameExplicit,"subunit has country-only name state");
        check(item->geometry.id=="world-country-GRL","Greenland lost its canonical geometry ID");
        const auto* relation=baseRelation(document,greenland);
        check(relation,"Greenland base relation is missing");
        check(relation->parent==std::optional<pandoeditor::ObjectRef>(pandoeditor::territorialRef("DNK")),
              "Greenland parent must be Denmark");
        check(relation->sovereign==std::optional<pandoeditor::ObjectRef>(pandoeditor::territorialRef("DNK")),
              "Greenland sovereign must be Denmark");

        const auto policy=std::find_if(document.extensions.begin(),document.extensions.end(),[](const auto& value){
            return value.id=="pandoeditor.builtin-territory-policy";
        });
        check(policy!=document.extensions.end(),"pinned built-in territory policy metadata is missing");
        const auto payload=QJsonDocument::fromJson(QByteArray::fromStdString(policy->payload)).object();
        check(payload.value("revision").toString()==QStringLiteral("builtin-subunits-2"),
              "wrong built-in territory policy revision");
        check(payload.value("subunitCount").toInt()==47,"wrong built-in subunit policy count");

        WorldDataset source(QStringLiteral(PANDOEDITOR_WORLD_ASSET_DIR));
        CanonicalCountryStore original(source.decompress("countryCanonical",12*1024*1024));
        const auto ids=original.ids();
        const auto originalPolygons=[&](const std::string& id) {
            const auto found=std::find(ids.begin(),ids.end(),id);
            if(found==ids.end())throw std::runtime_error("missing canonical country");
            return original.materializeGeometry(std::size_t(found-ids.begin())).polygons.size();
        };
        for(const auto& pair:std::vector<std::pair<std::string,std::string>>{
                {"BRI","BRA"},{"BJN","COL"},{"SER","COL"},{"SCR","CHN"}}) {
            check(!unit(document,pair.first),"merged source remained a logical country");
        }
        check(polygonCount(document,"BRA")==originalPolygons("BRA")+originalPolygons("BRI"),
              "Brazil merge lost source polygons");
        check(polygonCount(document,"COL")==
              originalPolygons("COL")+originalPolygons("BJN")+originalPolygons("SER"),
              "Colombia merge lost source polygons");
        check(polygonCount(document,"CHN")==originalPolygons("CHN")+originalPolygons("SCR"),
              "China merge lost source polygons");
        std::cout<<"builtin world policy tests passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"builtin world policy test failed: "<<error.what()<<'\n';
        return 1;
    }
}

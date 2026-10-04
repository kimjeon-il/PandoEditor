#include "territorial_fixture.h"
#include <pandoeditor/presentationcommands.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <iostream>
using namespace pandoeditor;
int main(){
    std::string line;while(std::getline(std::cin,line))try{
        auto input=QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        ProjectDocument d({{"A","A",{{{{0,0},{10,0},{10,10},{0,10},{0,0}}}},0x112233}},{{"countries","Countries"}});
        for(auto id:{"S","T"}){auto u=d.units.front();u.id=id;u.kind=UnitKind::General;appendTerritory(d,u,staticGeometryBinding(d,d.units.front().id).geometryRef,u.kind==UnitKind::General?"A":"");d.presentation.membership[territorialRef(id)]="countries";d.presentation.objectStyles[territorialRef(id)]={};setFixtureParent(d,territorialRef(id),territorialRef("A"));}
        auto& w=d.presentation.webPresentation;w.visibility["subunits"]=input["master"].toBool(true);
        w.styles["countries"].opacity=input["countryOpacity"].toDouble(1);w.styles["subunits"].opacity=input["groupOpacity"].toDouble(1);
        w.styles["countries"].blendMode=input["countryBlend"].toString("normal").toStdString();
        if(input.contains("objectOpacity"))w.objectStyles["territorial:entity:S"].opacity=input["objectOpacity"].toDouble();
        if(input.contains("objectBlend"))w.objectStyles["territorial:entity:S"].blendMode=input["objectBlend"].toString().toStdString();
        Project p;p.replace(d);
        if(input.contains("scoped"))PresentationCommandProcessor::apply(p,SetScopedVisibility{"subunits",{territorialRef("S")},input["scoped"].toBool()});
        auto r=resolvedTerritorialPresentation(p.document(),territorialRef("S"));
        const auto& out=p.document().presentation.webPresentation;
        QJsonObject result{{"master",groupVisible(out,"subunits")},{"S",itemVisible(out,"subunits","S")},{"T",itemVisible(out,"subunits","T")},{"opacity",r.opacity},{"blendMode",QString::fromStdString(r.blendMode)}};
        std::cout<<QJsonDocument(result).toJson(QJsonDocument::Compact).constData()<<'\n';
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

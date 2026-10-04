#include "territorial_fixture.h"
#include "territorialgeometry.h"
#include <pandoeditor/project.h>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <iostream>
#include <iterator>
using namespace pandoeditor;
Geometry decode(const QJsonObject& value) {
    Geometry g;g.type=value["type"].toString().toStdString();
    auto polygons=value["coordinates"].toArray();if(g.type=="Polygon")polygons=QJsonArray{polygons};
    for(auto p:polygons){Polygon poly;for(auto r:p.toArray()){Ring ring;for(auto v:r.toArray()){auto xy=v.toArray();ring.push_back({xy[0].toDouble(),xy[1].toDouble()});}poly.push_back(ring);}g.polygons.push_back(poly);}return g;
}
QJsonObject encode(const Geometry& g) {
    QJsonArray polygons;for(const auto& poly:g.polygons){QJsonArray rings;for(const auto& ring:poly){QJsonArray points;for(auto p:ring)points.append(QJsonArray{p.x,p.y});rings.append(points);}polygons.append(rings);}
    return {{"type","MultiPolygon"},{"coordinates",polygons}};
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    const std::string input{std::istreambuf_iterator<char>(std::cin),{}};
    QJsonArray output;
    for(auto v:QJsonDocument::fromJson(QByteArray::fromStdString(input)).array()) {
        const auto row=v.toObject();QJsonObject result{{"case",row["case"]}};
        try {
            ProjectDocument d;d.documentId="m4-oracle";
            const auto add=[&](const QJsonObject& feature,bool country){
                const auto id=feature["id"].toString().toStdString();const auto props=feature["properties"].toObject();
                GeometryRef gr{id,1};d.geometries.insert(gr,decode(feature["geometry"].toObject()));
                appendTerritory(d,{id,id,"",country?UnitKind::General:UnitKind::General,props["locked"].toBool()},gr);
                staticParentRelation(d,d.units.back().id).coverageMode=country?"explicit":"partition";d.presentation.objectStyles[territorialRef(id)]={};
                if(!country)setFixtureParent(d,territorialRef(id),territorialRef(props["parentId"].toString().toStdString()));
            };
            for(auto f:row["countries"].toArray())add(f.toObject(),true);
            for(auto f:row["units"].toArray())add(f.toObject(),false);
            Project p;p.replace(d);const auto target=territorialRef(row["targetId"].toString().toStdString());
            TerritorialMutationIntent intent=TransferSubunitIntent{target,territorialRef(row["countryId"].toString().toStdString())};
            if(row["operation"]=="promote")intent=ConvertTerritorialTypeIntent{target,UnitKind::General,{},{},{}};
            if(row["operation"]=="convert")intent=ConvertTerritorialTypeIntent{target,UnitKind::General,{},territorialRef("B"),target.id};
            if(row["operation"]=="merge") {std::vector<ObjectRef> donors;for(const auto& id:row["sourceIds"].toArray())donors.push_back(territorialRef(id.toString().toStdString()));intent=MergeTerritorialIntent{target,std::move(donors)};}
            if(row["operation"]=="annex"||row["operation"]=="drawn-annex") {
                std::vector<ObjectRef> donors;
                for(const auto& id:row["sourceIds"].toArray())donors.push_back(territorialRef(id.toString().toStdString()));
                if(donors.empty())donors.push_back(territorialRef(row["sourceId"].toString().toStdString()));
                intent=AnnexTerritoryIntent{target,std::move(donors),decode(row["draft"].toObject())};
            }
            if(row["operation"]=="country-boundary") {SharedBoundaryIntent boundary;for(const auto& value:row["featurePatches"].toArray()){const auto feature=value.toObject();boundary.drafts.push_back({territorialRef(feature["id"].toString().toStdString()),decode(feature["geometry"].toObject())});}intent=std::move(boundary);}
            if(row["operation"]=="coast")intent=CoastlineIntent{target,decode(row["draft"].toObject()),CoastlineAuthority::Country};
            const auto plan=CommandProcessor::planTerritorial(p,intent);
            if(!plan.ok()&&row["operation"]!="drawn-annex")throw std::runtime_error(plan.detail);
            JobScheduler jobs;auto job=jobs.enqueue(p.snapshot(),"oracle");jobs.takeNext();
            PrepareResult prepared;
            if(row["operation"]=="drawn-annex")prepared=prepareDrawnTerritoryAnnex(p.snapshot(),std::get<AnnexTerritoryIntent>(intent),job.token());
            else if(plan.plan->geometry.kind==GeometryRequirementKind::WorkerPatch)prepared=prepareTerritorialGeometry(p.snapshot(),*plan.plan,job.token());
            else {CommandArguments args;args.action=ApplyTerritorialMutation{*plan.plan,{}};prepared=CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,"territorial.relation.parent",args));}
            if(!prepared.ok()||!prepared.preview)throw std::runtime_error(prepared.detail);
            const auto applied=CommandProcessor::confirm(p,*prepared.preview);if(!applied.ok())throw std::runtime_error(applied.detail);
            QJsonArray features;
            for(const auto& unit:p.document().units) {
                const auto& relation=staticParentRelation(p.document(),unit.id);
                QJsonObject props{{"unitType",isRootGeneral(p.document(),unit)?"country":"subunit"},
                    {"parentId",QString::fromStdString(relation.parentId)}};
                features.append(QJsonObject{{"id",QString::fromStdString(unit.id)},{"geometry",encode(*p.document().geometries.get(staticGeometryBinding(p.document(),unit.id).geometryRef))},{"properties",props}});
            }
            result["ok"]=true;result["features"]=features;
        }catch(const std::exception& error){result["ok"]=false;result["error"]=QString::fromUtf8(error.what());}
        output.append(result);
    }
    std::cout<<QJsonDocument(output).toJson(QJsonDocument::Compact).toStdString();
}

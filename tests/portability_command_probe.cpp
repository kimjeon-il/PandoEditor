#include <pandoeditor/project.h>
#include "territorial_fixture.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
using namespace pandoeditor;
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) return 2;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(input.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return 3;
    const auto corpus = document.object();
    const bool properties=corpus["schema"]=="web-app-property-commands";
    const bool structure=corpus["schema"]=="web-app-structure-commands";
    if ((!structure&&!properties&&corpus["schema"] != "web-app-command-history") || corpus["version"] != 1) return 4;
    const auto initial = corpus["initial"].toObject();
    const auto id = initial["id"].toString().toStdString();
    Project project;
    project.replace(ProjectDocument({{id, initial["name"].toString().toStdString(),
        {{{{0,0},{8,0},{8,8},{0,8},{0,0}}}}, 0xcccccc}}, {{"countries","Countries"}}));
    if(structure) {
        auto seed=project.document();seed.units.clear();seed.timelineRecords={};
        const GeometryRef geometry{"parity-shape",1};Geometry shape;shape.type="Polygon";shape.polygons={{{{0,0},{8,0},{8,8},{0,8},{0,0}}}};seed.geometries.insert(geometry,shape);
        for(const auto value:initial["entities"].toArray()) {
            const auto row=value.toObject();const auto key=row["id"].toString().toStdString();
            appendTerritory(seed,{key,row["name"].toString().toStdString(),"",row["kind"]=="regional"?UnitKind::Regional:UnitKind::General,row["locked"].toBool()},geometry,row["parent"].toString().toStdString());
            seed.presentation.membership[territorialRef(key)]="countries";
            seed.presentation.objectStyles[territorialRef(key)]={};
        }
        project.replace(std::move(seed));
    }
    QJsonArray observations;
    for (const auto value : corpus["operations"].toArray()) {
        const auto operation = value.toObject();
        const auto op = operation["op"].toString();
        if(structure) {
            CommandResult result;
            const auto target=territorialRef(operation["target"].toString().toStdString());
            if(op=="undo"||op=="redo")result.status=(op=="undo"?project.undo():project.redo())?CommandStatus::Applied:CommandStatus::Rejected;
            else if(op=="canDelete")result.status=CommandProcessor::planTerritorial(project,DeleteTerritorialIntent{{target}}).ok()?CommandStatus::NoOp:CommandStatus::Rejected;
            else {
                CommandArguments args;std::string command;
                if(op=="parent") {
                    auto plan=CommandProcessor::planTerritorial(project,ChangeParentIntent{target,territorialRef(operation["parent"].toString().toStdString())});
                    if(plan.ok()){args.action=ApplyTerritorialMutation{*plan.plan,{}};command="territorial.relation.parent";}
                } else if(op=="lock"){args.action=TerritorialLockEdit{{target},operation["value"].toBool()};command="territorial.lock";}
                else if(op=="lockBatch"){
                    std::vector<ObjectRef> targets;
                    for(const auto value:operation["targets"].toArray())targets.push_back(territorialRef(value.toString().toStdString()));
                    args.action=TerritorialLockEdit{targets,operation["value"].toBool()};command="territorial.lock";
                }
                else return 5;
                if(!command.empty()) {
                    auto prepared=CommandProcessor::prepare(project,CommandProcessor::makeRequest(project,command,std::move(args)));
                    result=prepared.preview?CommandProcessor::confirm(project,*prepared.preview):CommandResult{prepared.status,prepared.error,prepared.detail};
                }
            }
            QJsonArray entities;
            for(const auto& unit:project.document().units)entities.append(QJsonObject{{"id",QString::fromStdString(unit.id)},
                {"parent",QString::fromStdString(staticParentRelation(project.document(),unit.id).parentId)},{"locked",unit.locked}});
            observations.append(QJsonObject{{"ok",result.ok()},{"changed",result.changed()},{"entities",entities},{"undo",project.canUndo()},{"redo",project.canRedo()}});continue;
        }
        if(properties) {
            CommandResult result;
            if(op=="undo"||op=="redo") {
                const bool changed=op=="undo"?project.undo():project.redo();
                result.status=changed?CommandStatus::Applied:CommandStatus::Rejected;
            } else {
                CommandArguments args;std::string command;
                const auto target=territorialRef(operation["target"].toString().toStdString());
                if(op=="lock") {args.action=TerritorialLockEdit{{target},operation["value"].toBool()};command="territorial.lock";}
                else if(op=="field") {
                    const auto field=operation["field"].toString();
                    if(field!="name"&&field!="notes"&&field!="validFrom")return 5;
                    args.action=TerritorialFieldEdit{target,field=="name"?TerritorialField::Name:field=="notes"?TerritorialField::Notes:TerritorialField::ValidFrom,operation["value"].toString().toStdString()};command="territorial.field";
                } else return 5;
                auto prepared=CommandProcessor::prepare(project,CommandProcessor::makeRequest(project,command,std::move(args)));
                result=prepared.preview?CommandProcessor::confirm(project,*prepared.preview):CommandResult{prepared.status,prepared.error,prepared.detail};
            }
            const auto& unit=project.document().units.front();
            observations.append(QJsonObject{{"ok",result.ok()},{"changed",result.changed()},
                {"name",QString::fromStdString(unit.name)},{"notes",QString::fromStdString(unit.notes)},
                {"locked",unit.locked},{"undo",project.canUndo()},{"redo",project.canRedo()}});
            continue;
        }
        if (op == "rename") project.renameCountry(operation["target"].toString().toStdString(), operation["value"].toString().toStdString());
        else if (op == "markSaved") project.markSaved();
        else if (op == "undo") project.undo();
        else if (op == "redo") project.redo();
        else if (op != "observe") return 5;
        observations.append(QJsonObject{{"name", QString::fromStdString(project.country(id)->name)},
            {"undo", project.canUndo()}, {"redo", project.canRedo()}, {"dirty", project.dirty()}});
    }
    std::cout << QJsonDocument(observations).toJson(QJsonDocument::Compact).constData();
}

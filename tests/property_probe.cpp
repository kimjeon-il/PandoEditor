#include "territorial_fixture.h"
#include "projectcodec.h"
#include <pandoeditor/project.h>
#include <pandoeditor/objectproperties.h>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QJSEngine>
#include <iostream>
using namespace pandoeditor;
static QJsonObject state(const Project& p){
 QJsonArray units;for(const auto& u:p.document().units){auto ref=territorialRef(u.id);auto style=p.document().presentation.objectStyles.at(ref);
  units.append(QJsonObject{{"id",QString::fromStdString(u.id)},{"name",QString::fromStdString(objectDisplayName(u))},{"rawName",QString::fromStdString(u.name)},
   {"notes",QString::fromStdString(u.notes)},{"hasName",u.kind==UnitKind::General?u.nameExplicit:true},{"color",QString("#%1").arg(effectiveObjectColor(p.document(),ref),6,16,QChar('0'))},
   {"explicit",style.explicitColor},{"locked",u.locked},{"from",staticLifetime(p.document(),u.id).validity.from?QJsonValue(QString::fromStdString(*staticLifetime(p.document(),u.id).validity.from)):QJsonValue()},
   {"to",staticLifetime(p.document(),u.id).validity.to?QJsonValue(QString::fromStdString(*staticLifetime(p.document(),u.id).validity.to)):QJsonValue()}});
 }return {{"units",units},{"undo",p.canUndo()},{"redo",p.canRedo()}};
}
static QJsonArray scenario(const QJsonArray& ops){
 ProjectDocument d({{"A","Alpha",{{{{0,0},{8,0},{8,8},{0,8},{0,0}}}},0xcccccc}},{{"countries","Countries"}});
 d.units[0].name="";d.units[0].nameExplicit=false;d.presentation.objectStyles[territorialRef("A")]={0,1,false};
 for(auto id:{"S","R"}){appendTerritory(d,{id,id,"",std::string(id)=="S"?UnitKind::General:UnitKind::Regional,false},staticGeometryBinding(d,d.units[0].id).geometryRef);d.presentation.membership[territorialRef(id)]="countries";d.presentation.objectStyles[territorialRef(id)]={0,1,false};if(std::string(id)=="S")setFixtureParent(d,territorialRef(id),territorialRef("A"));}
 Project p;p.replace(d);QJsonArray result;
 for(auto item:ops){auto op=item.toObject();auto type=op["op"].toString();bool ok=true;
  if(type=="undo")ok=p.undo();else if(type=="redo")ok=p.redo();else {
   CommandArguments args;std::string command;
   std::vector<ObjectRef> refs;for(auto id:op["ids"].toArray())refs.push_back(territorialRef(id.toString().toStdString()));
   if(type=="field"){
    auto f=op["field"].toString();auto kind=f=="name"?TerritorialField::Name:f=="notes"?TerritorialField::Notes:f=="validFrom"?TerritorialField::ValidFrom:TerritorialField::ValidTo;
    args.action=TerritorialFieldEdit{refs.at(0),kind,op["value"].toString().toStdString()};command="territorial.field";
   }else if(type=="lock"){args.action=TerritorialLockEdit{refs,op["value"].toBool()};command="territorial.lock";}
   else {std::optional<std::uint32_t> color;if(!op["value"].isNull())color=op["value"].toString().mid(1).toUInt(nullptr,16);args.action=TerritorialColorEdit{refs,color};command=refs.size()>1?"territorial.batch-color":color?"territorial.color":"territorial.color.reset";}
   auto prepared=CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,command,args));ok=prepared.ok();if(prepared.preview)ok=CommandProcessor::confirm(p,*prepared.preview).ok();
  }
  auto out=state(p);out["ok"]=ok;result.append(out);
 }return result;
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);QJSEngine js;QFile math(QStringLiteral(M32_ROOT)+"/ui/common/ColorMath.js");if(!math.open(QIODevice::ReadOnly))return 2;auto script=math.readAll();script.replace(".pragma library","");if(js.evaluate(script).isError())return 3;
 QFile input;if(!input.open(stdin,QIODevice::ReadOnly))return 4;const auto cases=QJsonDocument::fromJson(input.readAll()).array();QJsonArray answers;
 for(auto value:cases){auto c=value.toObject();auto kind=c["kind"].toString();if(kind=="math"){
   QJSValueList args;for(auto a:c["args"].toArray())args.append(js.toScriptValue(a.toVariant()));auto result=js.globalObject().property(c["fn"].toString()).call(args);if(result.isError())return 5;answers.append(QJsonValue::fromVariant(result.toVariant()));
  }else if(kind=="trim")answers.append(QString::fromStdString(trimWebText(c["value"].toString().toStdString())));
  else if(kind=="scenario")answers.append(scenario(c["ops"].toArray()));
  else {auto from=trimWebText(c["from"].toString().toStdString()),to=trimWebText(c["to"].toString().toStdString());Validity v;if(!from.empty())v.from=from;if(!to.empty())v.to=to;
   try{temporalBounds(v);answers.append(QJsonObject{{"ok",true},{"from",v.from?QJsonValue(QString::fromStdString(*v.from)):QJsonValue()},{"to",v.to?QJsonValue(QString::fromStdString(*v.to)):QJsonValue()}});}catch(...){answers.append(QJsonObject{{"ok",false}});}
  }
 }std::cout<<QJsonDocument(answers).toJson(QJsonDocument::Compact).constData();return 0;
}

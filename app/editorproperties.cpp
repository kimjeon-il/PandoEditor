#include "editorcontroller.h"
#include "defaultflagresolver.h"
#include "webjson.h"
#include <pandoeditor/temporal.h>
#include <QScopedValueRollback>
#include <QRegularExpression>
#include <QUuid>
#include <algorithm>
using namespace pandoeditor;
namespace {
QString q(const std::string& v){return QString::fromStdString(v);}
QString hex(std::uint32_t v){return QString("#%1").arg(v,6,16,QChar('0'));}
QString periodText(const std::optional<std::string>& from,const std::optional<std::string>& to) {
 if(!from&&!to)return {};
 return (q(from.value_or(""))+QStringLiteral(" ~ ")+q(to.value_or(""))).trimmed();
}
std::optional<TerritorialField> fieldKind(const QString& field) {
 if(field=="name")return TerritorialField::Name;if(field=="notes")return TerritorialField::Notes;
 if(field=="validFrom")return TerritorialField::ValidFrom;if(field=="validTo")return TerritorialField::ValidTo;
 return {};
}
}
const TerritorialUnit* EditorController::selectedUnit() const {
 if(selection_.primary() && selection_.primary()->domain!="territorial") return nullptr;
 const auto pos=project_.index().objects.find(territorialRef(selectedId().toStdString()));
 return pos==project_.index().objects.end()?nullptr:&project_.document().units.at(pos->second);
}
bool EditorController::propertyBusy() const {return jobBusy() || webImportBusy() || hasWebImportPreview();}
bool EditorController::selectedEditable() const {
 const auto u=selectedUnit();if(!u||selection_.items().size()!=1)return false;
 const auto member=project_.document().presentation.membership.find(territorialRef(u->id));
 const auto layer=member==project_.document().presentation.membership.end()?nullptr:project_.layer(member->second);
 return (!layer || !layer->locked) && !u->locked;
}
QVariantMap EditorController::objectProperties() const {
 if(!objectPropertiesCache_)objectPropertiesCache_=computeObjectProperties();
 return *objectPropertiesCache_;
}
QVariantMap EditorController::computeObjectProperties() const {
 QVariantMap result{{"count",int(selection_.items().size())},{"busy",propertyBusy()}};
 if(std::any_of(selection_.items().begin(),selection_.items().end(),[this](const auto& ref){if(ref.domain=="placeBuiltin")return !placeRuntime_.recordById(q(ref.id)).has_value();return ref.domain=="hydroBuiltin"?
   !hydroRuntime_.recordById(q(ref.id)).has_value():!project_.index().objects.count(ref);})) {
  result["count"]=0;result["lockEnabled"]=false;result["colorEnabled"]=false;result["editable"]=false;
  return result;
 }
 if(std::any_of(selection_.items().begin(),selection_.items().end(),[](const auto& ref){return ref.domain!="territorial";})) {
  result["lockEnabled"]=false;result["colorEnabled"]=false;result["editable"]=false;
  if(selection_.items().size()==1){const auto& ref=selection_.items().front();const auto view=project_.propertyView(ref);result["id"]=q(ref.id);result["type"]=q(ref.domain);if(view)result["displayName"]=q(view->displayName);
   if(ref.domain=="hydroBuiltin")if(const auto record=hydroRuntime_.recordById(q(ref.id)))result["displayName"]=record->name;
   if(ref.domain=="placeBuiltin")if(const auto record=placeRuntime_.recordById(q(ref.id))){result["displayName"]=record->name;result["sourceName"]=record->name;result["kind"]=record->kind;result["builtinSource"]=true;result["copyAvailable"]=objectVisible(ref)&&!propertyBusy();result["locked"]=true;}}
  return result;
 }
 bool allLocked=!selection_.items().empty(),anyLocked=false,layerLocked=false;
 for(const auto& ref:selection_.items()){
  const auto& u=project_.document().units.at(project_.index().objects.at(ref));allLocked=allLocked&&u.locked;anyLocked=anyLocked||u.locked;
  auto layer=project_.layer(nativeLayerId(project_.document(),ref));layerLocked=layerLocked||(layer&&layer->locked);
 }
 result["allLocked"]=allLocked;result["someLocked"]=anyLocked&&!allLocked;result["layerLocked"]=layerLocked;
 result["lockEnabled"]=!selection_.items().empty()&&!propertyBusy()&&!layerLocked;
 result["lockLabel"]=allLocked?QStringLiteral("잠금 해제"):selection_.items().size()>1?QStringLiteral("모두 잠금"):QStringLiteral("잠금");
 result["colorEnabled"]=!propertyBusy()&&!layerLocked&&!selection_.items().empty()&&(selection_.items().size()>1||!anyLocked);
 const auto u=selectedUnit();if(!u||selection_.items().size()!=1)return result;
 const auto ref=territorialRef(u->id);const auto& style=project_.document().presentation.objectStyles.at(ref);
 result["id"]=q(u->id);result["type"]=u->kind==UnitKind::General?"general":"regional";
 result["displayName"]=q(objectDisplayName(*u));result["namePending"]=nameDraft_!=q(u->kind==UnitKind::General?objectDisplayName(*u):u->name);result["nameDraft"]=nameDraft_;result["notesDraft"]=memoDraft_;
 result["color"]=hex(effectiveObjectColor(project_.document(),ref));result["colorExplicit"]=style.explicitColor;
 result["defaultColor"]=hex(effectiveObjectColor(project_.document(),ref,0xcccccc,0x8c68d8,true));
 result["colorLabel"]=style.explicitColor?result["color"].toString().toUpper():!staticParentRelation(project_.document(),u->id).parentId.empty()?QStringLiteral("상위 색상 상속"):QStringLiteral("기본 색상");
 result["locked"]=u->locked;result["editable"]=selectedEditable()&&!propertyBusy();result["dateFields"]=false;
 result["validFrom"]=validFromDraft_;result["validTo"]=validToDraft_;
 const auto& lifetime=staticLifetime(project_.document(),u->id).validity;
 result["periodInput"]=periodText(lifetime.from,lifetime.to);
 const auto& relation=staticParentRelation(project_.document(),u->id);
 result["parentId"]=q(relation.parentId);result["parentName"]=QString();
 if(!relation.parentId.empty()){const auto view=project_.propertyView(territorialRef(relation.parentId));if(view)result["parentName"]=q(view->displayName);}
 // Fixed Web a1555722 derives information rows from the live canonical parent
 // graph. Source catalog lineage and metadata never infer project ownership.
 const auto relationRow=[&](const TerritorialUnit& related) {
  const auto relatedRef=territorialRef(related.id);auto row=objectRefValue(relatedRef);
  const auto view=project_.propertyView(relatedRef);row["name"]=view?q(view->displayName):q(related.id);
  row["ref"]=objectRefValue(relatedRef);row["flagSource"]=resolveDefaultFlag(project_.document(),relatedRef).source;
  return row;
 };
 QVariantList parentRows,childRows;
 for(const auto& related:project_.document().units) {
  if(!relation.parentId.empty()&&related.id==relation.parentId)parentRows.push_back(relationRow(related));
  if(staticParentRelation(project_.document(),related.id).parentId==u->id)childRows.push_back(relationRow(related));
 }
 result["parentRows"]=parentRows;result["childRows"]=childRows;
 bool conflict=false;const auto normalized=q(trimWebText(u->name)).toLower();
 for(const auto& other:project_.document().units)if(other.id!=u->id&&other.kind==u->kind&&staticParentRelation(project_.document(),other.id).parentId==relation.parentId&&q(trimWebText(other.name)).toLower()==normalized){conflict=true;break;}
 result["nameConflict"]=conflict;return result;
}
void EditorController::setValidFromDraft(const QString& value){if(!selectionTransition_&&selectedUnit()&&selectedUnit()->kind==UnitKind::Regional){cancelPreview();validFromDraft_=value;emit draftsChanged();emit dirtyChanged();}}
void EditorController::setValidToDraft(const QString& value){if(!selectionTransition_&&selectedUnit()&&selectedUnit()->kind==UnitKind::Regional){cancelPreview();validToDraft_=value;emit draftsChanged();emit dirtyChanged();}}
void EditorController::refreshDraftField(const ObjectRef& ref,const QString& field) {
 const auto it=parkedCountryDrafts_.find(q(ref.id));if(it==parkedCountryDrafts_.end())return;
 it->second.fields.erase(field.toStdString());
 if(it->second.fields.empty())parkedCountryDrafts_.erase(it);
}
bool EditorController::runPropertyCommand(const std::string& id,CommandAction action,const QString& changedField) {
 if(propertyBusy()){emit errorOccurred("PROJECT_BUSY: 현재 작업을 마친 뒤 변경하세요.");return false;}
 try {
  CommandArguments args;args.action=std::move(action);auto request=CommandProcessor::makeRequest(project_,id,std::move(args));
  auto result=CommandProcessor::prepare(project_,request);
  if(!result.ok()){emit errorOccurred(QString::fromLatin1(commandErrorCode(result.error))+": "+q(result.detail));return false;}
  parkDrafts();
  if(result.preview){
   auto confirmed=CommandProcessor::confirm(project_,*result.preview);
   if(!confirmed.ok()){emit errorOccurred(QString::fromLatin1(commandErrorCode(confirmed.error)));return false;}
   noteAppliedImpact(confirmed.impact);
   cancelPreview();
  }
  for(const auto& ref:request.targets)if(!changedField.isEmpty())refreshDraftField(ref,changedField);
  publish(false);return true;
 }catch(const std::exception& e){emit errorOccurred("PROPERTY_FAILED: "+QString::fromUtf8(e.what()));return false;}
}
bool EditorController::commitObjectField(const QString& field) {
 if(fieldCommitInProgress_ || selectionTransition_ || selection_.items().size()!=1)return false;
 const auto kind=fieldKind(field);const auto u=selectedUnit();if(!kind||!u)return false;
 QScopedValueRollback<bool> guard(fieldCommitInProgress_,true);
 const auto target=territorialRef(u->id);const bool regionOrSubunit=u->kind!=UnitKind::General;
 const auto value=field=="name"?nameDraft_:field=="notes"?memoDraft_:field=="validFrom"?validFromDraft_:validToDraft_;
 const bool ok=runPropertyCommand("territorial.field",TerritorialFieldEdit{target,*kind,value.toStdString()},field);
 if(!ok && regionOrSubunit){
  // Web metadata rejection re-presents saved field values; other field drafts survive.
  parkDrafts();refreshDraftField(target,field);QScopedValueRollback<bool> rebinding(selectionTransition_,true);
  reloadDrafts();emit draftsChanged();emit dirtyChanged();
 }
 return ok;
}
QVariantMap EditorController::parseTerritorialPeriodInput(const QString& value) const {
 // Exact a1555722 input contract: one '~', or wholly empty input. The existing
 // temporal normalizer preserves endpoint precision and inclusive/open bounds.
 try {
  const auto input=webjson::jsTrim(value);
  const auto parts=input.isEmpty()?QStringList{QString(),QString()}:input.split(QChar('~'),Qt::KeepEmptyParts);
  if(parts.size()!=2)throw std::invalid_argument("PL-EDITOR-PERIOD: 존속기간의 시작과 끝을 ~ 하나로 구분하세요.");
  const auto endpoint=[](const QString& text)->std::optional<std::string> {
   const auto clean=webjson::jsTrim(text);return clean.isEmpty()?std::nullopt:std::optional<std::string>(clean.toStdString());
  };
  const auto interval=normalizeTemporalInterval(endpoint(parts[0]),endpoint(parts[1]));
  return {{"ok",true},{"validFrom",interval.validFrom?QVariant(q(*interval.validFrom)):QVariant()},
      {"validTo",interval.validTo?QVariant(q(*interval.validTo)):QVariant()},
      {"formatted",periodText(interval.validFrom,interval.validTo)}};
 }catch(const std::exception& error){return {{"ok",false},{"error",QString::fromUtf8(error.what())}};}
}
bool EditorController::commitTerritorialPeriod(const QString& token,const QString& input) {
 const auto found=fieldSessions_.find(token);if(found==fieldSessions_.end()||found->second.field!="validity")return false;
 const auto session=found->second;fieldSessions_.erase(found);refreshTypedScene();
 if(fieldCommitInProgress_||selectionTransition_||selection_.items().size()!=1||
    !session.base.matches(project_)||session.ref!=selection_.items().front()||!selectedEditable())return false;
 if(propertyBusy()){emit errorOccurred("PROJECT_BUSY: 현재 작업을 마친 뒤 변경하세요.");return false;}
 const auto parsed=parseTerritorialPeriodInput(input);
 if(!parsed.value("ok").toBool()){emit errorOccurred(parsed.value("error").toString());return false;}
 QScopedValueRollback<bool> guard(fieldCommitInProgress_,true);
 try {
  CommandArguments args;
  args.properties.fields={{session.ref,TerritorialField::ValidFrom,parsed.value("validFrom").toString().toStdString()},
      {session.ref,TerritorialField::ValidTo,parsed.value("validTo").toString().toStdString()}};
  // Both endpoints enter one unpublished candidate. Existing applyField rejects
  // any dated static edit with TIMELINE_ACTIVATION; no temporal editing is added.
  auto prepared=CommandProcessor::prepare(project_,CommandProcessor::makeRequest(project_,"edit.properties",std::move(args)));
  if(!prepared.ok()){emit errorOccurred(QString::fromLatin1(commandErrorCode(prepared.error))+": "+q(prepared.detail));return false;}
  if(prepared.preview) {
   const auto confirmed=CommandProcessor::confirm(project_,*prepared.preview);
   if(!confirmed.ok()){emit errorOccurred(QString::fromLatin1(commandErrorCode(confirmed.error)));return false;}
   noteAppliedImpact(confirmed.impact);
  }
  refreshDraftField(session.ref,"validFrom");refreshDraftField(session.ref,"validTo");
  publish(false);return true;
 }catch(const std::exception& error){emit errorOccurred("PROPERTY_FAILED: "+QString::fromUtf8(error.what()));return false;}
}
QString EditorController::beginPropertyEdit(const QString& field) {
 const auto u=selectedUnit();if(!u||selection_.items().size()!=1||(!fieldKind(field)&&field!="validity"))return {};
 if(field=="validity"&&(!selectedEditable()||propertyBusy()))return {};
 if((field=="validFrom"||field=="validTo")&&u->kind!=UnitKind::Regional)return {};
 const auto token=QUuid::createUuid().toString(QUuid::WithoutBraces);
 fieldSessions_.emplace(token,FieldSession{project_.snapshot(),territorialRef(u->id),field});refreshTypedScene();return token;
}
bool EditorController::updatePropertyEdit(const QString& token,const QString& value) {
 auto it=fieldSessions_.find(token);if(it==fieldSessions_.end()||!it->second.base.matches(project_)||it->second.ref.id!=selectedId().toStdString())return false;
 const auto field=it->second.field;
 if(field=="validity")return false; // Its complete input is owned by the QML field.
 if(field=="name")setNameDraft(value);else if(field=="notes")setMemoDraft(value);else if(field=="validFrom")setValidFromDraft(value);else setValidToDraft(value);
 return true;
}
bool EditorController::confirmPropertyEdit(const QString& token) {
 auto it=fieldSessions_.find(token);if(it==fieldSessions_.end())return false;
 const auto session=it->second;fieldSessions_.erase(it);
 refreshTypedScene();
 if(!session.base.matches(project_) || session.ref.id!=selectedId().toStdString())return false;
 return commitObjectField(session.field);
}
void EditorController::endPropertyEdit(const QString& token){if(fieldSessions_.erase(token))refreshTypedScene();}
bool EditorController::beginColorEdit() {
 if(!objectProperties()["colorEnabled"].toBool())return false;
 colorSession_=project_.snapshot();colorTargets_=selection_.items();emit colorEditChanged();return true;
}
void EditorController::cancelColorEdit(){screenColorPicker_.cancel();if(colorSession_){colorSession_.reset();colorTargets_.clear();emit colorEditChanged();}}
bool EditorController::confirmColorEdit(const QString& color,bool reset) {
 if(!colorSession_ || !colorSession_->matches(project_) || colorTargets_!=selection_.items()){
  cancelColorEdit();emit errorOccurred("STALE_COLOR_SESSION: 편집 대상이 변경되었습니다.");return false;
 }
 const auto refs=colorTargets_;if(reset&&refs.size()!=1)return false;
 static const QRegularExpression pattern("^#[0-9a-fA-F]{6}$");
 if(!reset&&!pattern.match(color).hasMatch()){emit errorOccurred("INVALID_COLOR: 6자리 HEX 색상이 필요합니다.");return false;}
 const std::string command=reset?"territorial.color.reset":refs.size()>1?"territorial.batch-color":"territorial.color";
 return runPropertyCommand(command,TerritorialColorEdit{refs,reset?std::nullopt:std::optional<std::uint32_t>(color.mid(1).toUInt(nullptr,16))},"color");
}
bool EditorController::toggleObjectLock() {
 const auto state=objectProperties();if(!state["lockEnabled"].toBool())return false;
 return runPropertyCommand("territorial.lock",TerritorialLockEdit{selection_.items(),!state["allLocked"].toBool()});
}

#include "editorcontroller.h"
#include <QScopedValueRollback>
#include <QRegularExpression>
#include <QUuid>
#include <algorithm>
using namespace pandoeditor;
namespace {
QString q(const std::string& v){return QString::fromStdString(v);}
QString hex(std::uint32_t v){return QString("#%1").arg(v,6,16,QChar('0'));}
std::optional<TerritorialField> fieldKind(const QString& field) {
 if(field=="name")return TerritorialField::Name;if(field=="notes")return TerritorialField::Notes;
 if(field=="validFrom")return TerritorialField::ValidFrom;if(field=="validTo")return TerritorialField::ValidTo;
 return {};
}
}
const TerritorialUnit* EditorController::selectedUnit() const {
 if(selection_.primary() && selection_.primary()->domain!="territorial") return nullptr;
 const auto pos=project_.index().objects.find(territorialRef(selected_.toStdString()));
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
 if(std::any_of(selection_.items().begin(),selection_.items().end(),[this](const auto& ref){return ref.domain=="hydroBuiltin"?
   !hydroRuntime_.recordById(q(ref.id)).has_value():!project_.index().objects.count(ref);})) {
  result["count"]=0;result["lockEnabled"]=false;result["colorEnabled"]=false;result["editable"]=false;
  return result;
 }
 if(std::any_of(selection_.items().begin(),selection_.items().end(),[](const auto& ref){return ref.domain!="territorial";})) {
  result["lockEnabled"]=false;result["colorEnabled"]=false;result["editable"]=false;
  if(selection_.items().size()==1){const auto& ref=selection_.items().front();const auto view=project_.propertyView(ref);result["id"]=q(ref.id);result["type"]=q(ref.domain);if(view)result["displayName"]=q(view->displayName);
   if(ref.domain=="hydroBuiltin")if(const auto record=hydroRuntime_.recordById(q(ref.id)))result["displayName"]=record->name;}
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
 const auto& relation=staticParentRelation(project_.document(),u->id);
 result["parentId"]=q(relation.parentId);result["parentName"]=QString();
 if(!relation.parentId.empty()){const auto view=project_.propertyView(territorialRef(relation.parentId));if(view)result["parentName"]=q(view->displayName);}
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
QString EditorController::beginPropertyEdit(const QString& field) {
 const auto u=selectedUnit();if(!u||selection_.items().size()!=1||!fieldKind(field))return {};
 if((field=="validFrom"||field=="validTo")&&u->kind!=UnitKind::Regional)return {};
 const auto token=QUuid::createUuid().toString(QUuid::WithoutBraces);
 fieldSessions_.emplace(token,FieldSession{project_.snapshot(),territorialRef(u->id),field});refreshTypedScene();return token;
}
bool EditorController::updatePropertyEdit(const QString& token,const QString& value) {
 auto it=fieldSessions_.find(token);if(it==fieldSessions_.end()||!it->second.base.matches(project_)||it->second.ref.id!=selected_.toStdString())return false;
 const auto field=it->second.field;
 if(field=="name")setNameDraft(value);else if(field=="notes")setMemoDraft(value);else if(field=="validFrom")setValidFromDraft(value);else setValidToDraft(value);
 return true;
}
bool EditorController::confirmPropertyEdit(const QString& token) {
 auto it=fieldSessions_.find(token);if(it==fieldSessions_.end())return false;
 const auto session=it->second;fieldSessions_.erase(it);
 refreshTypedScene();
 if(!session.base.matches(project_) || session.ref.id!=selected_.toStdString())return false;
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

#include "editorcontroller.h"
#include "retainedreferencerewriter.h"
#include <QUuid>
using namespace pandoeditor;
namespace { QString text(const std::string& v){return QString::fromStdString(v);} }
QVariantMap EditorController::structureState() const {
    QVariantMap out{{"open",structureDialogOpen()},{"geometryRequired",false},{"detail",QString()}};
    if(conversionDraft_) {
        out["kind"]=static_cast<int>(TerritorialMutationKind::ConvertCountryToSubunit);
        out["conversionSetup"]=true;
        out["generatedId"]=text(conversionDraft_->intent.generatedId);
        out["detail"]=QStringLiteral("새 하위단위의 소속 국가와 상위 영역을 선택하세요. 아직 프로젝트는 변경되지 않았습니다.");
        return out;
    }
    if(!structureSession_)return out;const auto& plan=structureSession_->plan;
    out["kind"]=static_cast<int>(plan.kind);out["requiresConfirmation"]=plan.requiresConfirmation;
    out["geometryRequired"]=plan.geometry.kind!=GeometryRequirementKind::None;
    out["detail"]=plan.geometry.kind==GeometryRequirementKind::WorkerPatch?QStringLiteral("도형 계산 단계가 필요합니다."):plan.geometry.kind==GeometryRequirementKind::Prepared?QStringLiteral("도형 준비 단계가 필요합니다."):QStringLiteral("변경 내용을 확인하세요.");
    QVariantList impacts;for(const auto& item:plan.impacts)impacts.append(QVariantMap{{"kind",text(item.kind)},{"id",text(item.target.id)},{"messageKey",text(item.messageKey)}});out["impacts"]=impacts;return out;
}
QVariantList EditorController::relationCountryOptions() const {QVariantList result;for(const auto& u:project_.document().units)if(u.kind==UnitKind::Country)result.append(QVariantMap{{"id",text(u.id)},{"name",text(objectDisplayName(u))},{"locked",u.locked}});return result;}
QVariantList EditorController::relationParentOptions() const {QVariantList result;const auto current=selectedUnit();if(!current)return result;for(const auto& u:project_.document().units)if(u.kind==UnitKind::Country||u.kind==UnitKind::Subunit)if(u.id!=current->id)result.append(QVariantMap{{"id",text(u.id)},{"name",text(objectDisplayName(u))},{"type",u.kind==UnitKind::Country?"country":"subunit"},{"locked",u.locked}});return result;}
bool EditorController::setStructurePlan(const TerritorialMutationIntent& intent) {auto result=CommandProcessor::planTerritorial(project_,intent);if(!result.ok()||!result.plan){commandError(result.error,text(result.detail));return false;}structureSession_=StructureSession{project_.snapshot(),std::move(*result.plan)};emit structureChanged();return true;}
bool EditorController::changeSelectedParent(const QString& parentId){const auto u=selectedUnit();return u&&u->kind==UnitKind::Subunit&&setStructurePlan(ChangeParentIntent{territorialRef(u->id),territorialRef(parentId.toStdString())});}
bool EditorController::changeSelectedRegionSovereign(const QString& countryId){const auto u=selectedUnit();if(!u||u->kind!=UnitKind::Region)return false;std::optional<ObjectRef> sovereign;if(!countryId.isEmpty())sovereign=territorialRef(countryId.toStdString());return setStructurePlan(ChangeRegionSovereignIntent{territorialRef(u->id),sovereign});}
bool EditorController::beginDeleteSelection(){return !selection_.items().empty()&&setStructurePlan(DeleteTerritorialIntent{selection_.items()});}
bool EditorController::confirmStructureMutation(){if(!structureSession_||!structureSession_->base.matches(project_)){cancelStructureMutation();emit errorOccurred("STALE_STRUCTURE_SESSION");return false;}const auto plan=structureSession_->plan;if(plan.geometry.kind!=GeometryRequirementKind::None){emit errorOccurred(plan.geometry.kind==GeometryRequirementKind::WorkerPatch?QStringLiteral("도형 계산 단계가 필요합니다."):QStringLiteral("도형 준비 단계가 필요합니다."));return false;}CommandArguments args;args.action=ApplyTerritorialMutation{plan,{}};auto request=CommandProcessor::makeRequest(project_,plan.kind==TerritorialMutationKind::CreateCountry||plan.kind==TerritorialMutationKind::CreateSubunit||plan.kind==TerritorialMutationKind::CreateRegion?"territorial.create":plan.kind==TerritorialMutationKind::DeleteCountry||plan.kind==TerritorialMutationKind::DeleteUnits?"territorial.delete":plan.kind==TerritorialMutationKind::ChangeParent?"territorial.relation.parent":"territorial.relation.sovereign",args);auto prepared=CommandProcessor::prepare(project_,request,[](const ProjectDocument& before,const TerritorialMutationPlan& mutation,std::vector<PreservedExtension>& candidate){auto result=retainedrefs::rewrite(before,mutation,candidate);return ExtensionRewriteResult{result.ok,result.detail,result.handledExtensionIds};});if(!prepared.ok()){commandError(prepared.error,text(prepared.detail));return false;}if(!prepared.preview)return true;auto applied=CommandProcessor::confirm(project_,*prepared.preview);if(!applied.ok()){commandError(applied.error,text(applied.detail));return false;}structureSession_.reset();if(plan.selectedAfter)selection_.replace(*plan.selectedAfter);else selection_.prune([&](const ObjectRef& ref){return project_.index().objects.count(ref);});publish(false);emit structureChanged();return true;}
void EditorController::cancelStructureMutation(){if(structureSession_||conversionDraft_){structureSession_.reset();conversionDraft_.reset();emit structureChanged();}}
bool EditorController::beginTypeConversion(){const auto u=selectedUnit();if(!u||!(u->kind==UnitKind::Country||u->kind==UnitKind::Subunit))return false;ConvertTerritorialTypeIntent intent;intent.source=territorialRef(u->id);intent.targetKind=u->kind==UnitKind::Country?UnitKind::Subunit:UnitKind::Country;if(intent.targetKind==UnitKind::Subunit){intent.generatedId=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();conversionDraft_=ConversionDraft{project_.snapshot(),std::move(intent)};emit structureChanged();return true;}return setStructurePlan(intent);}
bool EditorController::updateTypeConversionTarget(const QString& sovereignId,const QString& parentId){
    if(conversionDraft_) {
        if(!conversionDraft_->base.matches(project_)){cancelStructureMutation();emit errorOccurred("STALE_STRUCTURE_SESSION");return false;}
        if(sovereignId.isEmpty()||parentId.isEmpty())return false;
        auto intent=conversionDraft_->intent;intent.sovereign=territorialRef(sovereignId.toStdString());intent.parent=territorialRef(parentId.toStdString());
        conversionDraft_.reset();return setStructurePlan(intent);
    }
    if(!structureSession_)return false;auto conversion=std::get_if<ConvertTerritorialTypeIntent>(&structureSession_->plan.intent);if(!conversion)return false;if(!sovereignId.isEmpty())conversion->sovereign=territorialRef(sovereignId.toStdString());if(!parentId.isEmpty())conversion->parent=territorialRef(parentId.toStdString());return setStructurePlan(*conversion);
}
bool EditorController::beginTerritorialCreate(const QString& type){CreateTerritorialIntent intent;intent.kind=type=="country"?UnitKind::Country:type=="subunit"?UnitKind::Subunit:UnitKind::Region;intent.id=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();return setStructurePlan(intent);}
bool EditorController::updateTerritorialCreateSetup(const QString& name,const QString& sovereignId,const QString& parentId,const QString& sourceId){if(!structureSession_)return false;auto create=std::get_if<CreateTerritorialIntent>(&structureSession_->plan.intent);if(!create)return false;create->name=name.toStdString();if(!sovereignId.isEmpty())create->sovereign=territorialRef(sovereignId.toStdString());if(!parentId.isEmpty())create->parent=territorialRef(parentId.toStdString());if(!sourceId.isEmpty())create->id=sourceId.toStdString();return setStructurePlan(*create);}

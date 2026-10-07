#include "editorcontroller.h"
#include "retainedreferencerewriter.h"
#include "territorialgeometry.h"
#include <QUuid>
using namespace pandoeditor;
namespace { QString text(const std::string& v){return QString::fromStdString(v);} }
QVariantMap EditorController::structureState() const {
    QVariantMap out{{"open",structureDialogOpen()},{"geometryRequired",false},{"detail",QString()}};
    if(createDraft_) {
        out["createSetup"]=true;out["kind"]=static_cast<int>(createDraft_->intent.kind);out["generatedId"]=text(createDraft_->intent.id);out["name"]=text(createDraft_->intent.name);out["geometryRequired"]=true;
        out["detail"]=QStringLiteral("생성 설정을 보관했습니다. 지도 도구가 준비한 도형이 전달되기 전에는 프로젝트를 변경할 수 없습니다.");return out;
    }
    if(conversionDraft_) {
        out["kind"]=static_cast<int>(TerritorialMutationKind::ConvertCountryToSubunit);
        out["conversionSetup"]=true;
        out["generatedId"]=text(conversionDraft_->intent.generatedId);
        out["detail"]=QStringLiteral("상위 일반객체를 선택하세요. 객체 ID와 메타데이터는 유지됩니다.");
        return out;
    }
    if(!structureSession_)return out;const auto& plan=structureSession_->plan;
    if(plan.kind==TerritorialMutationKind::ConvertCountryToSubunit&&plan.selectedAfter)out["generatedId"]=text(plan.selectedAfter->id);
    out["kind"]=static_cast<int>(plan.kind);out["requiresConfirmation"]=plan.requiresConfirmation;
    out["geometryRequired"]=plan.geometry.kind==GeometryRequirementKind::WorkerPatch&&!structureSession_->preview;
    out["detail"]=plan.geometry.kind==GeometryRequirementKind::WorkerPatch?QStringLiteral("M4 도형 계산 결과를 기다리고 있습니다."):plan.geometry.kind==GeometryRequirementKind::Prepared?QStringLiteral("준비된 도형을 확인하고 생성을 확정하세요."):QStringLiteral("변경 내용을 확인하세요.");
    out["calculating"]=structureSession_->job.has_value();
    if(structureSession_->job)out["detail"]=QStringLiteral("영토 도형을 계산 중입니다. 취소해도 현재 지도는 바뀌지 않습니다.");
    if(!structureSession_->error.isEmpty())out["detail"]=structureSession_->error;
    if(structureSession_->preview) {
        out["detail"]=QStringLiteral("계산 결과입니다. 확인을 눌러야 지도에 적용됩니다.");
        out["previewPaths"]=structureSession_->projection.paths;
        out["previewWidth"]=structureSession_->projection.width;
        out["previewHeight"]=structureSession_->projection.height;
    }
    QVariantList impacts;for(const auto& item:plan.impacts)impacts.append(QVariantMap{{"kind",text(item.kind)},{"id",text(item.target.id)},{"messageKey",text(item.messageKey)}});out["impacts"]=impacts;return out;
}
QVariantList EditorController::relationCountryOptions() const {QVariantList result;for(const auto& u:project_.document().units)if(isRootGeneral(project_.document(),u))result.append(QVariantMap{{"id",text(u.id)},{"name",text(objectDisplayName(u))},{"locked",u.locked}});return result;}
QVariantList EditorController::relationParentOptions() const {QVariantList result;const auto current=selectedUnit();for(const auto& u:project_.document().units)if(u.kind==UnitKind::General)if(createDraft_||!current||u.id!=current->id)result.append(QVariantMap{{"id",text(u.id)},{"name",text(objectDisplayName(u))},{"type","general"},{"locked",u.locked}});return result;}
bool EditorController::setStructurePlan(const TerritorialMutationIntent& intent) {
    auto result=CommandProcessor::planTerritorial(project_,intent);
    if(!result.ok()||!result.plan){commandError(result.error,text(result.detail));return false;}
    cancelStructureMutation();
    structureSession_=StructureSession{project_.snapshot(),std::move(*result.plan)};
    if(structureSession_->plan.geometry.kind==GeometryRequirementKind::WorkerPatch) {
        const auto plan=structureSession_->plan;
        structureSession_->job=jobs_->submit(structureSession_->base,"territorial:geometry",
            [plan](const ProjectSnapshot& snapshot,const JobToken& token){return prepareTerritorialGeometry(snapshot,plan,token);},
            [this](std::uint64_t id,JobDisposition disposition,PrepareResult result){
                if(!structureSession_||!structureSession_->job||structureSession_->job->id()!=id)return;
                structureSession_->job.reset();
                if(disposition!=JobDisposition::Accepted||!structureSession_->base.matches(project_)) {
                    structureSession_->error=QStringLiteral("현재 문서가 변경되어 계산 결과를 폐기했습니다. 취소 후 다시 시도하세요.");
                } else if(!result.ok()||!result.preview) {
                    structureSession_->error=text(result.detail.empty()?"GEOMETRY_PREPARATION_FAILED":result.detail);
                } else {
                    try {
                        structureSession_->projection.rebuild(result.preview->change().after());
                        structureSession_->preview=std::move(result.preview);
                    }catch(const std::exception& error){structureSession_->error=QString::fromUtf8(error.what());}
                }
                emit structureChanged();
            });
    }
    emit structureChanged();return true;
}
bool EditorController::transferSelectedSubunit(const QString& countryId) {
    const auto u=selectedUnit();
    return u&&u->kind==UnitKind::General&&setStructurePlan(TransferSubunitIntent{territorialRef(u->id),territorialRef(countryId.toStdString())});
}
bool EditorController::changeSelectedParent(const QString& parentId){const auto u=selectedUnit();return u&&u->kind==UnitKind::General&&setStructurePlan(ChangeParentIntent{territorialRef(u->id),territorialRef(parentId.toStdString())});}
bool EditorController::commitSelectedParent(const QString& parentId){
    const auto u=selectedUnit();
    if(!u||parentId.isEmpty()||staticParentRelation(project_.document(),u->id).parentId==parentId.toStdString())return false;
    if(!changeSelectedParent(parentId))return false;
    if(confirmStructureMutation())return true;
    cancelStructureMutation();return false;
}
bool EditorController::beginDeleteSelection(){return !selection_.items().empty()&&setStructurePlan(DeleteTerritorialIntent{selection_.items()});}
bool EditorController::beginMergeSelection(){
    const auto primary=selection_.primary();const auto unit=selectedUnit();
    if(!primary||!unit||!selectedEditable()||geometryEdit_||structureDialogOpen()||hasPendingEdits())return false;
    geometryEdit_=GeometryEditSession{project_.snapshot(),*primary,*project_.document().geometries.get(pandoeditor::staticGeometryBinding(project_.document(),unit->id).geometryRef),QStringLiteral("merge"),{}};
    geometryEdit_->mergeIntent=MergeTerritorialIntent{*primary,{}};
    geometryEdit_->choosingProviders=true;emit geometryEditChanged();emit visualChanged();return true;
}
bool EditorController::beginAnnexGeometry(){
    const auto primary=selection_.primary();if(!primary||!selectedUnit()||!selectedEditable()||geometryEdit_||structureDialogOpen()||hasPendingEdits())return false;
    geometryEdit_=GeometryEditSession{project_.snapshot(),*primary,{"Polygon",{}, {}, {}},QStringLiteral("annex"),{}};geometryEdit_->annexIntent=AnnexTerritoryIntent{*primary,{}, {}};
    if(isRootGeneral(project_.document(),*selectedUnit())) {
        geometryEdit_->territorySelection.emplace(TerritorySelectionKind::Annex);
        geometryEdit_->annexIntent.reset();
        geometryEdit_->generation=++nextGeometrySession_;
        geometryEdit_->stage="setup";
    }
    geometryEdit_->choosingProviders=true;emit geometryEditChanged();emit visualChanged();return true;
}
bool EditorController::beginSplitGeometry(){
    const auto primary=selection_.primary();const auto unit=selectedUnit();if(!primary||!unit||unit->kind!=UnitKind::General||!selectedEditable()||geometryEdit_||structureDialogOpen()||hasPendingEdits())return false;
    const auto& parent=staticParentRelation(project_.document(),unit->id).parentId;
    if(!parent.empty()&&objectLocked(project_.document(),project_.index(),territorialRef(parent)))return false;
    geometryEdit_=GeometryEditSession{project_.snapshot(),*primary,*project_.document().geometries.get(pandoeditor::staticGeometryBinding(project_.document(),unit->id).geometryRef),QStringLiteral("split"),{}};
    geometryEdit_->splitIntent=SplitTerritorialIntent{*primary,{},QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(),"새 객체"};
    geometryEdit_->territorySelection.emplace(TerritorySelectionKind::BoundedCreation);
    const auto binding=staticGeometryBinding(project_.document(),unit->id).geometryRef;
    geometryEdit_->territorySelection->resetSources({{*primary,*project_.document().geometries.get(binding),binding,objectDisplayName(*unit)}});
    geometryEdit_->draft={"Polygon",{},{},{}};geometryEdit_->generation=++nextGeometrySession_;geometryEdit_->stage="setup";
    scheduleTerritorySelection(false);emit geometryEditChanged();return true;
}
bool EditorController::beginCoastlineGeometry(const QString& authority){
    if(!beginGeometryEdit(QStringLiteral("coast")))return false;auto mode=CoastlineAuthority::Country;if(authority=="subunit")mode=CoastlineAuthority::Subunit;else if(authority=="independent")mode=CoastlineAuthority::Independent;geometryEdit_->coastIntent=CoastlineIntent{geometryEdit_->target,geometryEdit_->draft,mode};emit geometryEditChanged();return true;
}
bool EditorController::confirmStructureMutation() {
    if(!structureSession_||!structureSession_->base.matches(project_)){
        cancelStructureMutation();emit errorOccurred("STALE_STRUCTURE_SESSION");return false;
    }
    const auto plan=structureSession_->plan;
    if(plan.geometry.kind!=GeometryRequirementKind::WorkerPatch) {
        CommandArguments args;args.action=ApplyTerritorialMutation{plan,{}};
        const auto id=plan.kind==TerritorialMutationKind::CreateCountry||plan.kind==TerritorialMutationKind::CreateSubunit||plan.kind==TerritorialMutationKind::CreateRegion?"territorial.create":
            plan.kind==TerritorialMutationKind::DeleteCountry||plan.kind==TerritorialMutationKind::DeleteUnits?"territorial.delete":
            "territorial.relation.parent";
        auto request=CommandProcessor::makeRequest(project_,id,args);
        auto prepared=CommandProcessor::prepare(project_,request,[](const ProjectDocument& before,const TerritorialMutationPlan& mutation,std::vector<PreservedExtension>& candidate){
            auto result=retainedrefs::rewrite(before,mutation,candidate);return ExtensionRewriteResult{result.ok,result.detail,result.handledExtensionIds};
        });
        if(!prepared.ok()){commandError(prepared.error,text(prepared.detail));return false;}
        if(!prepared.preview){cancelStructureMutation();return true;}
        structureSession_->preview=std::move(prepared.preview);
    }
    if(!structureSession_->preview||structureSession_->job)return false;
    // Allocate the rendering cache before committing the candidate.
    MapProjection next;
    try {next.rebuild(structureSession_->preview->change().after());}
    catch(const std::exception& error){emit errorOccurred(QString::fromUtf8(error.what()));return false;}
    const auto applied=CommandProcessor::confirm(project_,*structureSession_->preview);
    if(!applied.ok()){
        structureSession_->preview.reset();structureSession_->error=QStringLiteral("확정하지 못했습니다. 취소 후 다시 준비하세요.");
        commandError(applied.error,text(applied.detail));emit structureChanged();return false;
    }
    noteAppliedImpact(applied.impact);
    structureSession_.reset();projection_=std::move(next);
    hover_.reset();hoverSource_.clear();++hoverRevision_;
    closeObjectChooser();
    if(plan.selectedAfter)selection_.replace(*plan.selectedAfter);
    else selection_.prune([&](const ObjectRef& ref){return project_.index().objects.count(ref);});
    publish(false);emit geometryChanged();emit structureChanged();return true;
}
void EditorController::cancelStructureMutation(){if(structureSession_||conversionDraft_||createDraft_){if(structureSession_&&structureSession_->job)jobs_->cancel(structureSession_->job->id());structureSession_.reset();conversionDraft_.reset();createDraft_.reset();emit structureChanged();}}
bool EditorController::beginTypeConversion(){const auto u=selectedUnit();if(!u||u->kind!=UnitKind::General)return false;cancelStructureMutation();ConvertTerritorialTypeIntent intent;intent.source=territorialRef(u->id);intent.targetKind=UnitKind::General;intent.generatedId=u->id;if(staticParentRelation(project_.document(),u->id).parentId.empty()){conversionDraft_=ConversionDraft{project_.snapshot(),std::move(intent)};emit structureChanged();return true;}return setStructurePlan(intent);}
bool EditorController::updateTypeConversionTarget(const QString& parentId){
    if(conversionDraft_) {
        if(!conversionDraft_->base.matches(project_)){cancelStructureMutation();emit errorOccurred("STALE_STRUCTURE_SESSION");return false;}
        if(parentId.isEmpty())return false;
        auto intent=conversionDraft_->intent;intent.parent=territorialRef(parentId.toStdString());
        return setStructurePlan(intent);
    }
    if(!structureSession_)return false;const auto conversion=std::get_if<ConvertTerritorialTypeIntent>(&structureSession_->plan.intent);if(!conversion)return false;auto intent=*conversion;intent.parent=parentId.isEmpty()?std::optional<ObjectRef>{}:territorialRef(parentId.toStdString());return setStructurePlan(intent);
}
bool EditorController::beginTerritorialCreate(const QString& type){if(type!="general"&&type!="regional"){emit errorOccurred("INVALID_ENTITY_KIND");return false;}cancelStructureMutation();CreateTerritorialIntent intent;intent.kind=type=="general"?UnitKind::General:UnitKind::Regional;intent.id=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();createDraft_=CreateDraft{project_.snapshot(),std::move(intent)};emit structureChanged();return true;}
bool EditorController::updateTerritorialCreateSetup(const QString& name,const QString& parentId,const QString& sourceId){if(!createDraft_||!createDraft_->base.matches(project_)){cancelStructureMutation();emit errorOccurred("STALE_STRUCTURE_SESSION");return false;}auto& create=createDraft_->intent;create.name=name.toStdString();create.parent=parentId.isEmpty()?std::optional<ObjectRef>{}:territorialRef(parentId.toStdString());if(!sourceId.isEmpty())create.id=sourceId.toStdString();emit structureChanged();return true;}
bool EditorController::beginTerritorialCreatePrepared(const CreateTerritorialIntent& intent){if(structureDialogOpen())cancelStructureMutation();return setStructurePlan(intent);}

#include "editorcontroller.h"
#include <QRegularExpression>
#include <exception>

namespace {
bool validColor(const QString& value)
{
    static const QRegularExpression pattern("^#[0-9a-fA-F]{6}$");
    return pattern.match(value).hasMatch();
}
}
void EditorController::commandError(pandoeditor::CommandError error,const QString& detail)
{
    using pandoeditor::CommandError;
    QString message;
    switch(error) {
    case CommandError::InvalidArguments:
        message=QStringLiteral("이름·색상·불투명도 등 입력값을 확인하세요."); break;
    case CommandError::InvalidTargets:
        message=QStringLiteral("편집 대상을 찾을 수 없습니다. 선택한 국가와 레이어를 확인하세요."); break;
    case CommandError::Locked:
        message=QStringLiteral("국가 또는 소속 레이어가 잠겨 있어 변경할 수 없습니다."); break;
    case CommandError::UnsupportedDependency:
        message=QStringLiteral("미해석 데이터 보호를 위해 이 편집을 제한합니다."); break;
    case CommandError::ValidationFailed:
        message=QStringLiteral("변경 후 문서가 유효하지 않습니다. 참조 중이거나 마지막인 레이어는 삭제할 수 없습니다."); break;
    case CommandError::ProjectMismatch: case CommandError::DocumentMismatch: case CommandError::StaleRevision:
        message=QStringLiteral("미리보기 이후 문서가 변경되었습니다. 다시 준비해 주세요."); break;
    case CommandError::PreviewConsumed:
        message=QStringLiteral("확정할 미리보기가 없거나 이미 폐기되었습니다."); break;
    default:
        message=QStringLiteral("명령을 처리하지 못했습니다."); break;
    }
    emit errorOccurred(QString::fromLatin1(pandoeditor::commandErrorCode(error))+": "+message+
                       QStringLiteral(" 초안은 유지됩니다.")+(detail.isEmpty()?QString():"\n"+detail));
}
bool EditorController::collectPendingEdits(pandoeditor::CommandArguments& args)
{
    // Selection parks drafts by identity. An explicit apply/save collects them;
    // selecting/searching/focusing never calls this function.
    auto country=[&](const QString& id,const CountryDraft& draft) {
        using namespace pandoeditor;
        const auto ref=territorialRef(id.toStdString());auto pos=project_.index().objects.find(ref);
        if(pos==project_.index().objects.end()){commandError(CommandError::InvalidTargets);return false;}
        const auto& u=project_.document().units[pos->second];
        auto active=[&](const char* field){return draft.fields.empty()||draft.fields.count(field);};
        if(active("name") && draft.name.toStdString()!=(u.kind==UnitKind::General?objectDisplayName(u):u.name))args.properties.fields.push_back({ref,TerritorialField::Name,draft.name.toStdString()});
        if(active("notes") && draft.memo.toStdString()!=u.notes)args.properties.fields.push_back({ref,TerritorialField::Notes,draft.memo.toStdString()});
        if(active("validFrom") && draft.from.toStdString()!=pandoeditor::staticLifetime(project_.document(),u.id).validity.from.value_or(""))args.properties.fields.push_back({ref,TerritorialField::ValidFrom,draft.from.toStdString()});
        if(active("validTo") && draft.to.toStdString()!=pandoeditor::staticLifetime(project_.document(),u.id).validity.to.value_or(""))args.properties.fields.push_back({ref,TerritorialField::ValidTo,draft.to.toStdString()});
        if(!validColor(draft.color)){commandError(CommandError::InvalidArguments);return false;}
        const auto& style=project_.document().presentation.objectStyles.at(ref);
        if((active("color") && draft.color.mid(1).toUInt(nullptr,16)!=effectiveObjectColor(project_.document(),ref)) || (active("opacity") && draft.opacity&&*draft.opacity!=style.opacity)) {
            const auto c=project_.country(ref.id);if(!c){commandError(CommandError::InvalidArguments);return false;}
            args.properties.countries.push_back({c->id,{c->name,c->memo,draft.color.mid(1).toUInt(nullptr,16),draft.opacity.value_or(c->opacity),c->layerId}});
        }
        return true;
    };
    auto layer=[&](const QString& id,const LayerDraft& draft) {
        const auto l=project_.layer(id.toStdString());
        if(!l) {commandError(pandoeditor::CommandError::InvalidTargets);return false;}
        auto next=*l;next.name=draft.name.trimmed().toStdString();next.opacity=draft.opacity.value_or(l->opacity);
        if(next.name!=l->name||next.opacity!=l->opacity) args.properties.layers.push_back(std::move(next));
        return true;
    };
    for(const auto& [id,draft]:parkedCountryDrafts_) if(!country(id,draft))return false;
    for(const auto& [id,draft]:parkedLayerDrafts_) if(!layer(id,draft))return false;
    if(selectedUnit()&&!country(selectedId(),{nameDraft_,memoDraft_,colorDraft_,opacityPreview_,validFromDraft_,validToDraft_}))return false;
    if(project_.layer(selectedLayer_.toStdString())&&!layer(selectedLayer_,{layerNameDraft_,layerOpacityPreview_}))return false;
    return true;
}
pandoeditor::CommandStatus EditorController::prepareCommand(const std::string& commandId,pandoeditor::CommandArguments args)
{
    using namespace pandoeditor;
    cancelPreview();
    try {
        if(!collectPendingEdits(args)) return CommandStatus::Rejected;
        auto request=CommandProcessor::makeRequest(project_,commandId,std::move(args));
        auto result=CommandProcessor::prepare(project_,request);
        if(!result.ok()) {commandError(result.error,QString::fromStdString(result.detail));return result.status;}
        if(result.preview) {pendingPreview_=std::move(result.preview);emit previewChanged();}
        return result.status;
    }catch(const std::exception&) {commandError(CommandError::PrepareFailed);return CommandStatus::Rejected;}
}
bool EditorController::preparePendingEdits(){return prepareCommand("edit.properties",{})!=pandoeditor::CommandStatus::Rejected;}
bool EditorController::confirmCommand()
{
    if(!pendingPreview_) {commandError(pandoeditor::CommandError::PreviewConsumed);return false;}
    auto result=pandoeditor::CommandProcessor::confirm(project_,*pendingPreview_);
    pendingPreview_.reset();
    if(result.ok())clearParkedDrafts();
    emit previewChanged();
    if(!result.ok()) {commandError(result.error,QString::fromStdString(result.detail));return false;}
    noteAppliedImpact(result.impact);
    return true;
}
bool EditorController::confirmPreview(){if(!confirmCommand())return false;publish(false);return true;}
void EditorController::cancelPreview()
{
    cancelBackgroundWork();
    if(pendingPreview_){pandoeditor::CommandProcessor::cancel(*pendingPreview_);pendingPreview_.reset();emit previewChanged();}
}
void EditorController::discardPendingEdits()
{
    cancelContentEdit();cancelGeometryEdit();
    cancelPreview();clearParkedDrafts();reloadDrafts();emit draftsChanged();emit visualChanged();emit dirtyChanged();
}
bool EditorController::executeCommand(const std::string& commandId,pandoeditor::CommandAction action)
{
    if(startupBusy_||worldStatus_=="recovery-failed")return false;
    pandoeditor::CommandArguments args;args.action=std::move(action);
    const auto status=prepareCommand(commandId,std::move(args));
    if(status==pandoeditor::CommandStatus::NoOp){clearParkedDrafts();return true;}
    return status==pandoeditor::CommandStatus::Prepared&&confirmCommand();
}
bool EditorController::commitPendingEdits(){
    if(geometryEdit_)return false;
    if(contentSession_){
        // The integrated panel keeps a read/edit session open. Only creation,
        // deletion previews and geometry need explicit confirmation.
        if(contentSession_->edit.create||contentSession_->preview)return false;
        const auto pending=contentSession_->pendingFields;
        for(const auto& field:pending){
            if(contentSession_->pendingFields.count(field)&&
               !commitContentField(QString::fromStdString(field)))return false;
        }
    }
    if(!executeCommand("edit.properties",std::monostate{}))return false;
    publish(false);return true;
}
void EditorController::setColor(const QString& color)
{
    if(!validColor(color)){commandError(pandoeditor::CommandError::InvalidArguments);return;}
    if(executeCommand("country.color",pandoeditor::SetCountryColor{selectedId().toStdString(),color.mid(1).toUInt(nullptr,16)}))publish(false);
}

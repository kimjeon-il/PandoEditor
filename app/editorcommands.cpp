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
    const auto c=project_.country(selected_.toStdString());
    if(c) {
        if(!validColor(colorDraft_)) { commandError(pandoeditor::CommandError::InvalidArguments); return false; }
        pandoeditor::CountryProperties next{nameDraft_.trimmed().toStdString(),memoDraft_.toStdString(),
            colorDraft_.mid(1).toUInt(nullptr,16),opacityPreview_.value_or(c->opacity),c->layerId};
        const pandoeditor::CountryProperties before{c->name,c->memo,c->color,c->opacity,c->layerId};
        if(!(before==next)) args.properties.countries.push_back({c->id,std::move(next)});
    }
    if(const auto l=project_.layer(selectedLayer_.toStdString())) {
        auto next=*l; next.name=layerNameDraft_.trimmed().toStdString(); next.opacity=layerOpacityPreview_.value_or(l->opacity);
        if(next.name!=l->name || next.opacity!=l->opacity) args.properties.layers.push_back(std::move(next));
    }
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
        if(!result.ok()) { commandError(result.error,QString::fromStdString(result.detail)); return result.status; }
        if(result.preview) { pendingPreview_=std::move(result.preview); emit previewChanged(); }
        return result.status;
    } catch(const std::exception&) {
        commandError(CommandError::PrepareFailed); return CommandStatus::Rejected;
    }
}
bool EditorController::preparePendingEdits()
{
    return prepareCommand("edit.properties",{})!=pandoeditor::CommandStatus::Rejected;
}
bool EditorController::confirmCommand()
{
    if(!pendingPreview_) { commandError(pandoeditor::CommandError::PreviewConsumed); return false; }
    auto result=pandoeditor::CommandProcessor::confirm(project_,*pendingPreview_);
    pendingPreview_.reset(); emit previewChanged();
    if(!result.ok()) { commandError(result.error,QString::fromStdString(result.detail)); return false; }
    return true;
}
bool EditorController::confirmPreview()
{
    if(!confirmCommand()) return false;
    publish(false); return true;
}
void EditorController::cancelPreview()
{
    if(pendingPreview_) {
        pandoeditor::CommandProcessor::cancel(*pendingPreview_); pendingPreview_.reset(); emit previewChanged();
    }
}
void EditorController::discardPendingEdits()
{
    cancelPreview(); reloadDrafts(); emit draftsChanged(); emit visualChanged(); emit dirtyChanged();
}
bool EditorController::executeCommand(const std::string& commandId,pandoeditor::CommandAction action)
{
    pandoeditor::CommandArguments args; args.action=std::move(action);
    auto status=prepareCommand(commandId,std::move(args));
    if(status==pandoeditor::CommandStatus::NoOp) return true;
    return status==pandoeditor::CommandStatus::Prepared && confirmCommand();
}
bool EditorController::commitPendingEdits()
{
    if(!executeCommand("edit.properties",std::monostate{})) return false;
    publish(false); return true;
}
void EditorController::setColor(const QString& color)
{
    if(!validColor(color)) { commandError(pandoeditor::CommandError::InvalidArguments); return; }
    if(executeCommand("country.color",pandoeditor::SetCountryColor{selected_.toStdString(),color.mid(1).toUInt(nullptr,16)})) publish(false);
}

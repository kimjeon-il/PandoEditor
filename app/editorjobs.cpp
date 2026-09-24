#include "editorcontroller.h"
#include <exception>
using namespace pandoeditor;
bool EditorController::beginPendingWork(bool apply)
{
    if(contentSession_||geometryEdit_)return false;
    if(!hasPendingEdits())return true;
    try {
        if(pendingPreview_){CommandProcessor::cancel(*pendingPreview_);pendingPreview_.reset();emit previewChanged();}
        if(!hasPendingEdits())return true;
        CommandArguments args;if(!collectPendingEdits(args))return false;
        auto request=CommandProcessor::makeRequest(project_,"edit.properties",std::move(args));
        auto snapshot=project_.snapshot();
        auto task=[request=std::move(request)](const ProjectSnapshot& s,const JobToken& token){
            token.reportProgress(0);if(token.cancelled())return PrepareResult{};
            auto result=CommandProcessor::prepare(s,request);token.reportProgress(100);return result;
        };
        background_=jobs_->submit(std::move(snapshot),"editor:properties",std::move(task),
            [this,apply](std::uint64_t id,JobDisposition disposition,PrepareResult result){
                if(!background_||background_->id()!=id)return;
                background_.reset();
                if(disposition!=JobDisposition::Accepted){
                    if(disposition==JobDisposition::Stale)commandError(CommandError::StaleRevision);
                    emit jobChanged();return;
                }
                if(!result.ok()){commandError(result.error,QString::fromStdString(result.detail));emit jobChanged();return;}
                if(result.status==CommandStatus::NoOp){if(apply){clearParkedDrafts();publish(false);}emit jobChanged();return;}
                if(!result.preview){commandError(CommandError::PrepareFailed);emit jobChanged();return;}
                pendingPreview_=std::move(result.preview);emit previewChanged();
                if(apply&&pendingPreview_&&confirmCommand())publish(false);
                emit jobChanged();
            });
        emit jobChanged();return true;
    }catch(const std::exception&){commandError(CommandError::PrepareFailed);return false;}
}
bool EditorController::applyPendingEditsAsync(){return beginPendingWork(true);}
bool EditorController::preparePendingEditsAsync(){return beginPendingWork(false);}
void EditorController::cancelBackgroundWork()
{
    if(!background_)return;
    const auto id=background_->id();background_.reset();jobs_->cancel(id);emit jobChanged();
}

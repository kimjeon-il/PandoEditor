#include "editorcontroller.h"
#include <QScopedValueRollback>
#include <exception>
using namespace pandoeditor;
bool EditorController::commitCountryField(const QString& field)
{
    // Web country-property-controller binds name/notes independently to change.
    // Do not apply or reload another field's draft as a side effect of focus loss.
    if(fieldCommitInProgress_) return false;
    QScopedValueRollback<bool> committing(fieldCommitInProgress_,true);
    if(field!="name" && field!="notes") { commandError(CommandError::InvalidArguments); return false; }
    const auto c=project_.country(selected_.toStdString()); if(!c) return false;
    try {
        const auto id=c->id;
        CountryProperties before{c->name,c->memo,c->color,c->opacity,c->layerId},next=before;
        if(field=="name") next.name=nameDraft_.trimmed().toStdString(); else next.memo=memoDraft_.toStdString();
        if(!(before==next)) {
            cancelPreview();
            CommandArguments args; args.properties.countries.push_back({id,std::move(next)});
            auto request=CommandProcessor::makeRequest(project_,"edit.properties",std::move(args));
            auto result=CommandProcessor::prepare(project_,request);
            if(!result.ok()) { commandError(result.error,QString::fromStdString(result.detail)); return false; }
            if(result.preview) {
                auto committed=CommandProcessor::confirm(project_,*result.preview);
                if(!committed.ok()) { commandError(committed.error,QString::fromStdString(committed.detail)); return false; }
            }
        }
        const auto current=project_.country(id);
        if(field=="name") nameDraft_=QString::fromStdString(current->name);
        else memoDraft_=QString::fromStdString(current->memo);
        emit stateChanged(); emit draftsChanged(); emit dirtyChanged(); return true;
    } catch(const std::exception&) { commandError(CommandError::PrepareFailed); return false; }
}

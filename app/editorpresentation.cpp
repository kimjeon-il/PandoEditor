#include "editorcontroller.h"
#include <pandoeditor/presentationcommands.h>
#include "losslessjson.h"
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QDir>
#include <QStringList>
using namespace pandoeditor;
QVariantList EditorController::presentationGroups() const {
    QVariantList rows;const auto& p=project_.document().presentation.webPresentation;
    for(auto kind:{UnitKind::Country,UnitKind::Subunit,UnitKind::Region}) {
        const auto group=territorialGroup(kind);
        const auto name=kind==UnitKind::Country?"basemapLabels":kind==UnitKind::Subunit?"subunitLabels":"regionLabels";
        const auto flag=kind==UnitKind::Country?"countryFlags":kind==UnitKind::Subunit?"subunitFlags":"regionFlags";
        const auto found=p.styles.find(group);auto s=found==p.styles.end()?PresentationStyle{}:found->second;
        rows.append(QVariantMap{{"key",QString::fromStdString(group)},{"title",kind==UnitKind::Country?QStringLiteral("국가"):kind==UnitKind::Subunit?QStringLiteral("하위단위"):QStringLiteral("지방")},
            {"visible",groupVisible(p,group)},{"nameKey",name},{"flagKey",flag},{"names",groupVisible(p,name)},{"flags",groupVisible(p,flag)},{"opacity",s.opacity.value_or(1)},{"boundary",s.boundaryVisible.value_or(true)}});
    }return rows;
}
void EditorController::publishPresentation() {
    closeObjectChooser();
    emit presentationChanged();emit visualChanged();emit stateChanged();emit searchChanged();emit dirtyChanged();
    presentationSaveInstance_=project_.instanceId();presentationSaveTimer_.start();
}
QString EditorController::presentationRecoveryPath() const {
    const auto fingerprint=QCryptographicHash::hash(
        QByteArray::fromStdString(project_.document().documentId),QCryptographicHash::Sha256).toHex();
    return storage_.privateProjectPath()+".presentation-recovery."+QString::fromLatin1(fingerprint)+".json";
}
QString EditorController::availablePresentationRecoveryPath() const {
    const QFileInfo privateFile(storage_.privateProjectPath());
    QDir directory(privateFile.absolutePath());
    const auto files=directory.entryInfoList(
        QStringList{privateFile.fileName()+".presentation-recovery.*.json"},QDir::Files,QDir::Time);
    return files.empty()?QString():files.front().absoluteFilePath();
}
bool EditorController::presentationRecoveryAvailable() const { return !availablePresentationRecoveryPath().isEmpty(); }
bool EditorController::flushPresentationRecovery() {
    presentationSaveTimer_.stop();
    if(presentationSaveInstance_!=project_.instanceId())return false;
    try {
        using V=losslessjson::Value;V envelope=V::obj();
        envelope.object["format"]=V::str("pandoeditor-presentation-recovery");
        envelope.object["sourcePath"]=V::str(filePath_.toStdString());
        envelope.object["documentId"]=V::str(project_.document().documentId);
        envelope.object["contentRevision"]=V::num(project_.revision());
        envelope.object["presentationRevision"]=V::num(project_.presentationRevision());
        envelope.object["project"]=losslessjson::parse(projectcodec::encode(project_));
        storage_.write(QUrl::fromLocalFile(presentationRecoveryPath()),envelope.encode());
        emit presentationRecoveryChanged();return true;
    }catch(const std::exception& e){emit errorOccurred(QStringLiteral("표시 설정 복구본을 저장하지 못했습니다: ")+QString::fromUtf8(e.what()));return false;}
}
bool EditorController::discardPresentationRecovery() {
    presentationSaveTimer_.stop();presentationSaveInstance_.clear();
    const auto current=presentationRecoveryPath();
    const auto target=QFileInfo::exists(current)?current:availablePresentationRecoveryPath();
    const bool ok=target.isEmpty()||QFile::remove(target);
    if(ok)emit presentationRecoveryChanged();else emit errorOccurred(QStringLiteral("복구본을 삭제하지 못했습니다."));return ok;
}
bool EditorController::discardOwnPresentationRecovery() {
    const auto path=presentationRecoveryPath();
    if(!QFileInfo::exists(path))return true;
    if(!QFile::remove(path)){emit errorOccurred(QStringLiteral("복구본을 삭제하지 못했습니다."));return false;}
    emit presentationRecoveryChanged();return true;
}
bool EditorController::restorePresentationRecovery() {
    try {
        const auto recovery=availablePresentationRecoveryPath();
        losslessjson::require(!recovery.isEmpty(),"RECOVERY_NOT_FOUND");
        auto root=losslessjson::parse(storage_.read(QUrl::fromLocalFile(recovery)));
        losslessjson::require(root.object.at("format").string=="pandoeditor-presentation-recovery","INVALID_RECOVERY");
        losslessjson::require(root.object.at("contentRevision").kind==losslessjson::Value::Number,"INVALID_RECOVERY");
        losslessjson::require(root.object.at("presentationRevision").kind==losslessjson::Value::Number,"INVALID_RECOVERY");
        auto bytes=root.object.at("project").encode();auto candidate=projectcodec::decode(bytes);
        losslessjson::require(candidate.documentId==root.object.at("documentId").string,"INVALID_RECOVERY_DOCUMENT");
        replaceFromBytes(bytes,true,QString::fromStdString(root.object.at("sourcePath").string));
        return true;
    }catch(const std::exception& e){emit errorOccurred(QStringLiteral("복구본을 열지 못했습니다: ")+QString::fromUtf8(e.what()));return false;}
}
bool EditorController::setPresentationVisibility(const QString& key,bool visible) {
    auto r=PresentationCommandProcessor::apply(project_,SetPresentationVisibility{key.toStdString(),visible});if(r==PresentationResult::Applied)publishPresentation();return r==PresentationResult::Applied||r==PresentationResult::NoOp;
}
bool EditorController::setPresentationOpacity(const QString& group,double opacity) {
    PresentationStyle s;s.opacity=opacity;auto r=PresentationCommandProcessor::apply(project_,PatchGroupPresentation{group.toStdString(),s});if(r==PresentationResult::Applied)publishPresentation();return r==PresentationResult::Applied||r==PresentationResult::NoOp;
}
bool EditorController::setPresentationBoundary(const QString& group,bool visible) {
    PresentationStyle s;s.boundaryVisible=visible;auto r=PresentationCommandProcessor::apply(project_,PatchGroupPresentation{group.toStdString(),s});if(r==PresentationResult::Applied)publishPresentation();return r==PresentationResult::Applied||r==PresentationResult::NoOp;
}
bool EditorController::toggleSelectionVisibility() {
    auto r=PresentationCommandProcessor::apply(project_,SetBatchVisibility{selection_.items(),{}});if(r==PresentationResult::Applied)publishPresentation();return r==PresentationResult::Applied||r==PresentationResult::NoOp;
}
bool EditorController::setScopedObjectVisibility(const QVariantMap& value,bool visible) {
    auto ref=existingObjectRef(value);if(!ref)return false;
    auto group=territorialGroup(project_.document().units.at(project_.index().objects.at(*ref)).kind);
    auto r=PresentationCommandProcessor::apply(project_,SetScopedVisibility{group,{*ref},visible});if(r==PresentationResult::Applied)publishPresentation();return r==PresentationResult::Applied||r==PresentationResult::NoOp;
}

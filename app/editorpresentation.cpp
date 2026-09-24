#include "editorcontroller.h"
#include <pandoeditor/presentationcommands.h>
#include "losslessjson.h"
#include "hydrodataprovider.h"
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QDir>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QStringList>
#include <cmath>
#include <algorithm>
#include <limits>
using namespace pandoeditor;
namespace { QString displayText(const std::string& value){return QString::fromStdString(value);} }
QVariantList EditorController::presentationGroups() const {
    QVariantList rows;const auto& p=project_.document().presentation.webPresentation;
    for(auto kind:{UnitKind::Country,UnitKind::Subunit,UnitKind::Region}) {
        const auto group=territorialGroup(kind);
        const auto name=kind==UnitKind::Country?"basemapLabels":kind==UnitKind::Subunit?"subunitLabels":"regionLabels";
        const auto flag=kind==UnitKind::Country?"countryFlags":kind==UnitKind::Subunit?"subunitFlags":"regionFlags";
        const auto found=p.styles.find(group);auto s=found==p.styles.end()?PresentationStyle{}:found->second;
        rows.append(QVariantMap{{"key",QString::fromStdString(group)},{"title",kind==UnitKind::Country?QStringLiteral("국가"):kind==UnitKind::Subunit?QStringLiteral("하위단위"):QStringLiteral("지방")},
            {"visible",groupVisible(p,group)},{"nameKey",name},{"flagKey",flag},{"names",groupVisible(p,name)},{"flags",groupVisible(p,flag)},{"opacity",s.opacity.value_or(1)},{"boundary",s.boundaryVisible.value_or(true)}});
    }
    for(const auto& entry:std::vector<std::pair<std::string,QString>>{{"labels",QStringLiteral("지명")},{"rivers",QStringLiteral("강")},{"lakes",QStringLiteral("호수")},{"languages",QStringLiteral("언어")},{"ethnicities",QStringLiteral("민족")},{"religions",QStringLiteral("종교")},{"genericFeatures",QStringLiteral("기타 객체")}}) {
        const auto found=p.styles.find(entry.first);const auto style=found==p.styles.end()?PresentationStyle{}:found->second;
        rows.append(QVariantMap{{"key",QString::fromStdString(entry.first)},{"title",entry.second},{"visible",groupVisible(p,entry.first)},{"opacity",style.opacity.value_or(1)},{"content",true}});
    }
    return rows;
}
QVariantMap EditorController::distributionDisplay() const {
    const auto& settings=project_.document().presentation.webPresentation.distributionSettings;
    QString selected;
    if(const auto primary=selection_.primary()) {
        if(primary->domain=="distributionLayer")selected=displayText(primary->id);
        else if(primary->domain=="distributionEntry")for(const auto& entry:project_.document().distributionEntries)if(entry.id==primary->id){selected=displayText(entry.layerId);break;}
    }
    return {{"mode",settings.renderMode==DistributionRenderMode::Intensity?"intensity":"dominant"},{"boundaryVisible",settings.boundaryVisible},{"selectedLayerId",selected},{"intensityAvailable",!selected.isEmpty()}};
}
QVariantMap EditorController::hydroDataStatus() const {
    const auto& settings=project_.document().physicalData;if(settings.source.empty())return QVariantMap{{"ready",false},{"version",displayText(settings.version)},{"dataset",displayText(settings.dataset)},{"error",QStringLiteral("로컬 수계 자료가 선택되지 않았습니다.")}};
    if(hydroRuntime_.isOpen())return QVariantMap{{"ready",true},{"root",displayText(settings.source)},
        {"version",displayText(settings.version)},{"dataset",displayText(settings.dataset)},
        {"viewportLoaded",hydroViewportLoaded()},{"error",QString()}};
    const auto inspected=inspectHydroData(displayText(settings.source));return QVariantMap{{"ready",inspected.ready},{"root",inspected.root},{"version",inspected.version},{"dataset",inspected.dataset},{"error",inspected.error}};
}
void EditorController::requestHydroViewport(double zoom,double mapScale,double originX,double originY,
                                            double width,double height) {
    if(!hydroRuntime_.isOpen()||!std::isfinite(mapScale)||mapScale<=0||
       !std::isfinite(originX)||!std::isfinite(originY)||width<=0||height<=0)return;
    try {
        const auto center=projection_.unproject((width/2-originX)/mapScale,(height/2-originY)/mapScale);
        const auto unitX=projection_.project({1,0}).x-projection_.project({0,0}).x;
        const auto webScale=mapScale*std::min(1.,unitX)*180./3.14159265358979323846;
        hydroRuntime_.requestViewport({pandoeditor::webHydroThreshold(zoom),width,height,webScale,center.x,center.y});
    }catch(const std::exception& error){emit errorOccurred(QString::fromUtf8(error.what()));}
}
bool EditorController::configureHydroData(const QUrl& value) {
    if(hasPendingEdits()||jobBusy()||hasWebImportPreview())return false;
    const auto path=value.isLocalFile()?value.toLocalFile():value.toString();const auto inspected=inspectHydroData(path);if(!inspected.ready){emit errorOccurred(inspected.error);return false;}
    auto settings=project_.document().physicalData;settings.dataset=inspected.dataset.toStdString();settings.version=inspected.version.toStdString();settings.source=inspected.root.toStdString();
    CommandArguments args;args.action=SetPhysicalData{settings};auto request=CommandProcessor::makeRequest(project_,"physical-data.configure",args);auto prepared=CommandProcessor::prepare(project_,request);if(!prepared.ok()||!prepared.preview)return false;const auto result=CommandProcessor::confirm(project_,*prepared.preview);if(!result.ok())return false;
    syncHydroData();
    publish(false);return true;
}
void EditorController::syncHydroData() {
    hydroRuntime_.close(projectInstanceId());
    const auto& source=project_.document().physicalData.source;
    if(source.empty())return;
    QString error;
    if(!hydroRuntime_.open(QString::fromStdString(source),projectInstanceId(),mobileMode_,error))
        emit errorOccurred(error);
}
bool EditorController::setDistributionDisplay(const QString& mode,bool boundaryVisible) {
    if(mode!="dominant"&&mode!="intensity")return false;
    DistributionSettings settings;settings.renderMode=mode=="intensity"?DistributionRenderMode::Intensity:DistributionRenderMode::Dominant;settings.boundaryVisible=boundaryVisible;
    const auto result=PresentationCommandProcessor::apply(project_,SetDistributionSettings{settings});if(result==PresentationResult::Applied)publishPresentation();return result==PresentationResult::Applied||result==PresentationResult::NoOp;
}
QVariantList EditorController::labelLayout(double scale,double originX,double originY,double zoom,double viewportWidth,double viewportHeight) const {
    if(!std::isfinite(scale)||scale<=0||!std::isfinite(originX)||!std::isfinite(originY)||!std::isfinite(zoom))return {};
    const QFontMetricsF metrics(QGuiApplication::font());const auto visuals=countryVisuals();std::vector<LabelLayoutCandidate> candidates;std::map<ObjectRef,QVariantMap> rows;
    for(const auto& pathValue:projection_.paths) {
        const auto path=pathValue.toMap();ObjectRef ref;
        if(path.contains("domain")){if(path["domain"]!="label")continue;ref={"label",path["objectId"].toString().toStdString()};}
        else ref=territorialRef(path["countryId"].toString().toStdString());
        if(!objectVisible(ref))continue;const auto properties=project_.propertyView(ref);if(!properties)continue;
        const auto visualKey=ref.domain=="territorial"?displayText(ref.id):QStringLiteral("content/label/")+displayText(ref.id);const auto visual=visuals.value(visualKey).toMap();
        if(!visual.value("nameVisible").toBool())continue;
        LabelSettings stored;if(const auto found=project_.document().presentation.webPresentation.labelSettings.find(ref);found!=project_.document().presentation.webPresentation.labelSettings.end())stored=found->second;
        std::string kind="country";if(ref.domain=="label")kind=project_.document().labels.at(project_.index().objects.at(ref)).kind;else {const auto unitKind=project_.document().units.at(project_.index().objects.at(ref)).kind;if(unitKind!=UnitKind::Country)kind="region";}
        const auto settings=automaticLabelSettings(kind,stored);double mapX=path["left"].toDouble()+path["width"].toDouble()/2,mapY=path["top"].toDouble()+path["height"].toDouble()/2;
        if(settings.pinned&&settings.manualPosition){const auto point=projection_.project(*settings.manualPosition);mapX=point.x;mapY=point.y;}
        const auto name=QString::fromStdString(properties->displayName);const double x=originX+mapX*scale,y=originY+mapY*scale;
        LabelLayoutCandidate candidate{ref,ref.domain+":"+ref.id,settings.collisionGroup,x,y,std::max(22.,metrics.horizontalAdvance(name)+16),std::max(19.,metrics.height()),settings.priority.value_or(0),settings.minZoom.value_or(0),settings.maxZoom.value_or(std::numeric_limits<double>::infinity()),selection_.has(ref),settings.pinned};candidates.push_back(candidate);
        rows[ref]=QVariantMap{{"ref",objectRefValue(ref)},{"x",x},{"y",y},{"name",name},{"pinned",settings.pinned},{"flagSource",visual.value("flagSource")},{"flagVisible",visual.value("flagVisible")}};
    }
    QVariantList result;for(const auto& ref:layoutLabels(candidates,zoom,mobileMode_?5:3)){const auto row=rows.find(ref);if(row!=rows.end()&&row->second["x"].toDouble()>=-100&&row->second["x"].toDouble()<=viewportWidth+100&&row->second["y"].toDouble()>=-100&&row->second["y"].toDouble()<=viewportHeight+100)result.append(row->second);}return result;
}
bool EditorController::setLabelPinned(const QVariantMap& value,bool pinned,double longitude,double latitude,bool hasPosition) {
    const auto ref=existingObjectRef(value);if(!ref||(ref->domain!="territorial"&&ref->domain!="label"))return false;
    LabelSettings settings;if(const auto found=project_.document().presentation.webPresentation.labelSettings.find(*ref);found!=project_.document().presentation.webPresentation.labelSettings.end())settings=found->second;
    settings.pinned=pinned;if(hasPosition){if(!std::isfinite(longitude)||!std::isfinite(latitude))return false;settings.manualPosition=Point{longitude,latitude};}else if(!pinned)settings.manualPosition.reset();
    const auto result=PresentationCommandProcessor::apply(project_,SetLabelSettings{*ref,settings});if(result==PresentationResult::Applied)publishPresentation();return result==PresentationResult::Applied||result==PresentationResult::NoOp;
}
bool EditorController::resetLabelPosition(const QVariantMap& value) {
    const auto ref=existingObjectRef(value);if(!ref)return false;LabelSettings settings;if(const auto found=project_.document().presentation.webPresentation.labelSettings.find(*ref);found!=project_.document().presentation.webPresentation.labelSettings.end())settings=found->second;settings.pinned=false;settings.manualPosition.reset();
    const auto result=PresentationCommandProcessor::apply(project_,SetLabelSettings{*ref,settings});if(result==PresentationResult::Applied)publishPresentation();return result==PresentationResult::Applied||result==PresentationResult::NoOp;
}
bool EditorController::setLabelMapPosition(const QVariantMap& value,double mapX,double mapY) {
    if(!std::isfinite(mapX)||!std::isfinite(mapY))return false;const auto position=projection_.unproject(mapX,mapY);return setLabelPinned(value,true,position.x,position.y,true);
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
    auto group=ref->domain=="territorial"?territorialGroup(project_.document().units.at(project_.index().objects.at(*ref)).kind):contentGroup(project_.document(),*ref);
    auto r=PresentationCommandProcessor::apply(project_,SetScopedVisibility{group,{*ref},visible});if(r==PresentationResult::Applied)publishPresentation();return r==PresentationResult::Applied||r==PresentationResult::NoOp;
}

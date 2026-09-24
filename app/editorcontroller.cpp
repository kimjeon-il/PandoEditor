#include "editorcontroller.h"
#include "defaultflagresolver.h"
#include <QFile>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <cmath>
#include <map>
#include <stdexcept>

namespace {
QString text(const std::string& value) { return QString::fromStdString(value); }
QString rgb(std::uint32_t color) { return QString("#%1").arg(color,6,16,QChar('0')); }

bool validImageDataUrl(const QString& source)
{
    const auto separator=source.indexOf(QStringLiteral(";base64,"));
    if(!source.startsWith(QStringLiteral("data:image/")) || separator < 11) return false;
    const auto bytes=QByteArray::fromBase64(source.mid(separator+8).toLatin1());
    if(bytes.isEmpty()) return false;
    return bytes.startsWith("\x89PNG\r\n\x1a\n") || bytes.startsWith("\xff\xd8\xff") ||
           bytes.startsWith("GIF87a") || bytes.startsWith("GIF89a") ||
           (bytes.startsWith("RIFF") && bytes.mid(8,4)=="WEBP") ||
           bytes.trimmed().startsWith("<svg");
}

bool geometryBindingsChanged(const pandoeditor::ProjectDocument& before,
                            const pandoeditor::ProjectDocument& after)
{
    if(before.units.size()!=after.units.size()) return true;
    std::map<std::string,pandoeditor::GeometryRef> bindings;
    for(const auto& unit:before.units) bindings.emplace(unit.id,unit.geometry);
    for(const auto& unit:after.units) {
        const auto found=bindings.find(unit.id);
        if(found==bindings.end() || !(found->second==unit.geometry)) return true;
    }
    return false;
}
}
EditorController::EditorController(QObject* parent):EditorController(EditorControllerConfig{},parent) {}
EditorController::EditorController(EditorControllerConfig config,QObject* parent)
    :QObject(parent),storage_(std::move(config.privateProjectPath)),mobileMode_(config.mobileMode)
{
    QFile sample(":/assets/sample.pando.json");
    if(!sample.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot read bundled sample");
    project_.replace(projectcodec::decode(sample.readAll()));
    projection_.rebuild(project_.document());selectionInstance_=project_.instanceId();reloadDrafts();
    connect(&hydroRuntime_,&HydroRuntimeProvider::frameChanged,this,&EditorController::hydroFrameChanged);
    connect(&hydroRuntime_,&HydroRuntimeProvider::loadFailed,this,&EditorController::errorOccurred);
    connect(this,&EditorController::dirtyChanged,this,[this](){++importEditEpoch_;});
    connect(this,&EditorController::stateChanged,this,&EditorController::propertyChanged);
    connect(this,&EditorController::draftsChanged,this,&EditorController::propertyChanged);
    connect(this,&EditorController::jobChanged,this,&EditorController::propertyChanged);
    connect(this,&EditorController::webImportChanged,this,&EditorController::propertyChanged);
    jobs_=std::make_unique<CommandJobRunner>([this]() ->const pandoeditor::Project& {return project_;});
    connect(jobs_.get(),&CommandJobRunner::changed,this,&EditorController::jobChanged,Qt::QueuedConnection);
    presentationSaveTimer_.setSingleShot(true);presentationSaveTimer_.setInterval(500);
    connect(&presentationSaveTimer_,&QTimer::timeout,this,&EditorController::flushPresentationRecovery);
}
QVariantMap EditorController::colors() const
{
    QVariantMap result;
    for(const auto& unit:project_.document().units) {
        const auto style=project_.document().presentation.objectStyles.find(pandoeditor::territorialRef(unit.id));
        if(style!=project_.document().presentation.objectStyles.end()) result[text(unit.id)]=rgb(pandoeditor::effectiveObjectColor(project_.document(),pandoeditor::territorialRef(unit.id)));
    }
    return result;
}
QVariantMap EditorController::countryVisuals() const
{
    QVariantMap result;std::optional<std::string> selectedDistribution;
    if(const auto primary=selection_.primary()) {
        if(primary->domain=="distributionLayer")selectedDistribution=primary->id;
        else if(primary->domain=="distributionEntry")for(const auto& entry:project_.document().distributionEntries)if(entry.id==primary->id){selectedDistribution=entry.layerId;break;}
    }
    const auto distributionRows=pandoeditor::visibleDistributionEntries(project_.document(),selectedDistribution);
    const std::set<pandoeditor::ObjectRef> visibleDistribution(distributionRows.begin(),distributionRows.end());
    for(const auto& unit:project_.document().units) {
        const auto ref=pandoeditor::territorialRef(unit.id);
        const auto style=project_.document().presentation.objectStyles.find(ref);
        if(style==project_.document().presentation.objectStyles.end()) continue;
        const auto resolved=pandoeditor::resolvedTerritorialPresentation(project_.document(),ref);
        const auto nativeLayer=pandoeditor::nativeLayerId(project_.document(),ref);double nativeOpacity=1;int nativeOrder=-1;
        for(std::size_t i=0;i<project_.document().presentation.userLayers.size();++i)if(project_.document().presentation.userLayers[i].id==nativeLayer){nativeOpacity=project_.document().presentation.userLayers[i].opacity;nativeOrder=int(i);break;}
        QString flag;bool flagAvailable=false;QString flagReason;bool retainedFlag=false;
        for(const auto& e:project_.document().extensions)if(e.status=="unsupported" && e.jsonPointer.size()>=12 && e.jsonPointer.compare(e.jsonPointer.size()-12,12,"/flagDataUrl")==0 && std::find(e.dependencies.begin(),e.dependencies.end(),ref)!=e.dependencies.end()) {
            const auto value=QJsonDocument::fromJson("["+QByteArray::fromStdString(e.payload)+"]").array();
            if(!value.isEmpty()&&value[0].isString()&&validImageDataUrl(value[0].toString())) {flag=value[0].toString();flagAvailable=true;retainedFlag=true;}
        }
        if(!retainedFlag||project_.document().symbols.count(ref)){const auto resolvedFlag=resolveDefaultFlag(project_.document(),ref);flag=resolvedFlag.source;flagAvailable=resolvedFlag.available;flagReason=resolvedFlag.reason;}
        result[text(unit.id)]=QVariantMap{{"color",rgb(pandoeditor::effectiveObjectColor(project_.document(),pandoeditor::territorialRef(unit.id)))},
            {"name",text(project_.propertyView(ref)->displayName)},{"nameVisible",resolved.nameVisible},{"flagVisible",resolved.flagVisible},{"flagSource",flag},{"flagAvailable",flagAvailable},{"flagReason",flagReason},
            {"boundary",pandoeditor::resolvedTerritorialPresentation(project_.document(),ref).boundaryVisible},
            {"blendMode",text(resolved.blendMode)},
            {"kind",unit.kind==pandoeditor::UnitKind::Country?QStringLiteral("country"):unit.kind==pandoeditor::UnitKind::Subunit?QStringLiteral("subunit"):QStringLiteral("region")},
            {"layerId",text(nativeLayer)},{"layerOpacity",nativeOpacity},{"layerOrder",nativeOrder},
            {"rank",pandoeditor::territorialRenderOrder(project_.document(),ref)},
            {"visible",objectVisible(ref)},
            {"opacity",(text(unit.id)==selected_&&opacityPreview_?*opacityPreview_:style->second.opacity)*resolved.opacity}};
    }
    for(const auto& [ref,index]:project_.index().objects) if(ref.domain!="territorial") {
        const auto properties=project_.propertyView(ref); if(!properties) continue;
        const auto group=pandoeditor::contentGroup(project_.document(),ref);
        double opacity=1; const auto style=project_.document().presentation.webPresentation.styles.find(group);
        if(style!=project_.document().presentation.webPresentation.styles.end()) opacity=style->second.opacity.value_or(1);
        const auto layerId=pandoeditor::nativeLayerId(project_.document(),ref); double layerOpacity=1; int layerOrder=-1;
        for(std::size_t i=0;i<project_.layers().size();++i) if(project_.layers()[i].id==layerId) {layerOrder=int(i);layerOpacity=project_.layers()[i].opacity;}
        if(ref.domain=="distributionEntry") {
            if(!visibleDistribution.count(ref))continue;
            opacity=pandoeditor::distributionFillAlpha(project_.document().distributionEntries.at(index).share,opacity);
        }
        result[QStringLiteral("content/")+text(ref.domain)+"/"+text(ref.id)]=QVariantMap{
            {"color",rgb(properties->effectiveColor)},{"name",text(properties->displayName)},
            {"nameVisible",ref.domain=="label"},{"visible",objectVisible(ref)},{"opacity",opacity},
            {"kind",text(ref.domain)},{"rank",ref.domain=="label"?100:ref.domain=="hydro"?60:ref.domain=="generic"?70:50},
            {"layerId",text(layerId)},{"layerOrder",layerOrder},{"layerOpacity",layerOpacity},{"boundary",ref.domain=="distributionEntry"&&project_.document().presentation.webPresentation.distributionSettings.boundaryVisible},{"blendMode","normal"}};
    }
    return result;
}
QVariantMap EditorController::layerVisuals() const
{
    QVariantMap result;
    for(const auto& l:project_.layers()) result[text(l.id)]=QVariantMap{
        {"visible",l.visible},{"locked",l.locked},{"opacity",text(l.id)==selectedLayer_&&layerOpacityPreview_?*layerOpacityPreview_:l.opacity}};
    return result;
}
QVariantList EditorController::layers() const
{
    QVariantList result;
    const auto& layers=project_.layers();
    QVariantList unassigned;
    for(const auto& path:projection_.paths)if(pandoeditor::nativeLayerId(project_.document(),pandoeditor::territorialRef(path.toMap()["countryId"].toString().toStdString())).empty())unassigned.append(path);
    if(!unassigned.empty())result.append(QVariantMap{{"id",""},{"name",QStringLiteral("웹 기본 표시")},{"visible",true},{"locked",false},{"opacity",1.},{"order",-1},{"paths",unassigned},{"count",unassigned.size()}});
    for(int i=static_cast<int>(layers.size())-1;i>=0;--i) {
        const auto& l=layers[static_cast<std::size_t>(i)];
        QVariantList paths;
        for(const auto& path:projection_.paths) {
            const auto id=path.toMap()["countryId"].toString();
            const auto member=project_.document().presentation.membership.find(pandoeditor::territorialRef(id.toStdString()));
            if(member!=project_.document().presentation.membership.end()&&member->second==l.id) paths.push_back(path);
        }
        result.append(QVariantMap{{"id",text(l.id)},{"name",text(l.name)},{"visible",l.visible},
            {"locked",l.locked},{"opacity",l.opacity},{"order",i},{"paths",paths},{"count",paths.size()}});
    }
    return result;
}
QVariantList EditorController::countryRows() const
{
    QVariantList result;
    for(const auto& c:project_.countries()) {
        const auto l=project_.layer(c.layerId);
        result.append(QVariantMap{{"id",text(c.id)},{"name",text(project_.propertyView(pandoeditor::territorialRef(c.id))->displayName)},{"layerId",text(c.layerId)},
            {"visible",objectVisible(pandoeditor::territorialRef(c.id))},{"locked",(l&&l->locked)||c.locked},
            {"limited",!pandoeditor::effectAllowed(project_.document(),pandoeditor::territorialRef(c.id),"color")}});
    }
    return result;
}
QString EditorController::selectedName() const
{
    const auto ref=selection_.primary();if(!ref)return {};
    const auto view=project_.propertyView(*ref);return view?text(view->displayName):QString{};
}
QString EditorController::countryLayerId() const
{
    const auto it=project_.document().presentation.membership.find(pandoeditor::territorialRef(selected_.toStdString()));
    return it==project_.document().presentation.membership.end()?QString():text(it->second);
}
double EditorController::countryOpacity() const
{
    if(opacityPreview_) return *opacityPreview_;
    const auto c=project_.country(selected_.toStdString());return c?c->opacity:1;
}
double EditorController::layerOpacity() const
{
    if(layerOpacityPreview_) return *layerOpacityPreview_;
    const auto l=project_.layer(selectedLayer_.toStdString());return l?l->opacity:1;
}
bool EditorController::canDeleteLayer() const
{
    if(project_.layers().size()<=1) return false;
    const auto dependents=project_.index().dependents.find({"userLayer",selectedLayer_.toStdString()});
    if(dependents!=project_.index().dependents.end()&&!dependents->second.empty()) return false;
    if(!pandoeditor::effectAllowed(project_.document(),{"userLayer",selectedLayer_.toStdString()},"delete")) return false;
    return project_.layer(selectedLayer_.toStdString())!=nullptr;
}
QString EditorController::fileName() const {return filePath_.isEmpty()?QStringLiteral("새 프로젝트"):QFileInfo(filePath_).fileName();}
QString EditorController::documentNotice() const
{
    QString notice=QStringLiteral("저장 형식: Qt v6 · 이전 앱에서는 열 수 없습니다. 열기만으로 원본 파일은 변경되지 않습니다.");
    const auto& d=project_.document();
    if(d.nativeSourceVersion<4)notice+=QStringLiteral(" 이전 Qt 파일의 색은 명시값으로 보존했습니다. 과거 상속 의도와 최초 국명은 복원할 수 없으며 현재 값을 우선합니다.");
    if(d.units.size()>project_.countries().size())
        notice+=QStringLiteral(" 하위단위·지방 %1개의 기본 속성을 편집할 수 있습니다. 관계 편집은 후속 단계입니다.").arg(d.units.size()-project_.countries().size());
    if(!d.extensions.empty())
        notice+=QStringLiteral(" 미해석 데이터 %1개 보존 중: 관련 편집이 제한될 수 있습니다.").arg(d.extensions.size());
    return notice;
}
bool EditorController::dirty() const {return importedDirty_||project_.dirty()||hasPendingEdits();}
bool EditorController::hasPendingEdits() const
{
    // GeometryEditSession is intentionally not document data.  Treat it as a
    // pending draft so document-level undo/navigation cannot silently replace
    // its baseline while a user is dragging vertices.
    if(geometryEdit_||contentSession_) return true;
    if(!parkedCountryDrafts_.empty()||!parkedLayerDrafts_.empty()) return true;
    const auto u=selectedUnit();
    if(u && (nameDraft_!=QString::fromStdString(u->kind==pandoeditor::UnitKind::Country?pandoeditor::objectDisplayName(*u):u->name)
       || memoDraft_!=text(u->notes) || colorDraft_!=rgb(pandoeditor::effectiveObjectColor(project_.document(),pandoeditor::territorialRef(u->id)))
       || validFromDraft_!=text(u->validity.from.value_or("")) || validToDraft_!=text(u->validity.to.value_or(""))
       || (opacityPreview_&&*opacityPreview_!=project_.document().presentation.objectStyles.at(pandoeditor::territorialRef(u->id)).opacity)))return true;
    const auto l=project_.layer(selectedLayer_.toStdString());
    return l&&(layerNameDraft_!=text(l->name)||(layerOpacityPreview_&&*layerOpacityPreview_!=l->opacity));
}
void EditorController::setNameDraft(const QString& value) {if(!selectionTransition_&&selectedEditable()){cancelPreview();nameDraft_=value;emit draftsChanged();emit dirtyChanged();}}
void EditorController::setMemoDraft(const QString& value) {if(!selectionTransition_&&selectedEditable()){cancelPreview();memoDraft_=value;emit draftsChanged();emit dirtyChanged();}}
void EditorController::setColorDraft(const QString& value) {if(!selectionTransition_&&selectedEditable()){cancelPreview();colorDraft_=value;emit draftsChanged();emit dirtyChanged();}}
void EditorController::setLayerNameDraft(const QString& value) {cancelPreview();layerNameDraft_=value;emit draftsChanged();emit dirtyChanged();}
void EditorController::reloadDrafts()
{
    const auto u=selectedUnit();
    nameDraft_=u?text(u->kind==pandoeditor::UnitKind::Country?pandoeditor::objectDisplayName(*u):u->name):QString();
    memoDraft_=u?text(u->notes):QString();
    colorDraft_=u?rgb(pandoeditor::effectiveObjectColor(project_.document(),pandoeditor::territorialRef(u->id))):QString();
    validFromDraft_=u?text(u->validity.from.value_or("")):QString();
    validToDraft_=u?text(u->validity.to.value_or("")):QString();
    const auto l=project_.layer(selectedLayer_.toStdString());layerNameDraft_=l?text(l->name):QString();
    opacityPreview_.reset();layerOpacityPreview_.reset();
    restoreParkedDrafts();
}
void EditorController::publish(bool pruneSelection)
{
    Q_UNUSED(pruneSelection);
    QScopedValueRollback<bool> guard(selectionTransition_,true);
    // Hidden/locked is not missing. A selected object remains addressable from a list.
    reconcileSelection();
    if(!project_.layer(selectedLayer_.toStdString())) selectedLayer_=project_.layers().empty()?QString():text(project_.layers().back().id);
    reloadDrafts();
    emit stateChanged();emit selectionChanged();emit searchChanged();emit hoverChanged();
    emit visualChanged();emit draftsChanged();emit dirtyChanged();
    emit structureChanged();
    emit presentationChanged();
    if(presentationSaveInstance_==project_.instanceId()) {
        if(project_.dirty()||importedDirty_)presentationSaveTimer_.start();
        else discardOwnPresentationRecovery();
    }
}
void EditorController::previewCountryOpacity(double value)
{
    if(!selectedEditable()||!std::isfinite(value)||value<0||value>1) return;
    cancelPreview();opacityPreview_=value;emit visualChanged();emit draftsChanged();emit dirtyChanged();
}
void EditorController::previewLayerOpacity(double value)
{
    if(!std::isfinite(value)||value<0||value>1) return;
    cancelPreview();layerOpacityPreview_=value;emit visualChanged();emit draftsChanged();emit dirtyChanged();
}
void EditorController::addLayer()
{
    const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    if(executeCommand("layer.add",pandoeditor::AddLayer{id.toStdString(),"새 레이어"})) {selectedLayer_=id;publish(false);}
}
void EditorController::removeLayer() {if(executeCommand("layer.remove",pandoeditor::RemoveLayer{selectedLayer_.toStdString()}))publish();}
void EditorController::moveLayer(int delta) {if(executeCommand("layer.move",pandoeditor::MoveLayer{selectedLayer_.toStdString(),delta}))publish(false);}
void EditorController::setLayerVisible(bool value) {if(executeCommand("layer.visibility",pandoeditor::SetLayerVisible{selectedLayer_.toStdString(),value}))publish();}
void EditorController::setLayerLocked(bool value) {if(executeCommand("layer.lock",pandoeditor::SetLayerLocked{selectedLayer_.toStdString(),value}))publish();}
void EditorController::moveCountry(const QString& layerId) {if(executeCommand("country.move",pandoeditor::MoveCountry{selected_.toStdString(),layerId.toStdString()}))publish();}
void EditorController::undo()
{
    if(hasPendingEdits()){emit errorOccurred(QStringLiteral("PENDING_EDITS: 편집 중인 내용을 먼저 적용하거나 취소하세요."));return;}
    const auto before=project_.document();
    cancelPreview();if(project_.undo()){const auto changed=geometryBindingsChanged(before,project_.document());if(changed) projection_.rebuild(project_.document());if(before.physicalData.source!=project_.document().physicalData.source)syncHydroData();publish();if(changed) emit geometryChanged();}
}
void EditorController::redo()
{
    if(hasPendingEdits()){emit errorOccurred(QStringLiteral("PENDING_EDITS: 편집 중인 내용을 먼저 적용하거나 취소하세요."));return;}
    const auto before=project_.document();
    cancelPreview();if(project_.redo()){const auto changed=geometryBindingsChanged(before,project_.document());if(changed) projection_.rebuild(project_.document());if(before.physicalData.source!=project_.document().physicalData.source)syncHydroData();publish();if(changed) emit geometryChanged();}
}
bool EditorController::openFile(const QUrl& url)
{
    if(geometryEdit_){emit errorOccurred(QStringLiteral("GEOMETRY_EDIT_ACTIVE: 도형 편집을 확인하거나 취소한 뒤 프로젝트를 여세요."));return false;}
    try {
        if(!url.isLocalFile())throw std::runtime_error("Please choose a local file");
        return replaceFromBytes(storage_.read(url),false,url.toLocalFile());
    }catch(const std::exception& e){emit errorOccurred(QString::fromUtf8(e.what()));return false;}
}
bool EditorController::saveFile(const QUrl& url)
{
    if(geometryEdit_){emit errorOccurred(QStringLiteral("GEOMETRY_EDIT_ACTIVE: 미확정 도형은 저장되지 않습니다. 먼저 확인하거나 취소하세요."));return false;}
    if(isProtectedWebSource(url)){webImportFailure(QStringLiteral("SOURCE_OVERWRITE_BLOCKED: 웹 원본은 덮어쓰지 않습니다. 다른 이름으로 저장하세요."));return false;}
    if(!commitPendingEdits())return false;
    if(!url.isLocalFile()){emit errorOccurred(QStringLiteral("로컬 파일을 선택해 주세요."));return false;}
    try {
        storage_.write(url,projectcodec::encode(project_));
        filePath_=url.toLocalFile();importedDirty_=false;project_.markSaved();publish(false);return true;
    }catch(const std::exception& e){emit errorOccurred(QString::fromUtf8(e.what()));return false;}
}
bool EditorController::save(){if(mobileMode_)return savePrivate();return saveFile(QUrl::fromLocalFile(filePath_));}
bool EditorController::replaceFromBytes(const QByteArray& bytes,bool imported,const QString& path)
{
    if(geometryEdit_){emit errorOccurred(QStringLiteral("GEOMETRY_EDIT_ACTIVE: 도형 편집을 확인하거나 취소한 뒤 프로젝트를 바꾸세요."));return false;}
    pandoeditor::Project candidate;candidate.replace(projectcodec::decode(bytes));
    MapProjection nextProjection;nextProjection.rebuild(candidate.document());
    cancelPreview();cancelStructureMutation();project_=std::move(candidate);projection_=std::move(nextProjection);
    syncHydroData();
    filePath_=path;importedDirty_=imported;selected_.clear();selectedLayer_=project_.layers().empty()?QString():text(project_.layers().back().id);
    emit geometryChanged();publish(false);return true;
}
bool EditorController::restorePrivateProject()
{
    if(!mobileMode_||!storage_.privateProjectExists())return true;
    try {
        const auto restored=replaceFromBytes(storage_.readPrivate(),false);
        if(privateRecoveryRequired_){privateRecoveryRequired_=false;emit privateRecoveryRequiredChanged();}
        return restored;
    }catch(const std::exception& e){
        const bool newlyBlocked=!privateRecoveryRequired_;privateRecoveryRequired_=true;
        emit errorOccurred(QStringLiteral("저장된 프로젝트를 복원하지 못했습니다. 원본 파일은 보존되며, 명시적으로 복구를 허용하기 전에는 덮어쓰지 않습니다.\n%1").arg(QString::fromUtf8(e.what())));
        if(newlyBlocked)emit privateRecoveryRequiredChanged();return false;
    }
}
bool EditorController::importProject(const QUrl& url)
{
    if(url.isEmpty())return true;
    try {return replaceFromBytes(storage_.read(url),true);}
    catch(const std::exception& e){emit errorOccurred(QString::fromUtf8(e.what()));return false;}
}
bool EditorController::savePrivate()
{
    if(geometryEdit_){emit errorOccurred(QStringLiteral("GEOMETRY_EDIT_ACTIVE: 미확정 도형은 저장되지 않습니다. 먼저 확인하거나 취소하세요."));return false;}
    if(isProtectedWebSource(QUrl::fromLocalFile(storage_.privateProjectPath()))){webImportFailure(QStringLiteral("SOURCE_OVERWRITE_BLOCKED: 웹 원본은 덮어쓰지 않습니다."));return false;}
    if(privateRecoveryRequired_){emit errorOccurred(QStringLiteral("손상된 저장 파일이 보존되어 있습니다. 덮어쓰기를 허용한 뒤 다시 저장해 주세요."));return false;}
    if(!commitPendingEdits())return false;
    try {storage_.writePrivateAtomic(projectcodec::encode(project_));importedDirty_=false;project_.markSaved();publish(false);return true;}
    catch(const std::exception& e){emit errorOccurred(QString::fromUtf8(e.what()));return false;}
}
bool EditorController::exportProject(const QUrl& url)
{
    if(isProtectedWebSource(url)){webImportFailure(QStringLiteral("SOURCE_OVERWRITE_BLOCKED: 웹 원본은 덮어쓰지 않습니다."));return false;}
    if(url.isEmpty())return true;
    try {const auto snapshot=storage_.readPrivate();storage_.write(url,snapshot);return true;}
    catch(const std::exception& e){emit errorOccurred(QString::fromUtf8(e.what()));return false;}
}
bool EditorController::confirmPrivateRecovery()
{
    if(!privateRecoveryRequired_)return true;
    try {storage_.preserveCorruptPrivate();privateRecoveryRequired_=false;emit privateRecoveryRequiredChanged();return true;}
    catch(const std::exception& e){emit errorOccurred(QString::fromUtf8(e.what()));return false;}
}

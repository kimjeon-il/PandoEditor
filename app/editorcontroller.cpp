#include "editorcontroller.h"
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <cmath>
#include <stdexcept>

namespace {
QString text(const std::string& value) { return QString::fromStdString(value); }
QString rgb(std::uint32_t color) { return QString("#%1").arg(color,6,16,QChar('0')); }
}
EditorController::EditorController(QObject* parent):EditorController(EditorControllerConfig{}, parent) {}
EditorController::EditorController(EditorControllerConfig config, QObject* parent)
    : QObject(parent), storage_(std::move(config.privateProjectPath)), mobileMode_(config.mobileMode)
{
    QFile sample(":/assets/sample.pando.json");
    if(!sample.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot read bundled sample");
    project_.replace(projectcodec::decode(sample.readAll()));
    projection_.rebuild(project_.countries()); reloadDrafts();
    jobs_=std::make_unique<CommandJobRunner>([this]() ->const pandoeditor::Project& {return project_;});
    connect(jobs_.get(),&CommandJobRunner::changed,this,&EditorController::jobChanged,Qt::QueuedConnection);
}
QVariantMap EditorController::colors() const
{
    QVariantMap result;
    for(const auto& c:project_.countries()) result[text(c.id)]=rgb(c.color);
    return result;
}
QVariantMap EditorController::countryVisuals() const
{
    QVariantMap result;
    for(const auto& c:project_.countries()) result[text(c.id)]=QVariantMap{
        {"color",rgb(c.color)},{"opacity",text(c.id)==selected_ && opacityPreview_ ? *opacityPreview_ : c.opacity}};
    return result;
}
QVariantMap EditorController::layerVisuals() const
{
    QVariantMap result;
    for(const auto& l:project_.layers()) result[text(l.id)]=QVariantMap{
        {"visible",l.visible},{"locked",l.locked},{"opacity",text(l.id)==selectedLayer_ && layerOpacityPreview_ ? *layerOpacityPreview_ : l.opacity}};
    return result;
}
QVariantList EditorController::layers() const
{
    QVariantList result;
    const auto& layers=project_.layers();
    for(int i=static_cast<int>(layers.size())-1;i>=0;--i) {
        const auto& l=layers[static_cast<std::size_t>(i)];
        QVariantList paths;
        for(const auto& path:projection_.paths) {
            const auto id=path.toMap()["countryId"].toString();
            const auto c=project_.country(id.toStdString());
            if(c && c->layerId==l.id) paths.push_back(path);
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
        result.append(QVariantMap{{"id",text(c.id)},{"name",text(c.name)},{"layerId",text(c.layerId)},
            {"visible",l->visible},{"locked",l->locked || c.locked}});
    }
    return result;
}
QString EditorController::selectedName() const
{
    const auto c=project_.country(selected_.toStdString()); return c ? text(c->name) : QString();
}
QString EditorController::countryLayerId() const
{
    const auto c=project_.country(selected_.toStdString()); return c ? text(c->layerId) : QString();
}
double EditorController::countryOpacity() const
{
    if(opacityPreview_) return *opacityPreview_;
    const auto c=project_.country(selected_.toStdString()); return c ? c->opacity : 1;
}
double EditorController::layerOpacity() const
{
    if(layerOpacityPreview_) return *layerOpacityPreview_;
    const auto l=project_.layer(selectedLayer_.toStdString()); return l ? l->opacity : 1;
}
bool EditorController::canDeleteLayer() const
{
    if(project_.layers().size()<=1) return false;
    const auto dependents=project_.index().dependents.find({"userLayer",selectedLayer_.toStdString()});
    if(dependents!=project_.index().dependents.end() && !dependents->second.empty()) return false;
    if(!pandoeditor::effectAllowed(project_.document(),{"userLayer",selectedLayer_.toStdString()},"delete")) return false;
    return project_.layer(selectedLayer_.toStdString())!=nullptr;
}
QString EditorController::fileName() const { return filePath_.isEmpty() ? QStringLiteral("새 프로젝트") : QFileInfo(filePath_).fileName(); }
QString EditorController::documentNotice() const
{
    QString notice=QStringLiteral("저장 형식: Qt v3 · 이전 앱에서는 열 수 없습니다. 열기만으로 원본 파일은 변경되지 않습니다.");
    const auto& d=project_.document();
    if(d.units.size()>project_.countries().size())
        notice+=QStringLiteral(" 하위단위·지방 %1개는 보존되며 현재 화면에는 표시되지 않습니다.").arg(d.units.size()-project_.countries().size());
    if(!d.extensions.empty())
        notice+=QStringLiteral(" 미해석 데이터 %1개 보존 중: 관련 편집이 제한될 수 있습니다.").arg(d.extensions.size());
    return notice;
}
bool EditorController::dirty() const { return importedDirty_ || project_.dirty() || hasPendingEdits(); }
bool EditorController::hasPendingEdits() const
{
    const auto c=project_.country(selected_.toStdString());
    if(c && (nameDraft_!=text(c->name) || memoDraft_!=text(c->memo) || colorDraft_!=rgb(c->color) ||
             (opacityPreview_ && *opacityPreview_!=c->opacity))) return true;
    const auto l=project_.layer(selectedLayer_.toStdString());
    return l && (layerNameDraft_!=text(l->name) || (layerOpacityPreview_ && *layerOpacityPreview_!=l->opacity));
}
void EditorController::setNameDraft(const QString& value) { if(selectedEditable()) { cancelPreview(); nameDraft_=value; emit draftsChanged(); emit dirtyChanged(); } }
void EditorController::setMemoDraft(const QString& value) { if(selectedEditable()) { cancelPreview(); memoDraft_=value; emit draftsChanged(); emit dirtyChanged(); } }
void EditorController::setColorDraft(const QString& value) { if(selectedEditable()) { cancelPreview(); colorDraft_=value; emit draftsChanged(); emit dirtyChanged(); } }
void EditorController::setLayerNameDraft(const QString& value) { cancelPreview(); layerNameDraft_=value; emit draftsChanged(); emit dirtyChanged(); }
void EditorController::reloadDrafts()
{
    const auto c=project_.country(selected_.toStdString());
    nameDraft_=c ? text(c->name) : QString(); memoDraft_=c ? text(c->memo) : QString();
    colorDraft_=c ? rgb(c->color) : QString();
    const auto l=project_.layer(selectedLayer_.toStdString());
    layerNameDraft_=l ? text(l->name) : QString();
    opacityPreview_.reset(); layerOpacityPreview_.reset();
}
void EditorController::publish(bool pruneSelection)
{
    const auto c=project_.country(selected_.toStdString());
    const auto l=c ? project_.layer(c->layerId) : nullptr;
    if(!c || (pruneSelection && (!l->visible || l->locked))) selected_.clear();
    if(!project_.layer(selectedLayer_.toStdString())) selectedLayer_=text(project_.layers().back().id);
    reloadDrafts();
    emit stateChanged(); emit visualChanged(); emit draftsChanged(); emit dirtyChanged();
}
void EditorController::previewCountryOpacity(double value)
{
    if(!selectedEditable() || !std::isfinite(value) || value<0 || value>1) return;
    cancelPreview(); opacityPreview_=value; emit visualChanged(); emit draftsChanged(); emit dirtyChanged();
}
void EditorController::previewLayerOpacity(double value)
{
    if(!std::isfinite(value) || value<0 || value>1) return;
    cancelPreview(); layerOpacityPreview_=value; emit visualChanged(); emit draftsChanged(); emit dirtyChanged();
}
void EditorController::selectAt(double x,double y)
{
    if(!std::isfinite(x) || !std::isfinite(y) || !commitPendingEdits()) return;
    selected_=text(project_.pick(projection_.unproject(x,y))); publish(false);
}
void EditorController::selectCountry(const QString& id)
{
    if(!commitPendingEdits()) return;
    selected_=project_.country(id.toStdString()) ? id : QString(); publish(false);
}
void EditorController::selectLayer(const QString& id)
{
    if(!project_.layer(id.toStdString()) || !commitPendingEdits()) return;
    selectedLayer_=id; publish(false);
}
void EditorController::addLayer()
{
    const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    if(executeCommand("layer.add",pandoeditor::AddLayer{id.toStdString(),"새 레이어"})) {
        selectedLayer_=id; publish(false);
    }
}
void EditorController::removeLayer()
{
    if(executeCommand("layer.remove",pandoeditor::RemoveLayer{selectedLayer_.toStdString()})) publish();
}
void EditorController::moveLayer(int delta)
{
    if(executeCommand("layer.move",pandoeditor::MoveLayer{selectedLayer_.toStdString(),delta})) publish(false);
}
void EditorController::setLayerVisible(bool value)
{
    if(executeCommand("layer.visibility",pandoeditor::SetLayerVisible{selectedLayer_.toStdString(),value})) publish();
}
void EditorController::setLayerLocked(bool value)
{
    if(executeCommand("layer.lock",pandoeditor::SetLayerLocked{selectedLayer_.toStdString(),value})) publish();
}
void EditorController::moveCountry(const QString& layerId)
{
    if(executeCommand("country.move",pandoeditor::MoveCountry{selected_.toStdString(),layerId.toStdString()})) publish();
}
void EditorController::undo()
{
    if(hasPendingEdits()) {
        emit errorOccurred(QStringLiteral("PENDING_EDITS: 편집 중인 내용을 먼저 적용하거나 취소하세요.")); return;
    }
    cancelPreview(); if(project_.undo()) publish();
}
void EditorController::redo()
{
    if(hasPendingEdits()) {
        emit errorOccurred(QStringLiteral("PENDING_EDITS: 편집 중인 내용을 먼저 적용하거나 취소하세요.")); return;
    }
    cancelPreview(); if(project_.redo()) publish();
}
bool EditorController::openFile(const QUrl& url)
{
    try {
        if(!url.isLocalFile()) throw std::runtime_error("Please choose a local file");
        return replaceFromBytes(storage_.read(url), false, url.toLocalFile());
    } catch(const std::exception& e) { emit errorOccurred(QString::fromUtf8(e.what())); return false; }
}
bool EditorController::saveFile(const QUrl& url)
{
    if(!commitPendingEdits()) return false;
    if(!url.isLocalFile()) { emit errorOccurred(QStringLiteral("로컬 파일을 선택해 주세요.")); return false; }
    try {
        storage_.write(url, projectcodec::encode(project_));
        filePath_=url.toLocalFile(); importedDirty_=false; project_.markSaved(); publish(false); return true;
    } catch(const std::exception& e) { emit errorOccurred(QString::fromUtf8(e.what())); return false; }
}
bool EditorController::save()
{
    if(mobileMode_) return savePrivate();
    return saveFile(QUrl::fromLocalFile(filePath_));
}

bool EditorController::replaceFromBytes(const QByteArray& bytes, bool imported, const QString& path)
{
    pandoeditor::Project candidate;
    candidate.replace(projectcodec::decode(bytes));
    MapProjection nextProjection; nextProjection.rebuild(candidate.countries());
    cancelPreview();
    project_=std::move(candidate); projection_=std::move(nextProjection);
    filePath_=path; importedDirty_=imported; selected_.clear(); selectedLayer_=text(project_.layers().back().id);
    emit geometryChanged(); publish(false); return true;
}

bool EditorController::restorePrivateProject()
{
    if(!mobileMode_ || !storage_.privateProjectExists()) return true;
    try {
        const auto restored=replaceFromBytes(storage_.readPrivate(), false);
        if(privateRecoveryRequired_) {
            privateRecoveryRequired_=false;
            emit privateRecoveryRequiredChanged();
        }
        return restored;
    } catch(const std::exception& e) {
        const bool newlyBlocked=!privateRecoveryRequired_;
        privateRecoveryRequired_=true;
        emit errorOccurred(QStringLiteral("저장된 프로젝트를 복원하지 못했습니다. 원본 파일은 보존되며, 명시적으로 복구를 허용하기 전에는 덮어쓰지 않습니다.\n%1")
                               .arg(QString::fromUtf8(e.what())));
        if(newlyBlocked) emit privateRecoveryRequiredChanged();
        return false;
    }
}

bool EditorController::importProject(const QUrl& url)
{
    if(url.isEmpty()) return true;
    try {
        return replaceFromBytes(storage_.read(url), true);
    } catch(const std::exception& e) {
        emit errorOccurred(QString::fromUtf8(e.what()));
        return false;
    }
}

bool EditorController::savePrivate()
{
    if(privateRecoveryRequired_) {
        emit errorOccurred(QStringLiteral("손상된 저장 파일이 보존되어 있습니다. 덮어쓰기를 허용한 뒤 다시 저장해 주세요."));
        return false;
    }
    if(!commitPendingEdits()) return false;
    try {
        storage_.writePrivateAtomic(projectcodec::encode(project_));
        importedDirty_=false; project_.markSaved(); publish(false); return true;
    } catch(const std::exception& e) {
        emit errorOccurred(QString::fromUtf8(e.what()));
        return false;
    }
}

bool EditorController::exportProject(const QUrl& url)
{
    if(url.isEmpty()) return true;
    try {
        const auto snapshot=storage_.readPrivate();
        storage_.write(url, snapshot);
        return true;
    } catch(const std::exception& e) {
        emit errorOccurred(QString::fromUtf8(e.what()));
        return false;
    }
}

bool EditorController::confirmPrivateRecovery()
{
    if(!privateRecoveryRequired_) return true;
    try {
        storage_.preserveCorruptPrivate();
        privateRecoveryRequired_=false;
        emit privateRecoveryRequiredChanged();
        return true;
    } catch(const std::exception& e) {
        emit errorOccurred(QString::fromUtf8(e.what()));
        return false;
    }
}

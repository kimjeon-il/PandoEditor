#include "editorcontroller.h"
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUuid>
#include <cmath>
#include <stdexcept>

namespace {
QString text(const std::string& value) { return QString::fromStdString(value); }
QString rgb(std::uint32_t color) { return QString("#%1").arg(color,6,16,QChar('0')); }
bool validColor(const QString& value) { return QRegularExpression("^#[0-9a-fA-F]{6}$").match(value).hasMatch(); }
}
EditorController::EditorController(QObject* parent):EditorController(EditorControllerConfig{}, parent) {}
EditorController::EditorController(EditorControllerConfig config, QObject* parent)
    : QObject(parent), storage_(std::move(config.privateProjectPath)), mobileMode_(config.mobileMode)
{
    QFile sample(":/assets/sample.pando.json");
    if(!sample.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot read bundled sample");
    project_.replace(projectcodec::decode(sample.readAll()));
    projection_.rebuild(project_.countries()); reloadDrafts();
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
bool EditorController::dirty() const
{
    if(importedDirty_) return true;
    if(project_.dirty()) return true;
    const auto c=project_.country(selected_.toStdString());
    if(c && (nameDraft_!=text(c->name) || memoDraft_!=text(c->memo) || colorDraft_!=rgb(c->color) ||
             (opacityPreview_ && *opacityPreview_!=c->opacity))) return true;
    const auto l=project_.layer(selectedLayer_.toStdString());
    return l && (layerNameDraft_!=text(l->name) || (layerOpacityPreview_ && *layerOpacityPreview_!=l->opacity));
}
void EditorController::setNameDraft(const QString& value) { if(selectedEditable()) { nameDraft_=value; emit draftsChanged(); emit dirtyChanged(); } }
void EditorController::setMemoDraft(const QString& value) { if(selectedEditable()) { memoDraft_=value; emit draftsChanged(); emit dirtyChanged(); } }
void EditorController::setColorDraft(const QString& value) { if(selectedEditable()) { colorDraft_=value; emit draftsChanged(); emit dirtyChanged(); } }
void EditorController::setLayerNameDraft(const QString& value) { layerNameDraft_=value; emit draftsChanged(); emit dirtyChanged(); }
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
bool EditorController::commitPendingEdits()
{
    const auto c=project_.country(selected_.toStdString());
    if(c && selectedEditable()) {
        if(nameDraft_.trimmed().isEmpty() || !validColor(colorDraft_)) {
            nameDraft_=text(c->name); colorDraft_=rgb(c->color);
            emit draftsChanged(); emit dirtyChanged();
            emit errorOccurred(QStringLiteral("국가 이름을 비우거나 RGB 색상을 잘못 입력할 수 없습니다."));
            return false;
        }
    }
    const auto l=project_.layer(selectedLayer_.toStdString());
    if(l && layerNameDraft_.trimmed().isEmpty()) {
        layerNameDraft_=text(l->name); emit draftsChanged(); emit dirtyChanged();
        emit errorOccurred(QStringLiteral("레이어 이름을 입력해 주세요.")); return false;
    }
    const auto allowed=[&](bool changed,const pandoeditor::ObjectRef& ref,const char* effect) {
        return !changed || pandoeditor::effectAllowed(project_.document(),ref,effect);
    };
    bool safe=true;
    if(c) {
        const auto ref=pandoeditor::territorialRef(c->id);
        safe=allowed(nameDraft_.trimmed().toStdString()!=c->name,ref,"name") &&
             allowed(memoDraft_.toStdString()!=c->memo,ref,"notes") &&
             allowed(colorDraft_.mid(1).toUInt(nullptr,16)!=c->color,ref,"color") &&
             allowed(opacityPreview_ && *opacityPreview_!=c->opacity,ref,"opacity");
    }
    if(l) safe=safe && allowed(layerNameDraft_.trimmed().toStdString()!=l->name,{"userLayer",l->id},"name") &&
        allowed(layerOpacityPreview_ && *layerOpacityPreview_!=l->opacity,{"userLayer",l->id},"opacity");
    if(!safe) { emit errorOccurred(QStringLiteral("UNSUPPORTED_DEPENDENCY: 미해석 데이터 보호를 위해 이 편집을 제한합니다. 초안은 유지됩니다.")); return false; }
    bool changed=false;
    if(c && selectedEditable()) {
        changed|=project_.renameCountry(c->id,nameDraft_.trimmed().toStdString());
        changed|=project_.setMemo(c->id,memoDraft_.toStdString());
        changed|=project_.setColor(c->id,colorDraft_.mid(1).toUInt(nullptr,16));
        if(opacityPreview_) changed|=project_.setCountryOpacity(c->id,*opacityPreview_);
    }
    if(l) {
        // Layer mutation replaces the layer vector; use the stable ID, not the pointer afterwards.
        const auto id=l->id;
        changed|=project_.renameLayer(id,layerNameDraft_.trimmed().toStdString());
        if(layerOpacityPreview_) changed|=project_.setLayerOpacity(id,*layerOpacityPreview_);
    }
    if(changed) publish(false);
    else { reloadDrafts(); emit draftsChanged(); emit visualChanged(); emit dirtyChanged(); }
    return true;
}
void EditorController::previewCountryOpacity(double value)
{
    if(!selectedEditable() || !std::isfinite(value) || value<0 || value>1) return;
    opacityPreview_=value; emit visualChanged(); emit dirtyChanged();
}
void EditorController::previewLayerOpacity(double value)
{
    if(!std::isfinite(value) || value<0 || value>1) return;
    layerOpacityPreview_=value; emit visualChanged(); emit dirtyChanged();
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
    if(!commitPendingEdits() || !project_.layer(id.toStdString())) return;
    selectedLayer_=id; publish(false);
}
void EditorController::setColor(const QString& color)
{
    if(!validColor(color) || !commitPendingEdits()) return;
    if(project_.setColor(selected_.toStdString(),color.mid(1).toUInt(nullptr,16))) publish(false);
}
void EditorController::addLayer()
{
    if(!commitPendingEdits()) return;
    auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    if(project_.addLayer(id.toStdString(),"새 레이어")) { selectedLayer_=id; publish(false); }
}
void EditorController::removeLayer()
{
    if(!commitPendingEdits()) return;
    if(project_.removeLayer(selectedLayer_.toStdString())) publish();
    else emit errorOccurred(QStringLiteral("빈 레이어만 삭제할 수 있으며 마지막 레이어는 유지해야 합니다."));
}
void EditorController::moveLayer(int delta) { if(commitPendingEdits() && project_.moveLayer(selectedLayer_.toStdString(),delta)) publish(false); }
void EditorController::setLayerVisible(bool value) { if(commitPendingEdits() && project_.setLayerVisible(selectedLayer_.toStdString(),value)) publish(); }
void EditorController::setLayerLocked(bool value) { if(commitPendingEdits() && project_.setLayerLocked(selectedLayer_.toStdString(),value)) publish(); }
void EditorController::moveCountry(const QString& layerId)
{
    if(!commitPendingEdits()) return;
    if(project_.moveCountry(selected_.toStdString(),layerId.toStdString())) publish();
}
void EditorController::undo() { if(commitPendingEdits() && project_.undo()) publish(); }
void EditorController::redo() { if(commitPendingEdits() && project_.redo()) publish(); }
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

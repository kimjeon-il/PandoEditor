#include "editorcontroller.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QGuiApplication>
#include <QStyleHints>
#include <cmath>

namespace {
bool updateFinite(const QVariantMap& values,const char* key,double& target)
{
    if(!values.contains(key))return true;
    bool ok=false;const double value=values.value(key).toDouble(&ok);
    if(!ok||!std::isfinite(value))return false;
    target=value;return true;
}

QVariantMap defaultAppearance()
{
    return {{"theme",QStringLiteral("system")},{"accentPreset",QStringLiteral("blue")},
            {"statusBarVisible",true},{"smoothLines",true}};
}

QVariantMap normalizedAppearance(const QVariantMap& value,const QVariantMap& fallback=defaultAppearance())
{
    auto result=fallback;
    const auto theme=value.value("theme").toString();
    if(theme=="system"||theme=="light"||theme=="dark")result["theme"]=theme;
    const auto accent=value.value("accentPreset").toString();
    static const QStringList accents={"red","orange","green","teal","blue","purple","pink"};
    if(accents.contains(accent))result["accentPreset"]=accent;
    if(value.contains("statusBarVisible"))result["statusBarVisible"]=value.value("statusBarVisible").toBool();
    if(value.contains("smoothLines"))result["smoothLines"]=value.value("smoothLines").toBool();
    return result;
}
}

QVariantMap EditorController::appearancePreferences() const
{
    auto result=appearance_;
    const auto theme=result.value("theme").toString();
    result["effectiveTheme"]=theme=="system"?
        (QGuiApplication::styleHints()->colorScheme()==Qt::ColorScheme::Dark?QStringLiteral("dark"):QStringLiteral("light")):theme;
    return result;
}

void EditorController::loadAppearancePreferences()
{
    appearance_=defaultAppearance();
    if(appearancePath_.isEmpty())return;
    QFile file(appearancePath_);if(!file.open(QIODevice::ReadOnly))return;
    const auto document=QJsonDocument::fromJson(file.readAll());
    if(!document.isObject()||document.object().value("version").toInt()!=2)return;
    appearance_=normalizedAppearance(document.object().value("appearance").toObject().toVariantMap());
}

bool EditorController::saveAppearancePreferences() const
{
    if(appearancePath_.isEmpty())return true;
    if(!QDir().mkpath(QFileInfo(appearancePath_).absolutePath()))return false;
    QJsonObject root{{"version",2},{"appearance",QJsonObject::fromVariantMap(appearance_)}};
    QSaveFile file(appearancePath_);
    return file.open(QIODevice::WriteOnly)&&file.write(QJsonDocument(root).toJson(QJsonDocument::Compact))>=0&&file.commit();
}

void EditorController::beginAppearancePreview()
{
    if(appearancePreviewOpen_)return;
    appearanceOrigin_=appearance_;appearancePreviewOpen_=true;emit appearanceChanged();
}

bool EditorController::previewAppearance(const QVariantMap& changes)
{
    if(!appearancePreviewOpen_)return false;
    const auto next=normalizedAppearance(changes,appearance_);
    if(next==appearance_)return true;
    appearance_=next;emit appearanceChanged();return true;
}

void EditorController::resetAppearancePreview()
{
    if(!appearancePreviewOpen_)return;
    appearance_=defaultAppearance();emit appearanceChanged();
}

void EditorController::cancelAppearancePreview()
{
    if(!appearancePreviewOpen_)return;
    appearance_=appearanceOrigin_;appearanceOrigin_.clear();appearancePreviewOpen_=false;emit appearanceChanged();
}

bool EditorController::applyAppearancePreview()
{
    if(!appearancePreviewOpen_)return false;
    if(!saveAppearancePreferences())return false;
    appearanceOrigin_.clear();appearancePreviewOpen_=false;emit appearanceChanged();return true;
}

QString EditorController::terrainMode() const {return terrainMode_;}
bool EditorController::setTerrainMode(const QString& value)
{
    const auto mode=value.toLower();if(mode!="none"&&mode!="gray"&&mode!="color")return false;
    if(mode==terrainMode_)return true;terrainMode_=mode;
    if(mode=="none"&&terrainProvider_)terrainProvider_->protectVisible({});
    emit terrainChanged();return true;
}

QString EditorController::projectionMode() const
{
    return sceneBridge_.viewState().mode==ProjectionMode::Globe?QStringLiteral("globe"):QStringLiteral("flat");
}

QVariantMap EditorController::mapViewState() const
{
    const auto view=sceneBridge_.viewState();
    return {{"projection",view.mode==ProjectionMode::Globe?QStringLiteral("globe"):QStringLiteral("flat")},
        {"viewportWidth",view.viewportWidth},{"viewportHeight",view.viewportHeight},
        {"centerLongitude",view.centerLongitude},{"centerLatitude",view.centerLatitude},
        {"rotationLongitude",view.rotationLongitude},{"rotationLatitude",view.rotationLatitude},
        {"rotationRoll",view.rotationRoll},{"scale",view.scale},{"translateX",view.translateX},
        {"translateY",view.translateY},{"devicePixelRatio",view.devicePixelRatio},
        {"revision",QVariant::fromValue<qulonglong>(view.revision)}};
}

bool EditorController::publishMapView(const QVariantMap& values)
{
    auto view=sceneBridge_.viewState();
    if(!updateFinite(values,"viewportWidth",view.viewportWidth)||
       !updateFinite(values,"viewportHeight",view.viewportHeight)||
       !updateFinite(values,"centerLongitude",view.centerLongitude)||
       !updateFinite(values,"centerLatitude",view.centerLatitude)||
       !updateFinite(values,"rotationLongitude",view.rotationLongitude)||
       !updateFinite(values,"rotationLatitude",view.rotationLatitude)||
       !updateFinite(values,"rotationRoll",view.rotationRoll)||
       !updateFinite(values,"scale",view.scale)||
       !updateFinite(values,"translateX",view.translateX)||
       !updateFinite(values,"translateY",view.translateY)||
       !updateFinite(values,"devicePixelRatio",view.devicePixelRatio)||!validMapViewState(view))return false;
    sceneBridge_.publishView(view);
    const auto published=sceneBridge_.viewState();
    if(published.mode==ProjectionMode::Globe)globeView_=published;else flatView_=published;
    return true;
}

bool EditorController::setProjectionMode(const QString& value)
{
    ProjectionMode mode;
    if(value.compare(QStringLiteral("globe"),Qt::CaseInsensitive)==0)mode=ProjectionMode::Globe;
    else if(value.compare(QStringLiteral("flat"),Qt::CaseInsensitive)==0)mode=ProjectionMode::Flat;
    else return false;
    const auto current=sceneBridge_.viewState();
    if(current.mode==mode)return true;
    if(current.mode==ProjectionMode::Globe)globeView_=current;else flatView_=current;
    auto next=mode==ProjectionMode::Globe?globeView_:flatView_;
    next.mode=mode;
    sceneBridge_.publishView(next);
    return true;
}

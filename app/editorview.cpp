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
    invalidateViewportResources(ViewportResourceKind::Terrain);
    emit terrainChanged();return true;
}

MapCameraMetrics EditorController::mapCameraMetrics() const
{
    return {projection_.width,projection_.height,projection_.cosLatitudeValue(),
            projection_.minXValue(),projection_.maxLatitudeValue()};
}

bool EditorController::publishCameraView()
{
    sceneBridge_.publishView(camera_.view());
    camera_.acceptPublishedView(sceneBridge_.viewState());
    return true;
}

void EditorController::syncMapCameraMetrics(bool publishCurrent)
{
    const bool changed=camera_.setMetrics(mapCameraMetrics());
    if(!changed)return;
    if(publishCurrent&&camera_.mode()==ProjectionMode::Flat)publishCameraView();
    else emit viewStateChanged();
}

QString EditorController::projectionMode() const
{
    return camera_.mode()==ProjectionMode::Globe?QStringLiteral("globe"):QStringLiteral("flat");
}

QVariantMap EditorController::mapViewState() const
{
    const auto display=camera_.display();
    const auto& state=display.view;
    return {{"projection",state.mode==ProjectionMode::Globe?QStringLiteral("globe"):QStringLiteral("flat")},
        {"viewportWidth",state.viewportWidth},{"viewportHeight",state.viewportHeight},
        {"centerLongitude",state.centerLongitude},{"centerLatitude",state.centerLatitude},
        {"rotationLongitude",state.rotationLongitude},{"rotationLatitude",state.rotationLatitude},
        {"rotationRoll",state.rotationRoll},{"scale",state.scale},{"translateX",state.translateX},
        {"translateY",state.translateY},{"devicePixelRatio",state.devicePixelRatio},
        {"revision",QVariant::fromValue<qulonglong>(state.revision)},
        {"zoom",display.zoom},{"flatZoom",display.flatZoom},{"globeZoom",display.globeZoom},
        {"panX",display.panX},{"panY",display.panY},{"fitScale",display.fitScale},
        {"mapScale",display.mapScale},{"originX",display.originX},{"originY",display.originY}};
}

bool EditorController::publishMapView(const QVariantMap& values)
{
    auto state=sceneBridge_.viewState();
    if(!updateFinite(values,"viewportWidth",state.viewportWidth)||
       !updateFinite(values,"viewportHeight",state.viewportHeight)||
       !updateFinite(values,"centerLongitude",state.centerLongitude)||
       !updateFinite(values,"centerLatitude",state.centerLatitude)||
       !updateFinite(values,"rotationLongitude",state.rotationLongitude)||
       !updateFinite(values,"rotationLatitude",state.rotationLatitude)||
       !updateFinite(values,"rotationRoll",state.rotationRoll)||
       !updateFinite(values,"scale",state.scale)||
       !updateFinite(values,"translateX",state.translateX)||
       !updateFinite(values,"translateY",state.translateY)||
       !updateFinite(values,"devicePixelRatio",state.devicePixelRatio)||!validMapViewState(state))return false;
    camera_.adoptView(state);
    return publishCameraView();
}

bool EditorController::setProjectionMode(const QString& value)
{
    ProjectionMode mode;
    if(value.compare(QStringLiteral("globe"),Qt::CaseInsensitive)==0)mode=ProjectionMode::Globe;
    else if(value.compare(QStringLiteral("flat"),Qt::CaseInsensitive)==0)mode=ProjectionMode::Flat;
    else return false;
    worldFocusDetail_=false;
    if(!camera_.setProjectionMode(mode)){refreshTypedScene();return true;}
    return publishCameraView();
}

bool EditorController::resizeMapCamera(double width,double height,double devicePixelRatio)
{
    syncMapCameraMetrics(false);
    if(!camera_.resize(width,height,devicePixelRatio))return true;
    return publishCameraView();
}

bool EditorController::zoomMapCameraAt(double factor,double x,double y)
{
    if(!std::isfinite(factor)||factor<=0||!std::isfinite(x)||!std::isfinite(y))return false;
    worldFocusDetail_=false;
    if(!camera_.zoomAt(factor,x,y)){refreshTypedScene();return false;}
    return publishCameraView();
}

void EditorController::beginMapCameraPan()
{
    worldFocusDetail_=false;refreshTypedScene();
    camera_.beginPan();
}

bool EditorController::updateMapCameraPan(double deltaX,double deltaY)
{
    if(!camera_.panFromGesture(deltaX,deltaY))return false;
    return publishCameraView();
}

void EditorController::endMapCameraPan()
{
    camera_.endPan();
}

bool EditorController::fitMapCamera()
{
    worldFocusDetail_=false;
    if(!camera_.fit()){refreshTypedScene();return true;}
    return publishCameraView();
}

bool EditorController::focusMapCameraRect(double left,double top,double width,double height,double maxZoom)
{
    closeObjectChooser();
    if(!std::isfinite(left)||!std::isfinite(top)||!std::isfinite(width)||width<=0||
       !std::isfinite(height)||height<=0||!std::isfinite(maxZoom)||maxZoom<=0)return false;
    camera_.focusRect(left,top,width,height,maxZoom);
    // Focus is synchronous today. Keep detail through resize/publication until
    // the next explicit navigation, rather than tying it to a timer.
    worldFocusDetail_=true;refreshTypedScene();
    if(camera_.mode()==ProjectionMode::Flat)return publishCameraView();
    emit viewStateChanged();
    return true;
}

void EditorController::setMapEditorActive(bool active)
{
    if(mapEditorActive_==active)return;
    mapEditorActive_=active;refreshTypedScene();
}

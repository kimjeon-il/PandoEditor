#pragma once
#include <QQuickPaintedItem>
#include <QVariantList>
#include <QVariantMap>
#include <memory>
#include "renderscene.h"
#include "mapviewstate.h"
#include "../app/mapscenebridge.h"
#include <QPointer>

struct HydroRuntimeFrame;
class HydroRuntimeProvider;

// One deterministic canvas owns fill, boundary and selected-outline passes.
// QML remains responsible for the viewport transform and edit overlays only.
class MapRenderItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QVariantList paths READ paths WRITE setPaths NOTIFY pathsChanged)
    Q_PROPERTY(QVariantMap visuals READ visuals WRITE setVisuals NOTIFY visualsChanged)
    Q_PROPERTY(QVariantList selectedPaths READ selectedPaths WRITE setSelectedPaths NOTIFY selectedPathsChanged)
    Q_PROPERTY(QString primaryId READ primaryId WRITE setPrimaryId NOTIFY primaryIdChanged)
    Q_PROPERTY(double originX READ originX WRITE setOriginX NOTIFY viewportChanged)
    Q_PROPERTY(double originY READ originY WRITE setOriginY NOTIFY viewportChanged)
    Q_PROPERTY(double mapScale READ mapScale WRITE setMapScale NOTIFY viewportChanged)
    Q_PROPERTY(QObject* hydroSource READ hydroSource WRITE setHydroSource NOTIFY hydroSourceChanged)
    Q_PROPERTY(QVariantMap hydroProjection READ hydroProjection WRITE setHydroProjection NOTIFY hydroPresentationChanged)
    Q_PROPERTY(QVariantMap hydroStyle READ hydroStyle WRITE setHydroStyle NOTIFY hydroPresentationChanged)
    Q_PROPERTY(QVariantList hiddenHydroIds READ hiddenHydroIds WRITE setHiddenHydroIds NOTIFY hydroPresentationChanged)
    Q_PROPERTY(QString selectedHydroId READ selectedHydroId WRITE setSelectedHydroId NOTIFY hydroPresentationChanged)
    Q_PROPERTY(QObject* sceneBridge READ sceneBridge WRITE setSceneBridge NOTIFY sceneBridgeChanged)
    Q_PROPERTY(qulonglong sceneRevision READ sceneRevision NOTIFY sceneBridgeChanged)
    Q_PROPERTY(qulonglong viewRevision READ viewRevision NOTIFY sceneBridgeChanged)
    Q_PROPERTY(bool smoothLines READ smoothLines WRITE setSmoothLines NOTIFY smoothLinesChanged)
public:
    explicit MapRenderItem(QQuickItem* parent=nullptr);
    void paint(QPainter*) override;
    QVariantList paths() const{return paths_;} void setPaths(QVariantList);
    QVariantMap visuals() const{return visuals_;} void setVisuals(QVariantMap);
    QVariantList selectedPaths() const{return selected_;} void setSelectedPaths(QVariantList);
    QString primaryId() const{return primary_;} void setPrimaryId(QString);
    double originX() const{return originX_;} void setOriginX(double);
    double originY() const{return originY_;} void setOriginY(double);
    double mapScale() const{return scale_;} void setMapScale(double);
    QObject* hydroSource() const; void setHydroSource(QObject*);
    QVariantMap hydroProjection() const{return hydroProjection_;} void setHydroProjection(QVariantMap);
    QVariantMap hydroStyle() const{return hydroStyle_;} void setHydroStyle(QVariantMap);
    QVariantList hiddenHydroIds() const{return hiddenHydroIds_;} void setHiddenHydroIds(QVariantList);
    QString selectedHydroId() const{return selectedHydroId_;} void setSelectedHydroId(QString);
    QObject* sceneBridge() const{return sceneBridge_;} void setSceneBridge(QObject*);
    qulonglong sceneRevision() const{return typedScene_?typedScene_->revision:0;}
    qulonglong viewRevision() const{return typedView_.revision;}
    bool smoothLines() const{return smoothLines_;} void setSmoothLines(bool);
    void setHydroFrame(std::shared_ptr<const HydroRuntimeFrame>);
    void setSceneSnapshot(std::shared_ptr<const RenderScene>,const MapViewState&);
signals:
    void pathsChanged();void visualsChanged();void selectedPathsChanged();void primaryIdChanged();void viewportChanged();
    void hydroSourceChanged();void hydroPresentationChanged();
    void sceneBridgeChanged();
    void smoothLinesChanged();
private:
    void syncSceneBridge();
    QVariantList paths_,selected_;QVariantMap visuals_;QString primary_;
    double originX_=0,originY_=0,scale_=1;
    HydroRuntimeProvider* hydroSource_=nullptr;
    std::shared_ptr<const HydroRuntimeFrame> hydroFrame_;
    QVariantMap hydroProjection_,hydroStyle_;
    QVariantList hiddenHydroIds_;
    QString selectedHydroId_;
    std::shared_ptr<const RenderScene> typedScene_;
    MapViewState typedView_;
    QPointer<MapSceneBridge> sceneBridge_;
    bool smoothLines_=true;
};

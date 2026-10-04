#pragma once

#include <pandoeditor/map/renderscene.h>
#include <pandoeditor/map/mapviewstate.h>
#include "../app/mapscenebridge.h"
#include <QPointer>
#include <QQuickPaintedItem>
#include <memory>
#include <atomic>

// CPU fallback backend. It consumes exactly the same immutable RenderScene and
// MapViewState as the Qt Scene Graph GPU backend; no legacy path/visual/hydro
// presentation model is retained here.
class MapRenderItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QObject* sceneBridge READ sceneBridge WRITE setSceneBridge NOTIFY sceneBridgeChanged)
    Q_PROPERTY(qulonglong sceneRevision READ sceneRevision NOTIFY sceneBridgeChanged)
    Q_PROPERTY(qulonglong viewRevision READ viewRevision NOTIFY sceneBridgeChanged)
    Q_PROPERTY(bool smoothLines READ smoothLines WRITE setSmoothLines NOTIFY smoothLinesChanged)
public:
    explicit MapRenderItem(QQuickItem* parent=nullptr);
    void paint(QPainter*) override;

    QObject* sceneBridge() const{return sceneBridge_;}
    void setSceneBridge(QObject*);
    qulonglong sceneRevision() const{return scene_?scene_->revision:0;}
    qulonglong viewRevision() const{return view_.revision;}
    bool smoothLines() const{return smoothLines_;}
    void setSmoothLines(bool);
    std::uint64_t paintCount() const {return paintCount_.load();}
    double paintMilliseconds() const {return paintNanoseconds_.load()/1.e6;}

    void setSceneSnapshot(std::shared_ptr<const RenderScene>,const MapViewState&);
signals:
    void sceneBridgeChanged();
    void smoothLinesChanged();
private:
    void syncSceneBridge();

    std::shared_ptr<const RenderScene> scene_;
    std::shared_ptr<const MapFrame> frame_;
    MapViewState view_;
    QPointer<MapSceneBridge> sceneBridge_;
    bool smoothLines_=true;
    std::atomic<std::uint64_t> paintCount_{0};
    std::atomic<qint64> paintNanoseconds_{0};
};

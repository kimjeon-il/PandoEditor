#pragma once
#include "../app/mapscenebridge.h"
#include "../app/terrainimageprovider.h"
#include <QQuickItem>
#include <QPointer>
#include <atomic>

// One retained terrain stage, independent of visible-tile QML delegates.
class TerrainLayerItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject* sceneBridge READ sceneBridge WRITE setSceneBridge NOTIFY changed)
    Q_PROPERTY(QObject* terrainBridge READ terrainBridge WRITE setTerrainBridge NOTIFY changed)
    Q_PROPERTY(QQuickItem* landMaskSource READ landMaskSource WRITE setLandMaskSource NOTIFY changed)
public:
    explicit TerrainLayerItem(QQuickItem* parent=nullptr);
    ~TerrainLayerItem() override;
    QObject* sceneBridge() const{return scene_;}
    QObject* terrainBridge() const{return terrain_;}
    QQuickItem* landMaskSource() const{return mask_;}
    void setSceneBridge(QObject*);
    void setTerrainBridge(QObject*);
    void setLandMaskSource(QQuickItem*);
signals:
    void changed();
protected:
    QSGNode* updatePaintNode(QSGNode*,UpdatePaintNodeData*) override;
private:
    void attachWindow(QQuickWindow*);
    void publishOwner(bool alive);
    QPointer<MapSceneBridge> scene_;
    QPointer<TerrainImageBridge> terrain_;
    QPointer<QQuickItem> mask_;
    QPointer<QQuickWindow> connectedWindow_;
    std::atomic<quint64> windowEpoch_{0},contextEpoch_{0};
    std::atomic<bool> graphAlive_{false};
};

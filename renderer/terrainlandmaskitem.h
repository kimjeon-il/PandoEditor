#pragma once
#include "../app/mapscenebridge.h"
#include "scenegraph/mapscenenode.h"
#include <QPointer>
#include <QQuickItem>
#include <QSizeF>
#include <QVector4D>
#include <atomic>
#include <memory>

class QSGTexture;
class QSGTextureProvider;
class QQuickWindow;

// Prepared for sampling in the current ordered scene-graph pass. This is not
// a GPU fence or an onscreen presentation receipt.
struct TerrainLandMaskFrame {
    bool ready=false;
    qulonglong viewRevision=0,sceneRevision=0,resourceGeneration=0;
    QSizeF logicalSize;
    QSize textureSize;
    QVector4D uvTransform{1,1,0,0};
};

class TerrainLandMaskItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject* sceneBridge READ sceneBridge WRITE setSceneBridge NOTIFY sceneBridgeChanged)
    Q_PROPERTY(QQuickItem* textureSource READ textureSource WRITE setTextureSource NOTIFY textureSourceChanged)
    Q_PROPERTY(double originX READ originX WRITE setOriginX NOTIFY viewportChanged)
    Q_PROPERTY(double originY READ originY WRITE setOriginY NOTIFY viewportChanged)
    Q_PROPERTY(double mapScale READ mapScale WRITE setMapScale NOTIFY viewportChanged)
    Q_PROPERTY(double mapCosLatitude READ mapCosLatitude WRITE setMapCosLatitude NOTIFY viewportChanged)
    Q_PROPERTY(double mapMinX READ mapMinX WRITE setMapMinX NOTIFY viewportChanged)
    Q_PROPERTY(double mapMaxLatitude READ mapMaxLatitude WRITE setMapMaxLatitude NOTIFY viewportChanged)
    Q_PROPERTY(QVector4D maskUvTransform READ maskUvTransform WRITE setMaskUvTransform NOTIFY viewportChanged)
    Q_PROPERTY(bool maskReady READ maskReady NOTIFY maskReadyChanged)
    Q_PROPERTY(qulonglong maskViewRevision READ maskViewRevision NOTIFY maskReadyChanged)
    Q_PROPERTY(qulonglong maskSceneRevision READ maskSceneRevision NOTIFY maskReadyChanged)
    Q_PROPERTY(QSize maskPixelSize READ maskPixelSize NOTIFY maskReadyChanged)
    Q_PROPERTY(qulonglong resourceGeneration READ resourceGeneration NOTIFY maskReadyChanged)
    Q_PROPERTY(qulonglong geometryUploadCount READ geometryUploadCount NOTIFY statsChanged)
    Q_PROPERTY(qulonglong geometryBytes READ geometryBytes NOTIFY statsChanged)
public:
    explicit TerrainLandMaskItem(QQuickItem* parent=nullptr);
    ~TerrainLandMaskItem() override;
    QObject* sceneBridge() const {return bridge_;}
    void setSceneBridge(QObject*);
    QQuickItem* textureSource() const {return textureSource_;}
    void setTextureSource(QQuickItem*);
    bool isTextureProvider() const override;
    QSGTextureProvider* textureProvider() const override;
    double originX() const {return flat_.originX;} void setOriginX(double);
    double originY() const {return flat_.originY;} void setOriginY(double);
    double mapScale() const {return flat_.mapScale;} void setMapScale(double);
    double mapCosLatitude() const {return flat_.cosLatitude;} void setMapCosLatitude(double);
    double mapMinX() const {return flat_.minX;} void setMapMinX(double);
    double mapMaxLatitude() const {return flat_.maxLatitude;} void setMapMaxLatitude(double);
    QVector4D maskUvTransform() const {return uvTransform_;}
    void setMaskUvTransform(QVector4D);
    bool maskReady() const;
    qulonglong maskViewRevision() const;
    qulonglong maskSceneRevision() const;
    QSize maskPixelSize() const;
    qulonglong resourceGeneration() const {return resourceGeneration_.load();}
    qulonglong geometryUploadCount() const {return geometryUploadCount_.load();}
    qulonglong geometryBytes() const {return geometryBytes_.load();}

    // Render-thread only. The consumer must update the QSGDynamicTexture first,
    // then re-read provider->texture(). False only authenticates a clean texture
    // whose prior physical packet, view, dimensions and generation are identical.
    void noteRenderThreadTextureUse(QSGTextureProvider*,QSGTexture*,bool updatedThisFrame);
    TerrainLandMaskFrame renderThreadMaskFrame() const;
signals:
    void sceneBridgeChanged();
    void textureSourceChanged();
    void viewportChanged();
    void maskReadyChanged();
    void statsChanged();
protected:
    QSGNode* updatePaintNode(QSGNode*,UpdatePaintNodeData*) override;
private:
    void attachWindow(QQuickWindow*);
    void invalidateResources();
    void publishPreparedFrame(const TerrainLandMaskFrame&);
    struct RenderState;
    std::unique_ptr<RenderState> renderState_;
    QPointer<MapSceneBridge> bridge_;
    QPointer<QQuickWindow> connectedWindow_;
    QPointer<QQuickItem> textureSource_;
    MapFlatViewport flat_;
    QVector4D uvTransform_{1,1,0,0};
    std::atomic<qulonglong> resourceGeneration_{1},geometryUploadCount_{0},geometryBytes_{0};
    std::shared_ptr<const TerrainLandMaskFrame> publishedFrame_;
};

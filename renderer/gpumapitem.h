#pragma once
#include <QVariantMap>

#include "mapscenebridge.h"
#include "scenegraph/mapscenenode.h"
#include <QQuickItem>
#include <QPointer>
#include <atomic>
#include <QString>
#include <mutex>

// Immutable CPU/QSG submission observation, not a driver fence or DWM receipt.
struct MapRenderObservation {
    std::shared_ptr<const MapFrame> frame;
    MapGpuStats stats;
    qulonglong windowGeneration=0,resourceGeneration=0,bridgeGeneration=0;
    bool rendererReady=false;
    QVariantList strokeInventory;
};

class GpuMapItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject* sceneBridge READ sceneBridge WRITE setSceneBridge NOTIFY sceneBridgeChanged)
    Q_PROPERTY(bool rendererReady READ rendererReady NOTIFY rendererReadyChanged)
    Q_PROPERTY(bool forcedGpu READ forcedGpu CONSTANT)
    Q_PROPERTY(QString diagnostic READ diagnostic NOTIFY rendererReadyChanged)
    Q_PROPERTY(double originX READ originX WRITE setOriginX NOTIFY viewportChanged)
    Q_PROPERTY(double originY READ originY WRITE setOriginY NOTIFY viewportChanged)
    Q_PROPERTY(double mapScale READ mapScale WRITE setMapScale NOTIFY viewportChanged)
    Q_PROPERTY(double mapCosLatitude READ mapCosLatitude WRITE setMapCosLatitude NOTIFY viewportChanged)
    Q_PROPERTY(double mapMinX READ mapMinX WRITE setMapMinX NOTIFY viewportChanged)
    Q_PROPERTY(double mapMaxLatitude READ mapMaxLatitude WRITE setMapMaxLatitude NOTIFY viewportChanged)
    Q_PROPERTY(qulonglong sceneRevision READ sceneRevision NOTIFY statsChanged)
    Q_PROPERTY(qulonglong geometryUploadCount READ geometryUploadCount NOTIFY statsChanged)
    Q_PROPERTY(qulonglong materialUpdateCount READ materialUpdateCount NOTIFY statsChanged)
    Q_PROPERTY(qulonglong viewUniformUpdateCount READ viewUniformUpdateCount NOTIFY statsChanged)
    Q_PROPERTY(qulonglong geometryBytes READ geometryBytes NOTIFY statsChanged)
    Q_PROPERTY(qulonglong visibleCountryCount READ visibleCountryCount NOTIFY statsChanged)
    Q_PROPERTY(qulonglong drawIndexCount READ drawIndexCount NOTIFY statsChanged)
    Q_PROPERTY(qulonglong fullIndexCount READ fullIndexCount NOTIFY statsChanged)
    Q_PROPERTY(qulonglong uploadBytesThisFrame READ uploadBytesThisFrame NOTIFY statsChanged)
    Q_PROPERTY(bool uploadsPending READ uploadsPending NOTIFY statsChanged)
    Q_PROPERTY(qulonglong resourceGeneration READ resourceGeneration NOTIFY statsChanged)
    Q_PROPERTY(qulonglong uploadBudgetBytes READ uploadBudgetBytes WRITE setUploadBudgetBytes NOTIFY viewportChanged)
public:
    explicit GpuMapItem(QQuickItem* parent=nullptr);
    ~GpuMapItem() override;
    QObject* sceneBridge() const{return bridge_;}
    void setSceneBridge(QObject* bridge);
    bool rendererReady() const{return ready_;}
    bool forcedGpu() const;
    QString diagnostic() const{return diagnostic_;}
    double originX() const{return flat_.originX;}
    double originY() const{return flat_.originY;}
    double mapScale() const{return flat_.mapScale;}
    double mapCosLatitude() const{return flat_.cosLatitude;}
    double mapMinX() const{return flat_.minX;}
    double mapMaxLatitude() const{return flat_.maxLatitude;}
    void setOriginX(double value);
    void setOriginY(double value);
    void setMapScale(double value);
    void setMapCosLatitude(double value);
    void setMapMinX(double value);
    void setMapMaxLatitude(double value);
    qulonglong sceneRevision() const{return publishedStats_.sceneRevision;}
    qulonglong geometryUploadCount() const{return publishedStats_.geometryUploadCount;}
    qulonglong materialUpdateCount() const{return publishedStats_.materialUpdateCount;}
    qulonglong viewUniformUpdateCount() const{return publishedStats_.viewUniformUpdateCount;}
    qulonglong geometryBytes() const{return publishedStats_.geometryBytes;}
    qulonglong visibleCountryCount() const{return publishedStats_.visibleCountryCount;}
    qulonglong drawIndexCount() const{return publishedStats_.drawIndexCount;}
    qulonglong fullIndexCount() const{return publishedStats_.fullIndexCount;}
    qulonglong uploadBytesThisFrame() const{return publishedStats_.uploadBytesThisFrame;}
    bool uploadsPending() const{return publishedStats_.uploadsPending;}
    qulonglong resourceGeneration() const{return resourceGeneration_.load();}
    QVariantMap resourceCacheStats() const;
    const MapGpuStats& gpuStats() const {return publishedStats_;}
    std::shared_ptr<const MapRenderObservation> renderObservation() const {
        std::lock_guard lock(renderObservationMutex_);
        if(!renderObservation_||renderObservation_->windowGeneration!=windowGeneration_.load()||
           renderObservation_->resourceGeneration!=resourceGeneration_.load()||
           renderObservation_->bridgeGeneration!=bridgeGeneration_.load())return {};
        return renderObservation_;
    }
    qulonglong uploadContinuationCount() const {return uploadContinuations_;}
    qulonglong uploadBudgetBytes() const{return uploadBudgetBytes_;}
    void setUploadBudgetBytes(qulonglong bytes) {if(uploadBudgetBytes_!=bytes){uploadBudgetBytes_=bytes;emit viewportChanged();update();}}
signals:
    void frameSampled(double milliseconds);
    // Qt submitted/displayed-frame receipt; neither a GPU fence nor DWM proof.
    void framePresented(std::shared_ptr<const MapFrame> frame,QVariantList strokeInventory);
    void sceneBridgeChanged();
    void rendererReadyChanged();
    void viewportChanged();
    void statsChanged();
protected:
    QSGNode* updatePaintNode(QSGNode* oldNode,UpdatePaintNodeData*) override;
private:
    void evaluateBackend();
    void attachWindow(QQuickWindow* window);
    void setStatus(bool ready,const QString& reason);
    void publishPresentation();
    QPointer<MapSceneBridge> bridge_;
    QPointer<QQuickWindow> connectedWindow_;
    MapFlatViewport flat_;
    MapGpuStats publishedStats_;
    MapGpuStats renderStats_;
    QString diagnostic_;
    bool ready_=false;
    qulonglong uploadBudgetBytes_=8ull*1024*1024;
    std::atomic<qint64> frameStartNs_{0};
    std::atomic<bool> uploadContinuationQueued_{false};
    qulonglong uploadContinuations_=0;
    std::atomic<qulonglong> bridgeGeneration_{0},windowGeneration_{0},resourceGeneration_{0};
    qulonglong renderGeneration_=0; // Render thread only; statistics belong to one resource generation.
    mutable std::mutex renderObservationMutex_;
    std::shared_ptr<const MapRenderObservation> renderObservation_;
    mutable std::mutex presentationMutex_;
    std::shared_ptr<const MapRenderObservation> presentationObservation_;
    qulonglong presentationSequence_=0,publishedPresentationSequence_=0;
    bool presentationEnded_=false,presentationSwapped_=false;
};

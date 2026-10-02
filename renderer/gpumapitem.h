#pragma once

#include "mapscenebridge.h"
#include "scenegraph/mapscenenode.h"
#include <QQuickItem>
#include <QPointer>
#include <atomic>
#include <QString>

class GpuMapItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject* sceneBridge READ sceneBridge WRITE setSceneBridge NOTIFY sceneBridgeChanged)
    Q_PROPERTY(bool rendererReady READ rendererReady NOTIFY rendererReadyChanged)
    Q_PROPERTY(bool forcedGpu READ forcedGpu CONSTANT)
    Q_PROPERTY(QString diagnostic READ diagnostic NOTIFY rendererReadyChanged)
    Q_PROPERTY(bool contentReady READ contentReady WRITE setContentReady NOTIFY rendererReadyChanged)
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
    Q_PROPERTY(qulonglong uploadBudgetBytes READ uploadBudgetBytes WRITE setUploadBudgetBytes NOTIFY viewportChanged)
public:
    explicit GpuMapItem(QQuickItem* parent=nullptr);
    QObject* sceneBridge() const{return bridge_;}
    void setSceneBridge(QObject* bridge);
    bool rendererReady() const{return ready_&&contentReady_;}
    bool forcedGpu() const;
    bool contentReady() const{return contentReady_;}
    void setContentReady(bool ready);
    QString diagnostic() const{return contentReady_?diagnostic_:
        QStringLiteral("GPU content channel unavailable");}
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
    qulonglong uploadBudgetBytes() const{return uploadBudgetBytes_;}
    void setUploadBudgetBytes(qulonglong bytes) {if(uploadBudgetBytes_!=bytes){uploadBudgetBytes_=bytes;emit viewportChanged();update();}}
signals:
    void frameSampled(double milliseconds);
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
    QPointer<MapSceneBridge> bridge_;
    QPointer<QQuickWindow> connectedWindow_;
    MapFlatViewport flat_;
    MapGpuStats publishedStats_;
    MapGpuStats renderStats_;
    QString diagnostic_;
    bool ready_=false,contentReady_=true;
    qulonglong uploadBudgetBytes_=8ull*1024*1024;
    std::atomic<qint64> frameStartNs_{0};
};

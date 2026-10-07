#pragma once
#include "../renderer/terrainprovider.h"
#include "terraindisplaystate.h"
#include <QQuickImageProvider>
#include <QObject>
#include <memory>
#include <mutex>
#include <map>
#include <optional>
#include <QMetaType>

struct TerrainImageFrame {
    QImage image,tint;
    QSize levelSize;
    int gutter=0;
    bool dem=false;
    quint64 sourceEpoch=0;
};

struct TerrainRenderResource {
    TerrainDisplayResource resource;
    TerrainImageFrame frame;
    QString tintContentKey;
    bool gray=false,prefetch=false;
};
struct TerrainRenderOwnerSnapshot {
    quint64 windowEpoch=0,contextEpoch=0;
    bool alive=false;
};
struct TerrainRenderStats {
    quint64 pendingSubmittedFrames=0;
    quint64 cpuImageBytes=0,textureNominalBytes=0,stagingNominalBytes=0,meshBytes=0;
    quint64 textureCount=0,allocationCount=0,uploadOperations=0,uploadBytes=0;
    quint64 frameUploadOperations=0,frameUploadBytes=0,oversizedOperations=0;
    quint64 deferredResources=0,retiredResources=0,mandatoryOverflowBytes=0;
    quint64 temporaryProtectedBytes=0;
    quint64 auxiliaryTextureNominalBytes=0;
    quint64 allocationFailures=0,textureSizeChanges=0,logicalResourceCount=0;
    double frameUploadMillis=0;
    bool memoryPressure=false;
    bool uploadWorkPending=false;
};
struct TerrainRenderInput {
    TerrainDisplayScope scope;
    std::optional<TerrainDisplayCandidate> candidate;
    std::vector<TerrainRenderResource> resources;
    std::vector<TerrainDisplayResourceId> baseResources,protectedResources;
    QImage tint;
    QString tintContentKey;
    MapViewState view;
    quint64 sceneRevision=0;
    bool enabled=true,inputActive=false,gray=false,releaseResources=false,requiresTint=false;
    quint64 budgetBytes=128ull*1024*1024,uploadBytesPerFrame=2ull*1024*1024;
    double meshPhysicalScale=0;
    double uploadMillisPerFrame=2;
};
struct TerrainRenderObservation {
    enum Kind {UploadSubmitted,CandidateSubmitted,DisplayReceipt,ResourceRetired,Pressure};
    Kind kind=Pressure;
    TerrainDisplayScope scope;
    std::vector<TerrainDisplayResource> resources;
    std::vector<TerrainDisplayResourceId> retired;
    QString tintContentKey;
    std::optional<TerrainDisplayReceipt> receipt;
    std::vector<TerrainDisplayDraw> fallbackDraws;
    QString diagnostic;
    TerrainRenderStats stats;
};
struct TerrainRenderAdoption {
    TerrainDisplayReceipt receipt;
    std::vector<TerrainDisplayResourceId> protectedResources;
};
Q_DECLARE_METATYPE(TerrainRenderObservation)
Q_DECLARE_METATYPE(TerrainRenderOwnerSnapshot)

// Shared typed entry point for geographic terrain items and the image provider.
class TerrainImageBridge final : public QObject {
    Q_OBJECT
public:
    explicit TerrainImageBridge(QObject* parent=nullptr);
    void setSource(std::shared_ptr<TerrainTileProvider> source);
    QImage acquire(int level,int column,int row,bool gray=false) const;
    TerrainImageFrame acquireFrame(int level,int column,int row) const;
    void setRenderStyle(bool darkTheme,float shadeBlend);
    bool darkTheme() const {return darkTheme_;}
    float shadeBlend() const {return shadeBlend_;}
    quint64 sourceEpoch() const {return sourceEpoch_;}
    void setDisplayBacking(const QObject* owner,const QImage& backing);
    void releaseDisplayBacking(const QObject* owner);
    quint64 displayBackingBytes() const;
    quint64 displayBackingCount() const;
    TerrainRenderResource prepareRenderResource(const TerrainTileSpec&,bool gray=false) const;
    static TerrainRenderResource prepareRenderResource(std::shared_ptr<TerrainTileProvider>,quint64 sourceEpoch,
        const TerrainTileSpec&,bool gray=false,const QString& knownTintContentKey={},qint64 knownTintCacheKey=0);
    static QString imageContentKey(const QImage&,quint64 sourceEpoch);
    void setRenderInput(TerrainRenderInput);
    std::shared_ptr<const TerrainRenderInput> renderInput() const;
    TerrainRenderOwnerSnapshot ownerSnapshot() const;
    TerrainRenderStats renderStats() const;
    void acknowledgeDisplay(const TerrainDisplayReceipt&,std::vector<TerrainDisplayResourceId>);
    std::optional<TerrainRenderAdoption> renderAdoption() const;
    // Render-thread producers use these queued, guarded publication methods.
    void observeRender(TerrainRenderObservation);
    void observeOwner(TerrainRenderOwnerSnapshot);
signals:
    void sourceChanged();
    void renderStyleChanged();
    void displayBackingChanged();
    void renderInputChanged();
    void renderObserved(TerrainRenderObservation observation);
    void renderOwnerChanged(TerrainRenderOwnerSnapshot owner);
private:
    std::shared_ptr<TerrainTileProvider> source_;
    quint64 sourceEpoch_=0;
    bool darkTheme_=false;
    float shadeBlend_=0;
    // GUI-thread ledger of the extra opaque raster display images held by items.
    // Excludes decoder temporaries, upload staging, GPU textures and raw cache.
    std::map<const QObject*,std::pair<qint64,quint64>> displayBackings_;
    mutable std::mutex renderMutex_;
    std::shared_ptr<const TerrainRenderInput> renderInput_;
    TerrainRenderOwnerSnapshot renderOwner_;
    TerrainRenderStats renderStats_;
    std::optional<TerrainRenderAdoption> renderAdoption_;
    std::optional<TerrainDisplayReceipt> lastRenderReceipt_;
    mutable std::mutex tintIdentityMutex_;
    mutable quint64 tintIdentityEpoch_=0;
    mutable qint64 tintIdentityImageKey_=0;
    mutable QString tintIdentity_;
};

// Qt Quick requests only visible tile images; the provider keeps one pinned,
// byte-bounded decode cache shared with the viewport's terrain dataset.
class TerrainImageProvider final : public QQuickImageProvider {
public:
    TerrainImageProvider():QQuickImageProvider(QQuickImageProvider::Image){}
    void setSource(std::shared_ptr<TerrainTileProvider> source);
    QImage requestImage(const QString& id,QSize* size,const QSize& requestedSize) override;
private:
    std::mutex mutex_;
    std::shared_ptr<TerrainTileProvider> source_;
};

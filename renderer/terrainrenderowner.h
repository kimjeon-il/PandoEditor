#pragma once
#include "../app/terrainimageprovider.h"
#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QSGTexture>
#include <map>
#include <set>
#include <memory>

// An indivisible QSG texture commit can exceed either limit. It is counted
// explicitly; all remaining new operations are deferred to a later frame.
class TerrainUploadSchedule {
public:
    void beginFrame(quint64 bytes,double millis);
    bool reserve(quint64 bytes);
    void committed(quint64 bytes,double millis);
    void abandon(){reserved_=0;}
    quint64 bytes() const{return bytes_;}
    quint64 operations() const{return operations_;}
    quint64 oversizedOperations() const{return oversized_;}
    double millis() const{return millis_;}
private:
    quint64 limit_=0,bytes_=0,operations_=0,oversized_=0,reserved_=0;
    double timeLimit_=0,millis_=0;
    bool stopped_=false;
};

// Created, used and destroyed on Qt's scene-graph thread. Public Qt texture
// operations establish enqueue/submission observations, never a GPU fence.
class TerrainRenderOwner final : public QObject {
public:
    TerrainRenderOwner(QQuickWindow*,TerrainImageBridge*,TerrainRenderOwnerSnapshot);
    ~TerrainRenderOwner() override;
    void synchronize(std::shared_ptr<const TerrainRenderInput>);
    QSGTexture* texture(const TerrainDisplayResourceId&) const;
    QSGTexture* tintTexture() const;
    QSGTexture* tintTextureFor(const TerrainRenderResource&) const;
    QSGTexture* emptyMaskTexture() const{return emptyMask_.get();}
    bool bootstrapReady() const;
    bool candidateReady() const;
    std::vector<TerrainDisplayResourceId> pendingProbes() const;
    const TerrainRenderResource* resource(const TerrainDisplayResourceId&) const;
    const std::vector<TerrainDisplayDraw>& draws() const;
    const std::vector<TerrainDisplayDraw>& fallbackDraws() const{return fallbackDraws_;}
    bool needsFallbackUnderlay() const;
    bool candidateSelected() const{return candidateSelected_;}
    std::shared_ptr<const TerrainRenderInput> input() const{return input_;}
    void noteTextureCommit(QSGTexture*,double elapsedMillis,bool actualBackingValid);
    void noteDraw(std::size_t,const TerrainDisplayDraw&);
    void setMeshBytes(quint64 bytes){stats_.meshBytes=bytes;}
    TerrainRenderStats stats() const{return stats_;}
private:
    struct Backing {
        TerrainRenderResource data;
        std::shared_ptr<QSGTexture> texture;
        quint64 bytes=0,lastUse=0;
        bool committed=false,mandatory=false;
        std::optional<TerrainDisplayScope> announcedScope;
        bool failed=false;
    };
    struct SubmittedFrame {
        TerrainDisplayReceipt receipt;
        std::vector<TerrainDisplayResourceId> baseResources;
        QString tintContentKey;
    };
    bool scopeCurrent() const;
    void afterFrameEnd();
    void beforeFrameBegin();
    void frameSwapped();
    void publishCompletedFrame();
    void trim(quint64 incomingBytes=0);
    void publish(TerrainRenderObservation);
    bool allocate(const TerrainRenderResource&,bool mandatory);
    std::set<TerrainDisplayResourceId> protectedIds() const;
    bool hasRunnablePreparation() const;
    void updateCpuImageBytes();
    QPointer<QQuickWindow> window_;
    QPointer<TerrainImageBridge> bridge_;
    TerrainRenderOwnerSnapshot owner_;
    std::shared_ptr<const TerrainRenderInput> input_;
    std::map<TerrainDisplayResourceId,Backing> backings_;
    std::unique_ptr<QSGTexture> emptyMask_;
    TerrainUploadSchedule schedule_;
    TerrainRenderStats stats_;
    quint64 frame_=1,tick_=0,lastAdoptedFrame_=0;
    std::vector<TerrainDisplayResource> newlyCommitted_;
    QString newlyCommittedTint_;
    std::vector<TerrainDisplayDraw> retainedDraws_;
    std::vector<TerrainDisplayDraw> fallbackDraws_;
    std::vector<TerrainDisplayResourceId> adoptedBase_;
    QString adoptedTint_;
    std::vector<bool> drawObserved_;
    std::optional<TerrainDisplayReceipt> frameSubmitted_;
    std::optional<TerrainDisplayReceipt> adoptedReceipt_;
    std::map<quint64,SubmittedFrame> inFlight_;
    std::vector<TerrainDisplayResourceId> adoptedProtection_;
    bool candidateSelected_=false;
    bool receiptNeeded_=false;
    bool swapObserved_=false,frameEnded_=false;
    bool frameResourceFailure_=false;
    QString failureDiagnostic_;
    QMetaObject::Connection beginConnection_,endConnection_,swapConnection_,invalidateConnection_;
};

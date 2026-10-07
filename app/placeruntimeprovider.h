#pragma once
#include "placeruntimestore.h"
#include <pandoeditor/map/resourcecachepolicy.h>
#include <QObject>
#include <QMap>
#include <QStringList>
#include <atomic>
#include <optional>

struct PlaceSourceIdentity {
    QString revision,manifestSha256;
    quint64 generation=0;
    bool operator==(const PlaceSourceIdentity& other) const {return revision==other.revision&&manifestSha256==other.manifestSha256&&generation==other.generation;}
};
struct PlaceRuntimeSnapshot {
    PlaceSourceIdentity identity;
    quint64 requestRevision=0;
    QString signature;
    std::vector<PlaceRecord> records;
    std::size_t tileCount=0,candidatesExamined=0,residentBytes=0;
};
struct PlaceProviderStats {
    quint64 sourceOpens=0,viewportRequests=0,searchRequests=0,acceptedSnapshots=0,
        staleCompletionCount=0,stalePublicationCount=0,cancelledCount=0,failureCount=0,
        coalescedViewportRequests=0,coalescedSearchRequests=0;
    std::size_t pendingCount=0,queuedCount=0,retainedRecords=0,snapshotRecords=0,retainedBytes=0,snapshotBytes=0;
    bool moving=false;
};
// Owner-thread source lifecycle and immutable transient snapshot. Worker tasks
// capture the store, input values and cancellation flag; never this QObject.
class PlaceRuntimeProvider final:public QObject {
    Q_OBJECT
public:
    explicit PlaceRuntimeProvider(QObject* parent=nullptr);
    ~PlaceRuntimeProvider() override;
    bool open(const QString& manifestPath,const QString& projectInstance,QString& error,
        std::size_t cacheBudget=PlaceRuntimeLimits::CacheBytes);
    void close(const QString& projectInstance);
    bool isOpen() const {return bool(store_);}
    std::optional<PlaceSourceIdentity> sourceIdentity() const;
    std::shared_ptr<const PlaceRuntimeSnapshot> snapshot() const {return snapshot_;}
    bool requestViewport(const PlaceViewport&);
    void cancelViewport();
    void beginInteraction();
    bool settle();
    bool settle(const PlaceViewport&);
    bool search(const QString&);
    void cancelSearch();
    const std::vector<PlaceRecord>& searchResults() const {return searchResults_;}
    bool searchTruncated() const {return searchTruncated_;}
    std::optional<PlaceRecord> recordById(const QString&) const;
    void retain(const PlaceRecord&);
    void setProtectedIds(const QStringList&);
    PlaceStoreStats storeStats() const;
    PlaceProviderStats stats() const;
    pandoeditor::ResourceCacheSnapshot resourceCacheSnapshot() const;
signals:
    void snapshotChanged();
    void sourceChanged();
    void searchCompleted();
    void loadFailed(const QString& error);
private:
    using CancelFlag=std::shared_ptr<std::atomic_bool>;
    std::shared_ptr<PlaceRuntimeStore> store_;
    std::shared_ptr<const PlaceRuntimeSnapshot> snapshot_;
    QString projectInstance_;
    quint64 generation_=0,viewportRevision_=0,searchRevision_=0;
    CancelFlag viewportCancel_,searchCancel_;
    std::optional<PlaceViewport> desired_,requested_;
    std::optional<PlaceViewport> queuedViewport_;
    std::optional<QString> queuedSearch_;
    bool viewportRunning_=false,searchRunning_=false;
    bool moving_=false,searchTruncated_=false;
    std::vector<PlaceRecord> searchResults_;
    QMap<QString,PlaceRecord> retained_;
    QStringList retentionOrder_,protectedIds_;
    PlaceProviderStats stats_;
    void cancel(CancelFlag&);
    void trimRetention();
    void startViewport(const PlaceViewport&);
    void startSearch(const QString&);
    void drainViewport();
    void drainSearch();
};

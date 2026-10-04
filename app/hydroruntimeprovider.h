#pragma once
#include "hydroloadscheduler.h"
#include "hydromanifest.h"
#include "hydrometadata.h"
#include "hydroshardreader.h"
#include <pandoeditor/hydroviewport.h>
#include <pandoeditor/geobounds.h>
#include <pandoeditor/jobs.h>
#include <pandoeditor/map/resourcecachepolicy.h>
#include <set>
#include <QObject>
#include <memory>
#include <optional>
#include <functional>

struct HydroLogicalCopy {pandoeditor::Geometry geometry; QString source,sourceId;};

// A value copied from the published dataset. Generation changes on every successful
// open, including identical content; failed opens preserve the current identity.
struct HydroSourceIdentity {
    QString dataset, version, indexSha256;
    std::uint64_t generation=0;
    bool operator==(const HydroSourceIdentity& other) const {
        return dataset==other.dataset && version==other.version &&
            indexSha256==other.indexSha256 && generation==other.generation;
    }
};
// Caller snapshots only kind=="river" rows with geometry, in document order.
// sourceFeatureId is provenance, never an overlay key.
struct EditedRiverValue {
    QString id;
    pandoeditor::Geometry geometry;
    std::optional<QString> sourceFeatureId;
};
struct HydroRiverFeatureValue {
    QString id, pandolabId;
    std::optional<quint32> logicalFid;
    pandoeditor::Geometry geometry;
    std::optional<QString> sourceFeatureId;
};
struct HydroRiverSourceFailure {
    quint32 logicalFid=0;
    QString assetPath, detail;
};
struct HydroRiverSourceDiagnostics {
    std::size_t discoveredLogicalRivers=0, loadedRivers=0, failedRiverLoads=0;
};
enum class HydroRiverSourceStatus { Ready, Cancelled, SourceError, Error };
struct HydroRiverSourceResult {
    HydroRiverSourceStatus status=HydroRiverSourceStatus::Error;
    HydroSourceIdentity identity;
    std::vector<pandoeditor::GeoBounds> bounds;
    std::vector<quint32> discoveredLogicalIds, failedLogicalIds;
    std::vector<HydroRiverSourceFailure> failures;
    std::vector<HydroRiverFeatureValue> features;
    HydroRiverSourceDiagnostics diagnostics;
    QString detail;
};
struct HydroRiverAssetRequirement {
    HydroAssetSpec asset;
    QString physicalRelativePath;
};
// Exact web source adapters: per-polygon split donor bounds, raw edit bounds,
// and inclusive numerical overlap (no +/-180 alias or geographic normalization).
std::vector<pandoeditor::GeoBounds> riverPartitionQueryBounds(const pandoeditor::Geometry& donor);
std::optional<pandoeditor::GeoBounds> riverPartitionEditBounds(const pandoeditor::Geometry& edit);
bool riverPartitionBoundsOverlap(const pandoeditor::GeoBounds& left,const pandoeditor::GeoBounds& right);

class HydroRuntimeProvider : public QObject {
    Q_OBJECT
public:
    explicit HydroRuntimeProvider(QObject* parent=nullptr);
    bool open(const QString& path,const QString& projectInstance,bool mobile,QString& error);
    void close(const QString& projectInstance);
    void requestViewport(const pandoeditor::HydroFlatWindow& view);
    QStringList requiredAssetPaths(const pandoeditor::HydroFlatWindow& view) const;
    std::shared_ptr<const HydroRuntimeFrame> frame() const {return scheduler_.frame();}
    bool isOpen() const {return bool(dataset_);}
    const HydroMetadata* coreMetadata() const;
    std::optional<HydroMetadataRecord> recordById(const QString& id) const;
    std::optional<HydroMetadataRecord> recordByFid(quint32 fid) const;
    std::function<HydroLogicalCopy()> logicalGeometryJob(quint32 logicalFid) const;
    std::optional<HydroSourceIdentity> sourceIdentity() const;
    // Owner-thread preparation. Throws runtime_error when closed and
    // invalid_argument for non-finite, wrapped, or reversed query bounds.
    std::vector<quint32> queryLogicalRivers(const std::vector<pandoeditor::GeoBounds>& bounds) const;
    std::vector<HydroRiverAssetRequirement> riverPartitionAssetRequirements(
        const std::vector<pandoeditor::GeoBounds>& bounds) const;
    // Empty callable only when closed. Captures owned values and the dataset,
    // never this QObject. Invalid queries yield Error; empty discovery is Ready.
    std::function<HydroRiverSourceResult(const pandoeditor::JobToken&)> riverPartitionSourceJob(
        std::vector<pandoeditor::GeoBounds> bounds,std::vector<EditedRiverValue> edits) const;
    bool pinLogical(quint32 logicalFid);
    void clearPinned();
    void setSelectedLogical(std::optional<quint32> logicalFid);
    void setSelectedLogicals(const std::set<quint32>& logicalFids);
    pandoeditor::ResourceCacheSnapshot resourceCacheSnapshot() const;
    std::size_t activeFrameBytes() const {const auto current=frame();return current?current->retainedBytes:0;}
    std::size_t cachedPackCount() const;
    std::size_t cachedBytes() const;
    void setCacheBudget(std::size_t bytes);
signals:
    void frameChanged();
    void loadFailed(const QString& error);
private:
    struct Dataset;
    std::shared_ptr<Dataset> dataset_;
    HydroLoadScheduler scheduler_;
    std::uint64_t sourceGeneration_=0;
};

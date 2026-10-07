#pragma once
#include <pandoeditor/map/mapviewstate.h>
#include <pandoeditor/document.h>
#include <QByteArray>
#include <QString>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <vector>

// Fixed Web ebcfae4d27b29cbbea6416a7045a4806930204be place-contract/codec/store.
// Builtin records are source values, never project objects or history entries.
struct PlaceRuntimeLimits {
    static constexpr std::size_t Candidates=1500,LayoutCandidates=2048,TileRecords=512,
        QueryTiles=96,ShardBytes=512*1024,CacheBytes=24*1024*1024,
        SearchResults=50,RetainedRecords=256,ManifestBytes=8*1024*1024;
};
struct PlaceRecord {
    QString id,source,sourceId,name,kind,countryCode,featureCode;
    pandoeditor::Point coordinates;
    double population=0,priority=40,minZoom=0;
};
struct PlaceViewport {
    MapViewState view;
    double zoom=1,safeLeft=0,safeTop=0,safeRight=0,safeBottom=0;
};
struct PlaceQueryResult {
    std::vector<PlaceRecord> records;
    QString signature;
    std::size_t tileCount=0,candidatesExamined=0;
    bool truncated=false;
};
struct PlaceStoreStats {
    quint64 queryCount=0,searchCount=0,fileReadCount=0,cacheHits=0,evictions=0;
    quint64 candidatesExamined=0;
    std::size_t lastTileCount=0,lastCandidateCount=0,peakWorkingRecords=0;
    // Owned representation bytes: vector capacity, QString UTF-16 capacity,
    // QByteArray capacity and manifest descriptor storage. Excludes allocator and
    // QMap node bookkeeping; not process working set or driver/VRAM bytes.
    std::size_t cacheBytes=0,cacheBudget=0,sourceResidentBytes=0,cachedTiles=0,cachedShards=0,cachedEntries=0;
    std::size_t manifestTileCount=0,manifestShardCount=0,manifestStageCount=0;
    // The fixed manifest has no source record-count field. Do not substitute
    // current candidates or decoded rows for the unobserved dataset total.
    std::optional<std::size_t> sourceRecordCount;
};
class PlaceRuntimeCancelled final:public std::runtime_error {
public:PlaceRuntimeCancelled():std::runtime_error("PL-PLACE-CANCELLED"){}
};
class PlaceRuntimeStore final {
public:
    using Cancellation=std::function<bool()>;
    static std::shared_ptr<PlaceRuntimeStore> open(const QString& manifestPath,QString& error,
        std::size_t cacheBudget=PlaceRuntimeLimits::CacheBytes);
    static std::vector<PlaceRecord> decodeTile(const QByteArray&);
    static QString normalizeQuery(const QString&);
    static bool isBuiltinId(const QString&);
    static std::size_t recordBytes(const PlaceRecord&);
    PlaceQueryResult queryViewport(const PlaceViewport&,const Cancellation& cancelled={});
    PlaceQueryResult search(const QString&,const Cancellation& cancelled={});
    PlaceStoreStats stats() const;
    QString revision() const;
    QString manifestSha256() const;
private:
    struct Data;
    explicit PlaceRuntimeStore(std::unique_ptr<Data>);
    std::unique_ptr<Data> data_;
public:
    ~PlaceRuntimeStore();
};

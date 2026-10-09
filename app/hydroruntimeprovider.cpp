#include "hydroruntimeprovider.h"
#include "hydroassetreader.h"
#include "hydroruntimecache.h"
#include <pandoeditor/hydroformat.h>
#include <QDir>
#include <mutex>
#include <iterator>
#include <set>
#include <stdexcept>
#include <limits>
#include <algorithm>
#include <cmath>

struct HydroRuntimeProvider::Dataset {
    explicit Dataset(bool mobile):cache(mobile?48*1024*1024:96*1024*1024){}
    HydroManifest manifest;
    HydroSourceIdentity identity;
    pandoeditor::HydroIndex index;
    HydroMetadata metadata;
    QHash<QString,HydroMetadataRecord> logicalMetadata;
    std::map<std::uint32_t,std::uint32_t> logicalIds;
    std::vector<std::shared_ptr<HydroShardReader>> shards;
    std::vector<pandoeditor::HydroStageGrid> stages;
    mutable std::mutex mutex;
    HydroRuntimeCache cache;
    std::set<std::uint32_t> active,pinned,selected,displayed;
    void updateProtection() {
        cache.protectReasons(displayed,selected,pinned,active.empty()?std::set<std::uint32_t>{}:displayed,active);
    }
};
namespace {
void addFrameBytes(std::size_t& total,std::size_t count,std::size_t size) {
    if(size&&count>(std::numeric_limits<std::size_t>::max()-total)/size)throw std::overflow_error("hydro frame bytes overflow");
    total+=count*size;
}
std::size_t frameStorageBytes(const HydroRuntimeFrame& frame) {
    std::size_t bytes=sizeof(frame);
    addFrameBytes(bytes,frame.packIds.capacity(),sizeof(std::uint32_t));
    addFrameBytes(bytes,frame.features.capacity(),sizeof(pandoeditor::HydroPhysicalFeature));
    const auto polygons=[&](const std::vector<pandoeditor::HydroPolygon>& values) {
        addFrameBytes(bytes,values.capacity(),sizeof(pandoeditor::HydroPolygon));
        for(const auto& polygon:values){addFrameBytes(bytes,polygon.capacity(),sizeof(pandoeditor::HydroLine));
            for(const auto& ring:polygon)addFrameBytes(bytes,ring.capacity(),sizeof(pandoeditor::HydroPoint));}
    };
    for(const auto& feature:frame.features) {
        addFrameBytes(bytes,feature.geometry.lines.capacity(),sizeof(pandoeditor::HydroLine));
        for(const auto& line:feature.geometry.lines)addFrameBytes(bytes,line.capacity(),sizeof(pandoeditor::HydroPoint));
        polygons(feature.geometry.polygons);
        addFrameBytes(bytes,feature.widths.capacity(),sizeof(std::vector<double>));
        for(const auto& widths:feature.widths)addFrameBytes(bytes,widths.capacity(),sizeof(double));
    }
    addFrameBytes(bytes,frame.packet.rivers.capacity(),sizeof(pandoeditor::HydroRiverSegment));
    addFrameBytes(bytes,frame.packet.lakes.capacity(),sizeof(pandoeditor::HydroLakeShape));
    for(const auto& lake:frame.packet.lakes)polygons(lake.polygons);
    return bytes;
}
std::size_t decodedBytes(const pandoeditor::HydroPack& pack) {
    std::size_t bytes=sizeof(pack)+pack.features.capacity()*sizeof(pandoeditor::HydroPhysicalFeature);
    for(const auto& feature:pack.features){
        for(const auto& line:feature.geometry.lines)bytes+=sizeof(line)+line.capacity()*sizeof(pandoeditor::HydroPoint);
        for(const auto& polygon:feature.geometry.polygons){
            bytes+=sizeof(polygon)+polygon.capacity()*sizeof(pandoeditor::HydroLine);
            for(const auto& ring:polygon)bytes+=ring.capacity()*sizeof(pandoeditor::HydroPoint);
        }
        for(const auto& widths:feature.widths)bytes+=sizeof(widths)+widths.capacity()*sizeof(double);
    }
    return bytes;
}
}
HydroRuntimeProvider::HydroRuntimeProvider(QObject* parent):QObject(parent) {
    connect(&scheduler_,&HydroLoadScheduler::frameAccepted,this,[this] {
        if(dataset_) {
            const auto frame=scheduler_.frame();
            std::lock_guard lock(dataset_->mutex);
            dataset_->displayed=frame?std::set<std::uint32_t>(frame->packIds.begin(),frame->packIds.end()):std::set<std::uint32_t>{};
            dataset_->active.clear();dataset_->updateProtection();
        }
        emit frameChanged();
    });
    connect(&scheduler_,&HydroLoadScheduler::loadFailed,this,[this](const QString& error) {
        if(dataset_){std::lock_guard lock(dataset_->mutex);dataset_->active.clear();dataset_->updateProtection();}
        emit loadFailed(error);
    });
}
bool HydroRuntimeProvider::open(const QString& path,const QString& projectInstance,bool mobile,QString& error) {
    error.clear();
    try {
    auto candidate=std::make_shared<Dataset>(mobile);
    candidate->manifest=readHydroManifest(path);
    if(!candidate->manifest.valid()){error=candidate->manifest.error;return false;}
    if(candidate->manifest.version=="0.13.2" &&
       !verifyHydroAsset(candidate->manifest.container,error))return false;
    const auto index=readHydroAsset(candidate->manifest.index,true,error);
    if(!error.isEmpty())return false;
    const auto core=readHydroAsset(candidate->manifest.metadataCore,true,error);
    if(!error.isEmpty()||!parseHydroCoreMetadata(core,candidate->manifest.metadataFeatureCount,candidate->metadata,error))return false;
        std::vector<std::uint64_t> lengths;
        for(const auto& shard:candidate->manifest.shards)lengths.push_back(shard.asset.bytes);
        candidate->index=pandoeditor::decodeHydroIndex(
            {reinterpret_cast<const std::uint8_t*>(index.constData()),static_cast<std::size_t>(index.size())},lengths);
        if(candidate->index.tilePacks.size()!=static_cast<std::size_t>(candidate->manifest.indexTileCount)||
           candidate->index.logicalPacks.size()!=static_cast<std::size_t>(candidate->manifest.logicalFeatureCount))
            throw std::runtime_error("hydro index counts differ from manifest");
        for(auto it=candidate->metadata.cbegin();it!=candidate->metadata.cend();++it)
            {candidate->logicalIds.emplace(it.key(),it.value().logicalFid);
             candidate->logicalMetadata.insert(it.value().awId,it.value());}
        for(const auto& shard:candidate->manifest.shards)
            candidate->shards.push_back(std::make_shared<HydroShardReader>(shard.asset));
        for(const auto& stage:candidate->manifest.stages)
            candidate->stages.push_back({static_cast<std::uint8_t>(stage.id),stage.minZoom,
                static_cast<std::uint16_t>(stage.columns),static_cast<std::uint16_t>(stage.rows)});
    if(sourceGeneration_==std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("hydro source generation exhausted");
    candidate->identity={candidate->manifest.dataset,candidate->manifest.version,
        candidate->manifest.index.sha256,sourceGeneration_+1};
    dataset_=std::move(candidate);
    ++sourceGeneration_;
    scheduler_.resetDataset(projectInstance);
    emit frameChanged();
    return true;
    }catch(const std::exception& exception){error=QString::fromUtf8(exception.what());return false;}
}
void HydroRuntimeProvider::close(const QString& projectInstance) {
    if(sourceGeneration_==std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("hydro source generation exhausted");
    ++sourceGeneration_;
    dataset_.reset();scheduler_.resetDataset(projectInstance);emit frameChanged();
}
const HydroMetadata* HydroRuntimeProvider::coreMetadata() const {
    return dataset_?&dataset_->metadata:nullptr;
}
std::optional<HydroMetadataRecord> HydroRuntimeProvider::recordById(const QString& id) const {
    if(!dataset_)return {};
    const auto found=dataset_->logicalMetadata.constFind(id);
    return found==dataset_->logicalMetadata.cend()?std::nullopt:std::optional<HydroMetadataRecord>(*found);
}
std::optional<HydroMetadataRecord> HydroRuntimeProvider::recordByFid(quint32 fid) const {
    if(!dataset_)return {};
    const auto found=dataset_->metadata.constFind(fid);
    return found==dataset_->metadata.cend()?std::nullopt:std::optional<HydroMetadataRecord>(*found);
}
std::function<HydroLogicalCopy()> HydroRuntimeProvider::logicalGeometryJob(quint32 logicalFid) const {
    if(!dataset_||!dataset_->index.logicalPacks.count(logicalFid))return {};
    const auto dataset=dataset_;
    return [dataset,logicalFid]{
        pandoeditor::ResourceRequestToken token;
        {std::lock_guard lock(dataset->mutex);const auto& ids=dataset->index.logicalPacks.at(logicalFid);
            token=dataset->cache.beginCopy(std::set<std::uint32_t>(ids.begin(),ids.end()));}
        struct CopyGuard {
            std::shared_ptr<Dataset> dataset;pandoeditor::ResourceRequestToken token;bool completed=false;
            ~CopyGuard(){try{std::lock_guard lock(dataset->mutex);dataset->cache.endCopy(token,!completed);}catch(...){}}
        } guard{dataset,token};
        QString error;
        const auto detail=readHydroAsset(dataset->manifest.metadataDetail,true,error);
        if(!error.isEmpty())throw std::runtime_error(error.toStdString());
        auto metadata=dataset->metadata;
        if(!mergeHydroDetailMetadata(detail,metadata,error))throw std::runtime_error(error.toStdString());
        std::vector<pandoeditor::HydroPhysicalFeature> fragments;
        QString source,sourceId;
        for(const auto id:dataset->index.logicalPacks.at(logicalFid)){
            const auto& spec=dataset->index.packSpecs.at(id);
            std::shared_ptr<const pandoeditor::HydroPack> pack;
            {std::lock_guard lock(dataset->mutex);pack=dataset->cache.get(id);}
            if(!pack){
                const auto bytes=dataset->shards.at(spec.shard)->readPack(spec.offset,spec.length,error);
                if(!error.isEmpty())throw std::runtime_error(error.toStdString());
                pack=std::make_shared<pandoeditor::HydroPack>(pandoeditor::decodeHydroPack(
                    {reinterpret_cast<const std::uint8_t*>(bytes.constData()),static_cast<std::size_t>(bytes.size())},
                    id,dataset->logicalIds));
                std::lock_guard lock(dataset->mutex);
                dataset->cache.put(id,pack,decodedBytes(*pack));
            }
            for(const auto& feature:pack->features)if(feature.logicalFid==logicalFid){
                source=metadata.value(feature.fid).source;sourceId=metadata.value(feature.fid).sourceId;
                fragments.push_back(feature);
            }
        }
        auto result=HydroLogicalCopy{pandoeditor::mergeHydroLogicalFragments(std::move(fragments)),source,sourceId};
        guard.completed=true;return result;
    };
}
namespace {
struct RiverBoundsAccumulator {
    pandoeditor::GeoBounds bounds{std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),false};
    double eastMinimum=std::numeric_limits<double>::infinity();
    double westMaximum=-std::numeric_limits<double>::infinity();
    void add(pandoeditor::Point point) {
        if(!std::isfinite(point.x)||!std::isfinite(point.y))return;
        bounds.west=std::min(bounds.west,point.x);bounds.east=std::max(bounds.east,point.x);
        bounds.south=std::min(bounds.south,point.y);bounds.north=std::max(bounds.north,point.y);
        if(point.x>=0)eastMinimum=std::min(eastMinimum,point.x);
        else westMaximum=std::max(westMaximum,point.x);
    }
    bool empty() const {return !std::isfinite(bounds.west);}
};
void validateRiverBounds(const std::vector<pandoeditor::GeoBounds>& bounds) {
    for(const auto& b:bounds)
        if(b.wrapsDateline||!std::isfinite(b.west)||!std::isfinite(b.south)||
           !std::isfinite(b.east)||!std::isfinite(b.north)||b.west>b.east||b.south>b.north)
            throw std::invalid_argument("river source requires finite linear query bounds");
}
bool overlapsAnyRiverBounds(const pandoeditor::GeoBounds& candidate,
                           const std::vector<pandoeditor::GeoBounds>& bounds) {
    return std::any_of(bounds.begin(),bounds.end(),[&](const auto& b){return riverPartitionBoundsOverlap(candidate,b);});
}
std::vector<quint32> discoverLogicalRivers(const HydroMetadata& metadata,
                                         const std::vector<pandoeditor::GeoBounds>& bounds) {
    validateRiverBounds(bounds);
    std::set<quint32> ids;
    for(const auto& row:metadata)
        if(row.category=="river"&&overlapsAnyRiverBounds(
               {row.bounds[0],row.bounds[1],row.bounds[2],row.bounds[3],false},bounds))
            ids.insert(row.logicalFid);
    return {ids.begin(),ids.end()};
}
struct RiverSourceCancelled {};
void checkRiverCancellation(const pandoeditor::JobToken& token) {
    if(token.cancelled())throw RiverSourceCancelled{};
}
HydroRiverAssetRequirement riverAssetRequirement(const HydroAssetSpec& asset) {
    const auto relative=QDir::fromNativeSeparators(QDir(asset.assetRoot).relativeFilePath(asset.path));
    const auto parts=relative.split('/');
    if(relative.isEmpty()||QDir::isAbsolutePath(relative)||parts.contains("..")||
       parts.contains(".")||parts.contains(QString()))
        throw std::runtime_error("river source asset has no safe relative inventory path");
    return {asset,QStringLiteral("hydro/")+relative};
}
}
std::vector<pandoeditor::GeoBounds> riverPartitionQueryBounds(const pandoeditor::Geometry& donor) {
    std::vector<pandoeditor::GeoBounds> result;
    if(donor.type!="Polygon"&&donor.type!="MultiPolygon")return result;
    for(const auto& polygon:donor.polygons) {
        RiverBoundsAccumulator scan;
        for(const auto& ring:polygon)for(const auto point:ring)scan.add(point);
        if(scan.empty())continue;
        if(scan.bounds.east-scan.bounds.west<=180)result.push_back(scan.bounds);
        else {
            if(std::isfinite(scan.eastMinimum))result.push_back({scan.eastMinimum,scan.bounds.south,180,scan.bounds.north,false});
            if(std::isfinite(scan.westMaximum))result.push_back({-180,scan.bounds.south,scan.westMaximum,scan.bounds.north,false});
        }
    }
    return result;
}
std::optional<pandoeditor::GeoBounds> riverPartitionEditBounds(const pandoeditor::Geometry& edit) {
    RiverBoundsAccumulator scan;
    for(const auto point:edit.points)scan.add(point);
    for(const auto& line:edit.lines)for(const auto point:line)scan.add(point);
    for(const auto& polygon:edit.polygons)for(const auto& ring:polygon)for(const auto point:ring)scan.add(point);
    return scan.empty()?std::nullopt:std::optional<pandoeditor::GeoBounds>(scan.bounds);
}
bool riverPartitionBoundsOverlap(const pandoeditor::GeoBounds& a,const pandoeditor::GeoBounds& b) {
    return a.west<=b.east&&a.east>=b.west&&a.south<=b.north&&a.north>=b.south;
}
std::optional<HydroSourceIdentity> HydroRuntimeProvider::sourceIdentity() const {
    return dataset_?std::optional<HydroSourceIdentity>(dataset_->identity):std::nullopt;
}
std::vector<quint32> HydroRuntimeProvider::queryLogicalRivers(const std::vector<pandoeditor::GeoBounds>& bounds) const {
    if(!dataset_)throw std::runtime_error("hydro river source is not open");
    return discoverLogicalRivers(dataset_->metadata,bounds);
}
std::vector<HydroRiverAssetRequirement> HydroRuntimeProvider::riverPartitionAssetRequirements(
    const std::vector<pandoeditor::GeoBounds>& bounds) const {
    const auto ids=queryLogicalRivers(bounds);
    if(ids.empty())return {};
    std::set<std::uint16_t> shards;
    for(const auto id:ids)for(const auto pack:dataset_->index.logicalPacks.at(id))
        shards.insert(dataset_->index.packSpecs.at(pack).shard);
    std::vector<HydroRiverAssetRequirement> result{riverAssetRequirement(dataset_->manifest.metadataDetail)};
    for(const auto shard:shards)result.push_back(riverAssetRequirement(dataset_->manifest.shards.at(shard).asset));
    return result;
}
std::function<HydroRiverSourceResult(const pandoeditor::JobToken&)> HydroRuntimeProvider::riverPartitionSourceJob(
    std::vector<pandoeditor::GeoBounds> bounds,std::vector<EditedRiverValue> edits) const {
    if(!dataset_)return {};
    const auto dataset=dataset_;
    return [dataset,bounds=std::move(bounds),edits=std::move(edits)](const pandoeditor::JobToken& token) {
        HydroRiverSourceResult result;result.identity=dataset->identity;result.bounds=bounds;
        try {
            checkRiverCancellation(token);
            result.discoveredLogicalIds=discoverLogicalRivers(dataset->metadata,bounds);
            std::set<std::uint32_t> packs;
            for(const auto logical:result.discoveredLogicalIds) {
                checkRiverCancellation(token);
                const auto found=dataset->index.logicalPacks.find(logical);
                if(found!=dataset->index.logicalPacks.end())packs.insert(found->second.begin(),found->second.end());
            }
            // This copy token is independent of viewport, selected, and editing
            // protections. Cleanup happens before the final cancellation check.
            [&] {
                pandoeditor::ResourceRequestToken copy;
                {std::lock_guard lock(dataset->mutex);copy=dataset->cache.beginCopy(std::move(packs));}
                struct CopyGuard {
                    std::shared_ptr<Dataset> dataset;pandoeditor::ResourceRequestToken token;bool completed=false;
                    ~CopyGuard(){try{std::lock_guard lock(dataset->mutex);dataset->cache.endCopy(token,!completed);}catch(...){}}
                } guard{dataset,copy};
                token.reportProgress(5);
                auto fail=[&](quint32 logical,const QString& path,const QString& detail) {
                    result.failedLogicalIds.push_back(logical);result.failures.push_back({logical,path,detail});
                };
                auto metadata=dataset->metadata;
                QString detailError;
                if(!result.discoveredLogicalIds.empty()) {
                    checkRiverCancellation(token);
                    const auto detail=readHydroAsset(dataset->manifest.metadataDetail,true,detailError);
                    checkRiverCancellation(token);
                    if(detailError.isEmpty())mergeHydroDetailMetadata(detail,metadata,detailError);
                    checkRiverCancellation(token);
                }
                token.reportProgress(10);
                std::map<std::uint32_t,QString> failedPacks;
                std::vector<HydroRiverFeatureValue> loaded;
                for(const auto logical:result.discoveredLogicalIds) {
                    checkRiverCancellation(token);
                    if(!detailError.isEmpty()) {
                        fail(logical,dataset->manifest.metadataDetail.path,detailError);continue;
                    }
                    QString assetPath;
                    try {
                        std::vector<pandoeditor::HydroPhysicalFeature> fragments;
                        for(const auto id:dataset->index.logicalPacks.at(logical)) {
                            checkRiverCancellation(token);
                            const auto& spec=dataset->index.packSpecs.at(id);
                            assetPath=dataset->manifest.shards.at(spec.shard).asset.path;
                            if(const auto failed=failedPacks.find(id);failed!=failedPacks.end())
                                throw std::runtime_error(failed->second.toStdString());
                            std::shared_ptr<const pandoeditor::HydroPack> pack;
                            {std::lock_guard lock(dataset->mutex);pack=dataset->cache.get(id);}
                            if(!pack) {
                                try {
                                    QString error;
                                    const auto bytes=dataset->shards.at(spec.shard)->readPack(spec.offset,spec.length,error);
                                    checkRiverCancellation(token);
                                    if(!error.isEmpty())throw std::runtime_error(error.toStdString());
                                    pack=std::make_shared<pandoeditor::HydroPack>(pandoeditor::decodeHydroPack(
                                        {reinterpret_cast<const std::uint8_t*>(bytes.constData()),static_cast<std::size_t>(bytes.size())},id,dataset->logicalIds));
                                    checkRiverCancellation(token);
                                    {std::lock_guard lock(dataset->mutex);dataset->cache.put(id,pack,decodedBytes(*pack));}
                                }catch(const std::bad_alloc&){throw;}
                                catch(const std::exception& error){
                                    checkRiverCancellation(token);
                                    failedPacks.emplace(id,QString::fromUtf8(error.what()));throw;
                                }
                            }
                            checkRiverCancellation(token);
                            for(const auto& feature:pack->features)if(feature.logicalFid==logical)fragments.push_back(feature);
                        }
                        if(fragments.empty())throw std::runtime_error("empty hydro logical river");
                        const auto first=std::min_element(fragments.begin(),fragments.end(),
                            [](const auto& a,const auto& b){return a.fragmentIndex<b.fragmentIndex;});
                        const auto row=metadata.constFind(first->fid);
                        if(row==metadata.cend()||row->category!="river"||first->kind!=1)
                            throw std::runtime_error("invalid logical river metadata");
                        const QString awId=row->awId;
                        auto geometry=pandoeditor::mergeHydroLogicalFragments(std::move(fragments));
                        checkRiverCancellation(token);
                        loaded.push_back({awId,awId,logical,std::move(geometry),{}});
                    }catch(const std::bad_alloc&){throw;}
                    catch(const std::exception& error) {
                        checkRiverCancellation(token);fail(logical,assetPath,QString::fromUtf8(error.what()));
                    }
                    token.reportProgress(10+int(70*(loaded.size()+result.failedLogicalIds.size())/result.discoveredLogicalIds.size()));
                }
                checkRiverCancellation(token);
                if(!result.discoveredLogicalIds.empty()&&loaded.empty()&&
                   result.failedLogicalIds.size()==result.discoveredLogicalIds.size()) {
                    result.status=HydroRiverSourceStatus::SourceError;
                    result.detail=QStringLiteral("All discovered logical rivers failed to load.");return;
                }
                token.reportProgress(85);
                // Web uses insertion-preserving Map assignment. Native built-ins
                // are numeric-ID ordered; edits preserve their snapshot order.
                QHash<QString,std::size_t> positions;
                auto insert=[&](HydroRiverFeatureValue feature) {
                    const auto found=positions.constFind(feature.pandolabId);
                    if(found==positions.cend()) {
                        positions.insert(feature.pandolabId,result.features.size());result.features.push_back(std::move(feature));
                    }else result.features[*found]=std::move(feature);
                };
                for(auto& feature:loaded){checkRiverCancellation(token);insert(std::move(feature));}
                for(const auto& edit:edits) {
                    checkRiverCancellation(token);
                    const auto editBounds=riverPartitionEditBounds(edit.geometry);
                    if(editBounds&&overlapsAnyRiverBounds(*editBounds,bounds))
                        insert({edit.id,edit.id,{},edit.geometry,edit.sourceFeatureId});
                }
                checkRiverCancellation(token);
                result.status=HydroRiverSourceStatus::Ready;
                guard.completed=result.failedLogicalIds.empty();
            }();
        }catch(const RiverSourceCancelled&) {
            result.status=HydroRiverSourceStatus::Cancelled;
        }catch(const std::exception& error) {
            result.status=HydroRiverSourceStatus::Error;result.detail=QString::fromUtf8(error.what());
        }catch(...) {
            result.status=HydroRiverSourceStatus::Error;result.detail=QStringLiteral("Unknown hydro river source failure.");
        }
        if(token.cancelled())result.status=HydroRiverSourceStatus::Cancelled;
        if(result.status==HydroRiverSourceStatus::Cancelled||result.status==HydroRiverSourceStatus::Error)result.features.clear();
        result.diagnostics={result.discoveredLogicalIds.size(),result.features.size(),result.failedLogicalIds.size()};
        if(result.status==HydroRiverSourceStatus::Ready)token.reportProgress(100);
        return result;
    };
}

bool HydroRuntimeProvider::pinLogical(quint32 logicalFid) {
    if(!dataset_)return false;
    const auto found=dataset_->index.logicalPacks.find(logicalFid);
    if(found==dataset_->index.logicalPacks.end())return false;
    std::lock_guard lock(dataset_->mutex);
    auto pinned=dataset_->pinned;
    pinned.insert(found->second.begin(),found->second.end());
    dataset_->pinned.swap(pinned);dataset_->updateProtection();
    return true;
}
void HydroRuntimeProvider::clearPinned() {
    if(!dataset_)return;
    std::lock_guard lock(dataset_->mutex);
    dataset_->pinned.clear();dataset_->updateProtection();
}
void HydroRuntimeProvider::setSelectedLogical(std::optional<quint32> logicalFid) {
    setSelectedLogicals(logicalFid?std::set<quint32>{*logicalFid}:std::set<quint32>{});
}
void HydroRuntimeProvider::setSelectedLogicals(const std::set<quint32>& logicalFids) {
    if(!dataset_)return;
    std::set<std::uint32_t> selected;
    for(const auto fid:logicalFids)if(const auto found=dataset_->index.logicalPacks.find(fid);found!=dataset_->index.logicalPacks.end())
        selected.insert(found->second.begin(),found->second.end());
    std::lock_guard lock(dataset_->mutex);dataset_->selected.swap(selected);dataset_->updateProtection();
}
pandoeditor::ResourceCacheSnapshot HydroRuntimeProvider::resourceCacheSnapshot() const {
    auto snapshot=scheduler_.resourceCacheSnapshot();
    if(!dataset_)return snapshot;
    std::lock_guard lock(dataset_->mutex);auto cache=dataset_->cache.resourceCacheSnapshot();
    cache.scopeEpoch=snapshot.scopeEpoch;cache.pendingCount+=snapshot.pendingCount;
    cache.pendingEstimatedBytes+=snapshot.pendingEstimatedBytes;cache.pendingUnknownCount+=snapshot.pendingUnknownCount;
    cache.staleCompletionCount+=snapshot.staleCompletionCount;cache.failureCount+=snapshot.failureCount;return cache;
}
std::size_t HydroRuntimeProvider::cachedPackCount() const {
    if(!dataset_)return 0;
    std::lock_guard lock(dataset_->mutex);return dataset_->cache.packCount();
}
std::size_t HydroRuntimeProvider::cachedBytes() const {
    if(!dataset_)return 0;
    std::lock_guard lock(dataset_->mutex);return dataset_->cache.residentBytes();
}
void HydroRuntimeProvider::setCacheBudget(std::size_t bytes) {
    if(!dataset_)return;
    std::lock_guard lock(dataset_->mutex);
    dataset_->cache.setBudget(bytes);
}
void HydroRuntimeProvider::requestViewport(const pandoeditor::HydroFlatWindow& view) {
    if(!dataset_)return;
    const auto dataset=dataset_;
    std::set<std::uint32_t> ids;
    try {
        const auto tiles=pandoeditor::hydroViewportTiles(dataset->stages,view);
        for(const auto& tile:tiles){
            const auto found=dataset->index.tilePacks.find(tile);
            if(found!=dataset->index.tilePacks.end())ids.insert(found->second.begin(),found->second.end());
        }
        auto pending=ids;
        std::lock_guard lock(dataset->mutex);
        dataset->active.swap(pending);dataset->updateProtection();
    }catch(const std::exception& exception){emit loadFailed(QString::fromUtf8(exception.what()));return;}
    scheduler_.requestViewport([dataset,ids=std::move(ids)]{
        auto next=std::make_shared<HydroRuntimeFrame>();
        for(const auto id:ids){
            const auto& spec=dataset->index.packSpecs.at(id);
            std::shared_ptr<const pandoeditor::HydroPack> pack;
            {std::lock_guard lock(dataset->mutex);pack=dataset->cache.get(id);}
            if(!pack){
                QString error;
                const auto bytes=dataset->shards.at(spec.shard)->readPack(spec.offset,spec.length,error);
                if(!error.isEmpty())throw std::runtime_error(error.toStdString());
                pack=std::make_shared<pandoeditor::HydroPack>(pandoeditor::decodeHydroPack(
                    {reinterpret_cast<const std::uint8_t*>(bytes.constData()),static_cast<std::size_t>(bytes.size())},
                    id,dataset->logicalIds));
                std::lock_guard lock(dataset->mutex);
                dataset->cache.put(id,pack,decodedBytes(*pack));
            }
            next->packIds.push_back(id);
            next->features.insert(next->features.end(),pack->features.begin(),pack->features.end());
            auto packet=pandoeditor::buildHydroRenderPacket(*pack);
            next->packet.rivers.insert(next->packet.rivers.end(),
                std::make_move_iterator(packet.rivers.begin()),std::make_move_iterator(packet.rivers.end()));
            next->packet.lakes.insert(next->packet.lakes.end(),
                std::make_move_iterator(packet.lakes.begin()),std::make_move_iterator(packet.lakes.end()));
        }
        next->retainedBytes=frameStorageBytes(*next);
        return std::shared_ptr<const HydroRuntimeFrame>(std::move(next));
    });
}
QStringList HydroRuntimeProvider::requiredAssetPaths(const pandoeditor::HydroFlatWindow& view) const {
    QStringList result;if(!dataset_)return result;std::set<int> shards;
    try {
        const auto tiles=pandoeditor::hydroViewportTiles(dataset_->stages,view);
        for(const auto& tile:tiles)if(const auto found=dataset_->index.tilePacks.find(tile);found!=dataset_->index.tilePacks.end())
            for(const auto id:found->second)shards.insert(dataset_->index.packSpecs.at(id).shard);
        for(const auto shard:shards) {
            const auto relative=QDir(dataset_->manifest.assetRoot).relativeFilePath(dataset_->manifest.shards.at(shard).asset.path);
            result.push_back(QStringLiteral("hydro/")+QDir::fromNativeSeparators(relative));
        }
    }catch(const std::exception&) {}
    return result;
}

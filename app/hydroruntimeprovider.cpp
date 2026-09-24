#include "hydroruntimeprovider.h"
#include "hydroassetreader.h"
#include "hydroruntimecache.h"
#include <pandoeditor/hydroformat.h>
#include <mutex>
#include <set>
#include <stdexcept>

struct HydroRuntimeProvider::Dataset {
    explicit Dataset(bool mobile):cache(mobile?48*1024*1024:96*1024*1024){}
    HydroManifest manifest;
    pandoeditor::HydroIndex index;
    HydroMetadata metadata;
    std::map<std::uint32_t,std::uint32_t> logicalIds;
    std::vector<std::shared_ptr<HydroShardReader>> shards;
    std::vector<pandoeditor::HydroStageGrid> stages;
    mutable std::mutex mutex;
    HydroRuntimeCache cache;
    std::set<std::uint32_t> active,pinned;
};
namespace {
std::size_t decodedBytes(const pandoeditor::HydroPack& pack) {
    std::size_t bytes=sizeof(pack)+pack.features.capacity()*sizeof(pandoeditor::HydroPhysicalFeature);
    for(const auto& feature:pack.features){
        for(const auto& line:feature.geometry.lines)bytes+=sizeof(line)+line.capacity()*sizeof(pandoeditor::HydroPoint);
        for(const auto& polygon:feature.geometry.polygons){
            bytes+=sizeof(polygon)+polygon.capacity()*sizeof(pandoeditor::HydroLine);
            for(const auto& ring:polygon)bytes+=ring.capacity()*sizeof(pandoeditor::HydroPoint);
        }
        for(const auto& widths:feature.widths)bytes+=sizeof(widths)+widths.capacity()*sizeof(float);
    }
    return bytes;
}
}
HydroRuntimeProvider::HydroRuntimeProvider(QObject* parent):QObject(parent) {
    connect(&scheduler_,&HydroLoadScheduler::frameAccepted,this,&HydroRuntimeProvider::frameChanged);
    connect(&scheduler_,&HydroLoadScheduler::loadFailed,this,&HydroRuntimeProvider::loadFailed);
}
bool HydroRuntimeProvider::open(const QString& path,const QString& projectInstance,bool mobile,QString& error) {
    error.clear();
    auto candidate=std::make_shared<Dataset>(mobile);
    candidate->manifest=readHydroManifest(path);
    if(!candidate->manifest.valid()){error=candidate->manifest.error;return false;}
    const auto index=readHydroAsset(candidate->manifest.index,true,error);
    if(!error.isEmpty())return false;
    const auto core=readHydroAsset(candidate->manifest.metadataCore,true,error);
    if(!error.isEmpty()||!parseHydroCoreMetadata(core,candidate->manifest.metadataFeatureCount,candidate->metadata,error))return false;
    try {
        std::vector<std::uint64_t> lengths;
        for(const auto& shard:candidate->manifest.shards)lengths.push_back(shard.asset.bytes);
        candidate->index=pandoeditor::decodeHydroIndex(
            {reinterpret_cast<const std::uint8_t*>(index.constData()),static_cast<std::size_t>(index.size())},lengths);
        if(candidate->index.tilePacks.size()!=static_cast<std::size_t>(candidate->manifest.indexTileCount)||
           candidate->index.logicalPacks.size()!=static_cast<std::size_t>(candidate->manifest.logicalFeatureCount))
            throw std::runtime_error("hydro index counts differ from manifest");
        for(auto it=candidate->metadata.cbegin();it!=candidate->metadata.cend();++it)
            candidate->logicalIds.emplace(it.key(),it.value().logicalFid);
        for(const auto& shard:candidate->manifest.shards)
            candidate->shards.push_back(std::make_shared<HydroShardReader>(shard.asset));
        for(const auto& stage:candidate->manifest.stages)
            candidate->stages.push_back({static_cast<std::uint8_t>(stage.id),stage.minZoom,
                static_cast<std::uint16_t>(stage.columns),static_cast<std::uint16_t>(stage.rows)});
    }catch(const std::exception& exception){error=QString::fromUtf8(exception.what());return false;}
    dataset_=std::move(candidate);
    scheduler_.resetDataset(projectInstance);
    emit frameChanged();
    return true;
}
void HydroRuntimeProvider::close(const QString& projectInstance) {
    dataset_.reset();scheduler_.resetDataset(projectInstance);emit frameChanged();
}
const HydroMetadata* HydroRuntimeProvider::coreMetadata() const {
    return dataset_?&dataset_->metadata:nullptr;
}
bool HydroRuntimeProvider::pinLogical(quint32 logicalFid) {
    if(!dataset_)return false;
    const auto found=dataset_->index.logicalPacks.find(logicalFid);
    if(found==dataset_->index.logicalPacks.end())return false;
    std::lock_guard lock(dataset_->mutex);
    auto pinned=dataset_->pinned;
    pinned.insert(found->second.begin(),found->second.end());
    dataset_->cache.protect(dataset_->active,pinned);
    dataset_->pinned.swap(pinned);
    return true;
}
void HydroRuntimeProvider::clearPinned() {
    if(!dataset_)return;
    std::lock_guard lock(dataset_->mutex);
    dataset_->cache.protect(dataset_->active,{});dataset_->pinned.clear();
}
std::size_t HydroRuntimeProvider::cachedPackCount() const {
    if(!dataset_)return 0;
    std::lock_guard lock(dataset_->mutex);return dataset_->cache.packCount();
}
std::size_t HydroRuntimeProvider::cachedBytes() const {
    if(!dataset_)return 0;
    std::lock_guard lock(dataset_->mutex);return dataset_->cache.residentBytes();
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
        dataset->cache.protect(pending,dataset->pinned);
        dataset->active.swap(pending);
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
        }
        return std::shared_ptr<const HydroRuntimeFrame>(std::move(next));
    });
}

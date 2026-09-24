#include "hydroruntimeprovider.h"
#include "hydroassetreader.h"
#include <pandoeditor/hydroformat.h>
#include <set>
#include <stdexcept>

struct HydroRuntimeProvider::Dataset {
    HydroManifest manifest;
    pandoeditor::HydroIndex index;
    HydroMetadata metadata;
    std::map<std::uint32_t,std::uint32_t> logicalIds;
    std::vector<std::shared_ptr<HydroShardReader>> shards;
    std::vector<pandoeditor::HydroStageGrid> stages;
};
HydroRuntimeProvider::HydroRuntimeProvider(QObject* parent):QObject(parent) {
    connect(&scheduler_,&HydroLoadScheduler::frameAccepted,this,&HydroRuntimeProvider::frameChanged);
    connect(&scheduler_,&HydroLoadScheduler::loadFailed,this,&HydroRuntimeProvider::loadFailed);
}
bool HydroRuntimeProvider::open(const QString& path,const QString& projectInstance,bool,QString& error) {
    error.clear();
    auto candidate=std::make_shared<Dataset>();
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
void HydroRuntimeProvider::requestViewport(const pandoeditor::HydroFlatWindow& view) {
    if(!dataset_)return;
    const auto dataset=dataset_;
    scheduler_.requestViewport([dataset,view]{
        auto next=std::make_shared<HydroRuntimeFrame>();
        const auto tiles=pandoeditor::hydroViewportTiles(dataset->stages,view);
        std::set<std::uint32_t> ids;
        for(const auto& tile:tiles) {
            const auto found=dataset->index.tilePacks.find(tile);
            if(found!=dataset->index.tilePacks.end())ids.insert(found->second.begin(),found->second.end());
        }
        for(const auto id:ids){
            const auto& spec=dataset->index.packSpecs.at(id);
            QString error;
            const auto bytes=dataset->shards.at(spec.shard)->readPack(spec.offset,spec.length,error);
            if(!error.isEmpty())throw std::runtime_error(error.toStdString());
            const auto pack=pandoeditor::decodeHydroPack(
                {reinterpret_cast<const std::uint8_t*>(bytes.constData()),static_cast<std::size_t>(bytes.size())},
                id,dataset->logicalIds);
            next->packIds.push_back(id);
            next->features.insert(next->features.end(),pack.features.begin(),pack.features.end());
        }
        return std::shared_ptr<const HydroRuntimeFrame>(std::move(next));
    });
}

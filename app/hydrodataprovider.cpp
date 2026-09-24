#include "hydrodataprovider.h"
#include "hydromanifest.h"
#include "hydroassetreader.h"
#include "hydrometadata.h"
#include <pandoeditor/hydroformat.h>
#include <stdexcept>

HydroDataInspection inspectHydroData(const QString& path) {
    HydroDataInspection result;
    const auto manifest=readHydroManifest(path);
    result.root=manifest.root;result.version=manifest.version;result.dataset=manifest.dataset;
    if(!manifest.valid()){result.error=manifest.error;return result;}
    QString error;
    const auto index=readHydroAsset(manifest.index,true,error);
    if(!error.isEmpty()||index.isEmpty()){result.error=error;return result;}
    const auto metadata=readHydroAsset(manifest.metadataCore,true,error);
    if(!error.isEmpty()||metadata.isEmpty()){result.error=error;return result;}
    try {
        const auto parsed=pandoeditor::decodeHydroIndex(
            {reinterpret_cast<const std::uint8_t*>(index.constData()),static_cast<std::size_t>(index.size())},
            [&manifest]{std::vector<std::uint64_t> lengths;for(const auto& shard:manifest.shards)
                lengths.push_back(static_cast<std::uint64_t>(shard.asset.bytes));return lengths;}());
        if(parsed.tilePacks.size()!=static_cast<std::size_t>(manifest.indexTileCount)||
           parsed.logicalPacks.size()!=static_cast<std::size_t>(manifest.logicalFeatureCount)){
            result.error=QStringLiteral("수계 index 개수가 manifest와 다릅니다.");return result;
        }
    }catch(const std::exception& exception){
        result.error=QStringLiteral("수계 index 형식이 올바르지 않습니다: ")+QString::fromUtf8(exception.what());return result;
    }
    HydroMetadata core;
    if(!parseHydroCoreMetadata(metadata,manifest.metadataFeatureCount,core,error)){
        result.error=error;return result;
    }
    result.ready=true;return result;
}

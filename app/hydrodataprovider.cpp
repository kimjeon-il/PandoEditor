#include "hydrodataprovider.h"
#include "hydromanifest.h"
#include "hydroassetreader.h"

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
    for(const auto& shard:manifest.shards){
        if(!verifyHydroAsset(shard.asset,error)){result.error=error;return result;}
    }
    result.ready=true;return result;
}

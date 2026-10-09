#pragma once
#include <QString>
#include <QStringList>
#include <QVector>

struct HydroAssetSpec {
    QString url,path,assetRoot,sha256;
    qint64 bytes=0,offset=0,fileBytes=0;
};
struct HydroStageSpec { int id=0,columns=0,rows=0; double minZoom=0; };
struct HydroShardSpec { int id=0,packs=0; HydroAssetSpec asset; };
struct HydroManifest {
    QString manifestPath,root,assetRoot,version,dataset,schema,crs,error;
    HydroAssetSpec index,metadataCore,metadataDetail,container;
    QVector<HydroStageSpec> stages;
    QVector<HydroShardSpec> shards;
    QStringList layers;
    int metadataFeatureCount=0,indexTileCount=0,logicalFeatureCount=0;
    bool valid() const { return error.isEmpty(); }
};
HydroManifest readHydroManifest(const QString& path);

#include "hydromanifest.h"
#include "hydroassetreader.h"
#include "hydrometadata.h"
#include <pandoeditor/hydroformat.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <iostream>
#include <vector>

namespace {
QJsonArray numbers(const std::vector<std::uint32_t>& values) {
    QJsonArray result;
    for(const auto value:values)result.append(static_cast<qint64>(value));
    return result;
}
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    const auto manifest=readHydroManifest(QString::fromLocal8Bit(argv[1]));
    QString error=manifest.error;
    if(!error.isEmpty()){std::cerr<<error.toStdString()<<'\n';return 3;}
    const auto indexBytes=readHydroAsset(manifest.index,true,error);
    if(!error.isEmpty()){std::cerr<<error.toStdString()<<'\n';return 3;}
    const auto metadataBytes=readHydroAsset(manifest.metadataCore,true,error);
    if(!error.isEmpty()){std::cerr<<error.toStdString()<<'\n';return 3;}
    HydroMetadata core;
    if(!parseHydroCoreMetadata(metadataBytes,manifest.metadataFeatureCount,core,error)){
        std::cerr<<error.toStdString()<<'\n';return 3;
    }
    try {
        std::vector<std::uint64_t> lengths;
        for(const auto& shard:manifest.shards)lengths.push_back(shard.asset.bytes);
        const auto index=pandoeditor::decodeHydroIndex(
            {reinterpret_cast<const std::uint8_t*>(indexBytes.constData()),
             static_cast<std::size_t>(indexBytes.size())},lengths);
        QMap<QString,QJsonArray> sortedTiles;
        for(const auto& [tile,ids]:index.tilePacks){
            const auto key=QString("%1/%2-%3").arg(tile.stage).arg(tile.x).arg(tile.y);
            sortedTiles.insert(key,numbers(ids));
        }
        QJsonArray tiles,logical,packs,metadata;
        for(auto it=sortedTiles.cbegin();it!=sortedTiles.cend();++it)
            tiles.append(QJsonArray{it.key(),it.value()});
        for(const auto& [fid,ids]:index.logicalPacks)
            logical.append(QJsonArray{static_cast<qint64>(fid),numbers(ids)});
        for(const auto& [id,spec]:index.packSpecs)
            packs.append(QJsonArray{static_cast<qint64>(id),QJsonObject{
                {"id",static_cast<qint64>(spec.id)},
                {"shard",spec.shard},{"offset",static_cast<qint64>(spec.offset)},
                {"length",static_cast<qint64>(spec.length)},{"stage",spec.stage}}});
        const auto rows=QJsonDocument::fromJson(metadataBytes).object().value("features").toArray();
        QMap<quint32,QJsonObject> sortedRows;
        for(const auto& row:rows){const auto object=row.toObject();
            const auto fid=static_cast<quint32>(object.value("fid").toDouble());
            if(!core.contains(fid))return 3;
            sortedRows.insert(fid,object);
        }
        for(auto it=sortedRows.cbegin();it!=sortedRows.cend();++it)
            metadata.append(QJsonArray{static_cast<qint64>(it.key()),it.value()});
        const QJsonObject output{{"index",QJsonObject{{"tiles",tiles},{"logical",logical},{"packs",packs}}},
                                 {"metadata",metadata}};
        std::cout<<QJsonDocument(output).toJson(QJsonDocument::Compact).toStdString()<<'\n';
        return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<'\n';return 3;}
}

#include "hydromanifest.h"
#include "hydroassetreader.h"
#include "hydrometadata.h"
#include "hydroshardreader.h"
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
QJsonArray lineJson(const pandoeditor::HydroLine& line) {
    QJsonArray result;
    for(const auto& point:line)result.append(QJsonArray{point.longitude/1e6,point.latitude/1e6});
    return result;
}
QJsonObject geometryJson(const pandoeditor::HydroDecodedGeometry& geometry) {
    QJsonArray outer;
    if(geometry.kind<=2){
        for(const auto& line:geometry.lines)outer.append(lineJson(line));
        return {{"type",geometry.kind==1?"LineString":"MultiLineString"},
                {"coordinates",geometry.kind==1?outer.first():QJsonValue(outer)}};
    }
    for(const auto& polygon:geometry.polygons){
        QJsonArray rings;
        for(const auto& ring:polygon)rings.append(lineJson(ring));
        outer.append(rings);
    }
    return {{"type",geometry.kind==3?"Polygon":"MultiPolygon"},
            {"coordinates",geometry.kind==3?outer.first():QJsonValue(outer)}};
}
}
int main(int argc,char** argv) {
    const bool packMode=argc==3 && QByteArray(argv[1])=="--pack";
    const bool mergeMode=argc==3 && QByteArray(argv[1])=="--merge";
    if(!packMode&&!mergeMode&&argc!=2)return 2;
    const auto manifest=readHydroManifest(QString::fromLocal8Bit(argv[packMode||mergeMode?2:1]));
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
        if(packMode||mergeMode){
            std::map<std::uint32_t,std::uint32_t> logicalIds;
            for(auto it=core.cbegin();it!=core.cend();++it)logicalIds.emplace(it.key(),it.value().logicalFid);
            std::vector<std::unique_ptr<HydroShardReader>> readers;
            for(const auto& shard:manifest.shards)readers.push_back(std::make_unique<HydroShardReader>(shard.asset));
            QJsonArray packsJson;
            std::vector<pandoeditor::HydroPhysicalFeature> fragments;
            for(const auto& [id,spec]:index.packSpecs){
                QString packError;
                const auto bytes=readers.at(spec.shard)->readPack(spec.offset,spec.length,packError);
                if(!packError.isEmpty())throw std::runtime_error(packError.toStdString());
                const auto pack=pandoeditor::decodeHydroPack(
                    {reinterpret_cast<const std::uint8_t*>(bytes.constData()),static_cast<std::size_t>(bytes.size())},id,logicalIds);
                QJsonArray features;
                for(const auto& feature:pack.features){
                    if(feature.logicalFid==5)fragments.push_back(feature);
                    QJsonArray widths;
                    for(const auto& profile:feature.widths){QJsonArray part;
                        for(const auto width:profile)part.append(width);widths.append(part);}
                    features.append(QJsonObject{{"fid",static_cast<qint64>(feature.fid)},
                        {"logicalFid",static_cast<qint64>(feature.logicalFid)},
                        {"kind",feature.kind==1?"river":"lake"},{"flags",feature.flags},
                        {"geometry",geometryJson(feature.geometry)},{"widths",widths}});
                }
                packsJson.append(QJsonObject{{"id",static_cast<qint64>(id)},{"features",features}});
            }
            if(mergeMode){
                const auto merged=pandoeditor::mergeHydroLogicalFragments(std::move(fragments));
                QJsonArray lines;
                for(const auto& line:merged.lines){QJsonArray points;
                    for(const auto& point:line)points.append(QJsonArray{point.x,point.y});
                    lines.append(points);
                }
                std::cout<<QJsonDocument(QJsonObject{{"type",QString::fromStdString(merged.type)},
                    {"coordinates",lines}}).toJson(QJsonDocument::Compact).toStdString()<<'\n';
            }else std::cout<<QJsonDocument(packsJson).toJson(QJsonDocument::Compact).toStdString()<<'\n';
            return 0;
        }
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

#pragma once
#include <pandoeditor/document.h>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QString>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace m71fixture {
struct GeometryStats {
    std::size_t polygonCount=0,ringCount=0,holeCount=0,coordinateCount=0;
    double west=0,south=0,east=0,north=0,maxLongitudeJump=0;
};

inline QJsonObject readObject(const QString& root,const QString& name) {
    QFile file(QDir(root).filePath(name));
    if(!file.open(QIODevice::ReadOnly)) throw std::runtime_error("missing M7.1 fixture: "+name.toStdString());
    QJsonParseError error;
    const auto json=QJsonDocument::fromJson(file.readAll(),&error);
    if(error.error!=QJsonParseError::NoError || !json.isObject())
        throw std::runtime_error("invalid M7.1 JSON: "+name.toStdString());
    return json.object();
}

inline QJsonObject readCorpusManifest(const QString& fixtureRoot) {
    return readObject(fixtureRoot,QStringLiteral("manifest.json"));
}

inline pandoeditor::Point jsonPoint(const QJsonValue& value) {
    const auto a=value.toArray();
    if(a.size()<2 || !a.at(0).isDouble() || !a.at(1).isDouble() ||
       !std::isfinite(a.at(0).toDouble()) || !std::isfinite(a.at(1).toDouble()))
        throw std::runtime_error("invalid M7.1 source coordinate");
    return {a.at(0).toDouble(),a.at(1).toDouble()};
}

inline pandoeditor::Geometry geoJsonGeometry(const QJsonObject& source) {
    pandoeditor::Geometry geometry;
    geometry.type=source.value(QStringLiteral("type")).toString().toStdString();
    const auto coords=source.value(QStringLiteral("coordinates")).toArray();
    auto ring=[](const QJsonArray& a) {
        pandoeditor::Ring result;result.reserve(std::size_t(a.size()));
        for(const auto& point:a) result.push_back(jsonPoint(point));
        return result;
    };
    auto polygon=[&](const QJsonArray& a) {
        pandoeditor::Polygon result;result.reserve(std::size_t(a.size()));
        for(const auto& r:a) result.push_back(ring(r.toArray()));
        geometry.polygons.push_back(std::move(result));
    };
    if(geometry.type=="Point") geometry.points.push_back(jsonPoint(source.value(QStringLiteral("coordinates"))));
    else if(geometry.type=="LineString") geometry.lines.push_back(ring(coords));
    else if(geometry.type=="MultiLineString") for(const auto& line:coords) geometry.lines.push_back(ring(line.toArray()));
    else if(geometry.type=="Polygon") polygon(coords);
    else if(geometry.type=="MultiPolygon") for(const auto& part:coords) polygon(part.toArray());
    else throw std::runtime_error("unsupported M7.1 geometry type");
    return geometry;
}

inline GeometryStats geometryStats(const pandoeditor::Geometry& geometry) {
    GeometryStats result;
    result.west=result.south=std::numeric_limits<double>::infinity();
    result.east=result.north=-std::numeric_limits<double>::infinity();
    auto points=[&](const pandoeditor::Ring& ring) {
        bool previous=false;double longitude=0;
        for(const auto& point:ring) {
            if(!std::isfinite(point.x)||!std::isfinite(point.y)) throw std::runtime_error("non-finite M7.1 coordinate");
            result.west=std::min(result.west,point.x);result.south=std::min(result.south,point.y);
            result.east=std::max(result.east,point.x);result.north=std::max(result.north,point.y);
            if(previous) result.maxLongitudeJump=std::max(result.maxLongitudeJump,std::abs(point.x-longitude));
            longitude=point.x;previous=true;++result.coordinateCount;
        }
    };
    if(!geometry.points.empty()) for(const auto& point:geometry.points) points({point});
    for(const auto& line:geometry.lines) points(line);
    result.polygonCount=geometry.polygons.size();
    for(const auto& polygon:geometry.polygons) {
        result.ringCount+=polygon.size();
        result.holeCount+=polygon.empty()?0:polygon.size()-1;
        for(const auto& ring:polygon) points(ring);
    }
    if(!result.coordinateCount) throw std::runtime_error("empty M7.1 geometry");
    return result;
}

inline pandoeditor::ProjectDocument loadWorldCorpusProject(const QString& fixtureRoot) {
    using namespace pandoeditor;
    ProjectDocument d;d.documentId="m71-world-rendering";
    d.presentation.userLayers={{"countries","Countries"},{"composition","M7.1 composition"}};
    const auto manifest=readCorpusManifest(fixtureRoot);
    const auto sourceVersion=manifest.value(QStringLiteral("worldMapCommit")).toString().toStdString();
    auto put=[&](const std::string& id,const QJsonObject& object) {
        GeometryRef ref{"m71-"+id,1};d.geometries.insert(ref,geoJsonGeometry(object));return ref;
    };
    auto country=readObject(fixtureRoot,QStringLiteral("countries.geojson"));
    for(const auto& value:country.value(QStringLiteral("features")).toArray()) {
        const auto feature=value.toObject();const auto id=feature.value(QStringLiteral("id")).toString().toStdString();
        TerritorialUnit unit;unit.id=id;unit.name=feature.value(QStringLiteral("properties")).toObject()
            .value(QStringLiteral("name")).toString().toStdString();unit.baseName=unit.name;
        unit.kind=UnitKind::Country;unit.geometry=put("country-"+id,feature.value(QStringLiteral("geometry")).toObject());
        d.units.push_back(std::move(unit));
        d.presentation.membership[territorialRef(id)]="countries";
        d.presentation.objectStyles[territorialRef(id)]={0xa8c7db,1};
    }
    const auto composition=readObject(fixtureRoot,QStringLiteral("composition.json"));
    for(const auto& key:{"subunit","region"}) {
        const auto entry=composition.value(QString::fromLatin1(key)).toObject();
        const auto id=entry.value(QStringLiteral("id")).toString().toStdString();
        TerritorialUnit unit;unit.id=id;unit.name=entry.value(QStringLiteral("name")).toString().toStdString();
        unit.kind=QString::fromLatin1(key)==QStringLiteral("subunit")?UnitKind::Subunit:UnitKind::Region;
        unit.geometry=put("unit-"+id,entry.value(QStringLiteral("geometry")).toObject());
        d.units.push_back(std::move(unit));
        d.relations.push_back({"m71-relation-"+id,territorialRef(id),
            territorialRef(entry.value(QStringLiteral("parent")).toString().toStdString()),
            territorialRef(entry.value(QStringLiteral("sovereign")).toString().toStdString())});
        d.presentation.membership[territorialRef(id)]="composition";
        d.presentation.objectStyles[territorialRef(id)]={0x97b7c8,1};
    }
    for(const auto& value:readObject(fixtureRoot,QStringLiteral("sentinels.geojson"))
            .value(QStringLiteral("features")).toArray()) {
        const auto feature=value.toObject();const auto id=feature.value(QStringLiteral("id")).toString().toStdString();
        GenericFeature item;item.id=id;item.name=id;item.geometry=put("synthetic-"+id,feature.value(QStringLiteral("geometry")).toObject());
        item.source.kind="user";item.source.dataset="m71-synthetic";item.source.sourceId=id;
        d.genericFeatures.push_back(std::move(item));
    }
    for(const auto& row:{std::pair<const char*,const char*>{"hydro-river.geojson","river"},
                        {"hydro-lake.geojson","lake"}}) {
        const auto feature=readObject(fixtureRoot,QString::fromLatin1(row.first))
            .value(QStringLiteral("features")).toArray().at(0).toObject();
        HydroFeature item;item.id=feature.value(QStringLiteral("id")).toString().toStdString();
        item.name=feature.value(QStringLiteral("properties")).toObject().value(QStringLiteral("name")).toString().toStdString();
        if(item.name.empty()) item.name=item.id;
        item.kind=row.second;item.geometry=put("hydro-"+item.id,feature.value(QStringLiteral("geometry")).toObject());
        item.source.kind="gis";item.source.dataset="world-map";item.source.version=sourceVersion;
        item.source.sourceId=item.id;item.source.sourceFormat="geojson";
        d.hydro.push_back(std::move(item));
    }
    for(const auto& value:composition.value(QStringLiteral("distributions")).toArray()) {
        const auto entry=value.toObject();const auto id=entry.value(QStringLiteral("id")).toString().toStdString();
        DistributionLayer layer;layer.id=entry.value(QStringLiteral("layerId")).toString().toStdString();
        layer.name=entry.value(QStringLiteral("name")).toString().toStdString();
        layer.type=entry.value(QStringLiteral("type")).toString().toStdString();
        d.distributionLayers.push_back(std::move(layer));
        DistributionEntry item;item.id=id;item.layerId=d.distributionLayers.back().id;
        // Native entries require exactly one of territory or geometry. Keep
        // the composition's owning territory as provenance in metadata.
        item.geometry=put("distribution-"+id,entry.value(QStringLiteral("geometry")).toObject());
        item.share=entry.value(QStringLiteral("share")).toDouble();
        item.metadata=QJsonDocument(QJsonObject{{QStringLiteral("compositionTerritory"),
            entry.value(QStringLiteral("territory"))}}).toJson(QJsonDocument::Compact).toStdString();
        d.distributionEntries.push_back(std::move(item));
    }
    for(const auto& value:composition.value(QStringLiteral("generic")).toArray()) {
        const auto entry=value.toObject();const auto id=entry.value(QStringLiteral("id")).toString().toStdString();
        GenericFeature item;item.id=id;item.name=entry.value(QStringLiteral("name")).toString().toStdString();
        item.geometry=put("generic-"+id,entry.value(QStringLiteral("geometry")).toObject());
        d.genericFeatures.push_back(std::move(item));
    }
    for(const auto& value:composition.value(QStringLiteral("labels")).toArray()) {
        const auto entry=value.toObject();const auto id=entry.value(QStringLiteral("id")).toString().toStdString();
        PlaceLabel item;item.id=id;item.name=entry.value(QStringLiteral("name")).toString().toStdString();
        item.notes="placement:"+entry.value(QStringLiteral("placement")).toString().toStdString();
        item.geometry=put("label-"+id,entry.value(QStringLiteral("geometry")).toObject());
        item.territory=territorialRef(entry.value(QStringLiteral("territory")).toString().toStdString());
        if(entry.value(QStringLiteral("placement")).toString()==QStringLiteral("manual")) {
            LabelSettings settings;settings.pinned=true;
            settings.manualPosition=d.geometries.get(item.geometry)->points.front();
            d.presentation.webPresentation.labelSettings[{"label",id}]=settings;
        }
        d.labels.push_back(std::move(item));
    }
    validateDocument(d);
    return d;
}
}

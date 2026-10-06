#include "territorycutadapter.h"
#include "cutgeometrycalculator.h"
#include <QJsonArray>
#include <QJsonObject>
#include <stdexcept>
#include <utility>

namespace pandoeditor {
namespace {
QJsonObject cutGeometryJson(const Geometry& value) {
    QJsonArray polygons;for(const auto& polygon:value.polygons){QJsonArray rings;for(const auto& ring:polygon){QJsonArray points;for(const auto& point:ring)points.append(QJsonArray{point.x,point.y});rings.append(points);}polygons.append(rings);}
    return {{"type",QString::fromStdString(value.type)},{"coordinates",value.type=="Polygon"?polygons[0].toArray():polygons}};
}
Geometry cutGeometryValue(const QJsonObject& value) {
    Geometry geometry;geometry.type=value["type"].toString().toStdString();auto polygons=value["coordinates"].toArray();if(geometry.type=="Polygon")polygons=QJsonArray{polygons};
    for(const auto& polygon:polygons){Polygon rings;for(const auto& ring:polygon.toArray()){Ring points;for(const auto& point:ring.toArray()){const auto xy=point.toArray();if(xy.size()!=2)throw std::runtime_error("INVALID_CUT_COORDINATE");points.push_back({xy[0].toDouble(),xy[1].toDouble()});}rings.push_back(std::move(points));}geometry.polygons.push_back(std::move(rings));}return geometry;
}
QJsonObject cutViewJson(const MapViewState& view,bool touch) {
    return {{"kind",view.mode==ProjectionMode::Globe?"globe":"flat"},{"scale",view.scale},
      {"translate",QJsonArray{view.translateX,view.translateY}},{"rotate",QJsonArray{view.rotationLongitude,view.rotationLatitude,view.rotationRoll}},
      {"center",QJsonArray{view.centerLongitude,view.centerLatitude}},{"size",QJsonObject{{"width",view.viewportWidth},{"height",view.viewportHeight}}},
      {"snapDistance",QJsonObject{{"mouse",10},{"touch",18}}},{"coarsePointer",touch}};
}
}
TerritorySelectionDraftResult prepareTerritoryLineCandidates(const TerritoryCutRequest& request,
    const GeometryCancellation& cancelled) {
    QJsonArray points;for(const auto& point:request.coordinates)points.append(QJsonArray{point.x,point.y});
    auto calculated=prepareCutGeometry({{"source",cutGeometryJson(request.source)},{"coords",points},{"view",cutViewJson(request.view,request.coarsePointer)},{"buildPreview",true}},cancelled);
    TerritorySelectionDraftResult out;
    if(calculated.status==CutGeometryStatus::Cancelled){out.status=GeometryOperationStatus::Cancelled;return out;}
    if(!calculated.succeeded()){out.detail=calculated.detail.toStdString();return out;}
    if(!calculated.result["valid"].toBool()){out.status=GeometryOperationStatus::Empty;out.detail=calculated.result["message"].toString().toStdString();return out;}
    for(const auto& row:calculated.result["split"].toObject()["candidates"].toArray()){const auto item=row.toObject();out.candidates.push_back({item["id"].toString().toStdString(),cutGeometryValue(item["geometry"].toObject()),item["area"].toDouble()});}
    out.status=out.candidates.empty()?GeometryOperationStatus::Empty:GeometryOperationStatus::Completed;return out;
}
}

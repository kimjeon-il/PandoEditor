#pragma once
#include "geometrysnap.h"
#include "territorial_fixture.h"
#include <QJsonArray>
#include <QJsonObject>
#include <stdexcept>
namespace m974snapfixture {
using namespace pandoeditor;
inline Point point(QJsonValue value){const auto a=value.toArray();if(a.size()!=2||!a[0].isDouble()||!a[1].isDouble())throw std::runtime_error("invalid coordinate");return {a[0].toDouble(),a[1].toDouble()};}
inline QJsonArray json(Point value){return {value.x,value.y};}
inline Geometry geometry(QJsonObject value){Geometry g;g.type=value["type"].toString().toStdString();const auto c=value["coordinates"].toArray();if(g.type=="Point")g.points={point(c)};else if(g.type=="MultiPoint")for(const auto v:c)g.points.push_back(point(v));else if(g.type=="LineString"||g.type=="MultiLineString"){const auto lines=g.type=="LineString"?QJsonArray{c}:c;for(const auto l:lines){Ring line;for(const auto v:l.toArray())line.push_back(point(v));g.lines.push_back(line);}}else if(g.type=="Polygon"||g.type=="MultiPolygon"){const auto polygons=g.type=="Polygon"?QJsonArray{c}:c;for(const auto p:polygons){Polygon polygon;for(const auto r:p.toArray()){Ring ring;for(const auto v:r.toArray())ring.push_back(point(v));polygon.push_back(ring);}g.polygons.push_back(polygon);}}else throw std::runtime_error("unsupported geometry");return g;}
inline std::vector<std::string> strings(QJsonValue value){std::vector<std::string> result;for(const auto v:value.toArray())result.push_back(v.toString().toStdString());return result;}
inline QJsonArray json(const std::vector<std::string>& values){QJsonArray result;for(const auto& value:values)result.append(QString::fromStdString(value));return result;}
inline QJsonObject json(const geometrysnap::Candidate& value){QJsonObject r{{"kind",QString::fromStdString(value.kind)},{"ownerIds",json(value.ownerIds)}};if(value.coordinate)r["coordinate"]=json(*value.coordinate);if(value.a)r["a"]=json(*value.a);if(value.b)r["b"]=json(*value.b);if(!value.nodeKey.empty())r["nodeKey"]=QString::fromStdString(value.nodeKey);if(!value.segmentKey.empty())r["segmentKey"]=QString::fromStdString(value.segmentKey);return r;}
inline geometrysnap::Candidate candidate(QJsonObject value){geometrysnap::Candidate c;c.kind=value["kind"].toString().toStdString();c.ownerIds=strings(value["ownerIds"]);c.nodeKey=value["nodeKey"].toString().toStdString();c.segmentKey=value["segmentKey"].toString().toStdString();if(value.contains("coordinate"))c.coordinate=point(value["coordinate"]);if(value.contains("a"))c.a=point(value["a"]);if(value.contains("b"))c.b=point(value["b"]);return c;}
inline QJsonValue json(const std::optional<geometrysnap::Result>& result){if(!result)return QJsonValue::Null;auto r=json(result->candidate);r["coordinate"]=json(result->coordinate);r["distancePx"]=result->distancePx;if(result->segmentT)r["segmentT"]=*result->segmentT;if(result->segmentEndpoints)r["segmentEndpoints"]=QJsonArray{json((*result->segmentEndpoints)[0]),json((*result->segmentEndpoints)[1])};return r;}
}

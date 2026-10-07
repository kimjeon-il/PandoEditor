#include <pandoeditor/geometrypredicates.h>
#include "geometrycalculator.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <stdexcept>
using namespace pandoeditor;
// Representation conversion only. All geographic decisions are made by Core.
static Geometry decode(const QJsonObject& value) {
    Geometry geometry; geometry.type=value["type"].toString().toStdString();
    if (geometry.type!="Polygon" && geometry.type!="MultiPolygon") throw std::runtime_error("Unsupported geometry type");
    auto polygons=value["coordinates"].toArray();
    if (geometry.type=="Polygon") polygons=QJsonArray{polygons};
    for (const auto polygonValue:polygons) {
        Polygon polygon;
        for (const auto ringValue:polygonValue.toArray()) {
            Ring ring;
            for (const auto pointValue:ringValue.toArray()) {
                const auto point=pointValue.toArray();
                if (point.size()!=2 || !point[0].isDouble() || !point[1].isDouble()) throw std::runtime_error("Expected a two-dimensional coordinate");
                ring.push_back({point[0].toDouble(),point[1].toDouble()});
            }
            polygon.push_back(std::move(ring));
        }
        geometry.polygons.push_back(std::move(polygon));
    }
    return geometry;
}
static QJsonObject encode(const Geometry& geometry) {
    QJsonArray polygons;
    for (const auto& polygon:geometry.polygons) {
        QJsonArray rings;
        for (const auto& ring:polygon) {
            QJsonArray points; for (const auto& point:ring) points.append(QJsonArray{point.x,point.y});
            rings.append(points);
        }
        polygons.append(rings);
    }
    return {{"type",QString::fromStdString(geometry.type)}, {"coordinates",geometry.type=="Polygon"?polygons.at(0):QJsonValue(polygons)}};
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);QFile input;if(!input.open(stdin,QIODevice::ReadOnly))return 2;
    QJsonParseError error;const auto document=QJsonDocument::fromJson(input.readAll(),&error);
    if(error.error!=QJsonParseError::NoError||!document.isArray())return 3;
    QJsonArray output;
    try {
        for(const auto value:document.array()) {
            const auto row=value.toObject();
            if(row.contains("operation")) {
                const auto operation=row["operation"].toString();
                if(operation!="union"&&operation!="difference"&&operation!="intersection")throw std::runtime_error("Unknown calculation");
                GeometryOperationRequest request;request.operation=operation=="union"?GeometryOperation::Union:operation=="difference"?GeometryOperation::Difference:GeometryOperation::Intersection;
                for(const auto operand:row["operands"].toArray())request.operands.push_back(decode(operand.toObject()));
                const auto result=calculateGeometry(request);
                const auto status=result.status==GeometryOperationStatus::Completed?"completed":result.status==GeometryOperationStatus::Empty?"empty":result.status==GeometryOperationStatus::Cancelled?"cancelled":"failed";
                output.append(QJsonObject{{"id",row["id"]},{"status",status},{"geometry",result.status==GeometryOperationStatus::Completed?QJsonValue(encode(result.geometry)):QJsonValue(QJsonValue::Null)}});
                continue;
            }
            const auto parent=decode(row["parent"].toObject()),child=decode(row["child"].toObject());
            const auto contains=geometryContains(parent,child);
            output.append(QJsonObject{{"id",row["id"]},{"contains",contains},{"parent",encode(parent)},{"child",encode(child)}});
        }
    } catch(const std::exception& error) {std::cerr<<error.what();return 4;}
    std::cout<<QJsonDocument(output).toJson(QJsonDocument::Compact).constData();
}

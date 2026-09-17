#include "projectcodec.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <stdexcept>

namespace projectcodec {
namespace {
void require(bool valid, const char* message) { if (!valid) throw std::invalid_argument(message); }
}
pandoeditor::ProjectDocument decode(const QByteArray& data)
{
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(data,&error);
    require(error.error==QJsonParseError::NoError && doc.isObject(),"Invalid JSON document");
    const auto root = doc.object();
    require(root["format"].toString()=="pandoeditor-project","Unsupported project format");
    require(root["version"].isDouble() && (root["version"].toDouble()==1 || root["version"].toDouble()==2),"Unsupported project version");
    const bool legacy=root["version"].toDouble()==1;
    pandoeditor::ProjectDocument document;
    if (legacy) document.layers.push_back({"countries","국가"});
    else {
        require(root["layers"].isArray(),"Missing layers array");
        for (const auto value:root["layers"].toArray()) {
            require(value.isObject(),"Invalid layer");
            const auto l=value.toObject();
            require(l["id"].isString() && l["name"].isString() && l["visible"].isBool() &&
                    l["locked"].isBool() && l["opacity"].isDouble(),"Invalid layer properties");
            document.layers.push_back({l["id"].toString().toStdString(),l["name"].toString().trimmed().toStdString(),
                                       l["visible"].toBool(),l["locked"].toBool(),l["opacity"].toDouble()});
        }
    }
    require(root["countries"].isArray(),"Missing countries array");
    std::vector<pandoeditor::Country> countries;
    for (const auto value : root["countries"].toArray()) {
        require(value.isObject(),"Invalid country");
        const auto object = value.toObject();
        require(object["id"].isString() && object["name"].isString(),"Invalid country ID/name");
        const QString color = object["color"].toString();
        require(QRegularExpression("^#[0-9a-fA-F]{6}$").match(color).hasMatch(),"Invalid RGB color");
        pandoeditor::Country country{object["id"].toString().toStdString(),object["name"].toString().toStdString(),{},color.mid(1).toUInt(nullptr,16)};
        country.name=object["name"].toString().trimmed().toStdString();
        if (!legacy) {
            require(object["memo"].isString() && object["opacity"].isDouble() && object["layerId"].isString(),"Invalid country properties");
            country.memo=object["memo"].toString().toStdString();
            country.opacity=object["opacity"].toDouble();
            country.layerId=object["layerId"].toString().toStdString();
        }
        const auto geometry = object["geometry"].toObject();
        require(geometry["type"].toString()=="MultiPolygon" && geometry["coordinates"].isArray(),"Expected MultiPolygon");
        for (const auto polyValue : geometry["coordinates"].toArray()) {
            require(polyValue.isArray(),"Invalid polygon");
            pandoeditor::Polygon polygon;
            for (const auto ringValue : polyValue.toArray()) {
                require(ringValue.isArray(),"Invalid ring");
                pandoeditor::Ring ring;
                for (const auto pointValue : ringValue.toArray()) {
                    require(pointValue.isArray(),"Invalid point");
                    auto point = pointValue.toArray();
                    require(point.size()==2 && point[0].isDouble() && point[1].isDouble(),"Expected two numeric coordinates");
                    ring.push_back({point[0].toDouble(),point[1].toDouble()});
                }
                polygon.push_back(std::move(ring));
            }
            country.polygons.push_back(std::move(polygon));
        }
        countries.push_back(std::move(country));
    }
    document.countries=std::move(countries);
    pandoeditor::Project::validate(document);
    return document;
}
QByteArray encode(const pandoeditor::Project& project)
{
    QJsonArray countries;
    for (const auto& country : project.countries()) {
        QJsonArray polygons;
        for (const auto& polygon : country.polygons) {
            QJsonArray rings;
            for (const auto& ring : polygon) {
                QJsonArray points;
                for (auto point : ring) points.append(QJsonArray{point.x,point.y});
                rings.append(points);
            }
            polygons.append(rings);
        }
        countries.append(QJsonObject{{"id",QString::fromStdString(country.id)},
            {"name",QString::fromStdString(country.name)},
            {"color",QString("#%1").arg(country.color,6,16,QChar('0'))},
            {"memo",QString::fromStdString(country.memo)},
            {"opacity",country.opacity},
            {"layerId",QString::fromStdString(country.layerId)},
            {"geometry",QJsonObject{{"type","MultiPolygon"},{"coordinates",polygons}}}});
    }
    QJsonArray layers;
    for(const auto& l:project.layers())
        layers.append(QJsonObject{{"id",QString::fromStdString(l.id)},{"name",QString::fromStdString(l.name)},
                                  {"visible",l.visible},{"locked",l.locked},{"opacity",l.opacity}});
    return QJsonDocument(QJsonObject{{"format","pandoeditor-project"},{"version",2},{"countries",countries},{"layers",layers}}).toJson();
}
}

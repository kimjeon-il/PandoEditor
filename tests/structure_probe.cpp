#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <iostream>

namespace {
QJsonObject answer(const QJsonObject& row, QJsonValue value) {
    return {{"case", row.value("case")}, {"supported", true}, {"result", std::move(value)}};
}

QJsonObject deletion(const QJsonObject& row) {
    const auto units = row.value("units").toArray();
    const auto targets = row.value("targets").toArray();
    bool allowed = !targets.isEmpty();
    for (const auto& targetValue : targets) {
        const auto target = targetValue.toString();
        QJsonObject selected;
        bool found = false;
        for (const auto& unitValue : units) {
            const auto unit = unitValue.toObject();
            if (unit.value("id").toString() == target) { selected = unit; found = true; break; }
        }
        if (!found || selected.value("properties").toObject().value("locked").toBool()) { allowed = false; break; }
        for (const auto& unitValue : units)
            if (unitValue.toObject().value("properties").toObject().value("parentId").toString() == target) { allowed = false; break; }
        if (!allowed) break;
    }
    return answer(row, allowed);
}

bool onSegment(const QJsonArray& a, const QJsonArray& b, const QJsonArray& p) {
    if (a.size() < 2 || b.size() < 2 || p.size() < 2) return false;
    const double ax=a[0].toDouble(), ay=a[1].toDouble(), bx=b[0].toDouble(), by=b[1].toDouble();
    const double px=p[0].toDouble(), py=p[1].toDouble();
    if (std::abs((px-ax)*(by-ay)-(py-ay)*(bx-ax)) > 1e-7) return false;
    return px >= std::min(ax,bx)-1e-7 && px <= std::max(ax,bx)+1e-7
        && py >= std::min(ay,by)-1e-7 && py <= std::max(ay,by)+1e-7;
}

bool boundaryTouches(const QJsonObject& geometry, const QJsonArray& point) {
    const auto coordinates = geometry.value("coordinates").toArray();
    const auto polygons = geometry.value("type").toString() == "Polygon" ? QJsonArray{coordinates} : coordinates;
    for (const auto& polygonValue : polygons)
        for (const auto& ringValue : polygonValue.toArray()) {
            const auto ring = ringValue.toArray();
            for (int i=0; i<ring.size(); ++i)
                if (onSegment(ring[i].toArray(), ring[(i+1)%ring.size()].toArray(), point)) return true;
        }
    return false;
}

QJsonObject referenceDelete(const QJsonObject& row) {
    auto state = row.value("state").toObject();
    const auto id = row.value("targetId").toString();
    auto filter = [&](const char* name, auto remove) {
        QJsonArray next;
        for (const auto& value : state.value(name).toArray()) if (!remove(value.toObject())) next.append(value);
        state[name] = next;
    };
    filter("territorialUnits", [&](const QJsonObject& value){ return value.value("id").toString() == id; });
    filter("territorialRelations", [&](const QJsonObject& value){ return value.value("unitId").toString() == id || value.value("parentId").toString() == id; });
    const auto mode = row.value("territorialMode").toString();
    filter("distributionEntries", [&](const QJsonObject& value){ return value.value("mode").toString() == mode && value.value("territorialUnitId").toString() == id; });
    auto visibility = state.value("itemVisibility").toObject();
    for (const auto& group : {"subunits", "regions"}) { auto values=visibility.value(group).toObject(); values.remove(id); visibility[group]=values; }
    state["itemVisibility"] = visibility;
    auto labels = state.value("labelSettings").toObject();
    for (const auto& prefix : {"subunit:","region:","territorial:subunit:","territorial:region:"}) labels.remove(QString::fromLatin1(prefix)+id);
    state["labelSettings"] = labels;
    return state;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) return 2;
    const auto document = QJsonDocument::fromJson(input.readAll());
    if (!document.isArray()) return 3;
    QJsonArray output;
    for (const auto& value : document.array()) {
        const auto row = value.toObject();
        const auto kind = row.value("kind").toString();
        if (kind == "delete") output.append(deletion(row));
        else if (kind == "parent") output.append(answer(row, row.value("parentId")));
        else if (kind == "sovereign") output.append(answer(row, row.value("sovereignId")));
        else if (kind == "create") output.append(answer(row, QJsonObject{{"unitType",row.value("unitType")},{"parentId",row.value("parentId")},{"sovereignId",row.value("sovereignId")},{"coverageMode",row.value("coverageMode")}}));
        else if (kind == "convert") output.append(answer(row, row.value("targetType")));
        else if (kind == "transfer") output.append(answer(row, QJsonObject{{"parentId",row.value("destinationId")},{"sovereignId",row.value("destinationId")}}));
        else if (kind == "boundary") output.append(answer(row, boundaryTouches(row.value("geometry").toObject(), row.value("point").toArray())));
        else if (kind == "reference-delete") output.append(answer(row, referenceDelete(row)));
        else return 4;
    }
    std::cout << QJsonDocument(output).toJson(QJsonDocument::Compact).constData();
    return 0;
}

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <iostream>

static QJsonObject unsupported(const QJsonObject& row) {
    return {{"case", row.value("case")}, {"supported", false}};
}
static QJsonObject deletion(const QJsonObject& row) {
    const auto units=row.value("units").toArray();
    const auto targets=row.value("targets").toArray();
    bool allowed=!targets.isEmpty();
    for(const auto& targetValue:targets) {
        const auto target=targetValue.toString();QJsonObject selected;bool found=false;
        for(const auto& unitValue:units) {const auto unit=unitValue.toObject();if(unit.value("id").toString()==target){selected=unit;found=true;break;}}
        if(!found||selected.value("properties").toObject().value("locked").toBool()) {allowed=false;break;}
        for(const auto& unitValue:units)if(unitValue.toObject().value("properties").toObject().value("parentId").toString()==target) {allowed=false;break;}
        if(!allowed)break;
    }
    return {{"case",row.value("case")},{"supported",true},{"result",allowed}};
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) return 2;
    const auto document = QJsonDocument::fromJson(input.readAll());
    if (!document.isArray()) return 3;
    QJsonArray output;
    for (const auto& value : document.array()) {const auto row=value.toObject();output.append(row.value("kind").toString()=="delete"?deletion(row):unsupported(row));}
    std::cout << QJsonDocument(output).toJson(QJsonDocument::Compact).constData();
    return 0;
}

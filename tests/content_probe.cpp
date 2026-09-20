#include <pandoeditor/document.h>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    QFile input;
    if(!input.open(stdin,QIODevice::ReadOnly)) return 2;
    QJsonParseError error;
    const auto json=QJsonDocument::fromJson(input.readAll(),&error);
    if(error.error!=QJsonParseError::NoError || !json.isObject()) return 2;
    pandoeditor::ProjectDocument d;
    std::vector<std::string> visible;
    for(const auto& value:json.object()["layers"].toArray()) visible.push_back(value.toObject()["id"].toString().toStdString());
    for(const auto& value:json.object()["entries"].toArray()) {
        const auto row=value.toObject(); pandoeditor::DistributionEntry entry;
        entry.id=row["id"].toString().toStdString(); entry.layerId=row["layerId"].toString().toStdString();
        entry.share=row["share"].toDouble();
        if(row["mode"].toString()=="geometry") entry.geometry=pandoeditor::GeometryRef{"fixture",1};
        else entry.territory=pandoeditor::territorialRef(row["territorialUnitId"].toString().toStdString());
        d.distributionEntries.push_back(std::move(entry));
    }
    QJsonArray output;
    for(const auto& ref:pandoeditor::dominantDistributionEntries(d,visible)) output.append(QString::fromStdString(ref.id));
    std::cout<<QJsonDocument(output).toJson(QJsonDocument::Compact).constData()<<'\n';
}

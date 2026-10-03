#include <pandoeditor/document.h>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);QFile input;if(!input.open(stdin,QIODevice::ReadOnly))return 2;
    const auto json=QJsonDocument::fromJson(input.readAll()).object();pandoeditor::ProjectDocument d;
    for(const auto& row:json["layers"].toArray()) {
        const auto v=row.toObject();pandoeditor::DistributionLayer layer;layer.id=v["id"].toString().toStdString();
        const auto scale=v["valueScale"].toObject();if(scale["mode"].toString()=="manual")layer.valueScale={true,scale["min"].toDouble(),scale["max"].toDouble()};
        d.distributionLayers.push_back(layer);
    }
    for(const auto& row:json["entries"].toArray()) {const auto v=row.toObject();pandoeditor::DistributionEntry e;e.id=v["id"].toString().toStdString();e.layerId=v["layerId"].toString().toStdString();e.value=v["value"].toDouble();d.distributionEntries.push_back(e);}
    QJsonObject ranges,alpha;
    for(const auto& layer:d.distributionLayers) {const auto range=pandoeditor::distributionValueRange(d,layer.id);const auto key=QString::fromStdString(layer.id);ranges[key]=range?QJsonValue(QJsonObject{{"min",range->min},{"max",range->max}}):QJsonValue();}
    for(const auto& e:d.distributionEntries)alpha[QString::fromStdString(e.id)]=pandoeditor::distributionValueAlpha(e.value,pandoeditor::distributionValueRange(d,e.layerId),json["opacity"].toDouble(1));
    std::cout<<QJsonDocument(QJsonObject{{"ranges",ranges},{"alpha",alpha}}).toJson(QJsonDocument::Compact).constData()<<'\n';
}

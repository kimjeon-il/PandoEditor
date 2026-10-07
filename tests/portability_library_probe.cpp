#include "territoriallibrarycatalog.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);QFile input;if(!input.open(stdin,QIODevice::ReadOnly))return 2;
    QJsonParseError error;const auto parsed=QJsonDocument::fromJson(input.readAll(),&error);
    if(error.error!=QJsonParseError::NoError||!parsed.isObject())return 3;
    QJsonArray output;
    try {
        const auto payload=parsed.object();int reads=0;bool fail=false;
        pandoeditor::TerritorialLibraryCatalog catalog(payload["indexText"].toString().toUtf8(),payload["sha256"].toString().toLatin1(),QString(),
            [&](const QString& file)->QByteArray{++reads;if(payload["loading"].toBool()&&file!=payload["entityFile"].toString())throw std::runtime_error("Unexpected entity request path");if(payload["loading"].toBool()&&!fail)return QByteArray::fromBase64(payload["entityBytes"].toString().toLatin1());throw std::runtime_error("Fixed reader failure");});
        if(payload["loading"].toBool()) {
            for(const auto value:payload["operations"].toArray()) {
                const auto row=value.toObject();fail=row["fail"].toBool();const int before=reads;bool ok=true;QJsonValue entity(QJsonValue::Null);
                if(row["op"]=="load")try{entity=catalog.loadEntity(payload["entityId"].toString());}catch(const std::exception&){ok=false;}
                else if(row["op"]!="observe")throw std::runtime_error("Unknown loading operation");
                output.append(QJsonObject{{"id",row["id"]},{"ok",ok},{"readPerformed",row["op"]=="observe"?reads!=0:reads!=before},{"value",entity}});
            }
        } else
        for(const auto value:payload["cases"].toArray()) {
            const auto row=value.toObject();QJsonArray ids,versions;
            if(row["op"]=="version") {
                ids.append(row["entityId"]);const auto version=catalog.selectedVersionId(row["entityId"].toString(),row["date"].toString());
                versions.append(version.isEmpty()?QJsonValue::Null:QJsonValue(version));
            } else for(const auto group:catalog.search(row["query"].toString(),row["date"].toString()))
                for(const auto value:group.toObject()["entities"].toArray()) {
                    const auto entity=value.toObject();ids.append(entity["entityId"]);versions.append(entity["selectedVersionId"]);
                }
            output.append(QJsonObject{{"id",row["id"]},{"ids",ids},{"versions",versions},{"entityReads",reads}});
        }
    }catch(const std::exception& error){std::cerr<<error.what();return 4;}
    std::cout<<QJsonDocument(output).toJson(QJsonDocument::Compact).constData();
}

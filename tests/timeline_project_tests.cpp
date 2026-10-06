#include "projectcodec.h"
#include "webimport.h"
#include "losslessjson.h"
#include <pandoeditor/project.h>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>

static QJsonValue canonical(const QJsonValue& value) {
    if(value.isArray()) {
        std::vector<QJsonValue> rows;for(const auto& row:value.toArray())rows.push_back(canonical(row));
        if(!rows.empty()&&rows.front().isObject()&&rows.front().toObject().contains("id"))
            std::sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){const auto x=a.toObject(),y=b.toObject();return std::make_pair(x["id"].toString(),x["version"].toInt())<std::make_pair(y["id"].toString(),y["version"].toInt());});
        QJsonArray out;for(const auto& row:rows)out.append(row);return out;
    }
    if(value.isObject()){QJsonObject out;const auto object=value.toObject();for(auto it=object.begin();it!=object.end();++it)out[it.key()]=(it.key()=="validFrom"||it.key()=="validTo")&&it.value().isString()?QJsonValue(QString::fromStdString(pandoeditor::parseTemporal(it.value().toString().toStdString()).canonical)):canonical(it.value());return out;}
    return value;
}
static void sameMeaning(const QJsonObject& before,const QJsonObject& after) {
    for(const auto* key:{"territorialEntities","timelineRecords","geometries","sourceInfo","physicalSourceInfo","physicalSettings"})
        if(canonical(before[key])!=canonical(after[key]))throw std::runtime_error(std::string("exchange changed ")+key);
}
static QByteArray fileRoundTrip(const QByteArray& bytes,const QString& name) {
    QTemporaryDir dir;if(!dir.isValid())throw std::runtime_error("temporary directory");QFile file(dir.filePath(name));
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size())throw std::runtime_error("file write");file.close();
    if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("file reopen");return file.readAll();
}

int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    if(argc==3) {
        try {
        QFile input(QString::fromLocal8Bit(argv[1]));if(!input.open(QIODevice::ReadOnly))return 2;
        pandoeditor::Project project;project.replace(webimport::prepare(input.readAll()).document);
        pandoeditor::Project reopened;reopened.replace(projectcodec::decode(fileRoundTrip(projectcodec::encode(project.snapshot()),"native.pando.json")));
        QFile output(QString::fromLocal8Bit(argv[2]));const auto bytes=projectcodec::encodeWeb(reopened.snapshot());
        if(!output.open(QIODevice::WriteOnly)||output.write(bytes)!=bytes.size())return 2;return 0;
        } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    }
    QFile file(QStringLiteral(PANDOEDITOR_TIMELINE_PROJECT_FIXTURES));
    if(!file.open(QIODevice::ReadOnly))return 2;
    const auto cases=QJsonDocument::fromJson(file.readAll()).array();
    int failures=0;
    for(const auto& value:cases) {
        const auto row=value.toObject();const auto name=row.value("name").toString();
        const bool valid=row.value("expected").toString()=="OK";
        try {
            const auto source=QJsonDocument(row.value("project").toObject()).toJson(QJsonDocument::Compact);
            auto candidate=webimport::prepare(source);
            if(!valid)throw std::runtime_error("invalid project accepted");
            pandoeditor::Project project;project.replace(std::move(candidate.document));
            const auto encoded=projectcodec::encode(project.snapshot());
            const auto native=QJsonDocument::fromJson(encoded).object();
            if(native.value("version").toInt()!=10 || !native.value("timelineRecords").isObject())
                throw std::runtime_error("production native codec omitted timeline records/version 10");
            auto reopened=projectcodec::decode(fileRoundTrip(encoded,"native.pando.json"));
            pandoeditor::Project second;second.replace(std::move(reopened));
            if(projectcodec::encode(second.snapshot())!=encoded)
                throw std::runtime_error("native file round trip changed the project");
            const auto exchanged=fileRoundTrip(projectcodec::encodeWeb(second.snapshot()),"web.json");
            sameMeaning(row.value("project").toObject(),QJsonDocument::fromJson(exchanged).object());
            pandoeditor::Project third;third.replace(webimport::prepare(exchanged).document);
            sameMeaning(QJsonDocument::fromJson(exchanged).object(),QJsonDocument::fromJson(projectcodec::encodeWeb(third.snapshot())).object());
            std::cout << "PASS " << name.toStdString() << '\n';
        } catch(const std::exception& error) {
            if(valid || std::string(error.what())=="invalid project accepted") {
                ++failures;std::cerr << "FAIL " << name.toStdString() << ": " << error.what() << '\n';
            } else std::cout << "PASS rejection " << name.toStdString() << '\n';
        }
    }
    QFile content(QFileInfo(QStringLiteral(PANDOEDITOR_TIMELINE_PROJECT_FIXTURES)).dir().filePath("timeline-exchange/content.json"));
    if(!content.open(QIODevice::ReadOnly))return 2;
    const auto contentBytes=content.readAll();
    const auto numericFixture=webimport::prepare(contentBytes).document;
    // Opaque numeric payloads are source data at import, not untracked edits to a
    // decoded clean provenance ledger. Preserve the original lexical tokens.
    auto numericDocument=[&](const std::string& payload,bool source) {
        auto root=losslessjson::parse(contentBytes);const auto value=losslessjson::parse(QByteArray::fromStdString(payload));
        if(source)root.object.at("labels").array.front().object.at("source").object["details"]=value;
        else root.object.at("territorialEntities").array.front().object.at("properties").object["metadata"]=value;
        return webimport::prepare(root.encode()).document;
    };
    for(const auto* token:{"9007199254740993","1e400","0.123456789012345678901"})for(bool source:{false,true}) {
        const std::string payload=std::string("{\"nested\":[{\"number\":")+token+"}]}";
        auto document=numericDocument(payload,source);
        pandoeditor::Project project;project.replace(document);
        const auto bytes=projectcodec::encode(project);
        pandoeditor::Project reopened;reopened.replace(projectcodec::decode(fileRoundTrip(bytes,"precision.pando.json")));
        if(!bytes.contains(token)||projectcodec::encode(reopened)!=bytes){++failures;std::cerr<<"FAIL native numeric preservation\n";}
        bool refused=false;try{(void)projectcodec::encodeWeb(reopened.snapshot());}catch(const std::invalid_argument&){refused=true;}
        if(!refused){++failures;std::cerr<<"FAIL web numeric precision guard "<<token<<" source="<<source<<'\n';}
        if(projectcodec::encode(reopened)!=bytes){++failures;std::cerr<<"FAIL numeric rejection atomicity\n";}
    }
    for(const auto* token:{"0.1","1.2300e+02","9007199254740994"}) {
        auto document=numericDocument(std::string("{\"number\":")+token+"}",false);
        pandoeditor::Project project;project.replace(document);
        try{(void)projectcodec::encodeWeb(project.snapshot());}catch(const std::exception& e){++failures;std::cerr<<"FAIL representable numeric export "<<token<<": "<<e.what()<<'\n';}
    }
    for(bool payload:{false,true}) {
        auto document=numericFixture;auto& symbol=document.symbols.begin()->second;
        if(payload)symbol.defaultFlagDataUrl=symbol.embeddedDataUrl;else symbol.defaultCountryId="DEU";
        pandoeditor::Project project;project.replace(document);const auto bytes=projectcodec::encode(project);
        pandoeditor::Project reopened;reopened.replace(projectcodec::decode(fileRoundTrip(bytes,"flag-default.pando.json")));
        if(projectcodec::encode(reopened)!=bytes){++failures;std::cerr<<"FAIL native flag default preservation\n";}
        bool refused=false;try{(void)projectcodec::encodeWeb(reopened.snapshot());}catch(const std::invalid_argument&){refused=true;}
        if(!refused){++failures;std::cerr<<"FAIL web native flag default loss guard\n";}
    }
    std::cout << cases.size() << " storage cases + 9 numeric boundary cases + 2 flag default boundary cases, " << failures << " failures, 0 skipped\n";
    return failures?1:0;
}

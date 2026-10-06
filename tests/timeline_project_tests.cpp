#include "projectcodec.h"
#include "webimport.h"
#include "losslessjson.h"
#include "projectgeopackage.h"
#include <pandoeditor/project.h>
#include <pandoeditor/timeline-records.h>
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

static int intervalErrors() {
    QFile file(QStringLiteral(PANDOEDITOR_TIMELINE_PROJECT_FIXTURES));
    if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("interval fixture read");
    const auto web=QJsonDocument::fromJson(file.readAll()).array().first().toObject()["project"].toObject();
    pandoeditor::Project project;project.replace(projectcodec::decodeWeb(QJsonDocument(web).toJson()));
    const auto native=QJsonDocument::fromJson(projectcodec::encode(project)).object();
    int processed=0,failures=0;
    for(bool nativeFormat:{false,true})for(const auto* slot:{"lifetimes","geometryBindings","parentRelations"})
        for(const auto* endpoint:{"validFrom","validTo"})for(const auto* value:{"0000-02","1900-02-29"}) {
            auto input=nativeFormat?native:web;auto records=input["timelineRecords"].toObject();
            auto rows=records[slot].toArray();auto row=rows.first().toObject();row[endpoint]=value;rows[0]=row;
            records[slot]=rows;input["timelineRecords"]=records;++processed;
            try {
                const auto bytes=QJsonDocument(input).toJson();
                if(nativeFormat)(void)projectcodec::decode(bytes);else (void)projectcodec::decodeWeb(bytes);
                ++failures;std::cerr<<"FAIL interval accepted "<<slot<<' '<<endpoint<<' '<<value<<'\n';
            } catch(const pandoeditor::TimelineError& error) {
                if(error.code!="TIMELINE_INTERVAL"){++failures;std::cerr<<"FAIL interval category "<<error.what()<<'\n';}
            } catch(const std::exception& error) {
                ++failures;std::cerr<<"FAIL premature date parser "<<error.what()<<'\n';
            }
        }
    if(processed!=24)throw std::runtime_error("missing interval error cases");
    std::cout<<processed<<" native/web file interval cases, "<<failures<<" failures, 0 skipped\n";
    return failures;
}

// Expose each production file boundary for the independent fixed-web checker.
// No test normalization or alternate codec is used to create these files.
static int traceFile(const char* kind,const char* inputPath,const char* outputPath) {
    const auto directory=QString::fromLocal8Bit(outputPath);QString phase="app.read";bool ownsDirectory=false;
    const auto status=[&](bool complete,const QString& error={}) {
        if(!ownsDirectory)return;QFile file(QDir(directory).filePath("trace-status.json"));
        if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(QJsonObject{{"stage",phase},{"complete",complete},{"error",error}}).toJson());
    };
    try {
        if(QDir(directory).exists()||!QDir().mkpath(directory))throw std::runtime_error("trace directory must be fresh");
        ownsDirectory=true;
        const auto read=[](const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("trace read");return file.readAll();};
        const auto write=[&](const char* name,const QByteArray& bytes){QFile file(QDir(directory).filePath(name));if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size())throw std::runtime_error("trace write");};
        const auto bytes=read(QString::fromLocal8Bit(inputPath));
        pandoeditor::Project project;
        if(std::string(kind)=="web")project.replace(webimport::prepare(bytes).document);
        else if(std::string(kind)=="native")project.replace(projectcodec::decode(bytes));
        else if(std::string(kind)=="gpkg")project.replace(projectcodec::decode(pandoeditor::readProjectGeoPackage(QString::fromLocal8Bit(inputPath))));
        else throw std::runtime_error("trace kind must be web/native/gpkg");
        phase="app.activation";
        try{pandoeditor::requireStaticTimeline(project.document());write("activation.json","{\"result\":\"OK\"}");}
        catch(const pandoeditor::TimelineError& error){if(error.code!="TIMELINE_ACTIVATION")throw;write("activation.json","{\"result\":\"TIMELINE_ACTIVATION\"}");}
        phase="app.read.export";write("read.web.json",projectcodec::encodeWeb(project.snapshot()));
        phase="app.save.native-json";write("saved.native.json",projectcodec::encode(project.snapshot()));
        phase="app.reopen.native-json";
        pandoeditor::Project reopened;reopened.replace(projectcodec::decode(read(QDir(directory).filePath("saved.native.json"))));
        phase="app.reopened.export";write("reopened.web.json",projectcodec::encodeWeb(reopened.snapshot()));
        phase="app.save.native-gpkg";write("saved.native.gpkg",pandoeditor::exportProjectGeoPackage(project));
        phase="app.reopen.native-gpkg";
        pandoeditor::Project package;package.replace(projectcodec::decode(pandoeditor::readProjectGeoPackage(QDir(directory).filePath("saved.native.gpkg"))));
        phase="app.package-reopened.export";write("package-reopened.web.json",projectcodec::encodeWeb(package.snapshot()));
        phase="app.export";write("export.web.json",projectcodec::encodeWeb(reopened.snapshot()));status(true);
        std::cout<<"1 production file read/save/reopen/export trace completed\n";return 0;
    }catch(const std::exception& error){status(false,QString::fromUtf8(error.what()));std::cerr<<error.what()<<'\n';return 1;}
}

static int contentIntervalErrors() {
    QFile file(QFileInfo(QStringLiteral(PANDOEDITOR_TIMELINE_PROJECT_FIXTURES)).dir().filePath("timeline-exchange/content.json"));
    if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("content interval fixture read");
    const auto seed=QJsonDocument::fromJson(file.readAll()).object();int processed=0,failures=0;
    for(const auto* slot:{"distributionLayers","distributionEntries"})for(const auto* endpoint:{"validFrom","validTo"})for(const auto* value:{"","  "}) {
        auto input=seed;auto rows=input[slot].toArray();auto row=rows.first().toObject();row[endpoint]=value;rows[0]=row;input[slot]=rows;++processed;
        try{(void)projectcodec::decodeWeb(QJsonDocument(input).toJson());++failures;std::cerr<<"FAIL blank content endpoint accepted\n";}
        catch(const std::invalid_argument& error){if(std::string(error.what()).find("INVALID_DATE")!=0){++failures;std::cerr<<"FAIL content date category "<<error.what()<<'\n';}}
    }
    if(processed!=8)throw std::runtime_error("missing content interval cases");
    std::cout<<processed<<" existing content interval wire cases, "<<failures<<" failures, 0 skipped\n";return failures;
}

int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    if(argc==2&&std::string(argv[1])=="--interval-errors")return intervalErrors()?1:0;
    if(argc==2&&std::string(argv[1])=="--content-interval-errors")return contentIntervalErrors()?1:0;
    if(argc==5&&std::string(argv[1])=="--trace-file")return traceFile(argv[2],argv[3],argv[4]);
    if(argc==3) {
        try {
        QFile input(QString::fromLocal8Bit(argv[1]));if(!input.open(QIODevice::ReadOnly))return 2;
        pandoeditor::Project project;project.replace(webimport::prepare(input.readAll()).document);
        pandoeditor::Project reopened;reopened.replace(projectcodec::decode(fileRoundTrip(projectcodec::encode(project.snapshot()),"native.pando.json")));
        QFile output(QString::fromLocal8Bit(argv[2]));const auto bytes=projectcodec::encodeWeb(reopened.snapshot());
        if(!output.open(QIODevice::WriteOnly)||output.write(bytes)!=bytes.size())return 2;return 0;
        } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    }
    if(argc!=1){std::cerr<<"unsupported probe arguments\n";return 2;}
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
    failures+=intervalErrors();
    failures+=contentIntervalErrors();
    std::cout << cases.size() << " storage cases + 9 numeric boundary cases + 2 flag default boundary cases, " << failures << " failures, 0 skipped\n";
    return failures?1:0;
}

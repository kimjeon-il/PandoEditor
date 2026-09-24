#include "webimport.h"
#include "losslessjson.h"
#include "projectcodec.h"
#include <QtTest>
#include <QFile>
using namespace pandoeditor;
namespace {
QByteArray full(int schema=5, const char* format="pandolab-project-state") {
    return QString(R"({"format":"%1","schemaVersion":%2,"version":"0.33.0","landObjectModel":{"schemaVersion":2,"purpose":"lossless-fallback","directCreation":false,"sourceProvenanceSchemaVersion":1},"territorialModel":{"schemaVersion":2},"distributionModel":{"schemaVersion":2},"layerPresentation":{"schemaVersion":3},"countriesData":{"type":"FeatureCollection","features":[{"type":"Feature","id":"A","properties":{"name":"Alpha"},"geometry":{"type":"Polygon","coordinates":[[[0,0],[4,0],[4,4],[0,4],[0,0]]]}}]},"countryOverrides":{"A":{"name":"Renamed","notes":"notes","color":"#112233","flagDataUrl":null}},"territorialUnits":[],"territorialRelations":[]})").arg(format).arg(schema).toUtf8();
}
QString errorFor(const std::function<void()>& f) { try { f(); } catch(const std::exception& e) { return e.what(); } return {}; }
}
class WebImportTests : public QObject {
    Q_OBJECT
private slots:
    void formats() {
        QCOMPARE(webimport::classify(full()),webimport::FileKind::WebFull);
        QCOMPARE(webimport::classify(full(5,"pandolab-autosave-full")),webimport::FileKind::WebFull);
        QCOMPARE(webimport::classify(R"({"format":"pandoeditor-project","version":3})"),webimport::FileKind::QtProject);
    }
    void rejectAmbiguousOrUnsupported() {
        QVERIFY(errorFor([]{webimport::classify(full(5,"pandolab-autosave-delta"));}).startsWith("BASE_DATA_REQUIRED"));
        QVERIFY(errorFor([]{webimport::classify(full(6));}).startsWith("UNSUPPORTED_VERSION"));
        QVERIFY(errorFor([]{webimport::classify(full(2));}).startsWith("UNSUPPORTED_VERSION"));
        QVERIFY(errorFor([]{webimport::classify(full(5,"random"));}).startsWith("UNSUPPORTED_FORMAT"));
        QVERIFY(errorFor([]{webimport::classify(R"({"format":"pandolab-project-state","schemaVersion":5,"schemaVersion":4})");}).startsWith("DUPLICATE_KEY"));
    }
    void schema5IsNotRewritten() {
        auto b=full(); auto m=webimport::migrate(b);
        QCOMPARE(m.sourceSchema,5);
        QCOMPARE(m.normalized,losslessjson::parse(b).encode());
    }
    void originalJsGoldenMigration() {
        for(const char* version:{"v3","v4","v5","scalars"}) {
            QFile input(QStringLiteral(WEB_IMPORT_FIXTURES)+"/"+version+".input.json");
            QFile expected(QStringLiteral(WEB_IMPORT_FIXTURES)+"/"+version+".expected.json");
            QVERIFY(input.open(QIODevice::ReadOnly)); QVERIFY(expected.open(QIODevice::ReadOnly));
            QCOMPARE(webimport::migrate(input.readAll()).normalized,losslessjson::parse(expected.readAll()).encode());
        }
    }
    void allSupportedSchemasMapAndReopen() {
        for(const char* version:{"v3","v4","v5"}) {
            QFile input(QStringLiteral(WEB_IMPORT_FIXTURES)+"/"+version+".input.json");QVERIFY(input.open(QIODevice::ReadOnly));
            auto c=webimport::prepare(input.readAll());QCOMPARE(c.countries,2);
            Project p;p.replace(c.document);QVERIFY(p.renameCountry("A","after migration"));
            Project q;q.replace(projectcodec::decode(projectcodec::encode(p)));
            QVERIFY(semanticallyEqual(p.document(),q.document()));QCOMPARE(q.country("A")->name,std::string("after migration"));
        }
    }
    void sourceAndExpandedOutputRespectStorageLimit() {
        QVERIFY(errorFor([]{webimport::prepare(QByteArray(64ll*1024*1024+1,' '));}).contains("LIMIT_EXCEEDED"));
        auto b=full();b.chop(1);b+=",\"future\":\""+QByteArray(33ll*1024*1024,'x')+"\"}";
        auto error=errorFor([&]{webimport::prepare(b);});
        QVERIFY2(error.contains("LIMIT_EXCEEDED")&&error.contains("output"),qPrintable(error));
    }
    void mapsEffectiveCountryAndRoundtrip() {
        auto c=webimport::prepare(full()); QVERIFY2(!c.document.documentId.empty(),"prepared candidate must contain a validated document"); Project p; p.replace(c.document);
        QCOMPARE(c.countries,1); QCOMPARE(p.country("A")->name,std::string("Renamed"));
        QCOMPARE(p.country("A")->memo,std::string("notes")); QCOMPARE(p.country("A")->color,0x112233u);
        auto saved=projectcodec::encode(p); Project q; q.replace(projectcodec::decode(saved));
        QVERIFY(semanticallyEqual(p.document(),q.document()));
        QVERIFY(!c.candidateHash.isEmpty()); QVERIFY(!c.report.isEmpty());
    }
    void rejectsBrokenReferencesAndCoordinates() {
        auto b=full(); b.replace("[4,4]","[181,4]");
        QVERIFY(errorFor([&]{webimport::prepare(b);}).contains("INVALID_GEOMETRY"));
        b=full(); b.replace("\"territorialUnits\":[]",R"("territorialUnits":[{"type":"Feature","id":"00000000-0000-4000-8000-000000000011","properties":{"schemaVersion":2,"coverageMode":"explicit","unitType":"subunit","name":"Sub","parentId":"missing","sovereignId":"A"},"geometry":{"type":"Polygon","coordinates":[[[0,0],[1,0],[1,1],[0,1],[0,0]]]}}])");
        QVERIFY(errorFor([&]{webimport::prepare(b);}).contains("DANGLING_REF"));
    }
    void losslessUnknownAndGuard() {
        auto b=full(); b.chop(1); b+=R"(,"future":{"large":900719925474099312345,"null":null,"list":[1,null,"x"]}})";
        auto c=webimport::prepare(b); QVERIFY(!c.document.documentId.empty()); Project p; p.replace(c.document);
        QVERIFY(p.renameCountry("A","safe")); QVERIFY(!p.setColor("A",0));
        auto output=projectcodec::encode(p);
        QVERIFY(output.contains("900719925474099312345"));
        Project q; q.replace(projectcodec::decode(output));
        QCOMPARE(q.country("A")->name,std::string("safe"));
        QVERIFY(semanticallyEqual(p.document(),q.document()));
    }
    void flagsAndRootClassification() {
        for(const auto& mode:{QByteArray("null"),QByteArray("\"data:image/png;base64,AAAA\"")}) {
            auto b=full(); b.replace("\"flagDataUrl\":null", "\"flagDataUrl\":"+mode);
            auto c=webimport::prepare(b); bool found=false;
            for(const auto& row:c.report) if(row.toMap().value("flagState")== (mode=="null"?"None":"Embedded"))found=true;
            QVERIFY(found);
        }
        auto b=full();b.replace(",\"flagDataUrl\":null","");auto c=webimport::prepare(b);bool defaultFlag=false;
        for(const auto& row:c.report)if(row.toMap().value("flagState")=="Default")defaultFlag=true;
        QVERIFY(defaultFlag);
        auto root=losslessjson::parse(b);
        for(const auto& [key,value]:root.object) {
            bool present=false;for(const auto& r:c.report)if(r.toMap().value("path").toString()==QString::fromStdString("/"+key))present=true;
            QVERIFY2(present,key.c_str());
        }
    }
    void nestedAndRetained() {
        QFile input(QStringLiteral(WEB_IMPORT_FIXTURES)+"/v5.input.json");QVERIFY(input.open(QIODevice::ReadOnly));
        auto c=webimport::prepare(input.readAll());QCOMPARE(c.countries,2);QCOMPARE(c.subunits,3);QCOMPARE(c.regions,1);
        Project p;p.replace(c.document);QCOMPARE(p.document().relations.size(),std::size_t(5));
        bool generic=false;for(const auto& e:p.document().extensions)if(e.jsonPointer=="/genericFeatures")generic=true;
        QVERIFY(generic);QVERIFY(p.renameCountry("A","edited"));auto bytes=projectcodec::encode(p);
        Project q;q.replace(projectcodec::decode(bytes));QVERIFY(semanticallyEqual(p.document(),q.document()));
    }
    void malformedKnownContainersAreRejected() {
        for(const char* field:{"countryOverrides","layerVisibility","itemVisibility","labelSettings","distributionSettings"}) {
            auto doc=losslessjson::parse(full());doc.object[field]=losslessjson::Value::num(42);
            QVERIFY2(errorFor([&]{webimport::prepare(doc.encode());}).contains("INVALID"),field);
        }
        auto b=full();b.replace("\"name\":\"Alpha\"","\"name\":\"\"");
        QVERIFY(errorFor([&]{webimport::prepare(b);}).contains("INVALID_UNIT"));
        b=full();b.replace("\"notes\":\"notes\"","\"notes\":null");
        QVERIFY(errorFor([&]{webimport::prepare(b);}).contains("INVALID_OVERRIDE"));
    }
    void unknownModelFieldsReceivePreservationBarrier() {
        auto doc=losslessjson::parse(full());doc.object["landObjectModel"].object["futureDependency"]=losslessjson::parse(R"({"unit":"A","number":90071992547409931234})");
        auto c=webimport::prepare(doc.encode());bool found=false;
        for(const auto& e:c.document.extensions)if(e.jsonPointer=="/landObjectModel/futureDependency" && e.status=="unsupported" && e.dependencyKnowledge=="unknown")found=true;
        QVERIFY(found);
    }
    void distributionIndependentSharesAndReferences() {
        auto doc=losslessjson::parse(full());
        doc.object["distributionLayers"]=losslessjson::parse(R"([{"id":"00000000-0000-4000-8000-000000000011","schemaVersion":2,"type":"religion","name":"R"}])");
        doc.object["distributionEntries"]=losslessjson::parse(R"([{"id":"00000000-0000-4000-8000-000000000012","schemaVersion":2,"layerId":"00000000-0000-4000-8000-000000000011","mode":"territorial","territorialUnitId":"A","share":60},{"id":"00000000-0000-4000-8000-000000000013","schemaVersion":2,"layerId":"00000000-0000-4000-8000-000000000011","mode":"territorial","territorialUnitId":"A","share":70}])");
        auto c=webimport::prepare(doc.encode());bool archived=false;
        for(const auto& e:c.document.extensions)if(e.jsonPointer=="/distributionEntries") {
            QCOMPARE(e.payload,doc.object["distributionEntries"].encode().toStdString());
            archived=e.status=="migrationArchive";
        }
        QVERIFY(archived);
        QCOMPARE(c.document.distributionEntries.size(),std::size_t(2));
        for(const auto& entry:c.document.distributionEntries) {
            QVERIFY(entry.territory.has_value());
            QCOMPARE(*entry.territory,territorialRef("A"));
        }
        QCOMPARE(c.document.distributionEntries[0].share,60.);
        QCOMPARE(c.document.distributionEntries[1].share,70.);
        doc.object["distributionEntries"].array[0].object["territorialUnitId"]=losslessjson::Value::str("missing");
        QVERIFY(errorFor([&]{webimport::prepare(doc.encode());}).contains("DANGLING_REF"));
    }
    void escapedDuplicatesDepthAndExtraOrdinates() {
        QVERIFY(errorFor([]{webimport::classify(R"({"format":"pandolab-project-state","schemaVersion":5,"\u0073chemaVersion":4})");}).contains("DUPLICATE_KEY"));
        auto doc=full();doc.chop(1);doc+=",\"future\":"+QByteArray(130,'[')+"0"+QByteArray(130,']')+"}";
        QVERIFY(errorFor([&]{webimport::prepare(doc);}).contains("LIMIT_EXCEEDED"));
        auto b=full();b.replace("[4,4]","[4,4,123]");
        QVERIFY(errorFor([&]{webimport::prepare(b);}).contains("UNSUPPORTED_GEOMETRY"));
    }
    void holesAndMultiPolygonCoordinatesAreUnchanged() {
        auto doc=losslessjson::parse(full());
        auto geometry=losslessjson::parse(R"({"type":"MultiPolygon","coordinates":[[[[0,0],[10,0],[10,10],[0,10],[0,0]],[[2,2],[3,2],[3,3],[2,3],[2,2]]],[[[20,0],[21,0],[21,1],[20,1],[20,0]]]]})");
        doc.object["countriesData"].object["features"].array[0].object["geometry"]=geometry;
        auto c=webimport::prepare(doc.encode());Project p;p.replace(c.document);auto g=p.document().geometries.get(p.document().units[0].geometry);
        QCOMPARE(g->type,std::string("MultiPolygon"));QCOMPARE(g->polygons.size(),std::size_t(2));QCOMPARE(g->polygons[0].size(),std::size_t(2));
        QCOMPARE(g->polygons[0][1][2].x,3.);QCOMPARE(g->polygons[1][0][2].y,1.);
        QVERIFY(p.pick({2.5,2.5}).empty());QCOMPARE(p.pick({20.5,0.5}),std::string("A"));
        auto old=g.get();QVERIFY(p.renameCountry("A","safe geometry"));
        QCOMPARE(p.document().geometries.get(p.document().units[0].geometry).get(),old);
        Project q;q.replace(projectcodec::decode(projectcodec::encode(p)));QVERIFY(semanticallyEqual(p.document(),q.document()));
    }
    void invalidRetainedSourceContractIsRejected() {
        QFile f(QStringLiteral(WEB_IMPORT_FIXTURES)+"/v5.input.json");QVERIFY(f.open(QIODevice::ReadOnly));auto doc=losslessjson::parse(f.readAll());
        QVERIFY(!doc.object["genericFeatures"].array.empty());
        doc.object["genericFeatures"].array[0].object["properties"].object["source"].object["schemaVersion"]=losslessjson::Value::num(2);
        QVERIFY(errorFor([&]{webimport::prepare(doc.encode());}).contains("INVALID_SOURCE"));
    }
    void cancelled() {
        QVERIFY(errorFor([]{webimport::prepare(full(),[]{return true;});}).startsWith("CANCELLED"));
    }
};
QTEST_GUILESS_MAIN(WebImportTests)
#include "web_import_tests.moc"

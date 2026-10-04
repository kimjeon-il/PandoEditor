#include "riverpartitioncalculator.h"
#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJSEngine>
#include <atomic>
#include <limits>
#include <future>
using namespace pandoeditor;
class RiverPartitionTests : public QObject {
    Q_OBJECT
private slots:
    void originalCrossingAndComposition() {
        const auto result=calculateRiverPartitionsJson(R"({"request":{"donors":[{"countryId":"square","geometryRevision":1,"geometry":{"type":"Polygon","coordinates":[[[0,0],[1,0],[1,1],[0,1],[0,0]]]}}],"riverFeatures":[{"type":"Feature","id":"vertical","properties":{"pandolab_id":"vertical"},"geometry":{"type":"LineString","coordinates":[[0.5,-1],[0.5,2]]}}]},"components":[{"key":"original","countryId":"square","polygonIndex":0,"sourcePolygonIndex":7,"geometry":{"type":"Polygon","coordinates":[[[0,0],[1,0],[1,1],[0,1],[0,0]]]}}]})");
        QVERIFY2(result.succeeded(),qPrintable(result.detail));QCOMPARE(result.candidates.size(),std::size_t(2));QCOMPARE(result.components.size(),std::size_t(2));
        QCOMPARE(result.components[0].sourcePolygonIndex,7);QCOMPARE(result.donors[0].status,RiverDonorStatus::Ready);QVERIFY(result.inputUnchanged);QVERIFY(result.compositionInputUnchanged);
    }
    void typedValuesAndLiveJsSignatures() {
        RiverPartitionRequest request;
        RiverPartitionDonor donor;donor.countryId="typed";donor.geometry.type="Polygon";
        donor.geometry.polygons={{{{0,0},{1,0},{1,1},{0,1},{0,0}}}};
        donor.revisionMode=RiverRevisionMode::LiveCoordinates;request.donors.push_back(donor);
        HydroRiverFeatureValue river;river.id="physical";river.pandolabId="vertical";river.logicalFid=0;
        river.geometry.type="LineString";river.geometry.lines={{{.5,-1},{.5,2}}};request.riverFeatures.push_back(river);
        RiverBaseComponent base;base.key="original";base.countryId="typed";base.sourcePolygonIndex=17;base.geometry=donor.geometry;base.attributes={{"custom", "preserve"}};request.components.push_back(base);
        EditedRiverValue edit;edit.id="z";edit.geometry.type="LineString";edit.geometry.lines={{{1e-7,1e-6},{1e20,1e21}}};request.signatureEdits.push_back(edit);
        edit.id="a";request.signatureEdits.push_back(edit);request.hydroRevision=123;
        const auto result=std::async(std::launch::async,[request]{return calculateRiverPartitions(request);}).get();
        QVERIFY2(result.succeeded(),qPrintable(result.detail));QCOMPARE(result.candidates.size(),std::size_t(2));
        QCOMPARE(result.donorRevisionStrings,QStringList{"typed:[[[0,0],[1,0],[1,1],[0,1],[0,0]]]"});
        QCOMPARE(result.editedRiverSignature,QString("a:[[1e-7,0.000001],[100000000000000000000,1e+21]]|z:[[1e-7,0.000001],[100000000000000000000,1e+21]]"));
        QCOMPARE(result.diagnostics.hydroRevision,QJsonValue(123));QCOMPARE(result.components[0].sourcePolygonIndex,17);
        QCOMPARE(result.candidates[0].geometry.type,std::string("Polygon"));QCOMPARE(result.candidates[0].geometry.polygons.size(),std::size_t(1));QCOMPARE(result.candidates[0].geometry.polygons[0][0].size(),std::size_t(5));
        QVERIFY(result.components[0].river.has_value());QCOMPARE(result.candidates[0].sourceRiverIds,QStringList{"vertical"});
        QCOMPARE(request.donors[0].geometry.polygons[0][0][0].x,0.0);QVERIFY(request.donors[0].geometryRevision.isUndefined());
        request.riverFeatures.clear();const auto original=calculateRiverPartitions(request);QVERIFY(original.succeeded());
        QCOMPARE(original.components.size(),std::size_t(1));QVERIFY(!original.components[0].isRiver);QVERIFY(!original.components[0].river);
        QCOMPARE(original.components[0].key,QString("original"));QCOMPARE(original.components[0].attributes["custom"].toString(),QString("preserve"));
    }
    void exactSyntheticBehavior() {
        QFile file(QStringLiteral(M972_RIVER_FIXTURE)+"/synthetic-cases.json");QVERIFY(file.open(QIODevice::ReadOnly));
        const auto cases=QJsonDocument::fromJson(file.readAll()).array();QCOMPARE(cases.size(),18);
        QJsonObject byName;
        for(const auto& entry:cases){const auto row=entry.toObject();const auto result=calculateRiverPartitionsJson(QJsonDocument(row).toJson(QJsonDocument::Compact));
            QVERIFY2(result.succeeded(),qPrintable(row["name"].toString()+": "+result.detail));
            QVERIFY(result.inputUnchanged);QVERIFY(result.compositionInputUnchanged);
            if(!row["expectedCount"].isNull())QCOMPARE(result.candidates.size(),std::size_t(row["expectedCount"].toInt()));
            auto observation=QJsonDocument::fromJson(result.json).object();auto raw=observation["result"].toObject();auto diag=raw["diagnostics"].toObject();diag.remove("computeMs");raw["diagnostics"]=diag;observation["result"]=raw;byName[row["name"].toString()]=observation;
            if(row["name"]=="donor-wide-invalidation"){QCOMPARE(result.donors[0].status,RiverDonorStatus::Invalid);QCOMPARE(result.diagnostics.tracedFaceCount,4);QCOMPARE(result.components.size(),std::size_t(1));QCOMPARE(result.components[0].countryId,QString("unsplit"));}
            if(row["name"]=="multipolygon-retains-original"){QCOMPARE(result.components.size(),std::size_t(3));QCOMPARE(result.components[2].sourcePolygonIndex,9);QVERIFY(!result.components[2].isRiver);QVERIFY(!result.components[2].attributes.contains("sourceRiverIds"));}
            if(row["name"]=="branch-with-dangling"){QVERIFY(result.diagnostics.prunedRiverEdges>0);for(const auto& c:result.candidates)QVERIFY(!c.sourceRiverIds.contains("dangling"));}
        }
        QCOMPARE(byName["crossing-pair"],byName["crossing-pair-reversed"]);
    }
    void platformAdaptersAreBoundedAndPrivate() {
        QJSEngine engine;const auto previousCos=engine.evaluate("Math.cos(0.7719766168394622)").toNumber();
        engine.globalObject().setProperty("globalThis",engine.globalObject());
        QFile platform(QStringLiteral(M972_RIVER_ASSETS)+"/platform.js");QVERIFY(platform.open(QIODevice::ReadOnly));
        const auto loaded=engine.evaluate(QString::fromUtf8(platform.readAll()));QVERIFY2(!loaded.isError(),qPrintable(loaded.toString()));
        QFile checks(QStringLiteral(M972_RIVER_FIXTURE)+"/platform-tests.js");QVERIFY(checks.open(QIODevice::ReadOnly));
        const auto checked=engine.evaluate(QString::fromUtf8(checks.readAll()));QVERIFY2(!checked.isError(),qPrintable(checked.toString()));
        QCOMPARE(engine.evaluate("Object.keys("+engine.globalObject().property("JSON").property("stringify").call({checked}).toString()+").length").toInt(),24);
        const auto result=calculateRiverPartitionsJson(R"({"request":{},"components":[]})");QVERIFY(result.succeeded());
        QCOMPARE(engine.evaluate("Math.cos(0.7719766168394622)").toNumber(),previousCos);
        QJSEngine untouched;QCOMPARE(untouched.evaluate("typeof structuredClone").toString(),QString("undefined"));
        QCOMPARE(untouched.evaluate("Math.cos(0.7719766168394622)").toNumber(),previousCos);
    }
    void errorsAndNonfiniteInputsFailClosed() {
        const auto error=calculateRiverPartitionsJson(R"({"request":{},"omitClipper":true})");
        QCOMPARE(error.status,RiverPartitionStatus::Failed);QVERIFY(error.detail.contains("polygon-clipping"));QVERIFY(error.candidates.empty());
        const auto invalid=calculateRiverPartitionsJson("not json");QCOMPARE(invalid.status,RiverPartitionStatus::Failed);
        const auto nonfinite=calculateRiverPartitionsJson(R"({"request":{"hydroRevision":1e999}})");QCOMPARE(nonfinite.status,RiverPartitionStatus::Failed);
        RiverPartitionRequest typed;RiverPartitionDonor donor;donor.countryId="bad";donor.geometry.type="Polygon";donor.geometry.polygons={{{{std::numeric_limits<double>::infinity(),0}}}};typed.donors.push_back(donor);
        QCOMPARE(calculateRiverPartitions(typed).status,RiverPartitionStatus::Failed);
        int checks=0;const auto cancelled=calculateRiverPartitionsJson(R"({"request":{},"omitClipper":true})",[&]{return ++checks>=5;});
        QCOMPARE(cancelled.status,RiverPartitionStatus::Cancelled);QVERIFY(cancelled.detail.isEmpty());QVERIFY(cancelled.json.isEmpty());
    }
    void wrappedOutputParseFailureDiscardsResult() {
        const auto nested=[](int depth){return QByteArray("{\"request\":{\"hydroRevision\":")+QByteArray(depth,'[')+'0'+QByteArray(depth,']')+"},\"components\":[]}";};
        const auto supported=calculateRiverPartitionsJson(nested(1020));
        QVERIFY2(supported.succeeded(),qPrintable(supported.detail));QVERIFY(supported.diagnostics.hydroRevision.isArray());
        // The finite acyclic depth-1022 input is accepted by QJSEngine. Wrapping
        // its revision under result.diagnostics exceeds QJsonDocument's limit.
        int checksUntilReturn=0;
        const auto result=calculateRiverPartitionsJson(nested(1022),[&]{++checksUntilReturn;return false;});
        QCOMPARE(result.status,RiverPartitionStatus::Failed);
        QVERIFY(result.detail.startsWith("RIVER_RESULT_JSON_PARSE_ERROR"));
        QVERIFY(result.candidates.empty());QVERIFY(result.components.empty());QVERIFY(result.donors.empty());
        QVERIFY(result.invalidDonorIds.empty());QVERIFY(result.donorRevisionStrings.empty());QVERIFY(result.editedRiverSignature.isEmpty());
        QVERIFY(result.json.isEmpty());QVERIFY(!result.inputUnchanged);QVERIFY(!result.compositionInputUnchanged);
        QCOMPARE(result.diagnostics.candidateCount,0);QVERIFY(result.diagnostics.algorithmRevision.isEmpty());
        // Turn cancellation on at the failing call's final cancellation query.
        // It must override the output-parser exception and retain no error/data.
        int checks=0;
        const auto cancelled=calculateRiverPartitionsJson(nested(1022),[&]{return ++checks>=checksUntilReturn;});
        QCOMPARE(cancelled.status,RiverPartitionStatus::Cancelled);QVERIFY(cancelled.detail.isEmpty());
        QVERIFY(cancelled.candidates.empty());QVERIFY(cancelled.components.empty());QVERIFY(cancelled.donors.empty());QVERIFY(cancelled.json.isEmpty());
    }
    void cancellationWinsOverErrors() {
        const auto result=calculateRiverPartitionsJson("not json",[]{return true;});
        QCOMPARE(result.status,RiverPartitionStatus::Cancelled);QVERIFY(result.candidates.empty());QVERIFY(result.json.isEmpty());
    }
};
QTEST_GUILESS_MAIN(RiverPartitionTests)
#include "river_partition_tests.moc"

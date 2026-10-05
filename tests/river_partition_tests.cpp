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
namespace {
Geometry decoded(const QJsonObject& row) {
    Geometry value;value.type=row["type"].toString().toStdString();auto polygons=row["coordinates"].toArray();
    if(value.type=="Polygon")polygons=QJsonArray{polygons};
    for(const auto& p:polygons){Polygon polygon;for(const auto& r:p.toArray()){Ring ring;
        for(const auto& xy:r.toArray()){auto point=xy.toArray();ring.push_back({point[0].toDouble(),point[1].toDouble()});}
        polygon.push_back(std::move(ring));}value.polygons.push_back(std::move(polygon));}return value;
}
std::vector<RiverBaseComponent> bases(const QJsonArray& rows) {
    std::vector<RiverBaseComponent> result;
    for(const auto& value:rows){const auto row=value.toObject();RiverBaseComponent base;
        base.key=row["key"].toString();base.countryId=row["countryId"].toString();base.componentKey=row["componentKey"].toString();
        base.polygonIndex=row["polygonIndex"].toInt();base.sourcePolygonIndex=row["sourcePolygonIndex"].toInt(base.polygonIndex);
        base.geometry=decoded(row["geometry"].toObject());base.attributes=row;result.push_back(std::move(base));}return result;
}
}
class RiverPartitionTests : public QObject {
    Q_OBJECT
private slots:
    void independentPresentationMatchesEverySyntheticKernelBoundary() {
        QFile file(QStringLiteral(M972_RIVER_FIXTURE)+"/synthetic-cases.json");QVERIFY(file.open(QIODevice::ReadOnly));
        for(const auto& value:QJsonDocument::fromJson(file.readAll()).array()) {
            const auto row=value.toObject();const auto raw=calculateRiverPartitionsJson(QJsonDocument(row).toJson(QJsonDocument::Compact));
            QVERIFY2(raw.succeeded(),qPrintable(raw.detail));
            const auto normalized=normalizeRiverPartitionCandidates(raw.candidates);
            QVERIFY2(normalized.succeeded(),qPrintable(normalized.detail));
            const auto expected=QJsonDocument::fromJson(raw.presentationJson).object();
            QCOMPARE(QJsonDocument::fromJson(normalized.json).array(),expected["candidates"].toArray());
            const auto composed=composeRiverPartitionComponents(bases(row["components"].toArray()),normalized.candidates,raw.donors);
            QVERIFY2(composed.succeeded(),qPrintable(composed.detail));
            QCOMPARE(QJsonDocument::fromJson(composed.json).object(),expected["composed"].toObject());
        }
    }
    void nullNormalizationPrecedesFallbackAndCacheRecomposition() {
        auto base=bases(QJsonDocument::fromJson(R"([{"key":"base","countryId":"d","polygonIndex":0,"sourcePolygonIndex":7,"geometry":{"type":"Polygon","coordinates":[[[0,0],[1,0],[1,1],[0,1],[0,0]]]},"custom":"untouched"}])").array());
        RiverPartitionCell microscopic;microscopic.key="raw-identity";microscopic.donorCountryId="d";microscopic.componentKey="d:0";
        microscopic.geometry.type="Polygon";microscopic.geometry.polygons={{{{0,0},{1e-8,0},{1e-8,1e-8},{0,1e-8},{0,0}}}};
        microscopic.area=123;microscopic.areaM2=456;microscopic.algorithmRevision="raw-revision";
        microscopic.sourceRiverIds={"z","a"};microscopic.riverBoundarySegments={{{{1,1},{0,0}}}};
        const auto filtered=normalizeRiverPartitionCandidates({microscopic});QVERIFY(filtered.succeeded());QVERIFY(filtered.candidates.empty());
        const auto fallback=composeRiverPartitionComponents(base,filtered.candidates,{});QVERIFY(fallback.succeeded());
        QCOMPARE(fallback.components.size(),std::size_t(1));QVERIFY(!fallback.components[0].isRiver);
        QCOMPARE(fallback.components[0].key,QString("base"));QCOMPARE(fallback.components[0].sourcePolygonIndex,7);
        QCOMPARE(fallback.components[0].attributes["custom"].toString(),QString("untouched"));
        QCOMPARE(fallback.components[0].geometry.polygons[0][0][0].y,0.);QVERIFY(!fallback.components[0].attributes.contains("sourceRiverIds"));
        microscopic.geometry=base[0].geometry;microscopic.attributes={{"futureProvenance","retained"}};
        auto missingDonor=microscopic;missingDonor.donorCountryId.clear();
        const auto normalized=normalizeRiverPartitionCandidates({microscopic,missingDonor});QVERIFY(normalized.succeeded());QCOMPARE(normalized.candidates.size(),std::size_t(1));
        const auto& cell=normalized.candidates[0];QCOMPARE(cell.key,microscopic.key);QCOMPARE(cell.area,123.);QCOMPARE(cell.areaM2,456.);
        QCOMPARE(cell.sourceRiverIds,microscopic.sourceRiverIds);QCOMPARE(cell.algorithmRevision,microscopic.algorithmRevision);
        QCOMPARE(cell.riverBoundarySegments[0][0].x,1.);QCOMPARE(cell.riverBoundarySegments[0][1].x,0.);
        QCOMPARE(cell.attributes["futureProvenance"].toString(),QString("retained"));
        auto untouched=base[0];untouched.countryId="untouched";untouched.key="current-first";
        base[0].sourcePolygonIndex=19;base.insert(base.begin(),untouched);
        const auto cacheHit=composeRiverPartitionComponents(base,normalized.candidates,{});QVERIFY(cacheHit.succeeded());
        QCOMPARE(cacheHit.components[0].key,QString("current-first"));QCOMPARE(cacheHit.components[1].sourcePolygonIndex,19);
        QCOMPARE(cacheHit.components[1].key,QString("raw-identity"));QCOMPARE(cacheHit.riverCandidateCount,1);QCOMPARE(cacheHit.splitComponentCount,1);
        QCOMPARE(cacheHit.components[1].attributes["futureProvenance"].toString(),QString("retained"));
        RiverPartitionDonorResult invalid;invalid.donorCountryId="d";invalid.status=RiverDonorStatus::Invalid;
        const auto suppressed=composeRiverPartitionComponents(base,normalized.candidates,{invalid,invalid});QVERIFY(suppressed.succeeded());
        QCOMPARE(suppressed.components.size(),std::size_t(1));QCOMPARE(suppressed.invalidDonorIds,QStringList{"d"});
        QCOMPARE(suppressed.riverCandidateCount,0);QCOMPARE(suppressed.splitComponentCount,0);
    }
    void pinnedNormalizerRetainsExactRingSemanticsAndNull() {
        Geometry input;input.type="Polygon";input.polygons={{{{0,0},{0.5,0},{0.25,0},{1,0},{1,1},{1,1+5e-11},{0,1},{0,5e-11}},
            {{0.1,0.1},{0.1+1e-8,0.1},{0.1+1e-8,0.1+1e-8},{0.1,0.1+1e-8},{0.1,0.1}}}};
        const auto result=normalizeRiverGeometry(input);QVERIFY2(result.succeeded(),qPrintable(result.detail));QVERIFY(result.geometry);
        QCOMPARE(result.geometry->type,std::string("Polygon"));QCOMPARE(result.geometry->polygons[0].size(),std::size_t(1));
        const auto& ring=result.geometry->polygons[0][0];QCOMPARE(ring.size(),std::size_t(6));
        QCOMPARE(ring[0].x,0.);QCOMPARE(ring[0].y,1.);QCOMPARE(ring[1].x,1.);QCOMPARE(ring[1].y,1.);
        QCOMPARE(ring[2].x,1.);QCOMPARE(ring[2].y,0.);
        // Upstream removes backtracks, but preserves forward collinear points.
        QCOMPARE(ring[3].x,0.25);QCOMPARE(ring[3].y,0.);QCOMPARE(ring[4].x,0.);QCOMPARE(ring[4].y,0.);
        Geometry tiny;tiny.type="Polygon";tiny.polygons={{{{0,0},{1e-8,0},{1e-8,1e-8},{0,0}}}};
        const auto none=normalizeRiverGeometry(tiny);QVERIFY(none.succeeded());QVERIFY(!none.geometry);
        QCOMPARE(input.polygons[0][0].size(),std::size_t(8));
        const auto cancelled=normalizeRiverGeometry(input,[]{return true;});QCOMPARE(cancelled.status,RiverPartitionStatus::Cancelled);QVERIFY(!cancelled.geometry);
        input.polygons[0][0][0].x=std::numeric_limits<double>::infinity();
        const auto bad=normalizeRiverGeometry(input);QCOMPARE(bad.status,RiverPartitionStatus::Failed);QVERIFY(!bad.geometry);
        QCOMPARE(normalizeRiverPartitionCandidates({},[]{return true;}).status,RiverPartitionStatus::Cancelled);
        QCOMPARE(composeRiverPartitionComponents({}, {}, {},[]{return true;}).status,RiverPartitionStatus::Cancelled);
    }
    void latePresentationCancellationDiscardsAllOwnedValues() {
        auto base=bases(QJsonDocument::fromJson(R"([{"key":"base","countryId":"d","polygonIndex":0,"sourcePolygonIndex":7,"geometry":{"type":"Polygon","coordinates":[[[0,0],[1,0],[1,1],[0,1],[0,0]]]}}])").array());
        RiverPartitionCell cell;cell.key="cell";cell.donorCountryId="d";cell.componentKey="d:0";cell.geometry=base[0].geometry;
        int completedChecks=0;const auto normalized=normalizeRiverPartitionCandidates({cell},[&]{++completedChecks;return false;});QVERIFY(normalized.succeeded());
        int cancelledChecks=0;const auto cancelled=normalizeRiverPartitionCandidates({cell},[&]{return ++cancelledChecks>=completedChecks;});
        QCOMPARE(cancelled.status,RiverPartitionStatus::Cancelled);QVERIFY(cancelled.candidates.empty());QVERIFY(cancelled.json.isEmpty());QVERIFY(cancelled.detail.isEmpty());
        completedChecks=0;const auto composed=composeRiverPartitionComponents(base,normalized.candidates,{},[&]{++completedChecks;return false;});QVERIFY(composed.succeeded());
        cancelledChecks=0;const auto stopped=composeRiverPartitionComponents(base,normalized.candidates,{},[&]{return ++cancelledChecks>=completedChecks;});
        QCOMPARE(stopped.status,RiverPartitionStatus::Cancelled);QVERIFY(stopped.components.empty());QVERIFY(stopped.json.isEmpty());QVERIFY(stopped.detail.isEmpty());
        auto invalid=cell;invalid.area=std::numeric_limits<double>::infinity();
        const auto failed=normalizeRiverPartitionCandidates({cell,invalid});QCOMPARE(failed.status,RiverPartitionStatus::Failed);
        QVERIFY(failed.candidates.empty());QVERIFY(failed.json.isEmpty());QVERIFY(!failed.detail.isEmpty());
    }
    void originalCrossingAndComposition() {
        const auto result=calculateRiverPartitionsJson(R"({"request":{"donors":[{"countryId":"square","geometryRevision":1,"geometry":{"type":"Polygon","coordinates":[[[0,0],[1,0],[1,1],[0,1],[0,0]]]}}],"riverFeatures":[{"type":"Feature","id":"vertical","properties":{"pandolab_id":"vertical"},"geometry":{"type":"LineString","coordinates":[[0.5,-1],[0.5,2]]}}]},"components":[{"key":"original","countryId":"square","polygonIndex":0,"sourcePolygonIndex":7,"geometry":{"type":"Polygon","coordinates":[[[0,0],[1,0],[1,1],[0,1],[0,0]]]}}]})");
        QVERIFY2(result.succeeded(),qPrintable(result.detail));QCOMPARE(result.candidates.size(),std::size_t(2));QCOMPARE(result.components.size(),std::size_t(2));
        QCOMPARE(result.components[0].sourcePolygonIndex,7);QCOMPARE(result.donors[0].status,RiverDonorStatus::Ready);QVERIFY(result.inputUnchanged);QVERIFY(result.compositionInputUnchanged);
    }
    void presentationRunsAfterRawIdentity() {
        QFile file(QStringLiteral(M972_RIVER_FIXTURE)+"/synthetic-cases.json");QVERIFY(file.open(QIODevice::ReadOnly));
        const auto row=QJsonDocument::fromJson(file.readAll()).array()[0].toObject();
        const auto result=calculateRiverPartitionsJson(QJsonDocument(row).toJson(QJsonDocument::Compact));
        QVERIFY2(result.succeeded(),qPrintable(result.detail));QCOMPARE(result.presentationCandidates.size(),result.candidates.size());
        QCOMPARE(result.presentationCandidates[0].key,result.candidates[0].key);
        QCOMPARE(result.presentationCandidates[0].sourceRiverIds,result.candidates[0].sourceRiverIds);
        QVERIFY(result.presentationCandidates[0].geometry.polygons[0][0][0].y!=result.candidates[0].geometry.polygons[0][0][0].y);
        QCOMPARE(result.presentationComponents[0].sourcePolygonIndex,7);QVERIFY(!result.presentationJson.isEmpty());QVERIFY(!result.workspaceJson.isEmpty());
    }
    void liveHydroRevisionUsesExactSortedJsEditedSignatureBeforeKernel() {
        auto row=QJsonDocument::fromJson(R"({"request":{"hydroRevision":"old"},"components":[],"includeIdentity":true,"liveHydroRevisionPrefix":"v0.13.1:sha256:","signatureEdits":[{"id":"z","geometry":{"type":"LineString","coordinates":[[1e-7,1e-6],[1e20,1e21]]}},{"id":"a","geometry":{"type":"LineString","coordinates":[[1e-7,1e-6],[1e20,1e21]]}}]})").object();
        const auto first=calculateRiverPartitionsJson(QJsonDocument(row).toJson(QJsonDocument::Compact));
        QVERIFY2(first.succeeded(),qPrintable(first.detail));
        const QString expected="a:[[1e-7,0.000001],[100000000000000000000,1e+21]]|z:[[1e-7,0.000001],[100000000000000000000,1e+21]]";
        QCOMPARE(first.editedRiverSignature,expected);
        QCOMPARE(first.diagnostics.hydroRevision,QJsonValue("v0.13.1:sha256:"+expected));
        QVERIFY(first.inputUnchanged);
        auto edits=row["signatureEdits"].toArray();row["signatureEdits"]=QJsonArray{edits[1],edits[0]};
        const auto reordered=calculateRiverPartitionsJson(QJsonDocument(row).toJson(QJsonDocument::Compact));
        QVERIFY(reordered.succeeded());QCOMPARE(reordered.diagnostics.hydroRevision,first.diagnostics.hydroRevision);
        auto changed=edits[0].toObject();auto geometry=changed["geometry"].toObject();
        auto coordinates=geometry["coordinates"].toArray();coordinates[0]=QJsonArray{2e-7,1e-6};geometry["coordinates"]=coordinates;
        changed["geometry"]=geometry;edits[0]=changed;row["signatureEdits"]=edits;
        const auto next=calculateRiverPartitionsJson(QJsonDocument(row).toJson(QJsonDocument::Compact));QVERIFY(next.succeeded());
        QVERIFY(next.diagnostics.hydroRevision!=first.diagnostics.hydroRevision);
        QCOMPARE(next.diagnostics.hydroRevision,QJsonValue("v0.13.1:sha256:"+next.editedRiverSignature));
        row.remove("liveHydroRevisionPrefix");
        const auto explicitRevision=calculateRiverPartitionsJson(QJsonDocument(row).toJson(QJsonDocument::Compact));
        QVERIFY(explicitRevision.succeeded());QCOMPARE(explicitRevision.diagnostics.hydroRevision,QJsonValue("old"));
    }
    void typedLiveHydroPrefixIsOptionalAndUsesExactJsFormatting() {
        RiverPartitionRequest request;request.hydroRevision=123;request.liveHydroRevisionPrefix="v0.13.1:index:";
        EditedRiverValue edit;edit.id="z";edit.geometry.type="LineString";
        edit.geometry.lines={{{1e-7,1e-6},{1e20,1e21}}};request.signatureEdits.push_back(edit);
        edit.id="a";request.signatureEdits.push_back(edit);
        const auto result=calculateRiverPartitions(request);QVERIFY2(result.succeeded(),qPrintable(result.detail));
        QCOMPARE(result.diagnostics.hydroRevision,QJsonValue(*request.liveHydroRevisionPrefix+result.editedRiverSignature));
        QCOMPARE(result.editedRiverSignature,QString("a:[[1e-7,0.000001],[100000000000000000000,1e+21]]|z:[[1e-7,0.000001],[100000000000000000000,1e+21]]"));
        request.liveHydroRevisionPrefix=QString{};
        const auto emptyPrefix=calculateRiverPartitions(request);QVERIFY(emptyPrefix.succeeded());
        QCOMPARE(emptyPrefix.diagnostics.hydroRevision,QJsonValue(emptyPrefix.editedRiverSignature));
        request.liveHydroRevisionPrefix.reset();
        const auto explicitRevision=calculateRiverPartitions(request);QVERIFY(explicitRevision.succeeded());
        QCOMPARE(explicitRevision.diagnostics.hydroRevision,QJsonValue(123));
        request.liveHydroRevisionPrefix="v0.13.1:index:";request.signatureEdits.clear();
        const auto noEdits=calculateRiverPartitions(request);QVERIFY(noEdits.succeeded());
        QCOMPARE(noEdits.diagnostics.hydroRevision,QJsonValue("v0.13.1:index:"));QVERIFY(noEdits.editedRiverSignature.isEmpty());
        QCOMPARE(calculateRiverPartitions(request,[]{return true;}).status,RiverPartitionStatus::Cancelled);
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

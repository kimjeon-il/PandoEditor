#include "editorcontroller.h"
#include "placeruntimestore.h"
#include "placenamedisplay.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QSet>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>
#include <algorithm>
#include <stdexcept>

namespace {
QJsonObject contract() {
    QFile file(QString::fromUtf8(PLACE_SYNC_CONTRACT_PATH));
    if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Missing frozen Web v2 exchange contract");
    const auto doc=QJsonDocument::fromJson(file.readAll());
    if(!doc.isObject())throw std::runtime_error("Malformed frozen Web exchange contract");
    return doc.object();
}
PlaceLanguageSelection flags(const QJsonObject& value) {
    return normalizedPlaceLanguages(value.toVariantMap());
}
QJsonArray rows(const std::vector<PlaceDisplayRow>& actual) {
    QJsonArray result;
    for(const auto& row:actual)result.append(QJsonArray{row.language,row.text});
    return result;
}
QByteArray writeV2Manifest(const QJsonObject& fixture,const QString& directory) {
    const auto payload=QByteArray::fromHex(fixture.value("tileHex").toString().toLatin1());
    QFile shard(directory+"/place.bin");
    if(!shard.open(QIODevice::WriteOnly)||shard.write(payload)!=payload.size())
        throw std::runtime_error("Failed writing v2 native fixture shard");
    shard.close();
    const auto sha=QString::fromLatin1(QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex());
    QJsonObject row{{"shard","sample"},{"offset",0},{"length",payload.size()},{"sha256",sha}};
    const QJsonObject manifest{
        {"version",1},{"revision","native-multilingual-v2-test"},
        {"stages",QJsonArray{QJsonObject{{"id",0},{"minZoom",0},{"columns",1},{"rows",1}}}},
        {"tiles",QJsonObject{{"0/0-0",row}}},
        {"shards",QJsonObject{{"sample",QJsonObject{{"url","place.bin"},{"bytes",payload.size()}}}}},
        {"search",QJsonObject{}}
    };
    return QJsonDocument(manifest).toJson(QJsonDocument::Compact);
}
bool hasLines(EditorController& editor,const QString& id,int count) {
    for(const auto& row:editor.placedLabels()) {
        const auto item=row.toMap(),ref=item.value("ref").toMap();
        if(ref.value("id").toString()==id&&ref.value("domain").toString()=="placeBuiltin")
            return item.value("nameLines").toList().size()==count;
    }
    return false;
}
}

class PlaceSyncContractTests final : public QObject {
    Q_OBJECT
private slots:
    void publishedWireFieldsArePortable() {
        const auto source=contract();
        QCOMPARE(source.value("schema").toString(),QStringLiteral("pando-place-sync-v2"));
        const auto wire=source.value("wire").toObject();
        QCOMPARE(wire.value("version").toInt(),2);
        QCOMPARE(wire.value("recordBytes").toInt(),68);
        QCOMPARE(wire.value("headerBytes").toInt(),32);
        QCOMPARE(wire.value("stringOffsetBase").toInt(),36);
        QCOMPARE(wire.value("stringFields").toArray().size(),8);
        QCOMPARE(wire.value("maxStringBytes").toInt(),16*1024);
        QCOMPARE(source.value("fixtures").toArray().size(),4);
    }
    void reviewedSourceMirrorIsIdenticalToPinnedWebGitBlobs() {
        const auto root=QString::fromUtf8(PLACE_SYNC_REPOSITORY_ROOT);
        QFile manifest(root+"/reports/places/source-manifest.json");
        QVERIFY(manifest.open(QIODevice::ReadOnly));
        const auto report=QJsonDocument::fromJson(manifest.readAll()).object();
        const auto sourceCommit=report.value("webCommit").toString();
        QVERIFY(QRegularExpression("^[a-f0-9]{40}$").match(sourceCommit).hasMatch());
        const auto files=report.value("files").toArray();
        QVERIFY(files.size()>2);
        const auto inventory=report.value("reviewInventory").toObject();
        QVERIFY(inventory.value("batchCount").toInt()>0);
        QVERIFY(inventory.value("recordCount").toInt()>0);
        QCOMPARE(inventory.value("distinctGeoNames").toInt(),
                 inventory.value("recordCount").toInt());
        const auto batches=inventory.value("byBatch").toArray();
        QCOMPARE(batches.size(),inventory.value("batchCount").toInt());
        QSet<QString> mirroredFiles;
        for(const auto& item:files) {
            const auto path=item.toObject().value("path").toString();
            QVERIFY(!mirroredFiles.contains(path));
            mirroredFiles.insert(path);
        }
        QVERIFY(mirroredFiles.contains("reports/places/historical-display-policy.json"));
        QVERIFY(mirroredFiles.contains("reports/places/korean-map-label-policy.json"));
        int examined=0;QSet<int> ids;
        for(const auto& item:inventory.value("byBatch").toArray()) {
            const auto meta=item.toObject();
            const auto relative=meta.value("path").toString();
            QVERIFY(relative.startsWith("reports/places/tier1-major-cities-batch")&&!relative.contains(".."));
            QVERIFY(mirroredFiles.contains(relative));
            QFile reviewed(root+"/"+relative);QVERIFY(reviewed.open(QIODevice::ReadOnly));
            const auto rows=QJsonDocument::fromJson(reviewed.readAll()).object().value("records").toArray();
            QCOMPARE(rows.size(),meta.value("recordCount").toInt());
            examined+=rows.size();
            for(const auto& row:rows) {
                const int id=row.toObject().value("geonameId").toInt();
                QVERIFY(id>0);QVERIFY(!ids.contains(id));ids.insert(id);
            }
        }
        QCOMPARE(examined,inventory.value("recordCount").toInt());
        QCOMPARE(ids.size(),inventory.value("distinctGeoNames").toInt());
        QCOMPARE(files.size(),batches.size()+2);
        for(const auto& value:files) {
            const auto row=value.toObject();
            const auto path=row.value("path").toString();
            QVERIFY(path.startsWith("reports/places/")&&!path.contains(".."));
            QFile data(root+"/"+path);QVERIFY2(data.open(QIODevice::ReadOnly),qPrintable(path));
            const auto raw=data.readAll();
            QByteArray blob="blob "+QByteArray::number(raw.size());
            blob.append(char(0));blob.append(raw);
            const auto actual=QString::fromLatin1(QCryptographicHash::hash(blob,QCryptographicHash::Sha1).toHex());
            QCOMPARE(actual,row.value("gitBlobSha").toString());
        }
        for(const auto& fixtureValue:contract().value("fixtures").toArray()) {
            const auto fixture=fixtureValue.toObject();
            const auto provenance=fixture.value("sourceReview").toObject();
            const auto relative=provenance.value("reviewFile").toString();
            QVERIFY(relative.startsWith("reports/places/tier1-major-cities-batch")&&!relative.contains(".."));
            QFile original(root+"/"+relative);QVERIFY(original.open(QIODevice::ReadOnly));
            const auto rows=QJsonDocument::fromJson(original.readAll()).object().value("records").toArray();
            const auto id=provenance.value("geonameId").toInt();
            QJsonObject reviewed;
            for(const auto& item:rows)
                if(item.toObject().value("geonameId").toInt()==id)reviewed=item.toObject();
            QVERIFY(!reviewed.isEmpty());
            const auto record=fixture.value("normalized").toObject();
            QCOMPARE(reviewed.value("defaultDisplayNameKo"),record.value("name"));
            QCOMPARE(reviewed.value("longitude"),record.value("coordinates").toArray().at(0));
            QCOMPARE(reviewed.value("latitude"),record.value("coordinates").toArray().at(1));
        }
    }
    void nativeDecoderMatchesAllFrozenWebHexVectors() {
        const auto fixtures=contract().value("fixtures").toArray();
        for(const auto& value:fixtures) {
            const auto fixture=value.toObject();
            const auto payload=QByteArray::fromHex(fixture.value("tileHex").toString().toLatin1());
            const auto records=PlaceRuntimeStore::decodeTile(payload);
            QCOMPARE(records.size(),std::size_t(1));
            const auto& row=records.front();
            const auto expected=fixture.value("normalized").toObject();
            QCOMPARE(row.id,expected.value("id").toString());
            QCOMPARE(row.source,expected.value("source").toString());
            QCOMPARE(row.sourceId,expected.value("sourceId").toString());
            QCOMPARE(row.name,expected.value("name").toString());
            QCOMPARE(row.nameEn,expected.value("nameEn").toString());
            QCOMPARE(row.nameNative,expected.value("nameNative").toString());
            QCOMPARE(row.kind,expected.value("kind").toString());
            QCOMPARE(row.coordinates.x,expected.value("coordinates").toArray().at(0).toDouble());
            QCOMPARE(row.coordinates.y,expected.value("coordinates").toArray().at(1).toDouble());
            QCOMPARE(row.nameTimeline.size(),std::size_t(expected.value("nameTimeline").toArray().size()));
            const auto scenarios=fixture.value("scenarios").toArray();
            for(const auto& scenarioValue:scenarios) {
                const auto scenario=scenarioValue.toObject();
                const auto date=scenario.value("date").isNull()?QString():scenario.value("date").toString();
                QCOMPARE(rows(resolvePlaceDisplayRows(row,flags(scenario.value("languages").toObject()),date)),
                    scenario.value("rows").toArray());
            }
            QVERIFY_EXCEPTION_THROWN(PlaceRuntimeStore::decodeTile(payload.left(payload.size()-1)),
                std::exception);
        }
    }
    void languageToggleRejectsAllOffAndResolvesDuplicates() {
        PlaceLanguageSelection flags;
        QVERIFY(!toggledPlaceLanguage(flags,"ko",false).has_value());
        QVERIFY(!toggledPlaceLanguage(flags,"ko",true).has_value());
        auto english=toggledPlaceLanguage(flags,"en",true);
        QVERIFY(english.has_value());QVERIFY(english->ko&&english->en&&!english->native);
        english=toggledPlaceLanguage(*english,"ko",false);
        QVERIFY(english.has_value());QVERIFY(!english->ko&&english->en);
        QVERIFY_EXCEPTION_THROWN(toggledPlaceLanguage(*english,"fr",true),std::invalid_argument);
        PlaceRecord record;record.name=QStringLiteral("부다페스트");
        record.nameEn="Budapest";record.nameNative="Budapest";
        auto all=flags;all.en=true;all.native=true;
        const auto resolved=resolvePlaceDisplayRows(record,all);
        QCOMPARE(resolved.size(),std::size_t(2));QCOMPARE(resolved.back().language,QStringLiteral("en"));
    }
    void controllerPersistsLanguageTogglesWithoutMutatingProject() {
        QTemporaryDir temporary;QVERIFY(temporary.isValid());
        EditorControllerConfig config;config.appearancePath=temporary.filePath("appearance.json");
        {
            EditorController editor(config);
            QSignalSpy signal(&editor,&EditorController::placeLanguagesChanged);
            QCOMPARE(editor.placeLanguages().value("ko").toBool(),true);
            QVERIFY(editor.setPlaceLanguage("en",true));
            QVERIFY(editor.setPlaceLanguage("native",true));
            QVERIFY(editor.setPlaceLanguage("ko",false));
            QCOMPARE(editor.placeLanguages().value("ko").toBool(),false);
            QVERIFY(editor.placeLanguages().value("en").toBool());
            QVERIFY(editor.placeLanguages().value("native").toBool());
            QVERIFY(!editor.dirty());QCOMPARE(signal.size(),3);
        }
        EditorController reloaded(config);
        QVERIFY(!reloaded.placeLanguages().value("ko").toBool());
        QVERIFY(reloaded.placeLanguages().value("en").toBool());
        QVERIFY(reloaded.placeLanguages().value("native").toBool());
        QVERIFY(!reloaded.dirty());
    }
    void builtinV2LabelsHaveOneSelectionRefAndThreeDisplayLines() {
        QTemporaryDir temporary;QVERIFY(temporary.isValid());
        const auto fixture=contract().value("fixtures").toArray().at(0).toObject();
        QFile manifest(temporary.filePath("manifest.json"));
        const auto data=writeV2Manifest(fixture,temporary.path());
        QVERIFY(manifest.open(QIODevice::WriteOnly));QCOMPARE(manifest.write(data),qint64(data.size()));
        manifest.close();
        EditorController editor;
        editor.resizeMapCamera(800,600);
        editor.publishMapView({{"viewportWidth",800},{"viewportHeight",600},{"centerLongitude",0},
            {"centerLatitude",0},{"scale",100},{"translateX",400},{"translateY",300}});
        QVERIFY(editor.setPlaceLanguage("en",true));
        QVERIFY(editor.setPlaceLanguage("native",true));
        QVERIFY(editor.configurePlaceData(QUrl::fromLocalFile(manifest.fileName())));
        const QString id="builtin:place:geonames:703448";
        QTRY_VERIFY_WITH_TIMEOUT(hasLines(editor,id,3),10000);
        const auto placed=editor.placedLabels();
        const auto count=std::count_if(placed.cbegin(),placed.cend(),
            [&](const QVariant& value){return value.toMap().value("ref").toMap().value("id").toString()==id;});
        QCOMPARE(count,1);
        QVERIFY(!editor.dirty());
        QVERIFY(editor.setPlaceLanguage("native",false));
        QTRY_VERIFY_WITH_TIMEOUT(hasLines(editor,id,2),10000);
        QVERIFY(!editor.dirty());
    }
};

QTEST_MAIN(PlaceSyncContractTests)
#include "place_sync_contract_tests.moc"

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
    void batch11CityIdsAndPodgoricaHistoricalKoreanTimeline() {
        const QString root=QString::fromUtf8(PLACE_SYNC_REPOSITORY_ROOT);
        QFile file(root+"/reports/places/tier1-major-cities-batch11-western-balkans-capitals.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto document=QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(document.value("status").toString(),
                 QStringLiteral("verified-source-staging-not-runtime"));
        const auto records=document.value("records").toArray();
        QCOMPARE(records.size(),5);
        const QList<int> ids{3191281,3193044,785842,3183875,786714};
        const QStringList koreans{QStringLiteral("사라예보"),
            QStringLiteral("포드고리차"),QStringLiteral("스코페"),
            QStringLiteral("티라나"),QStringLiteral("프리슈티나")};
        for(qsizetype i=0;i<records.size();++i) {
            const auto record=records.at(i).toObject();
            QCOMPARE(record.value("geonameId").toInt(),ids.at(i));
            QCOMPARE(record.value("defaultDisplayNameKo").toString(),koreans.at(i));
            QCOMPARE(record.value("featureClass").toString(),QStringLiteral("P"));
            QCOMPARE(record.value("featureCode").toString(),QStringLiteral("PPLC"));
            const auto names=record.value("names").toArray();
            bool korean=false,english=false,native=false;
            for(const auto& value:names) {
                const auto name=value.toObject();
                const auto language=name.value("language").toString();
                if(language=="ko"&&name.value("text").toString()==koreans.at(i))
                    korean=true;
                if(language=="en"&&name.value("usage")=="standard")english=true;
                if(language!="ko"&&language!="en"&&name.value("usage")=="standard")
                    native=true;
            }
            QVERIFY(korean&&english&&native);
        }
        const auto city=records.at(1).toObject();
        PlaceRecord testRecord;testRecord.name=city.value("defaultDisplayNameKo").toString();
        const auto timeline=city.value("displayTimeline").toArray();
        QCOMPARE(timeline.size(),4);
        for(const auto& value:timeline) {
            const auto item=value.toObject();PlaceNameTransition transition;
            if(item.contains("fromDate"))transition.fromDate=item.value("fromDate").toString();
            else transition.fromYear=item.value("fromYear").toInt();
            transition.ko=item.value("nameKo").toString();
            testRecord.nameTimeline.push_back(std::move(transition));
        }
        const QStringList dates{"1946-07-12","1946-07-13","1992-04-01","1992-04-02"};
        const QStringList expected{QStringLiteral("포드고리차"),
            QStringLiteral("티토그라드"),QStringLiteral("티토그라드"),
            QStringLiteral("포드고리차")};
        for(qsizetype i=0;i<dates.size();++i) {
            const auto actual=resolvePlaceDisplayRows(testRecord,PlaceLanguageSelection{},dates.at(i));
            QCOMPARE(actual.size(),std::size_t(1));
            QCOMPARE(actual.front().text,expected.at(i));
        }
        const auto pristina=records.at(4).toObject().value("displayTimeline").toArray();
        QCOMPARE(pristina.size(),5);
        QCOMPARE(pristina.at(3).toObject().value("fromDate").toString(),
                 QStringLiteral("1990-09-28"));
        QCOMPARE(pristina.at(4).toObject().value("fromDate").toString(),
                 QStringLiteral("2000-07-27"));
        QCOMPARE(records.at(4).toObject().value("sourceCountryCode").toString(),
                 QStringLiteral("XK"));
    }

    void batch12BratislavaNameChangeAndLanguageVariants() {
        const QString root=QString::fromUtf8(PLACE_SYNC_REPOSITORY_ROOT);
        QFile file(root+"/reports/places/tier1-major-cities-batch12-central-eastern-europe-capitals.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto dataset=QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(dataset.value("status").toString(),
                 QStringLiteral("verified-source-staging-not-runtime"));
        const auto records=dataset.value("records").toArray();
        QCOMPARE(records.size(),4);
        const QList<int> ids{3060972,618426,2960316,3042030};
        const QStringList expectedKo{QStringLiteral("브라티슬라바"),
            QStringLiteral("키시너우"),QStringLiteral("룩셈부르크"),
            QStringLiteral("파두츠")};
        const QStringList countries{"SK","MD","LU","LI"};
        for(qsizetype i=0;i<records.size();++i) {
            const auto city=records.at(i).toObject();
            QCOMPARE(city.value("geonameId").toInt(),ids.at(i));
            QCOMPARE(city.value("defaultDisplayNameKo").toString(),expectedKo.at(i));
            QCOMPARE(city.value("sourceCountryCode").toString(),countries.at(i));
            QCOMPARE(city.value("featureClass").toString(),QStringLiteral("P"));
            QCOMPARE(city.value("featureCode").toString(),QStringLiteral("PPLC"));
            bool korean=false,english=false,native=false;
            for(const auto& item:city.value("names").toArray()) {
                const auto name=item.toObject();
                const auto language=name.value("language").toString();
                if(language=="ko"&&name.value("text")==expectedKo.at(i))korean=true;
                if(language=="en"&&name.value("usage")=="standard")english=true;
                if(language!="ko"&&language!="en"&&name.value("usage")=="standard")native=true;
            }
            QVERIFY(korean&&english&&native);
        }
        const auto bratislava=records.at(0).toObject();
        const auto timeline=bratislava.value("displayTimeline").toArray();
        QCOMPARE(timeline.size(),4);
        PlaceRecord place;place.name=bratislava.value("defaultDisplayNameKo").toString();
        for(const auto& entry:timeline) {
            const auto raw=entry.toObject();PlaceNameTransition transition;
            if(raw.contains("fromDate"))transition.fromDate=raw.value("fromDate").toString();
            else transition.fromYear=raw.value("fromYear").toInt();
            transition.ko=raw.value("nameKo").toString();
            place.nameTimeline.push_back(std::move(transition));
        }
        const QStringList dates{"1919-03-26","1919-03-27","1920-01-01"};
        const QStringList expected{QStringLiteral("포조니"),
            QStringLiteral("브라티슬라바"),QStringLiteral("브라티슬라바")};
        for(qsizetype i=0;i<dates.size();++i) {
            const auto rows=resolvePlaceDisplayRows(place,PlaceLanguageSelection{},dates.at(i));
            QCOMPARE(rows.size(),std::size_t(1));
            QCOMPARE(rows.front().text,expected.at(i));
        }
        const auto chisinau=records.at(1).toObject().value("displayTimeline").toArray();
        QCOMPARE(chisinau.size(),8);
        QCOMPARE(chisinau.at(6).toObject().value("fromDate").toString(),
                 QStringLiteral("1944-09-12"));
        QCOMPARE(chisinau.at(7).toObject().value("fromDate").toString(),
                 QStringLiteral("1989-08-31"));
        QCOMPARE(records.at(3).toObject().value("sourceCountryCode").toString(),
                 QStringLiteral("LI"));
    }

    void batch13FiveCapitalsAndNicosiaMultilingualNames() {
        const QString root=QString::fromUtf8(PLACE_SYNC_REPOSITORY_ROOT);
        QFile file(root+"/reports/places/tier1-major-cities-batch13-european-microstates-mediterranean.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto document=QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(document.value("status").toString(),QStringLiteral("verified-source-staging-not-runtime"));
        const auto records=document.value("records").toArray();
        QCOMPARE(records.size(),5);
        const QList<int> ids{3041563,3168070,2993458,2562305,146268};
        const QStringList labels{QStringLiteral("안도라라베야"),QStringLiteral("산마리노"),
            QStringLiteral("모나코"),QStringLiteral("발레타"),QStringLiteral("니코시아")};
        const QStringList countries{"AD","SM","MC","MT","CY"};
        for(qsizetype i=0;i<records.size();++i) {
            const QJsonObject city=records.at(i).toObject();
            QCOMPARE(city.value("geonameId").toInt(),ids.at(i));
            QCOMPARE(city.value("defaultDisplayNameKo").toString(),labels.at(i));
            QCOMPARE(city.value("sourceCountryCode").toString(),countries.at(i));
            QCOMPARE(city.value("featureClass").toString(),QStringLiteral("P"));
            QCOMPARE(city.value("featureCode").toString(),QStringLiteral("PPLC"));
            const auto timeline=city.value("displayTimeline").toArray();
            QVERIFY(!timeline.isEmpty());
            QCOMPARE(timeline.at(0).toObject().value("fromYear").toInt(),1801);
            QVERIFY(!timeline.at(0).toObject().value("nameKo").toString().isEmpty());
        }
        const auto monaco=records.at(2).toObject().value("displayTimeline").toArray();
        QCOMPARE(monaco.size(),2);
        QCOMPARE(monaco.at(0).toObject().value("nameKo").toString(),
                 QStringLiteral("포르에르퀼"));
        QCOMPARE(monaco.at(1).toObject().value("fromDate").toString(),
                 QStringLiteral("1814-05-30"));
        const auto valletta=records.at(3).toObject().value("displayTimeline").toArray();
        QCOMPARE(valletta.size(),4);
        QCOMPARE(valletta.at(2).toObject().value("fromDate").toString(),
                 QStringLiteral("1934-10-01"));
        QCOMPARE(valletta.at(3).toObject().value("fromDate").toString(),
                 QStringLiteral("1936-09-02"));
        const auto nicosia=records.at(4).toObject();
        QCOMPARE(nicosia.value("displayTimeline").toArray().size(),1);
        const auto names=nicosia.value("names").toArray();
        bool greek=false,turkish=false;
        for(const auto& value:names) {
            const QJsonObject name=value.toObject();
            if(name.value("language")=="el"&&name.value("text")==QStringLiteral("Λευκωσία"))
                greek=true;
            if(name.value("language")=="tr"&&name.value("text")==QStringLiteral("Lefkoşa"))
                turkish=true;
        }
        QVERIFY(greek&&turkish);
        const auto events=nicosia.value("historicalGeography").toObject().value("events").toArray();
        QCOMPARE(events.size(),2);
        QCOMPARE(events.at(0).toObject().value("date").toString(),QStringLiteral("1963-12-30"));
        QCOMPARE(events.at(1).toObject().value("date").toString(),QStringLiteral("1974-08-16"));
        PlaceRecord record;
        record.name=QStringLiteral("니코시아");
        record.nameEn="Nicosia";
        record.nameNative=QStringLiteral("Λευκωσία");
        PlaceLanguageSelection languages;languages.en=true;languages.native=true;
        const auto three=resolvePlaceDisplayRows(record,languages);
        QCOMPARE(three.size(),std::size_t(3));
        QCOMPARE(three.at(0).text,QStringLiteral("니코시아"));
        QCOMPARE(three.at(1).text,QStringLiteral("Nicosia"));
        QCOMPARE(three.at(2).text,QStringLiteral("Λευκωσία"));
        record.name=QStringLiteral("산마리노");
        record.nameEn="San Marino";record.nameNative="San Marino";
        const auto deduplicated=resolvePlaceDisplayRows(record,languages);
        QCOMPARE(deduplicated.size(),std::size_t(2));
        QCOMPARE(deduplicated.at(1).language,QStringLiteral("en"));
    }

    void batch14OmittedVaticanAndYearPrecisionLabels() {
        const QString root=QString::fromUtf8(PLACE_SYNC_REPOSITORY_ROOT);
        QFile file(root+"/reports/places/tier1-major-cities-batch14-north-atlantic-anatolia-caucasus.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject dataset=QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(dataset.value("status").toString(),QStringLiteral("verified-source-staging-not-runtime"));
        const QJsonArray cities=dataset.value("records").toArray();
        QCOMPARE(cities.size(),5);
        const QList<int> ids{3413829,323786,611717,616052,587084};
        const QStringList names{QStringLiteral("레이캬비크"),QStringLiteral("앙카라"),
            QStringLiteral("트빌리시"),QStringLiteral("예레반"),QStringLiteral("바쿠")};
        for(qsizetype i=0;i<cities.size();++i) {
            const QJsonObject city=cities.at(i).toObject();
            QCOMPARE(city.value("geonameId").toInt(),ids.at(i));
            QCOMPARE(city.value("defaultDisplayNameKo").toString(),names.at(i));
            QCOMPARE(city.value("featureClass").toString(),QStringLiteral("P"));
            QCOMPARE(city.value("featureCode").toString(),QStringLiteral("PPLC"));
            QVERIFY(!names.at(i).contains(QStringLiteral("바티칸")));
        }
        const auto tbilisi=cities.at(2).toObject().value("displayTimeline").toArray();
        QCOMPARE(tbilisi.size(),3);
        QCOMPARE(tbilisi.at(0).toObject().value("nameKo").toString(),
                 QStringLiteral("티플리스"));
        QCOMPARE(tbilisi.at(1).toObject().value("fromDate").toString(),
                 QStringLiteral("1918-05-26"));
        QCOMPARE(tbilisi.at(2).toObject().value("fromDate").toString(),
                 QStringLiteral("1936-08-17"));
        QCOMPARE(tbilisi.at(2).toObject().value("nameKo").toString(),
                 QStringLiteral("트빌리시"));
        const auto yerevan=cities.at(3).toObject().value("displayTimeline").toArray();
        QCOMPARE(yerevan.size(),5);
        QCOMPARE(yerevan.at(0).toObject().value("nameKo").toString(),
                 QStringLiteral("이라반"));
        QCOMPARE(yerevan.at(1).toObject().value("fromDate").toString(),
                 QStringLiteral("1828-02-22"));
        QCOMPARE(yerevan.at(2).toObject().value("fromDate").toString(),
                 QStringLiteral("1918-05-28"));
        QCOMPARE(yerevan.at(2).toObject().value("nameKo").toString(),
                 QStringLiteral("예레반"));
        QCOMPARE(cities.at(1).toObject().value("sourceCountryCode").toString(),QStringLiteral("TR"));
        QCOMPARE(cities.at(4).toObject().value("sourceCountryCode").toString(),QStringLiteral("AZ"));
    }

    void batch15CentralAsiaKoreanHistoricalNameSelections() {
        const QString root=QString::fromUtf8(PLACE_SYNC_REPOSITORY_ROOT);
        QFile file(root+"/reports/places/tier1-major-cities-batch15-central-asia-capitals.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto data=QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(data.value("status").toString(),QStringLiteral("verified-source-staging-not-runtime"));
        const auto records=data.value("records").toArray();
        QCOMPARE(records.size(),5);
        const QList<int> ids{1526273,1512569,1528675,1221874,162183};
        const QStringList names{QStringLiteral("아스타나"),QStringLiteral("타슈켄트"),
            QStringLiteral("비슈케크"),QStringLiteral("두샨베"),QStringLiteral("아시가바트")};
        for(qsizetype i=0;i<records.size();++i) {
            const QJsonObject value=records.at(i).toObject();
            QCOMPARE(value.value("geonameId").toInt(),ids.at(i));
            QCOMPARE(value.value("featureClass").toString(),QStringLiteral("P"));
            QCOMPARE(value.value("featureCode").toString(),QStringLiteral("PPLC"));
            QCOMPARE(value.value("defaultDisplayNameKo").toString(),names.at(i));
        }
        const auto createFrom=[&](int index) {
            const QJsonObject place=records.at(index).toObject();
            PlaceRecord item;
            item.name=place.value("defaultDisplayNameKo").toString();
            for(const auto& value:place.value("displayTimeline").toArray()) {
                const QJsonObject row=value.toObject();
                PlaceNameTransition name;
                if(row.contains("fromDate"))name.fromDate=row.value("fromDate").toString();
                else name.fromYear=row.value("fromYear").toInt();
                name.ko=row.value("nameKo").toString();
                item.nameTimeline.push_back(std::move(name));
            }
            return item;
        };
        const auto expectedName=[&](int index,const QString& date,const QString& expected) {
            const auto item=createFrom(index);
            const auto result=resolvePlaceDisplayRows(item,PlaceLanguageSelection{},date);
            QCOMPARE(result.size(),std::size_t(1));
            QCOMPARE(result.front().text,expected);
        };
        expectedName(0,"1961-03-19",QStringLiteral("아크몰린스크"));
        expectedName(0,"1961-03-20",QStringLiteral("첼리노그라드"));
        expectedName(0,"1992-07-06",QStringLiteral("아크몰라"));
        expectedName(0,"1998-05-06",QStringLiteral("아스타나"));
        expectedName(0,"2019-03-23",QStringLiteral("누르술탄"));
        expectedName(0,"2022-09-18",QStringLiteral("누르술탄"));
        expectedName(0,"2022-09-19",QStringLiteral("아스타나"));
        expectedName(2,"1926-05-11",QStringLiteral("피슈페크"));
        expectedName(2,"1926-05-12",QStringLiteral("프룬제"));
        expectedName(2,"1991-02-04",QStringLiteral("프룬제"));
        expectedName(2,"1991-02-05",QStringLiteral("비슈케크"));
        expectedName(3,"1950-06-15",QStringLiteral("스탈리나바드"));
        expectedName(3,"1961-06-15",QStringLiteral("두샨베"));
        expectedName(4,"1927-04-06",QStringLiteral("폴토라츠크"));
        expectedName(4,"1927-04-07",QStringLiteral("아슈하바트"));
        expectedName(4,"1991-10-27",QStringLiteral("아시가바트"));
        QCOMPARE(records.at(1).toObject().value("displayTimeline").toArray().size(),1);
        QCOMPARE(records.at(0).toObject().value("temporalEligibility").toObject()
            .value("cityEstablishedFromYear").toInt(),1830);
        QCOMPARE(records.at(2).toObject().value("temporalEligibility").toObject()
            .value("cityEstablishedFromYear").toInt(),1868);
        QCOMPARE(records.at(4).toObject().value("temporalEligibility").toObject()
            .value("cityEstablishedFromYear").toInt(),1881);
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

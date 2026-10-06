#include "projectcodec.h"
#include "losslessjson.h"
#include <QFile>
#include <QCryptographicHash>
#include <pandoeditor/geometryprovenance.h>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <functional>

using namespace pandoeditor;
namespace {
using V=losslessjson::Value;
QByteArray readFile(const QString& path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("fixture read failed");return file.readAll();
}
void writeFile(const QString& path,const QByteArray& bytes) {
    QFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size())throw std::runtime_error("fixture write failed");
}
V source() { return losslessjson::parse(readFile(QStringLiteral(M975_OWNERSHIP_FIXTURES)+"/m975-annex-full-recursive.json")); }
V refValue(const std::string& id,unsigned version=1) {auto value=V::obj();value.object={{"id",V::str(id)},{"version",V::num(version)}};return value;}
V rowValue(const std::string& id,const V& shape,unsigned version=1) {auto value=refValue(id,version);value.object["geojson"]=shape;return value;}
V labelShape(const V& label) {auto shape=V::obj();shape.object={{"type",V::str("Point")},{"coordinates",label.object.at("coordinates")}};return shape;}
std::map<QByteArray,QByteArray> archive(const V& root) {
    std::map<QByteArray,QByteArray> rows;
    for(const auto& row:root.object.at("geometries").array) {
        const auto key=refValue(row.object.at("id").string,row.object.at("version").raw.toUInt()).encode();
        if(!rows.emplace(key,row.object.at("geojson").encode()).second)throw std::runtime_error("duplicate archive ref");
    }
    return rows;
}
QByteArray nativeBytes(const V& web) {Project project;project.replace(projectcodec::decodeWeb(web.encode()));return projectcodec::encode(project);}
QString invalidArgument(const std::function<void()>& action) {try{action();}catch(const std::invalid_argument& failure){return QString::fromUtf8(failure.what());}return {};}
void checkError(const std::function<void()>& action,const char* expected) {
    const auto error=invalidArgument(action);QVERIFY2(error.contains(QString::fromLatin1(expected)),qPrintable("Expected "+QString::fromLatin1(expected)+", got: "+error));
}
}

class M975GeometryProvenanceCodecTests final:public QObject {
    Q_OBJECT
private slots:
    void ordinaryArchiveSurvivesFileSaveReopenAndDoubleExchange() {
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto input=source();QCOMPARE(input.object.at("geometries").array.size(),std::size_t(5));
        const auto sourcePath=dir.filePath("source.web.json");writeFile(sourcePath,input.encode());
        Project project;project.replace(projectcodec::decodeWeb(readFile(sourcePath)));QCOMPARE(project.document().geometries.versions().size(),std::size_t(8));
        const auto first=losslessjson::parse(projectcodec::encodeWeb(project.snapshot()));QVERIFY2(archive(first)==archive(input),"Web export must retain exactly five source archive rows, not three adapter-only allocations");
        const auto nativePath=dir.filePath("saved.native.json");writeFile(nativePath,projectcodec::encode(project));
        const auto saved=losslessjson::parse(readFile(nativePath));QCOMPARE(saved.object.at("version").raw,QByteArray("10"));
        QVERIFY(saved.object.count("geometryProvenance"));QCOMPARE(saved.object.at("geometryProvenance").object.at("inlineAllocations").array.size(),std::size_t(3));
        Project reopened;reopened.replace(projectcodec::decode(readFile(nativePath)));QCOMPARE(projectcodec::encode(reopened),readFile(nativePath));
        for(int cycle=0;cycle<2;++cycle) {
            const auto bytes=projectcodec::encodeWeb(reopened.snapshot());QVERIFY(archive(losslessjson::parse(bytes))==archive(input));
            auto next=projectcodec::decodeWeb(bytes);QVERIFY(next.labels.front().geometry==reopened.document().labels.front().geometry);QVERIFY(next.hydro.front().geometry==reopened.document().hydro.front().geometry);QVERIFY(next.genericFeatures.front().geometry==reopened.document().genericFeatures.front().geometry);
            reopened.replace(std::move(next));writeFile(nativePath,projectcodec::encode(reopened));reopened.replace(projectcodec::decode(readFile(nativePath)));
        }
        QCOMPARE(readFile(sourcePath),input.encode());
    }
    void nativeWritesExplicitVersionTenLedger() {
        const auto value=losslessjson::parse(nativeBytes(source()));QCOMPARE(value.object.at("version").raw,QByteArray("10"));
        QVERIFY(value.object.count("geometryProvenance"));const auto& provenance=value.object.at("geometryProvenance");
        QCOMPARE(provenance.object.at("schemaVersion").raw,QByteArray("1"));QCOMPARE(provenance.object.at("originalArchive").array.size(),std::size_t(5));QCOMPARE(provenance.object.at("inlineAllocations").array.size(),std::size_t(3));
        QCOMPARE(provenance.object.at("opaqueUncertain").raw,QByteArray("false"));QVERIFY(!provenance.object.at("opaqueBaseline").array.empty());
    }
    void equalInlineShapesReceiveDistinctOwnerRefs() {
        auto input=source();auto second=input.object.at("labels").array.front();second.object["id"]=V::str("97500000-0000-4000-8000-000000000099");input.object["labels"].array.push_back(second);
        const auto document=projectcodec::decodeWeb(input.encode());QCOMPARE(document.labels.size(),std::size_t(2));
        QVERIFY2(!(document.labels[0].geometry==document.labels[1].geometry),"Equal inline shapes must not reuse another newly synthesized owner's allocation");
        for(const auto& label:document.labels)QVERIFY(label.geometry==GeometryRef({"web-label:"+label.id,1}));
        std::reverse(input.object["labels"].array.begin(),input.object["labels"].array.end());const auto reversed=projectcodec::decodeWeb(input.encode());
        for(const auto& label:reversed.labels)QVERIFY(label.geometry==GeometryRef({"web-label:"+label.id,1}));
        Project project;project.replace(document);QVERIFY(archive(losslessjson::parse(projectcodec::encodeWeb(project.snapshot())))==archive(input));
    }
    void originalSyntheticLookingRowsRemainSemanticAfterOwnerDeletion() {
        auto input=source();const auto& label=input.object.at("labels").array.front();const auto id="web-label:"+label.object.at("id").string;
        input.object["geometries"].array.push_back(rowValue(id,labelShape(label)));
        input.object["labelSettings"]=V::obj();auto document=projectcodec::decodeWeb(input.encode());document.labels.clear();Project project;project.replace(std::move(document));
        const auto result=losslessjson::parse(projectcodec::encodeWeb(project.snapshot()));QVERIFY(archive(result)==archive(input));
    }
    void originalSyntheticConflictKeepsDeclaredError() {
        auto input=source();const auto& label=input.object.at("labels").array.front();auto shape=labelShape(label);shape.object["coordinates"].array[0]=V::num(88);
        input.object["geometries"].array.push_back(rowValue("web-label:"+label.object.at("id").string,shape));
        checkError([&]{projectcodec::decodeWeb(input.encode());},"GEOMETRY_ARCHIVE_CONFLICT");
    }
    void nativeNineInputTreatsEveryArchivedRowAsSemantic() {
        const auto legacy=losslessjson::parse(readFile(QStringLiteral(M975_OWNERSHIP_FIXTURES)+"/../native-v9/content.pando.json"));
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("legacy.json");writeFile(path,legacy.encode());
        Project reopened;reopened.replace(projectcodec::decode(readFile(path)));QCOMPARE(readFile(path),legacy.encode());
        const auto saved=losslessjson::parse(projectcodec::encode(reopened));QCOMPARE(saved.object.at("version").raw,QByteArray("10"));QVERIFY(saved.object.count("geometryProvenance"));
        const auto& ledger=saved.object.at("geometryProvenance");QVERIFY(ledger.object.at("inlineAllocations").array.empty());QCOMPARE(ledger.object.at("originalArchive").array.size(),legacy.object.at("geometries").array.size());
        QVERIFY(archive(losslessjson::parse(projectcodec::encodeWeb(reopened.snapshot())))==archive(legacy));
    }

    void everyInlineDomainHasExactCreationProvenance_data() {
        QTest::addColumn<QString>("domain");QTest::addColumn<QByteArray>("shape");
        const QByteArray point=R"({"type":"Point","coordinates":[71,41]})";
        const QByteArray line=R"({"type":"LineString","coordinates":[[71,41],[72,42]]})";
        const QByteArray polygon=R"({"type":"Polygon","coordinates":[[[71,41],[72,41],[72,42],[71,41]]]})";
        QTest::newRow("label")<<QString("label")<<point;
        QTest::newRow("river")<<QString("river")<<line;
        QTest::newRow("lake")<<QString("lake")<<polygon;
        QTest::newRow("generic-point")<<QString("generic")<<point;
        QTest::newRow("generic-line")<<QString("generic")<<line;
        QTest::newRow("generic-polygon")<<QString("generic")<<polygon;
        QTest::newRow("geometric-distribution")<<QString("distributionEntry")<<polygon;
    }
    void everyInlineDomainHasExactCreationProvenance() {
        QFETCH(QString,domain);QFETCH(QByteArray,shape);auto input=source();const auto geometry=losslessjson::parse(shape);
        const auto label=input.object.at("labels").array.front(),hydro=input.object.at("hydroEdits").array.front(),generic=input.object.at("genericFeatures").array.front();
        input.object["labels"]=V::arr();input.object["hydroEdits"]=V::arr();input.object["genericFeatures"]=V::arr();input.object["labelSettings"]=V::obj();input.object["itemVisibility"]=V::obj();
        const std::string id="97500000-0000-4000-8000-000000000098";std::string nativeDomain=domain.toStdString(),webDomain=nativeDomain;
        if(domain=="label") {auto row=label;row.object["id"]=V::str(id);row.object["coordinates"]=geometry.object.at("coordinates");input.object["labels"].array.push_back(row);}
        else if(domain=="river"||domain=="lake") {auto row=hydro;webDomain="hydroEdits";nativeDomain="hydro";row.object["id"]=V::str(id);row.object["geometry"]=geometry;row.object["properties"].object["category"]=V::str(domain.toStdString());row.object["properties"].object["pandolab_id"]=V::str(id);input.object[webDomain].array.push_back(row);}
        else if(domain=="generic") {auto row=generic;webDomain="genericFeatures";row.object["id"]=V::str(id);row.object["geometry"]=geometry;input.object[webDomain].array.push_back(row);}
        else {auto row=input.object.at("distributionEntries").array.front();row.object["id"]=V::str(id);row.object["mode"]=V::str("geometry");row.object["territorialUnitId"]=V::str("");row.object["geometry"]=geometry;input.object["distributionEntries"].array.push_back(row);}
        auto document=projectcodec::decodeWeb(input.encode());QCOMPARE(document.geometryProvenance.inlineAllocations.size(),std::size_t(1));const GeometryRef expected{"web-"+webDomain+":"+id,1};
        QVERIFY(document.geometryProvenance.inlineAllocations.count(expected));const auto allocation=document.geometryProvenance.inlineAllocations.at(expected);QVERIFY(allocation.createdFor==ObjectRef({nativeDomain,id}));QVERIFY(!allocation.promoted);
        QCOMPARE(allocation.geometrySha256,QCryptographicHash::hash(geometry.encode(),QCryptographicHash::Sha256).toHex().toStdString());
        Project project;project.replace(std::move(document));QVERIFY(archive(losslessjson::parse(projectcodec::encodeWeb(project.snapshot())))==archive(input));
    }
    void malformedNativeTenLedgerRejectsBeforeActivation_data() {
        QTest::addColumn<QString>("mutation");QTest::addColumn<QString>("expected");
        for(const auto* name:{"missing","schema","duplicate-original","duplicate-allocation","overlap","missing-target","creator-domain","creator-id","wrong-deterministic-ref","bad-shape-hash","wrong-shape-hash","bad-opaque-hash","duplicate-opaque-slot","stale-opaque","new-opaque"})QTest::newRow(name)<<QString(name)<<QString("INVALID_GEOMETRY_PROVENANCE");
        for(const auto* name:{"root-field","ledger-field","allocation-field","creator-field","ref-field","baseline-field","v9-with-ledger"})QTest::newRow(name)<<QString(name)<<QString("UNSUPPORTED_FIELD");
        QTest::newRow("native-version")<<QString("native-version")<<QString("UNSUPPORTED_VERSION");
        QTest::newRow("untyped-promoted")<<QString("untyped-promoted")<<QString("INVALID_JSON");
    }
    void malformedNativeTenLedgerRejectsBeforeActivation() {
        QFETCH(QString,mutation);QFETCH(QString,expected);auto value=losslessjson::parse(nativeBytes(source()));QVERIFY(value.object.count("geometryProvenance"));auto& ledger=value.object["geometryProvenance"];
        auto& originals=ledger.object["originalArchive"].array;auto& allocations=ledger.object["inlineAllocations"].array;auto& baselines=ledger.object["opaqueBaseline"].array;
        QVERIFY(!allocations.empty());QVERIFY(!baselines.empty());
        if(mutation=="missing")value.object.erase("geometryProvenance");
        else if(mutation=="schema")ledger.object["schemaVersion"]=V::num(2);
        else if(mutation=="duplicate-original")originals.push_back(originals.front());
        else if(mutation=="duplicate-allocation")allocations.push_back(allocations.front());
        else if(mutation=="overlap")originals.push_back(allocations.front().object.at("ref"));
        else if(mutation=="missing-target")originals.push_back(refValue("not-stored"));
        else if(mutation=="creator-domain")allocations.front().object["createdFor"].object["domain"]=V::str("territorial");
        else if(mutation=="creator-id")allocations.front().object["createdFor"].object["id"]=V::str("");
        else if(mutation=="wrong-deterministic-ref")allocations.front().object["createdFor"].object["id"]=V::str("97500000-0000-4000-8000-000000000097");
        else if(mutation=="bad-shape-hash")allocations.front().object["geometrySha256"]=V::str("NOT_A_SHA256");
        else if(mutation=="wrong-shape-hash")allocations.front().object["geometrySha256"]=V::str(std::string(64,'0'));
        else if(mutation=="bad-opaque-hash")baselines.front().object["sha256"]=V::str("BAD");
        else if(mutation=="duplicate-opaque-slot")baselines.push_back(baselines.front());
        else if(mutation=="stale-opaque")value.object["exchangeMetadata"].object["savedAt"]=V::str("changed");
        else if(mutation=="new-opaque")value.object["units"].array.front().object["metadata"].object["new"]=V::str("dependency");
        else if(mutation=="root-field")value.object["mystery"]=V::boolean(true);
        else if(mutation=="ledger-field")ledger.object["mystery"]=V::boolean(true);
        else if(mutation=="allocation-field")allocations.front().object["mystery"]=V::boolean(true);
        else if(mutation=="creator-field")allocations.front().object["createdFor"].object["mystery"]=V::boolean(true);
        else if(mutation=="ref-field")allocations.front().object["ref"].object["mystery"]=V::boolean(true);
        else if(mutation=="baseline-field")baselines.front().object["mystery"]=V::boolean(true);
        else if(mutation=="v9-with-ledger")value.object["version"]=V::num(9);
        else if(mutation=="native-version")value.object["version"]=V::num(11);
        else if(mutation=="untyped-promoted")allocations.front().object["promoted"]=V::str("false");
        Project active;active.replace(projectcodec::decodeWeb(source().encode()));const auto before=projectcodec::encode(active);const auto revision=active.revision();
        const auto error=invalidArgument([&]{active.replace(projectcodec::decode(value.encode()));});QVERIFY2(error.contains(expected),qPrintable("Expected "+expected+", got: "+error));
        QCOMPARE(projectcodec::encode(active),before);QCOMPARE(active.revision(),revision);
    }
    void originalMatchingShapesAndVersionsRemainExact() {
        auto input=source();const auto shape=labelShape(input.object.at("labels").array.front());
        input.object["geometries"].array.push_back(rowValue("a-matching-source",shape,1));input.object["geometries"].array.push_back(rowValue("a-matching-source",shape,2));input.object["geometries"].array.push_back(rowValue("z-matching-source",shape,7));
        auto document=projectcodec::decodeWeb(input.encode());QVERIFY(document.labels.front().geometry==GeometryRef({"a-matching-source",1}));QCOMPARE(document.geometryProvenance.inlineAllocations.size(),std::size_t(2));
        Project project;project.replace(document);QVERIFY(archive(losslessjson::parse(projectcodec::encodeWeb(project.snapshot())))==archive(input));
        document.labels.front().geometry={"a-matching-source",2};project.replace(document);QByteArray result="not published";
        checkError([&]{result=projectcodec::encodeWeb(project.snapshot());},"UNSUPPORTED_WEB_EXPORT: non-territorial geometry reference loss");QCOMPARE(result,QByteArray("not published"));
    }
    void promotedEqualShapeCannotStealAnotherDerivedOwnersRef() {
        auto input=source();auto second=input.object.at("labels").array.front();second.object["id"]=V::str("97500000-0000-4000-8000-000000000099");input.object["labels"].array.push_back(second);
        const auto before=projectcodec::decodeWeb(input.encode());auto candidate=before;GenericFeature adopter;adopter.id="97500000-0000-4000-8000-000000000096";adopter.geometry=before.labels.front().geometry;candidate.genericFeatures.push_back(adopter);
        reconcileGeometryProvenance(before,candidate);QVERIFY(candidate.geometryProvenance.inlineAllocations.at(before.labels.front().geometry).promoted);QVERIFY(!candidate.geometryProvenance.inlineAllocations.at(before.labels.back().geometry).promoted);
        Project project;project.replace(candidate);const auto bytes=projectcodec::encode(project);project.replace(projectcodec::decode(bytes));
        checkError([&]{projectcodec::encodeWeb(project.snapshot());},"UNSUPPORTED_WEB_EXPORT: non-territorial geometry reference loss");QCOMPARE(projectcodec::encode(project),bytes);
    }
    void uncertainNativeReopenNeverGrantsCleanEvidence() {
        const auto before=projectcodec::decodeWeb(source().encode());auto candidate=before;candidate.labels.front().source.details=R"({"newDependency":"retained native geometry"})";
        reconcileGeometryProvenance(before,candidate);QVERIFY(candidate.geometryProvenance.opaqueUncertain);Project project;project.replace(candidate);const auto bytes=projectcodec::encode(project);
        auto reopened=projectcodec::decode(bytes);project.replace(reopened);QCOMPARE(projectcodec::encode(project),bytes);
        checkError([&]{projectcodec::encodeWeb(project.snapshot());},"UNSUPPORTED_WEB_EXPORT: opaque geometry dependencies");
        reopened.geometryProvenance.opaqueUncertain=false;checkError([&]{project.replace(reopened);},"INVALID_GEOMETRY_PROVENANCE");QCOMPARE(projectcodec::encode(project),bytes);
    }
    void opaqueArraysNullAndUnsafeNumberTokensAreNotNormalizedAway_data() {
        QTest::addColumn<QByteArray>("changed");
        QTest::newRow("array-order")<<QByteArray(R"({"items":[null,1,{"n":9007199254740993}]})");
        QTest::newRow("null-presence")<<QByteArray(R"({"items":[1,{"n":9007199254740993}]})");
        QTest::newRow("unsafe-number")<<QByteArray(R"({"items":[1,null,{"n":9007199254740992}]})");
        QTest::newRow("numeric-token")<<QByteArray(R"({"items":[1.0,null,{"n":9007199254740993}]})");
    }
    void opaqueArraysNullAndUnsafeNumberTokensAreNotNormalizedAway() {
        QFETCH(QByteArray,changed);auto input=source();const auto original=losslessjson::parse(R"({"items":[1,null,{"n":9007199254740993}]})");input.object["territorialEntities"].array.front().object["properties"].object["metadata"]=original;
        auto bytes=nativeBytes(input);QVERIFY(bytes.contains("9007199254740993"));auto native=losslessjson::parse(bytes);QCOMPARE(native.object.at("units").array.front().object.at("metadata").encode(),original.encode());
        native.object["units"].array.front().object["metadata"]=losslessjson::parse(changed);
        checkError([&]{projectcodec::decode(native.encode());},"INVALID_GEOMETRY_PROVENANCE: opaque baseline mismatch");
    }
    void deletingOpaqueOwnerAllowsNativeReopenWithExactSerializedLedger() {
        auto input=source();input.object["labelSettings"]=V::obj();auto document=projectcodec::decodeWeb(input.encode());const auto before=document;document.labels.clear();reconcileGeometryProvenance(before,document);QVERIFY(!document.geometryProvenance.opaqueUncertain);
        Project project;project.replace(document);const auto bytes=projectcodec::encode(project);const auto reopened=projectcodec::decode(bytes);QVERIFY(semanticallyEqual(document,reopened));project.replace(reopened);QCOMPARE(projectcodec::encode(project),bytes);
        QVERIFY(archive(losslessjson::parse(projectcodec::encodeWeb(project.snapshot())))==archive(input));
    }

    void nonUuidDeletedCreatorCannotForgeInlineOrigin_data() {
        QTest::addColumn<QString>("invalidId");
        QTest::newRow("non-uuid")<<QString("not-a-web-uuid");
        QTest::newRow("terminal-lf")<<QString("97500000-0000-4000-8000-000000000002\n");
        QTest::newRow("terminal-crlf")<<QString("97500000-0000-4000-8000-000000000002\r\n");
    }
    void nonUuidDeletedCreatorCannotForgeInlineOrigin() {
        QFETCH(QString,invalidId);auto input=source();input.object["labelSettings"]=V::obj();auto native=losslessjson::parse(nativeBytes(input));auto& allocations=native.object["geometryProvenance"].object["inlineAllocations"].array;
        auto allocation=std::find_if(allocations.begin(),allocations.end(),[](const V& row){return row.object.at("createdFor").object.at("domain").string=="label";});QVERIFY(allocation!=allocations.end());
        const auto oldId=allocation->object.at("ref").object.at("id").string;const std::string forged="web-label:"+invalidId.toStdString();
        allocation->object["createdFor"].object["id"]=V::str(invalidId.toStdString());allocation->object["ref"].object["id"]=V::str(forged);
        for(auto& row:native.object["geometries"].array)if(row.object.at("id").string==oldId)row.object["id"]=V::str(forged);
        native.object["content"].object["labels"]=V::arr();
        checkError([&]{projectcodec::decode(native.encode());},"INVALID_GEOMETRY_PROVENANCE: invalid creator UUID");
    }

    void promotedOnlyDecodeCannotSealUncheckedOpaqueBytesForLaterDemotion() {
        auto native=losslessjson::parse(nativeBytes(source()));for(auto& row:native.object["geometryProvenance"].object["inlineAllocations"].array)row.object["promoted"]=V::boolean(true);
        native.object["content"].object["labels"].array.front().object["source"].object["details"]=losslessjson::parse(R"({"new":"unchecked while entirely semantic"})");
        auto document=projectcodec::decode(native.encode());Project active;active.replace(document);const auto before=projectcodec::encode(active);
        for(auto& [ref,allocation]:document.geometryProvenance.inlineAllocations)allocation.promoted=false;
        const auto error=invalidArgument([&]{active.replace(document);});QVERIFY2(error.contains("INVALID_GEOMETRY_PROVENANCE: missing clean evidence"),qPrintable("Unchecked all-promoted decode granted clean evidence: "+error));QCOMPARE(projectcodec::encode(active),before);
    }

    void nativeVersionRebindingKeepsTheOldAllocationDerivedAndDetached() {
        auto input=source();const auto before=projectcodec::decodeWeb(input.encode());auto candidate=before;const auto oldRef=candidate.labels.front().geometry;const GeometryRef nextRef{oldRef.id,2};
        const Geometry nextShape{"Point",{{79,42}},{},{}};candidate.geometries.insert(nextRef,nextShape);candidate.labels.front().geometry=nextRef;reconcileGeometryProvenance(before,candidate);
        QVERIFY(!candidate.geometryProvenance.inlineAllocations.at(oldRef).promoted);QVERIFY(!candidate.geometryProvenance.inlineAllocations.count(nextRef));QVERIFY(!geometryProvenanceUsers(candidate).count(oldRef));
        Project project;project.replace(candidate);QTemporaryDir directory;QVERIFY(directory.isValid());const auto path=directory.filePath("rebound.native.json");writeFile(path,projectcodec::encode(project));project.replace(projectcodec::decode(readFile(path)));
        QVERIFY(project.document().geometries.get(oldRef));QVERIFY(project.document().labels.front().geometry==nextRef);QVERIFY(!project.document().geometryProvenance.inlineAllocations.at(oldRef).promoted);
        auto expected=archive(input);expected.emplace(refValue(nextRef.id,nextRef.version).encode(),losslessjson::parse(R"({"type":"Point","coordinates":[79,42]})").encode());
        const auto bytes=projectcodec::encodeWeb(project.snapshot());QVERIFY(archive(losslessjson::parse(bytes))==expected);const auto restored=projectcodec::decodeWeb(bytes);QVERIFY(restored.labels.front().geometry==nextRef);QVERIFY(!restored.geometries.get(oldRef));
    }
    void absentAllocationRecordsDefaultToSemanticWithoutPrefixInference() {
        const auto bytes=nativeBytes(source());auto native=losslessjson::parse(bytes);native.object["geometryProvenance"].object["inlineAllocations"]=V::arr();
        Project project;project.replace(projectcodec::decode(native.encode()));QVERIFY(project.document().geometryProvenance.inlineAllocations.empty());QVERIFY(archive(losslessjson::parse(projectcodec::encodeWeb(project.snapshot())))==archive(native));
        auto document=project.document();document.geometryProvenance=GeometryProvenance{};project.replace(document);const auto saved=projectcodec::encode(project);project.replace(projectcodec::decode(saved));QVERIFY(archive(losslessjson::parse(projectcodec::encodeWeb(project.snapshot())))==archive(native));
    }
    void equalNewNativeRowDoesNotPromoteOrRebindDerivedGeometry() {
        const auto before=projectcodec::decodeWeb(source().encode());auto candidate=before;const auto ref=candidate.labels.front().geometry;
        candidate.geometries.insert({"a-native-equal",1},*candidate.geometries.get(ref));reconcileGeometryProvenance(before,candidate);QVERIFY(!candidate.geometryProvenance.inlineAllocations.at(ref).promoted);
        Project project;project.replace(candidate);const auto bytes=projectcodec::encode(project);checkError([&]{projectcodec::encodeWeb(project.snapshot());},"UNSUPPORTED_WEB_EXPORT: non-territorial geometry reference loss");QCOMPARE(projectcodec::encode(project),bytes);
    }

    void terminalNewlineInlineCreatorRejectsBeforeWebCandidateActivation() {
        auto input=source();input.object["labelSettings"]=V::obj();auto& label=input.object["labels"].array.front();label.object["id"]=V::str(label.object.at("id").string+"\n");
        Project active;active.replace(projectcodec::decodeWeb(source().encode()));const auto before=projectcodec::encode(active);const auto revision=active.revision();
        const auto error=invalidArgument([&]{active.replace(projectcodec::decodeWeb(input.encode()));});
        QVERIFY2(error.contains("INVALID_GEOMETRY_PROVENANCE: invalid creator UUID"),qPrintable("Malformed newly allocated provenance activated: "+error));
        QCOMPARE(projectcodec::encode(active),before);QCOMPARE(active.revision(),revision);
    }
};
QTEST_GUILESS_MAIN(M975GeometryProvenanceCodecTests)
#include "m975_geometry_provenance_codec_tests.moc"

#include "editorcontroller.h"
#include "projectgeopackage.h"
#include "webimport.h"
#include "autosavecoordinator.h"
#include "territorial_fixture.h"
#include "defaultflagresolver.h"
#include <pandoeditor/commands.h>
#include <pandoeditor/presentationcommands.h>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>
#include <QSet>
#include <future>
#include <functional>
#include <map>
#include <algorithm>

using namespace pandoeditor;
namespace {
QByteArray read(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("fixture read");return file.readAll();}
void write(const QString& path,const QByteArray& bytes){QFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size())throw std::runtime_error("fixture write");}
class PackageDatabase {
public:
    const QString name=QUuid::createUuid().toString();QSqlDatabase db;
    explicit PackageDatabase(const QString& path) {db=QSqlDatabase::addDatabase("QSQLITE",name);db.setDatabaseName(path);if(!db.open())throw std::runtime_error("package database open");}
    ~PackageDatabase(){db.close();db={};QSqlDatabase::removeDatabase(name);}
};
std::map<QString,QByteArray> packageRows(const QString& path) {
    PackageDatabase connection(path);QSqlQuery query(connection.db);std::map<QString,QByteArray> rows;
    for(const auto& pair:{std::pair{"project_state","SELECT json_value FROM pandolab_project_settings WHERE setting_key='project_state'"},
                         std::pair{"source","SELECT json_value FROM pandolab_source_info WHERE info_key='source'"},
                         std::pair{"asset_count","SELECT COUNT(*) FROM pandolab_country_assets"}}) {
        if(!query.exec(pair.second)||!query.next())throw std::runtime_error("package row missing");rows[pair.first]=query.value(0).toString().toUtf8();
    }return rows;
}
void replacePackageRow(const QString& path,const QString& key,const QByteArray& bytes) {
    PackageDatabase connection(path);QSqlQuery query(connection.db);
    const auto statement=key=="source"?"UPDATE pandolab_source_info SET json_value=? WHERE info_key='source'":"UPDATE pandolab_project_settings SET json_value=? WHERE setting_key='project_state'";
    if(!query.prepare(statement))throw std::runtime_error("package row prepare");query.addBindValue(QString::fromUtf8(bytes));
    if(!query.exec()||query.numRowsAffected()!=1)throw std::runtime_error("package row update");
}
QString packageError(const QString& path) {try{readProjectGeoPackage(path);}catch(const std::invalid_argument& error){return QString::fromUtf8(error.what());}return {};}
ProjectDocument fixture(const char* kind){return webimport::prepare(read(QStringLiteral(PANDOEDITOR_EXCHANGE_FIXTURES)+"/"+kind+".json")).document;}
QByteArray inlineOnlyWebFixture() {
    auto root=QJsonDocument::fromJson(read(QStringLiteral(PANDOEDITOR_EXCHANGE_FIXTURES)+"/content.json")).object();
    // This test input deliberately stores these two shapes only inline. The
    // original content fixture archives both, so it cannot exercise derivation.
    const QSet<QString> inlineOnly{"web-label:22000000-0000-4000-8000-000000000001","web-genericFeatures:22000000-0000-4000-8000-000000000002"};
    QJsonArray archive;int removed=0;
    for(const auto& row:root["geometries"].toArray()) {if(inlineOnly.contains(row.toObject()["id"].toString()))++removed;else archive.append(row);}
    if(removed!=2)throw std::runtime_error("inline-only test input changed");root["geometries"]=archive;return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
ProjectDocument derivedFixture(){return webimport::prepare(inlineOnlyWebFixture()).document;}
const std::string contentId="97050000-0000-4000-8000-000000000001";
const std::string distributionLayerId="97050000-0000-4000-8000-000000000002";
Geometry inlineShape(const QString& domain,double offset=0) {
    if(domain=="hydroEdits")return {"LineString",{},{{{31+offset,41},{32+offset,42}}},{}};
    if(domain=="distributionEntry")return {"Polygon",{},{},{{{{31+offset,41},{32+offset,41},{32+offset,42},{31+offset,41}}}}};
    return {"Point",{{31+offset,41}},{},{}};
}
void addInlineContent(ProjectDocument& document,const QString& domain,const GeometryRef& geometry) {
    if(domain=="label") {PlaceLabel value;value.id=contentId;value.name="Exchange label";value.geometry=geometry;document.labels.push_back(value);}
    else if(domain=="hydroEdits") {HydroFeature value;value.id=contentId;value.name="Exchange river";value.geometry=geometry;document.hydro.push_back(value);}
    else if(domain=="genericFeatures") {GenericFeature value;value.id=contentId;value.name="Exchange fallback";value.geometry=geometry;document.genericFeatures.push_back(value);}
    else {DistributionLayer layer;layer.id=distributionLayerId;layer.name="Exchange distribution";document.distributionLayers.push_back(layer);DistributionEntry value;value.id=contentId;value.layerId=layer.id;value.geometry=geometry;document.distributionEntries.push_back(value);}
}
GeometryRef inlineContentRef(const ProjectDocument& document,const QString& domain) {
    if(domain=="label")return document.labels.front().geometry;
    if(domain=="hydroEdits")return document.hydro.front().geometry;
    if(domain=="genericFeatures")return document.genericFeatures.front().geometry;
    return *document.distributionEntries.front().geometry;
}
void verifyStoredColorHistory(Project& project,const QByteArray& baseline,const QByteArray& firstEdit,const QByteArray& secondEdit) {
    QVERIFY(project.undo());QCOMPARE(projectcodec::encode(project),baseline);
    QVERIFY(project.redo());QCOMPARE(projectcodec::encode(project),firstEdit);
    QVERIFY(project.redo());QCOMPARE(projectcodec::encode(project),secondEdit);
    QVERIFY(project.undo());QCOMPARE(projectcodec::encode(project),firstEdit);
    QVERIFY(project.canUndo());QVERIFY(project.canRedo());
}
void visibilityBoundaryData() {
    QTest::addColumn<QString>("key");QTest::addColumn<bool>("item");
    for(const auto* key:{"hydro","terrain","countryLabels"})QTest::newRow((QByteArray("layer-")+key).constData())<<QString(key)<<false;
    for(const auto* key:{"rivers","lakes","terrain"})QTest::newRow((QByteArray("item-")+key).constData())<<QString(key)<<true;
}
}
class TimelineEditorPersistenceTests final:public QObject {
 Q_OBJECT
private slots:
 void webItemVisibilityPreservesExactGroupPresence_data() {
    QTest::addColumn<QJsonObject>("visibility");
    QJsonObject all;
    for(const auto* group:{"countries","subunits","regions","distributions","hydro","genericFeatures","labels","countryLabels"}) {
        all[group]=QJsonObject{};QTest::newRow(group)<<QJsonObject{{group,QJsonObject{}}};
    }
    QTest::newRow("absent-groups")<<QJsonObject{};
    QTest::newRow("all-empty")<<all;
    all["countries"]=QJsonObject{{"A",false}};QTest::newRow("empty-and-hidden")<<all;
    QTest::newRow("partial-groups")<<QJsonObject{{"countries",QJsonObject{{"A",false}}},{"labels",QJsonObject{}}};
 }
 void webItemVisibilityPreservesExactGroupPresence() {
    QFETCH(QJsonObject,visibility);
    auto input=QJsonDocument::fromJson(read(QStringLiteral(PANDOEDITOR_EXCHANGE_FIXTURES)+"/static.json")).object();input["itemVisibility"]=visibility;
    auto document=projectcodec::decodeWeb(QJsonDocument(input).toJson());
    QCOMPARE(document.presentation.webPresentation.hiddenItems.size(),std::size_t(visibility.size()));
    normalizePresentation(document);
    QCOMPARE(document.presentation.webPresentation.hiddenItems.size(),std::size_t(visibility.size()));
    Project project;project.replace(std::move(document));
    for(int cycle=0;cycle<2;++cycle) {
        const auto native=projectcodec::encode(project);project.replace(projectcodec::decode(native));QCOMPARE(projectcodec::encode(project),native);
        const auto web=projectcodec::encodeWeb(project.snapshot());QCOMPARE(QJsonDocument::fromJson(web).object(),input);
        project.replace(projectcodec::decodeWeb(web));
    }
 }
 void nativeItemVisibilityPreservesEmptyGroups_data() {
    QTest::addColumn<int>("version");QTest::newRow("native-nine")<<9;QTest::newRow("native-ten")<<10;
 }
 void nativeItemVisibilityPreservesEmptyGroups() {
    QFETCH(int,version);Project source;source.replace(fixture("static"));auto input=QJsonDocument::fromJson(projectcodec::encode(source)).object();
    input["version"]=version;if(version==9)input.remove("geometryProvenance");
    QJsonObject hidden;for(const auto* group:{"countries","subunits","regions","distributions","hydro","genericFeatures","labels","countryLabels"})hidden[group]=QJsonArray{};
    auto presentation=input["presentation"].toObject(),web=presentation["webPresentation"].toObject();web["hiddenItems"]=hidden;presentation["webPresentation"]=web;input["presentation"]=presentation;
    Project restored;restored.replace(projectcodec::decode(QJsonDocument(input).toJson()));
    QCOMPARE(restored.document().presentation.webPresentation.hiddenItems.size(),std::size_t(hidden.size()));
    const auto output=QJsonDocument::fromJson(projectcodec::encode(restored)).object();
    QCOMPARE(output["presentation"].toObject()["webPresentation"].toObject()["hiddenItems"].toObject(),hidden);
 }
 void emptyVisibilityGroupsSurviveEditorHistoryAutosaveAndGeoPackage() {
    QTemporaryDir dir;QVERIFY(dir.isValid());auto input=QJsonDocument::fromJson(read(QStringLiteral(PANDOEDITOR_EXCHANGE_FIXTURES)+"/static.json")).object();
    QJsonObject visibility;for(const auto* group:{"countries","subunits","regions","distributions","hydro","genericFeatures","labels","countryLabels"})visibility[group]=QJsonObject{};
    input["itemVisibility"]=visibility;const auto webPath=dir.filePath("input.web.json"),nativePath=dir.filePath("saved.pando.json");write(webPath,QJsonDocument(input).toJson());
    EditorController editor({false,dir.filePath("private.json")});QVERIFY(editor.prepareWebImport(QUrl::fromLocalFile(webPath)));QTRY_VERIFY(editor.hasWebImportPreview());QVERIFY(editor.confirmWebImport(editor.webImportHash(),"discard"));
    const auto baseline=editor.documentBytes();editor.selectCountry("A");editor.setColor("#123456");editor.undo();QCOMPARE(editor.documentBytes(),baseline);editor.redo();
    QVERIFY(editor.saveFile(QUrl::fromLocalFile(nativePath)));EditorController reopened({false,dir.filePath("private-reopened.json")});QVERIFY(reopened.openFile(QUrl::fromLocalFile(nativePath)));QCOMPARE(reopened.documentBytes(),editor.documentBytes());
    Project project;project.replace(projectcodec::decode(reopened.documentBytes()));QCOMPARE(QJsonDocument::fromJson(projectcodec::encodeWeb(project.snapshot())).object()["itemVisibility"].toObject(),visibility);
    ProjectAutosave autosave(dir.filePath("autosave.json"),dir.filePath("view.json"));autosave.scheduleDocument(project.snapshot());QVERIFY(autosave.flushNow());
    Project recovered;recovered.replace(projectcodec::decode(autosave.restoreDocument()));QCOMPARE(projectcodec::encode(recovered),projectcodec::encode(project));
    const auto packagePath=dir.filePath("saved.gpkg");write(packagePath,exportProjectGeoPackage(project));recovered.replace(projectcodec::decode(readProjectGeoPackage(packagePath)));QCOMPARE(projectcodec::encode(recovered),projectcodec::encode(project));
    QCOMPARE(QJsonDocument::fromJson(projectcodec::encodeWeb(recovered.snapshot())).object()["itemVisibility"].toObject(),visibility);
 }
 void webObjectOrderRetainsDeletedRankHintsWithoutWeakeningTypedReferences() {
    auto input=QJsonDocument::fromJson(read(QStringLiteral(PANDOEDITOR_EXCHANGE_FIXTURES)+"/static.json")).object();auto layer=input["layerPresentation"].toObject();
    const QJsonArray order{"territorial:entity:deleted-first","territorial:entity:A","territorial:entity:deleted-last"};layer["objectOrder"]=order;input["layerPresentation"]=layer;
    Project project;QString error;try{project.replace(projectcodec::decodeWeb(QJsonDocument(input).toJson()));}catch(const std::invalid_argument& failure){error=QString::fromUtf8(failure.what());}
    QVERIFY2(error.isEmpty(),qPrintable(error));
    QCOMPARE(territorialRenderOrder(project.document(),territorialRef("A")),-1000.+10.+1./4.);
    for(int cycle=0;cycle<2;++cycle) {
        const auto bytes=projectcodec::encode(project);project.replace(projectcodec::decode(bytes));QCOMPARE(projectcodec::encode(project),bytes);
        const auto web=projectcodec::encodeWeb(project.snapshot());QCOMPARE(QJsonDocument::fromJson(web).object(),input);project.replace(projectcodec::decodeWeb(web));
    }
    auto normalized=project.document();normalizePresentation(normalized);
    QCOMPARE(normalized.presentation.webPresentation.objectOrder,std::vector<std::string>{"territorial:entity:A"});
    for(const auto& invalid: {QJsonArray{"territorial:entity:A","territorial:entity:A"},QJsonArray{"territorial:country:A"},QJsonArray{"territorial:entity:"},QJsonArray{42}}) {
        auto bad=input;auto presentation=layer;presentation["objectOrder"]=invalid;bad["layerPresentation"]=presentation;
        QVERIFY_EXCEPTION_THROWN(projectcodec::decodeWeb(QJsonDocument(bad).toJson()),std::invalid_argument);
    }
    auto bad=input;layer["objectStyles"]=QJsonObject{{"territorial:entity:deleted-first",QJsonObject{{"opacity",0.5}}}};bad["layerPresentation"]=layer;
    QVERIFY_EXCEPTION_THROWN(projectcodec::decodeWeb(QJsonDocument(bad).toJson()),std::invalid_argument);
    bad=input;bad["labelSettings"]=QJsonObject{{"territorial:deleted-first",QJsonObject{}}};QVERIFY_EXCEPTION_THROWN(projectcodec::decodeWeb(QJsonDocument(bad).toJson()),std::invalid_argument);
 }
 void webOverlayOrderPreservesOptionalPresence_data() {
    QTest::addColumn<QString>("kind");QTest::addColumn<int>("presence");
    for(const auto* kind:{"static","complex"})for(int presence=0;presence<3;++presence)QTest::newRow((QByteArray(kind)+QByteArray::number(presence)).constData())<<QString(kind)<<presence;
 }
 void savedRankHintsDoNotTurnNoopsIntoEdits_data() {
    QTest::addColumn<bool>("presentation");QTest::newRow("presentation")<<true;QTest::newRow("content")<<false;
 }
 void savedRankHintsDoNotTurnNoopsIntoEdits() {
    QFETCH(bool,presentation);
    auto document=fixture("static");document.presentation.webPresentation.objectOrder={"territorial:entity:deleted","territorial:entity:A"};
    Project project;project.replace(document);const auto before=projectcodec::encode(project);
    if(presentation)QCOMPARE(PresentationCommandProcessor::apply(project,SetBatchVisibility{{territorialRef("A")},true}),PresentationResult::NoOp);
    else QVERIFY(!project.setColor("A",project.country("A")->color));
    QCOMPARE(projectcodec::encode(project),before);QVERIFY(!project.dirty());QCOMPARE(project.presentationRevision(),std::uint64_t(0));QVERIFY(!project.canUndo());
    QVERIFY(project.setColor("A",0x123456));QCOMPARE(project.document().presentation.webPresentation.objectOrder,std::vector<std::string>{"territorial:entity:A"});
 }
 void savedRankHintsKeepDeliberateNativeCheckpoints_data() {
    QTest::addColumn<bool>("nameCheckpoint");QTest::newRow("batch-color")<<false;QTest::newRow("fallback-name")<<true;
 }
 void savedRankHintsKeepDeliberateNativeCheckpoints() {
    QFETCH(bool,nameCheckpoint);auto document=fixture("static");document.presentation.webPresentation.objectOrder={"territorial:entity:deleted","territorial:entity:A"};
    if(nameCheckpoint){document.units.front().name.clear();document.units.front().nameExplicit=true;document.units.front().baseName="Fallback";}
    Project project;project.replace(document);CommandArguments args;
    if(nameCheckpoint)args.action=TerritorialFieldEdit{territorialRef("A"),TerritorialField::Name,""};
    else args.action=TerritorialColorEdit{{territorialRef("A"),territorialRef("B")},project.country("A")->color};
    QString error;const auto apply=[&](){auto prepared=CommandProcessor::prepare(project,CommandProcessor::makeRequest(project,nameCheckpoint?"territorial.field":"territorial.batch-color",args));if(!prepared.preview){error=QString::fromStdString(prepared.detail);return false;}const auto result=CommandProcessor::confirm(project,*prepared.preview);error=QString::fromStdString(result.detail);return result.changed();};
    QVERIFY2(apply(),qPrintable(error));QCOMPARE(project.document().presentation.webPresentation.objectOrder,std::vector<std::string>{"territorial:entity:A"});
    const auto before=projectcodec::encode(project);project.markSaved();const auto revision=project.revision();QVERIFY2(apply(),qPrintable(error));QCOMPARE(projectcodec::encode(project),before);QCOMPARE(project.revision(),revision+1);QVERIFY(project.dirty());QVERIFY(project.canUndo());
 }
 void webOverlayOrderPreservesOptionalPresence() {
    QFETCH(QString,kind);QFETCH(int,presence);QTemporaryDir dir;QVERIFY(dir.isValid());
    auto input=QJsonDocument::fromJson(read(QStringLiteral(PANDOEDITOR_EXCHANGE_FIXTURES)+"/"+kind+".json")).object();auto layer=input["layerPresentation"].toObject();
    if(presence==0)layer.remove("overlayOrder");else layer["overlayOrder"]=presence==1?QJsonArray{}:QJsonArray{"regions","subunits","distributions","genericFeatures"};input["layerPresentation"]=layer;
    Project project;project.replace(projectcodec::decodeWeb(QJsonDocument(input).toJson()));
    QCOMPARE(project.document().presentation.webPresentation.overlayOrderPresent,presence!=0);
    for(int cycle=0;cycle<2;++cycle) {
        const auto native=projectcodec::encode(project);const auto path=dir.filePath("saved.pando.json");write(path,native);project.replace(projectcodec::decode(read(path)));QCOMPARE(projectcodec::encode(project),native);
        QCOMPARE(QJsonDocument::fromJson(native).object()["presentation"].toObject()["webPresentation"].toObject().contains("overlayOrder"),presence!=0);
        const auto web=projectcodec::encodeWeb(project.snapshot());QCOMPARE(QJsonDocument::fromJson(web).object(),input);project.replace(projectcodec::decodeWeb(web));
    }
    const auto path=dir.filePath("saved.gpkg");write(path,exportProjectGeoPackage(project));project.replace(projectcodec::decode(readProjectGeoPackage(path)));
    QCOMPARE(QJsonDocument::fromJson(projectcodec::encodeWeb(project.snapshot())).object(),input);
 }
 void nativeVersionClassificationAndSaveNotice() {
    for(int version:{9,10}) {
        const auto bytes=QJsonDocument(QJsonObject{{"format","pandoeditor-project"},{"version",version}}).toJson();
        QString error;try{QCOMPARE(webimport::classify(bytes),webimport::FileKind::QtProject);}
        catch(const std::invalid_argument& failure){error=QString::fromUtf8(failure.what());}
        QVERIFY2(error.isEmpty(),qPrintable(error));
    }
    for(const auto version:{QJsonValue(8),QJsonValue(11),QJsonValue("10"),QJsonValue(10.5),QJsonValue()}) {
        const auto bytes=QJsonDocument(QJsonObject{{"format","pandoeditor-project"},{"version",version}}).toJson();
        QVERIFY_EXCEPTION_THROWN(webimport::classify(bytes),std::invalid_argument);
    }
 }
 void nativeSaveCompatibilityNotice() {
    EditorController editor;
    QVERIFY(editor.documentNotice().contains("Qt v10"));
    QVERIFY(editor.documentNotice().contains(QStringLiteral("이전 앱에서는 열 수 없습니다")));
 }
 void webImportPreviewNamesNative10AndWeb9() {
    QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("web.json");
    write(path,read(QStringLiteral(PANDOEDITOR_EXCHANGE_FIXTURES)+"/static.json"));
    EditorController editor({false,dir.filePath("private.json")});
    QVERIFY(editor.prepareWebImport(QUrl::fromLocalFile(path)));QTRY_VERIFY(editor.hasWebImportPreview());
    QVERIFY(editor.webImportSummary().contains(QStringLiteral("웹 v9 → 앱 v10")));
 }
 void existingNative9FilesRemainUnchangedUntilExplicitSave() {
    QTemporaryDir dir;QVERIFY(dir.isValid());
    for(const auto* name:{"static","content"}) {
        const auto old=read(QStringLiteral(PANDOEDITOR_EXCHANGE_FIXTURES)+"/../native-v9/"+name+".pando.json");
        const auto oldRoot=QJsonDocument::fromJson(old).object();QCOMPARE(oldRoot["version"].toInt(),9);QVERIFY(!oldRoot.contains("geometryProvenance"));
        const auto source=dir.filePath(QString(name)+"-v9.json"),saved=dir.filePath(QString(name)+"-v10.json");write(source,old);
        EditorController editor({false,dir.filePath(QString(name)+"-private.json")});QVERIFY(editor.openFile(QUrl::fromLocalFile(source)));
        QCOMPARE(read(source),old);QVERIFY(!editor.dirty());
        QVERIFY(editor.saveFile(QUrl::fromLocalFile(saved)));const auto migrated=QJsonDocument::fromJson(read(saved)).object();
        QCOMPARE(migrated["version"].toInt(),10);QCOMPARE(migrated["geometries"],oldRoot["geometries"]);
        const auto ledger=migrated["geometryProvenance"].toObject();QCOMPARE(ledger["originalArchive"].toArray().size(),oldRoot["geometries"].toArray().size());
        QVERIFY(ledger["inlineAllocations"].toArray().isEmpty());QVERIFY(!ledger["opaqueUncertain"].toBool());
        QCOMPARE(read(source),old);EditorController reopened({false,dir.filePath(QString(name)+"-reopened-private.json")});
        QVERIFY(reopened.openFile(QUrl::fromLocalFile(saved)));QCOMPARE(reopened.documentBytes(),read(saved));
    }
 }
 void immutableSnapshotWorkerAutosaveRetainsNative10LedgerAndReads9() {
    QTemporaryDir dir;QVERIFY(dir.isValid());Project project;project.replace(derivedFixture());
    const auto frozen=project.snapshot();const auto expected=projectcodec::encode(frozen);const auto root=QJsonDocument::fromJson(expected).object();
    QCOMPARE(root["version"].toInt(),10);QVERIFY(!root["geometryProvenance"].toObject()["inlineAllocations"].toArray().isEmpty());
    ProjectAutosave autosave(dir.filePath("autosave.json"),dir.filePath("view.json"));QSignalSpy saved(&autosave,&ProjectAutosave::saved),failed(&autosave,&ProjectAutosave::saveFailed);
    autosave.scheduleDocument(frozen);QVERIFY(project.setMemo("A","edited after queued snapshot"));
    // Let the production timer dispatch QtConcurrent::run; flushNow alone would
    // exercise the synchronous fallback rather than the immutable worker path.
    QTRY_COMPARE_WITH_TIMEOUT(saved.count(),1,5000);QCOMPARE(failed.count(),0);
    const auto envelope=QJsonDocument::fromJson(read(autosave.projectPath())).object();
    QCOMPARE(envelope["format"].toString(),QString("pandoeditor-autosave"));QCOMPARE(envelope["version"].toInt(),1);
    QCOMPARE(QByteArray::fromBase64(envelope["project"].toString().toLatin1()),expected);
    QCOMPARE(autosave.restoreDocument(),expected);Project restored;restored.replace(projectcodec::decode(autosave.restoreDocument()));
    QCOMPARE(projectcodec::encode(restored),expected);QCOMPARE(projectcodec::encode(frozen),expected);QVERIFY(projectcodec::encode(project)!=expected);
    const auto old=read(QStringLiteral(PANDOEDITOR_EXCHANGE_FIXTURES)+"/../native-v9/content.pando.json");
    autosave.scheduleDocument(old);QTRY_COMPARE_WITH_TIMEOUT(saved.count(),2,5000);QCOMPARE(failed.count(),0);QCOMPARE(autosave.restoreDocument(),old);
    Project migrated;migrated.replace(projectcodec::decode(autosave.restoreDocument()));const auto migratedRoot=QJsonDocument::fromJson(projectcodec::encode(migrated)).object();
    QCOMPARE(migratedRoot["version"].toInt(),10);QVERIFY(migratedRoot["geometryProvenance"].toObject()["inlineAllocations"].toArray().isEmpty());
    QCOMPARE(migratedRoot["geometries"],QJsonDocument::fromJson(old).object()["geometries"]);
 }
 void inlineAllocationFatesSurviveFilesAutosaveAndGeoPackage_data() {
    QTest::addColumn<QString>("fate");
    for(const auto* fate:{"owner-deleted","owner-rebound","promoted-then-deleted"})QTest::newRow(fate)<<QString(fate);
 }
 void inlineAllocationFatesSurviveFilesAutosaveAndGeoPackage() {
    QFETCH(QString,fate);QTemporaryDir dir;QVERIFY(dir.isValid());Project project;project.replace(derivedFixture());
    const auto initial=project.snapshot();auto label=project.document().labels.front();const auto oldRef=label.geometry;
    const auto apply=[&](ContentEdit edit) {
        CommandArguments args;args.action=std::move(edit);auto prepared=CommandProcessor::prepare(project,CommandProcessor::makeRequest(project,"content.edit",args));
        if(!prepared.ok())return prepared.detail;const auto result=CommandProcessor::confirm(project,*prepared.preview);return result.changed()?std::string{}:(result.detail.empty()?std::string("content edit did not apply"):result.detail);
    };
    if(fate=="promoted-then-deleted") {
        PlaceLabel adopter;adopter.id="22000000-0000-4000-8000-000000000009";adopter.name="Native adopter";adopter.geometry=oldRef;
        QVERIFY(apply({{"label",adopter.id},adopter,{},true}).empty());
        QVERIFY(project.document().geometryProvenance.inlineAllocations.at(oldRef).promoted);
        QVERIFY(apply({{"label",adopter.id},{},{}}).empty());
    }
    const auto beforeLast=projectcodec::encode(project);
    if(fate=="owner-rebound") {
        label.geometry={oldRef.id,2};Geometry replacement{"Point",{{126,36}},{},{}};
        QVERIFY(apply({{"label",label.id},label,std::make_pair(label.geometry,replacement)}).empty());
    } else QVERIFY(apply({{"label",label.id},{},{}}).empty());
    const auto expected=projectcodec::encode(project);QVERIFY(project.document().geometries.get(oldRef));
    QCOMPARE(project.document().geometryProvenance.inlineAllocations.at(oldRef).promoted,fate=="promoted-then-deleted");
    QVERIFY(project.undo());QCOMPARE(projectcodec::encode(project),beforeLast);QVERIFY(project.redo());QCOMPARE(projectcodec::encode(project),expected);
    const auto file=dir.filePath("ownership.json");write(file,expected);Project reopened;reopened.replace(projectcodec::decode(read(file)));QCOMPARE(projectcodec::encode(reopened),expected);
    EditorController editor({false,dir.filePath("private.json")});QVERIFY(editor.openFile(QUrl::fromLocalFile(file)));QCOMPARE(editor.documentBytes(),expected);
    ProjectAutosave autosave(dir.filePath("autosave.json"),dir.filePath("view.json"));autosave.scheduleDocument(project.snapshot());QVERIFY(autosave.flushNow());
    Project recovered;recovered.replace(projectcodec::decode(autosave.restoreDocument()));QCOMPARE(projectcodec::encode(recovered),expected);
    const auto gpkg=dir.filePath("ownership.gpkg");write(gpkg,exportProjectGeoPackage(project));Project packaged;packaged.replace(projectcodec::decode(readProjectGeoPackage(gpkg)));QCOMPARE(projectcodec::encode(packaged),expected);
    const auto web=QJsonDocument::fromJson(projectcodec::encodeWeb(reopened.snapshot())).object();QCOMPARE(web["schemaVersion"].toInt(),9);
    bool emitted=false;for(const auto& row:web["geometries"].toArray())if(row.toObject()["id"].toString()==QString::fromStdString(oldRef.id)&&row.toObject()["version"].toInt()==int(oldRef.version))emitted=true;
    QCOMPARE(emitted,fate=="promoted-then-deleted");
    for(const auto& [ref,geometry]:initial.document().geometries.versions())QVERIFY(project.document().geometries.get(ref)==geometry);
 }
 void historicalBindingPromotionPersistsWithoutRelaxingEditorActivation() {
    QTemporaryDir dir;QVERIFY(dir.isValid());
    auto web=QJsonDocument::fromJson(read(QStringLiteral(PANDOEDITOR_EXCHANGE_FIXTURES)+"/complex.json")).object();
    auto generic=QJsonDocument::fromJson(inlineOnlyWebFixture()).object()["genericFeatures"].toArray().first().toObject();
    generic["geometry"]=QJsonObject{{"type","Polygon"},{"coordinates",QJsonArray{QJsonArray{QJsonArray{30,30},QJsonArray{32,30},QJsonArray{32,32},QJsonArray{30,30}}}}};
    auto properties=generic["properties"].toObject(),source=properties["source"].toObject();source["sourceType"]="Polygon";properties["source"]=source;generic["properties"]=properties;
    web["genericFeatures"]=QJsonArray{generic};Project imported;imported.replace(projectcodec::decodeWeb(QJsonDocument(web).toJson()));
    QVERIFY(!isStaticTimeline(imported.document()));auto candidate=imported.document();const auto ref=candidate.genericFeatures.front().geometry;
    QVERIFY(!candidate.geometryProvenance.inlineAllocations.at(ref).promoted);
    auto historical=std::find_if(candidate.timelineRecords.geometryBindings.begin(),candidate.timelineRecords.geometryBindings.end(),[](const auto& binding){return binding.validity.to.has_value();});
    QVERIFY(historical!=candidate.timelineRecords.geometryBindings.end());historical->geometryRef=ref;
    // Exercise the shared transition on rich storage, not the static-only UI
    // command entry point. Noncurrent typed adoption must still be semantic.
    reconcileGeometryProvenance(imported.document(),candidate);QVERIFY(candidate.geometryProvenance.inlineAllocations.at(ref).promoted);
    Project rich;rich.replace(candidate);const auto expected=projectcodec::encode(rich);const auto path=dir.filePath("rich.json");write(path,expected);
    Project reopened;reopened.replace(projectcodec::decode(read(path)));QCOMPARE(projectcodec::encode(reopened),expected);QVERIFY(!isStaticTimeline(reopened.document()));
    ProjectAutosave autosave(dir.filePath("autosave.json"),dir.filePath("view.json"));autosave.scheduleDocument(rich.snapshot());QVERIFY(autosave.flushNow());QCOMPARE(autosave.restoreDocument(),expected);
    const auto package=dir.filePath("rich.gpkg");write(package,exportProjectGeoPackage(rich));Project packaged;packaged.replace(projectcodec::decode(readProjectGeoPackage(package)));QCOMPARE(projectcodec::encode(packaged),expected);
    EditorController editor({false,dir.filePath("private.json")});const auto before=editor.documentBytes();const auto instance=editor.projectInstanceId();
    QVERIFY(!editor.openFile(QUrl::fromLocalFile(path)));QCOMPARE(editor.documentBytes(),before);QCOMPARE(editor.projectInstanceId(),instance);
    QVERIFY(!editor.openProjectGeoPackage(QUrl::fromLocalFile(package)));QCOMPARE(editor.documentBytes(),before);QCOMPARE(editor.projectInstanceId(),instance);
 }
 void nativeGeoPackageVersionAndMarkerAgreement() {
    QTemporaryDir dir;QVERIFY(dir.isValid());Project source;source.replace(derivedFixture());const auto expected=projectcodec::encode(source);
    const auto path=dir.filePath("native10.gpkg");write(path,exportProjectGeoPackage(source));
    const auto rows=packageRows(path);const auto root=QJsonDocument::fromJson(rows.at("project_state")).object();
    QCOMPARE(root["version"].toInt(),10);QCOMPARE(QJsonDocument::fromJson(rows.at("source")).object(),(QJsonObject{{"format","pandoeditor-project"},{"version",10}}));
    QCOMPARE(root["geometryProvenance"],QJsonDocument::fromJson(expected).object()["geometryProvenance"]);
    QCOMPARE(root["geometries"],QJsonDocument::fromJson(expected).object()["geometries"]);QVERIFY(rows.at("asset_count")=="1");
    Project restored;restored.replace(projectcodec::decode(readProjectGeoPackage(path)));QCOMPARE(projectcodec::encode(restored),expected);
    const auto web=dir.filePath("web-inline-only.gpkg");QVERIFY(QFile::copy(QStringLiteral(PANDOEDITOR_EXCHANGE_FIXTURES)+"/content.gpkg",web));
    replacePackageRow(web,"project_state",inlineOnlyWebFixture());Project imported;imported.replace(projectcodec::decode(readProjectGeoPackage(web)));
    QCOMPARE(projectcodec::encode(imported),expected);
    const auto wrong10=dir.filePath("native10-marker9.gpkg");QVERIFY(QFile::copy(path,wrong10));
    replacePackageRow(wrong10,"source",QJsonDocument(QJsonObject{{"format","pandoeditor-project"},{"version",9}}).toJson());
    QCOMPARE(packageError(wrong10),QString("PROJECT_GPKG_SOURCE_MISMATCH"));
    // Reuse the genuine v9 writer's payload, never strip provenance from v10.
    // Its static vectors have the same semantic document and no spooled assets.
    const auto old=read(QStringLiteral(PANDOEDITOR_EXCHANGE_FIXTURES)+"/../native-v9/static.pando.json");Project oldProject;oldProject.replace(projectcodec::decode(old));
    const auto oldPath=dir.filePath("native9.gpkg");write(oldPath,exportProjectGeoPackage(oldProject));
    replacePackageRow(oldPath,"project_state",old);replacePackageRow(oldPath,"source",QJsonDocument(QJsonObject{{"format","pandoeditor-project"},{"version",9}}).toJson());
    const auto oldBytes=read(oldPath);Project oldReopened;oldReopened.replace(projectcodec::decode(readProjectGeoPackage(oldPath)));
    QCOMPARE(projectcodec::encode(oldReopened),projectcodec::encode(oldProject));QCOMPARE(read(oldPath),oldBytes);
    replacePackageRow(oldPath,"source",QJsonDocument(QJsonObject{{"format","pandoeditor-project"},{"version",10}}).toJson());
    QCOMPARE(packageError(oldPath),QString("PROJECT_GPKG_SOURCE_MISMATCH"));
 }
 void invalidProvenanceAndGeoPackageMarkersKeepActiveStateAtomic() {
    QTemporaryDir dir;QVERIFY(dir.isValid());Project source;source.replace(derivedFixture());const auto valid=projectcodec::encode(source);const auto root=QJsonDocument::fromJson(valid).object();
    QCOMPARE(root["version"].toInt(),10);QVERIFY(!root["geometryProvenance"].toObject()["inlineAllocations"].toArray().isEmpty());
    const auto target=dir.filePath("active.json"),invalidPath=dir.filePath("invalid.json");write(target,valid);
    EditorController editor({false,dir.filePath("private.json")});QVERIFY(editor.openFile(QUrl::fromLocalFile(target)));editor.selectCountry("A");
    editor.setColor("#123456");editor.setColor("#654321");editor.undo();editor.setNameDraft("keep pending draft");
    const auto before=editor.documentBytes();const auto revision=editor.revision();const auto selection=editor.primaryObject();const auto fileName=editor.fileName();const auto instance=editor.projectInstanceId();
    const auto assertUnchanged=[&] {
        QCOMPARE(editor.documentBytes(),before);QCOMPARE(editor.revision(),revision);QCOMPARE(editor.primaryObject(),selection);
        QCOMPARE(editor.fileName(),fileName);QCOMPARE(editor.projectInstanceId(),instance);QCOMPARE(editor.nameDraft(),QString("keep pending draft"));
        QVERIFY(editor.dirty());QVERIFY(editor.canUndo());QVERIFY(editor.canRedo());QVERIFY(editor.hasFile());QCOMPARE(read(target),valid);
    };
    const std::vector<std::function<void(QJsonObject&)>> changes={
        [](auto& value){value.remove("geometryProvenance");},
        [](auto& value){auto ledger=value["geometryProvenance"].toObject();ledger["schemaVersion"]=2;value["geometryProvenance"]=ledger;},
        [](auto& value){auto ledger=value["geometryProvenance"].toObject();auto rows=ledger["inlineAllocations"].toArray();auto row=rows[0].toObject();row["geometrySha256"]=QString(64,'0');rows[0]=row;ledger["inlineAllocations"]=rows;value["geometryProvenance"]=ledger;},
        [](auto& value){auto content=value["content"].toObject();auto labels=content["labels"].toArray();auto label=labels[0].toObject();auto source=label["source"].toObject();source["details"]=QJsonObject{{"unexpected","opaque dependency"}};label["source"]=source;labels[0]=label;content["labels"]=labels;value["content"]=content;}
    };
    for(const auto& change:changes) {
        auto invalid=root;change(invalid);write(invalidPath,QJsonDocument(invalid).toJson());
        QVERIFY(!editor.openFile(QUrl::fromLocalFile(invalidPath)));assertUnchanged();
    }
    const auto gpkg=dir.filePath("wrong-marker.gpkg");write(gpkg,exportProjectGeoPackage(source));
    replacePackageRow(gpkg,"source",QJsonDocument(QJsonObject{{"format","pandoeditor-project"},{"version",9}}).toJson());
    QCOMPARE(packageError(gpkg),QString("PROJECT_GPKG_SOURCE_MISMATCH"));QVERIFY(!editor.openProjectGeoPackage(QUrl::fromLocalFile(gpkg)));assertUnchanged();
    editor.discardPendingEdits();QVERIFY(editor.save());QCOMPARE(read(target),before);QCOMPARE(read(invalidPath),QJsonDocument([&]{auto value=root;changes.back()(value);return value;}()).toJson());
 }
 void webVisibilityImportRejectsUnsupportedNamespacesAtomically_data() {visibilityBoundaryData();}
 void webVisibilityImportRejectsUnsupportedNamespacesAtomically() {
    QFETCH(QString,key);QFETCH(bool,item);
    Project current;current.replace(fixture("static"));const auto baseline=projectcodec::encode(current);
    QVERIFY(current.setColor("A",0x123456));const auto firstEdit=projectcodec::encode(current);
    QVERIFY(current.setColor("A",0x654321));const auto secondEdit=projectcodec::encode(current);QVERIFY(current.undo());
    QVERIFY(current.setTimelineCursor("1914-06"));const auto before=projectcodec::encode(current);
    const auto input=QJsonDocument::fromJson(read(QStringLiteral(PANDOEDITOR_EXCHANGE_FIXTURES)+"/static.json")).object();
    // A true item entry must not hide an unsupported namespace by being normalized away.
    for(bool visible:{false,true}) {
        const auto revision=current.revision();auto invalid=input;invalid[item?"itemVisibility":"layerVisibility"]=item?QJsonObject{{key,QJsonObject{{"A",visible}}}}:QJsonObject{{key,visible}};
        QString error;try{current.replace(projectcodec::decodeWeb(QJsonDocument(invalid).toJson()));}
        catch(const std::invalid_argument& failure){error=QString::fromUtf8(failure.what());}
        QVERIFY2(error.contains("UNSUPPORTED_WEB_VISIBILITY"),qPrintable(error.isEmpty()?"Invalid real-web visibility namespace was accepted":error));
        QCOMPARE(projectcodec::encode(current),before);QCOMPARE(current.revision(),revision);QCOMPARE(current.timelineCursor(),std::string("1914-06"));
        QVERIFY(current.canUndo());QVERIFY(current.canRedo());QVERIFY(current.dirty());
        verifyStoredColorHistory(current,baseline,firstEdit,secondEdit);
        QCOMPARE(current.timelineCursor(),std::string("1914-06"));
    }
 }
 void nativeOnlyVisibilitySurvivesNativeSaveButRefusesWebExport_data() {visibilityBoundaryData();}
 void nativeOnlyVisibilitySurvivesNativeSaveButRefusesWebExport() {
    QFETCH(QString,key);QFETCH(bool,item);auto document=fixture("static");
    if(item)document.presentation.webPresentation.hiddenItems[key.toStdString()].insert("A");
    else document.presentation.webPresentation.visibility[key.toStdString()]=false;
    Project original;original.replace(document);const auto before=projectcodec::encode(original);
    Project reopened;reopened.replace(projectcodec::decode(before));QCOMPARE(projectcodec::encode(reopened),before);
    QByteArray output="not published";QString error;
    try{output=projectcodec::encodeWeb(reopened.snapshot());}catch(const std::invalid_argument& failure){error=QString::fromUtf8(failure.what());}
    QVERIFY2(error.contains("UNSUPPORTED_WEB_EXPORT")&&error.contains(key),qPrintable(error.isEmpty()?"Native-only visibility produced incompatible web JSON":error));
    QCOMPARE(output,QByteArray("not published"));QCOMPARE(projectcodec::encode(reopened),before);QCOMPARE(projectcodec::encode(original),before);
 }
 void supportedWebPresentationNamespacesRoundTrip() {
    auto document=fixture("static");auto& presentation=document.presentation.webPresentation;
    // Pinned real-web project-state.js uses distinct layer, item and style namespaces.
    for(const auto* key:{"countries","subunits","regions","distributions","rivers","lakes","genericFeatures","labels","basemapLabels","countryFlags","subunitLabels","subunitFlags","regionLabels","regionFlags"})presentation.visibility[key]=false;
    for(const auto* key:{"countries","subunits","regions","distributions","hydro","genericFeatures","labels","countryLabels"})presentation.hiddenItems[key].insert("A");
    for(const auto* key:{"countries","subunits","regions","distributions","rivers","lakes","hydro","genericFeatures","labels","countryLabels","terrain"})presentation.styles[key].opacity=0.5;
    Project original;original.replace(document);const auto before=projectcodec::encode(original);
    Project reopened;reopened.replace(projectcodec::decodeWeb(projectcodec::encodeWeb(original.snapshot())));
    QVERIFY(reopened.document().presentation.webPresentation==presentation);
    QCOMPARE(projectcodec::encode(original),before);
 }
 void inlineGeometryReferencesArePreservedOrExplicitlyRefused_data() {
    QTest::addColumn<QString>("domain");QTest::addColumn<QString>("scenario");QTest::addColumn<bool>("representable");
    for(const auto* domain:{"label","hydroEdits","genericFeatures","distributionEntry"}) {
        for(const auto* scenario:{"unique","duplicate-first","synthetic-preferred","changed-version-unique"})QTest::newRow((QByteArray(domain)+"-"+scenario).constData())<<QString(domain)<<QString(scenario)<<true;
        for(const auto* scenario:{"duplicate-later","equal-versions","synthetic-equal-versions","synthetic-conflict"})QTest::newRow((QByteArray(domain)+"-"+scenario).constData())<<QString(domain)<<QString(scenario)<<false;
    }
 }
 void inlineGeometryReferencesArePreservedOrExplicitlyRefused() {
    QFETCH(QString,domain);QFETCH(QString,scenario);QFETCH(bool,representable);
    auto document=fixture("static");const auto shape=inlineShape(domain);GeometryRef target{"z-native-content",3};
    const GeometryRef synthetic{"web-"+domain.toStdString()+":"+contentId,1};
    if(scenario=="duplicate-first")target={"a-native-content",1};
    if(scenario=="synthetic-preferred")target=synthetic;
    if(scenario=="equal-versions"||scenario=="changed-version-unique")target={"native-content",2};
    if(scenario.startsWith("synthetic-")&&scenario!="synthetic-preferred")target={synthetic.id,2};
    if(scenario=="duplicate-later"||scenario=="synthetic-preferred")document.geometries.insert({"a-native-content",1},shape);
    if(scenario=="duplicate-first")document.geometries.insert({"z-native-content",3},shape);
    if(scenario=="equal-versions")document.geometries.insert({target.id,1},shape);
    if(scenario=="changed-version-unique")document.geometries.insert({target.id,1},inlineShape(domain,1));
    if(scenario=="synthetic-equal-versions")document.geometries.insert(synthetic,shape);
    if(scenario=="synthetic-conflict")document.geometries.insert(synthetic,inlineShape(domain,1));
    document.geometries.insert(target,shape);addInlineContent(document,domain,target);
    Project original;original.replace(document);const auto baseline=projectcodec::encode(original);
    QVERIFY(original.setColor("A",0x123456));const auto firstEdit=projectcodec::encode(original);
    QVERIFY(original.setColor("A",0x654321));const auto secondEdit=projectcodec::encode(original);QVERIFY(original.undo());
    QVERIFY(original.setTimelineCursor("1914-06"));const auto before=projectcodec::encode(original);const auto revision=original.revision();const auto frozen=original.snapshot();
    Project native;native.replace(projectcodec::decode(before));QCOMPARE(projectcodec::encode(native),before);QVERIFY(inlineContentRef(native.document(),domain)==target);
    QByteArray output="not published";QString error;
    try{output=projectcodec::encodeWeb(original.snapshot());}catch(const std::invalid_argument& failure){error=QString::fromUtf8(failure.what());}
    if(representable) {
        QVERIFY2(error.isEmpty(),qPrintable(error));Project restored;restored.replace(projectcodec::decodeWeb(output));
        QVERIFY(inlineContentRef(restored.document(),domain)==target);QVERIFY(sameContent(original.document(),restored.document()));
        const auto restoredJson=QJsonDocument::fromJson(projectcodec::encode(restored)).object(),beforeJson=QJsonDocument::fromJson(before).object();
        QCOMPARE(restoredJson["geometries"],beforeJson["geometries"]);QCOMPARE(restoredJson["timelineRecords"],beforeJson["timelineRecords"]);
    } else {
        if(error.isEmpty()) {
            const auto restored=projectcodec::decodeWeb(output);const auto actual=inlineContentRef(restored,domain);
            QFAIL(qPrintable(QString("Web exchange rebound %1/%2 to %3/%4 instead of refusing").arg(QString::fromStdString(target.id)).arg(target.version).arg(QString::fromStdString(actual.id)).arg(actual.version)));
        }
        QVERIFY2(error.contains(scenario=="synthetic-conflict"?"GEOMETRY_ARCHIVE_CONFLICT":"UNSUPPORTED_WEB_EXPORT: non-territorial geometry reference"),qPrintable(error));
        QCOMPARE(output,QByteArray("not published"));
    }
    QCOMPARE(projectcodec::encode(original),before);QCOMPARE(projectcodec::encode(frozen),before);QCOMPARE(original.revision(),revision);
    QCOMPARE(original.timelineCursor(),std::string("1914-06"));QVERIFY(original.canUndo());QVERIFY(original.canRedo());QVERIFY(original.dirty());
    for(const auto& [ref,stored]:frozen.document().geometries.versions())QVERIFY(original.document().geometries.get(ref)==stored);
    verifyStoredColorHistory(original,baseline,firstEdit,secondEdit);
    QCOMPARE(projectcodec::encode(frozen),before);QCOMPARE(original.timelineCursor(),std::string("1914-06"));
 }
 void nativeOnlyFieldsRefuseWebExportWithoutLoss_data() {
    QTest::addColumn<QString>("field");QTest::addColumn<QString>("expected");
    for(const auto* field:{"captured-country","captured-empty-flag","captured-flag"})QTest::newRow(field)<<QString(field)<<QString("native captured flag defaults");
    QTest::newRow("extension")<<QString("extension")<<QString("retained native extensions");
    for(const auto* field:{"physical-dataset","physical-version","physical-source","physical-hidden"})QTest::newRow(field)<<QString(field)<<QString("native physical dataset settings");
    for(const auto* field:{"style-label","style-hydroEdits","style-genericFeatures","style-distributionEntry"})QTest::newRow(field)<<QString(field)<<QString("native non-territorial style override");
    for(const auto* field:{"user-layer","membership"})QTest::newRow(field)<<QString(field)<<QString("native user layer membership");
    QTest::newRow("territorial-opacity")<<QString("territorial-opacity")<<QString("native entity opacity");
 }
 void nativeOnlyFieldsRefuseWebExportWithoutLoss() {
    QFETCH(QString,field);QFETCH(QString,expected);auto document=fixture("static");const auto owner=territorialRef("A");
    if(field=="captured-country")document.symbols[owner].defaultCountryId="DEU";
    else if(field=="captured-empty-flag")document.symbols[owner].defaultFlagDataUrl=std::string{};
    else if(field=="captured-flag")document.symbols[owner].defaultFlagDataUrl="data:image/svg+xml;base64,PHN2Zy8+";
    else if(field=="extension") {PreservedExtension extension;extension.id="preserved";extension.jsonPointer="/future";extension.payload="{\"original\":[1,2]}";extension.dependencyKnowledge="known";document.extensions.push_back(extension);}
    else if(field=="physical-dataset")document.physicalData.dataset="native-hydro";
    else if(field=="physical-version")document.physicalData.version="1";
    else if(field=="physical-source")document.physicalData.source="native source";
    else if(field=="physical-hidden")document.physicalData.hiddenHydroIds={"source-river"};
    else if(field.startsWith("style-")) {
        const auto domain=field.mid(6);const GeometryRef geometry{"native-content",1};document.geometries.insert(geometry,inlineShape(domain));addInlineContent(document,domain,geometry);
        const auto nativeDomain=domain=="hydroEdits"?"hydro":domain=="genericFeatures"?"generic":domain.toStdString();
        document.presentation.objectStyles[{nativeDomain,contentId}]={0x123456,0.5,true};
    } else if(field=="territorial-opacity")document.presentation.objectStyles[owner].opacity=0.5;
    else {document.presentation.userLayers.push_back({"native-layer","Native layer"});if(field=="membership")document.presentation.membership[owner]="native-layer";}
    Project original;original.replace(document);const auto baseline=projectcodec::encode(original);
    QVERIFY(original.setColor("A",0x123456));const auto firstEdit=projectcodec::encode(original);
    QVERIFY(original.setColor("A",0x654321));const auto secondEdit=projectcodec::encode(original);QVERIFY(original.undo());
    const auto before=projectcodec::encode(original);const auto revision=original.revision();const auto frozen=original.snapshot();
    Project reopened;reopened.replace(projectcodec::decode(before));QCOMPARE(projectcodec::encode(reopened),before);
    QByteArray output="not published";QString error;
    try{output=projectcodec::encodeWeb(original.snapshot());}catch(const std::invalid_argument& failure){error=QString::fromUtf8(failure.what());}
    QVERIFY2(error.contains("UNSUPPORTED_WEB_EXPORT")&&error.contains(expected),qPrintable(error));QCOMPARE(output,QByteArray("not published"));
    QCOMPARE(projectcodec::encode(original),before);QCOMPARE(projectcodec::encode(frozen),before);QCOMPARE(original.revision(),revision);
    QVERIFY(original.canUndo());QVERIFY(original.canRedo());QVERIFY(original.dirty());
    verifyStoredColorHistory(original,baseline,firstEdit,secondEdit);QCOMPARE(projectcodec::encode(frozen),before);
 }
 void canonicalFlagMetadataSurvivesWebImportAndNativeReopen() {
    auto document=fixture("static");auto& unit=document.units.front();
    unit.metadata="{\"builtinSubunit\":{\"sourceCountryId\":\"KOR\"}}";
    Project source;source.replace(document);
    Project web;web.replace(projectcodec::decodeWeb(projectcodec::encodeWeb(source.snapshot())));
    Project native;native.replace(projectcodec::decode(projectcodec::encode(web)));
    QCOMPARE(resolveDefaultFlag(native.document(),territorialRef(unit.id)).source,QString("qrc:/defaults/flags/native/kr.svg"));
 }
 void hugeIntrinsicSvgKeepsOriginalBytesThroughNativeAndWebStorage() {
    QFile flag(":/defaults/flags/native/gu.svg");QVERIFY(flag.open(QIODevice::ReadOnly));
    const auto original=flag.readAll();const auto data=QString::fromLatin1("data:image/svg+xml;base64,"+original.toBase64()).toStdString();
    auto document=fixture("static");const auto owner=territorialRef(document.units.front().id);
    document.symbols[owner]={FlagPolicy::Embedded,data};Project source;source.replace(document);
    Project native;native.replace(projectcodec::decode(projectcodec::encode(source)));
    QCOMPARE(native.document().symbols.at(owner).embeddedDataUrl,data);
    Project web;web.replace(projectcodec::decodeWeb(projectcodec::encodeWeb(native.snapshot())));
    QCOMPARE(web.document().symbols.at(owner).embeddedDataUrl,data);
 }
 void nativeCapturedFlagDefaultsCannotDisappearInWebExport() {
    for(const auto symbol:{TerritorialSymbolStyle{FlagPolicy::Default,{},"DEU",{}},
                           TerritorialSymbolStyle{FlagPolicy::Default,{},{},std::string{}},
                           TerritorialSymbolStyle{FlagPolicy::Default,{},{},std::string("data:image/svg+xml;base64,PHN2Zy8+")}}) {
        auto document=fixture("static");document.symbols[territorialRef(document.units.front().id)]=symbol;
        Project source;source.replace(document);const auto before=projectcodec::encode(source);
        Project native;native.replace(projectcodec::decode(before));QCOMPARE(projectcodec::encode(native),before);
        bool refused=false;try{(void)projectcodec::encodeWeb(native.snapshot());}
        catch(const std::invalid_argument& error){refused=QString::fromUtf8(error.what()).contains("captured flag defaults");}
        QVERIFY(refused);QCOMPARE(projectcodec::encode(native),before);
    }
 }
 void staticQtFileSaveOpenAndAtomicFailures() {
    QTemporaryDir dir;QVERIFY(dir.isValid());Project source;source.replace(fixture("static"));
    const auto input=dir.filePath("input.pando.json"),saved=dir.filePath("saved.pando.json");write(input,projectcodec::encode(source));
    EditorController editor(EditorControllerConfig{false,dir.filePath("private.json")});
    QVERIFY(editor.openFile(QUrl::fromLocalFile(input)));editor.selectCountry("A");editor.setColor("#123456");
    QVERIFY(editor.saveFile(QUrl::fromLocalFile(saved)));QCOMPARE(QJsonDocument::fromJson(read(saved)).object()["version"].toInt(),10);
    editor.setColor("#654321");const auto previous=editor.documentBytes();editor.setColor("#abcdef");const auto future=editor.documentBytes();editor.undo();QVERIFY(editor.canUndo());QVERIFY(editor.canRedo());QVERIFY(editor.dirty());
    const auto bytes=editor.documentBytes();const auto selection=editor.primaryObject();const auto paths=editor.paths();
    QCOMPARE(bytes,previous);const auto instance=editor.projectInstanceId();const auto documentId=editor.documentId();
    const auto scene=editor.renderQuality();
    const auto unchanged=[&]{
        QCOMPARE(editor.documentBytes(),bytes);QCOMPARE(editor.primaryObject(),selection);QCOMPARE(editor.paths(),paths);
        QCOMPARE(editor.projectInstanceId(),instance);QCOMPARE(editor.documentId(),documentId);
        QVERIFY(editor.canUndo());QVERIFY(editor.canRedo());QVERIFY(editor.dirty());
        const auto after=editor.renderQuality();
        for(const auto* key:{"scenePublicationCount","scenePatchCount","sceneFullBuildCount","scenePreparationCount","viewportResourceGeneration"})
            QCOMPARE(after.value(key),scene.value(key));
    };
    const auto rejected=dir.filePath("rejected.json");Project complex;complex.replace(fixture("complex"));write(rejected,projectcodec::encode(complex));
    QVERIFY(!editor.openFile(QUrl::fromLocalFile(rejected)));unchanged();
    auto invalid=QJsonDocument::fromJson(bytes).object();invalid["geometries"]=QJsonArray{};write(rejected,QJsonDocument(invalid).toJson());
    QVERIFY(!editor.openFile(QUrl::fromLocalFile(rejected)));unchanged();
    editor.redo();QCOMPARE(editor.documentBytes(),future);editor.undo();QCOMPARE(editor.documentBytes(),bytes);
    QVERIFY(editor.save());QCOMPARE(read(saved),bytes);QCOMPARE(read(input),projectcodec::encode(source));
    EditorController reopened(EditorControllerConfig{false,dir.filePath("private-2.json")});QVERIFY(reopened.openFile(QUrl::fromLocalFile(saved)));QCOMPARE(reopened.documentBytes(),bytes);
 }
 void privateRestoreFailurePreservesActiveState() {
    QTemporaryDir dir;QVERIFY(dir.isValid());const auto privatePath=dir.filePath("private.json"),input=dir.filePath("input.json");
    Project source;source.replace(fixture("static"));write(input,projectcodec::encode(source));
    EditorController editor(EditorControllerConfig{true,privatePath});QVERIFY(editor.openFile(QUrl::fromLocalFile(input)));
    editor.selectCountry("A");editor.setColor("#123456");editor.setColor("#654321");const auto future=editor.documentBytes();editor.undo();
    const auto bytes=editor.documentBytes();const auto selection=editor.primaryObject();const auto paths=editor.paths();
    const auto instance=editor.projectInstanceId();const auto scene=editor.renderQuality();
    Project complex;complex.replace(fixture("complex"));
    auto invalid=QJsonDocument::fromJson(bytes).object();invalid["geometries"]=QJsonArray{};
    for(const auto& rejected:{projectcodec::encode(complex),QJsonDocument(invalid).toJson()}) {
        write(privatePath,rejected);QVERIFY(!editor.restorePrivateProject());
        QCOMPARE(read(privatePath),rejected);QCOMPARE(editor.documentBytes(),bytes);QCOMPARE(editor.primaryObject(),selection);
        QCOMPARE(editor.paths(),paths);QCOMPARE(editor.projectInstanceId(),instance);QVERIFY(editor.dirty());QVERIFY(editor.canUndo());QVERIFY(editor.canRedo());
        const auto after=editor.renderQuality();for(const auto* key:{"scenePublicationCount","scenePreparationCount","viewportResourceGeneration"})QCOMPARE(after.value(key),scene.value(key));
    }
    editor.redo();QCOMPARE(editor.documentBytes(),future);editor.undo();QCOMPARE(editor.documentBytes(),bytes);
 }
 void realNativeAndWebGeoPackageArchives() {
    QTemporaryDir dir;QVERIFY(dir.isValid());
    for(const auto* kind:{"static","complex"}) {
        Project original;original.replace(fixture(kind));const auto path=dir.filePath(QString(kind)+".gpkg");write(path,exportProjectGeoPackage(original));
        Project native;native.replace(projectcodec::decode(readProjectGeoPackage(path)));QCOMPARE(projectcodec::encode(native),projectcodec::encode(original));
        Project web;web.replace(projectcodec::decode(readProjectGeoPackage(QStringLiteral(PANDOEDITOR_EXCHANGE_FIXTURES)+"/"+kind+".gpkg")));
        QCOMPARE(projectcodec::encodeWeb(web.snapshot()),projectcodec::encodeWeb(original.snapshot()));
        EditorController editor(EditorControllerConfig{false,dir.filePath("private.json")});const auto before=editor.documentBytes();
        if(QString(kind)=="static")QVERIFY(editor.openProjectGeoPackage(QUrl::fromLocalFile(path)));
        else {QVERIFY(!editor.openProjectGeoPackage(QUrl::fromLocalFile(path)));QCOMPARE(editor.documentBytes(),before);}
    }
    Project empty;empty.replace(ProjectDocument({},{}));const auto path=dir.filePath("empty.gpkg");write(path,exportProjectGeoPackage(empty));Project restored;restored.replace(projectcodec::decode(readProjectGeoPackage(path)));QCOMPARE(projectcodec::encode(restored),projectcodec::encode(empty));
 }
 void immutableWorkerHistoryCursorAndFullAutosave() {
    QTemporaryDir dir;QVERIFY(dir.isValid());Project project;project.replace(fixture("static"));const auto before=project.snapshot();
    const auto oldGeometry=staticGeometryBinding(project.document(),"A").geometryRef;const auto geometry=project.document().geometries.get(oldGeometry);
    QVERIFY(project.setTimelineCursor("1914-06"));QVERIFY(!project.dirty());QVERIFY(!project.canUndo());QCOMPARE(project.revision(),before.revision());
    QVERIFY(project.setColor("A",0x123456));const auto edited=projectcodec::encode(project);QVERIFY(project.undo());QCOMPARE(projectcodec::encode(project),projectcodec::encode(before));QVERIFY(project.redo());QCOMPARE(projectcodec::encode(project),edited);
    const auto revision=project.revision();QVERIFY(project.setTimelineCursor("1914-07"));QCOMPARE(project.revision(),revision);QCOMPARE(projectcodec::encode(project),edited);QVERIFY(project.canUndo());
    const auto frozen=project.snapshot();auto worker=std::async(std::launch::async,[frozen]{return projectcodec::encode(frozen);});QVERIFY(project.setMemo("A","new metadata"));QCOMPARE(worker.get(),edited);QVERIFY(before.document().geometries.get(oldGeometry)==geometry);
    ProjectAutosave autosave(dir.filePath("autosave.json"),dir.filePath("view.json"));autosave.scheduleDocument(project.snapshot());QVERIFY(autosave.flushNow());
    Project recovered;recovered.replace(projectcodec::decode(autosave.restoreDocument()));QCOMPARE(projectcodec::encode(recovered),projectcodec::encode(project));
    Project rich;rich.replace(fixture("complex"));autosave.scheduleDocument(rich.snapshot());QVERIFY(autosave.flushNow());
    Project recoveredRich;recoveredRich.replace(projectcodec::decode(autosave.restoreDocument()));QCOMPARE(projectcodec::encode(recoveredRich),projectcodec::encode(rich));QVERIFY(!isStaticTimeline(recoveredRich.document()));
    auto invalid=project.document();invalid.timelineRecords.geometryBindings.front().geometryRef.version=999;const auto saved=projectcodec::encode(project);const auto cursor=project.timelineCursor();
    QVERIFY_EXCEPTION_THROWN(project.replace(invalid),std::invalid_argument);QCOMPARE(projectcodec::encode(project),saved);QCOMPARE(project.timelineCursor(),cursor);QVERIFY(project.canUndo());
 }
 void canonicalGeometryAndParentCommandsRestoreTogether() {
    const Geometry square{"Polygon",{},{},{{{{0,0},{10,0},{10,10},{0,10},{0,0}}}}};
    ProjectDocument d({{"A","A",square.polygons,0x123456},{"C","C",square.polygons,0x123456}},{});
    const GeometryRef shared=staticGeometryBinding(d,"A").geometryRef;
    appendTerritory(d,{"S","S","",UnitKind::General,false},shared,"A");d.presentation.objectStyles[territorialRef("S")]={};
    d.presentation.membership.clear();Project project;project.replace(d);const auto original=projectcodec::encode(project);
    const auto frozen=project.snapshot();const auto oldShape=project.document().geometries.get(shared);
    auto apply=[&](const TerritorialMutationIntent& intent,const char* command,std::optional<GeometryPatch> patch={}) {
        const auto plan=CommandProcessor::planTerritorial(project,intent);if(!plan.ok())return false;
        CommandArguments args;args.action=ApplyTerritorialMutation{*plan.plan,std::move(patch)};
        auto prepared=CommandProcessor::prepare(project,CommandProcessor::makeRequest(project,command,args));
        return prepared.ok()&&CommandProcessor::confirm(project,*prepared.preview).changed();
    };
    QVERIFY(apply(ChangeParentIntent{territorialRef("S"),territorialRef("C")},"territorial.relation.parent"));
    const auto parentChanged=projectcodec::encode(project);QCOMPARE(staticParentRelation(project.document(),"S").parentId,std::string("C"));
    const Geometry smaller{"Polygon",{},{},{{{{1,1},{2,1},{2,2},{1,2},{1,1}}}}};
    QVERIFY(apply(ReplaceGeometryIntent{territorialRef("S")},"territorial.geometry.commit",GeometryPatch{project.revision(),{{territorialRef("S"),smaller}},{},{}}));
    const auto edited=projectcodec::encode(project);const auto next=staticGeometryBinding(project.document(),"S").geometryRef;
    QVERIFY(next.version>shared.version);QVERIFY(project.document().geometries.get(shared)==oldShape);
    QVERIFY(staticGeometryBinding(project.document(),"A").geometryRef==shared);QCOMPARE(projectcodec::encode(frozen),original);
    QVERIFY(project.undo());QCOMPARE(projectcodec::encode(project),parentChanged);QVERIFY(project.undo());QCOMPARE(projectcodec::encode(project),original);
    QVERIFY(project.redo());QVERIFY(project.redo());QCOMPARE(projectcodec::encode(project),edited);
    Project reopened;reopened.replace(projectcodec::decode(edited));QCOMPARE(staticParentRelation(reopened.document(),"S").parentId,std::string("C"));QVERIFY(reopened.document().geometries.get(shared));QVERIFY(reopened.document().geometries.get(next));
 }
 void malformedNativeOwnershipIsRejectedBeforePublication() {
    QTemporaryDir dir;Project source;source.replace(fixture("static"));const auto valid=projectcodec::encode(source);
    const auto path=dir.filePath("current.json"),invalidPath=dir.filePath("invalid.json");write(path,valid);
    EditorController editor({false,dir.filePath("private.json")});QVERIFY(editor.openFile(QUrl::fromLocalFile(path)));editor.selectCountry("A");editor.setColor("#123456");editor.setColor("#654321");editor.undo();
    const auto current=editor.documentBytes();const auto selection=editor.primaryObject();QVERIFY(editor.dirty());QVERIFY(editor.canUndo());QVERIFY(editor.canRedo());
    const std::vector<std::function<void(QJsonObject&)>> mutations={
        [](auto& root){auto rows=root["units"].toArray();auto unit=rows[0].toObject();auto metadata=unit["metadata"].toObject();metadata["sovereignId"]="A";unit["metadata"]=metadata;rows[0]=unit;root["units"]=rows;},
        [](auto& root){auto rows=root["units"].toArray();auto unit=rows[0].toObject();auto metadata=unit["metadata"].toObject();metadata["capital"]="conflicting second owner";unit["metadata"]=metadata;rows[0]=unit;root["units"]=rows;},
        [](auto& root){auto presentation=root["presentation"].toObject();auto web=presentation["webPresentation"].toObject();web["objectStyles"]=QJsonObject{{"territorial:entity:missing",QJsonObject{{"opacity",0.5}}}};presentation["webPresentation"]=web;root["presentation"]=presentation;},
        [](auto& root){auto presentation=root["presentation"].toObject();auto web=presentation["webPresentation"].toObject();web["labelSettings"]=QJsonArray{QJsonObject{{"ref",QJsonObject{{"domain","label"},{"id","missing"}}},{"pinned",false},{"collisionGroup","map"}}};presentation["webPresentation"]=web;root["presentation"]=presentation;},
        [](auto& root){auto presentation=root["presentation"].toObject();auto web=presentation["webPresentation"].toObject();web["overlayOrder"]=QJsonArray{"labels"};presentation["webPresentation"]=web;root["presentation"]=presentation;},
        [](auto& root){auto presentation=root["presentation"].toObject();auto web=presentation["webPresentation"].toObject();web["objectOrder"]=QJsonArray{"territorial:entity:A","territorial:entity:A"};presentation["webPresentation"]=web;root["presentation"]=presentation;},
    };
    for(const auto& mutate:mutations){auto root=QJsonDocument::fromJson(valid).object();mutate(root);const auto invalid=QJsonDocument(root).toJson();QVERIFY_EXCEPTION_THROWN(projectcodec::decode(invalid),std::invalid_argument);write(invalidPath,invalid);QVERIFY(!editor.openFile(QUrl::fromLocalFile(invalidPath)));QCOMPARE(editor.documentBytes(),current);QCOMPARE(editor.primaryObject(),selection);QVERIFY(editor.canUndo());QVERIFY(editor.canRedo());QVERIFY(editor.dirty());}
 }
};
QTEST_MAIN(TimelineEditorPersistenceTests)
#include "timeline_editor_persistence_tests.moc"

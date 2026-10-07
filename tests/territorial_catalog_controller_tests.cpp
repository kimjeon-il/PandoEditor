#include "editorcontroller.h"
#include "projectcodec.h"
#include "territorialcatalogadapter.h"
#include "historicaltransaction.h"
#include "geometrycalculator.h"
#include "defaultflagresolver.h"
#include <pandoeditor/historicalinstantiation.h>
#include <pandoeditor/project.h>
#include <pandoeditor/geometrypredicates.h>
#include <pandoeditor/objectproperties.h>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <type_traits>

// Expectations come from fixed Web ebcfae4d27b29cbbea6416a7045a4806930204be:
// territorial-library{,-service}.js, library-ownership.js and
// app-library-assembly.js. The actual stored index/chunks remain unmodified.
namespace {
const QByteArray indexPin="63c072095fd6c95034365f6d7f9d4e4ff99f71684890bffc8e678fe741b4334c";
QString indexFile() {return QFINDTESTDATA("../assets/territorial-library-v2/index.json");}
QByteArray read(const QString& path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("test fixture unreadable");
    return file.readAll();
}
bool load(EditorController& editor) {return editor.loadHistoricalLibrary(QUrl::fromLocalFile(indexFile()));}
QVariantMap status(const EditorController& editor) {return editor.property("historicalCatalogStatus").toMap();}
QStringList resultIds(const EditorController& editor) {
    QStringList ids;for(const auto& row:editor.historicalResults())ids.append(row.toMap().value("id").toString());return ids;
}
QVariantMap resultRow(const EditorController& editor,const QString& id) {
    for(const auto& row:editor.historicalResults())if(row.toMap().value("id")==id)return row.toMap();return {};
}
std::vector<pandoeditor::TerritorialUnit> copies(const pandoeditor::ProjectDocument& document,const std::string& source) {
    std::vector<pandoeditor::TerritorialUnit> rows;
    for(const auto& unit:document.units)if(unit.sourceEntityId==source)rows.push_back(unit);return rows;
}
QVariantMap addOptions(const QString& id,const QString& date="2026-10-06") {
    return {{"libraryId",id},{"referenceDate",date},{"childDepth","none"}};
}
pandoeditor::HistoricalLibrary coreCatalog() {
    pandoeditor::HistoricalEntity entity;entity.libraryId="state:fixture";entity.canonicalName="Source";
    pandoeditor::HistoricalGeometryVersion version;version.id="source-boundary-v1";
    version.geometry={"Polygon",{},{},{{{{0,0},{1,0},{1,1},{0,1},{0,0}}}}};
    entity.geometryVersions.push_back(version);
    return {2,{entity},{}};
}
// This deliberately compiles against the pre-extension structs. The RED
// assertions exercise actual planning/apply behavior, not whether a field exists.
template<class T,class=void>struct InstanceId {static void set(T&,const std::string&) {}};
template<class T>struct InstanceId<T,std::void_t<decltype(std::declval<T>().instanceId)>> {
    static void set(T& value,const std::string& id){value.instanceId=id;}
};
template<class T,class=void>struct InitialDefaults {static void set(T&) {}};
template<class T>struct InitialDefaults<T,std::void_t<decltype(std::declval<T>().initialCountryDetails),
    decltype(std::declval<T>().initialSymbolStyle),decltype(std::declval<T>().initialObjectStyle)>> {
    static void set(T& value) {
        value.initialCountryDetails=pandoeditor::CountryDetails{"Capital"};
        value.initialSymbolStyle=pandoeditor::TerritorialSymbolStyle{pandoeditor::FlagPolicy::Embedded,
            "data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciLz4="};
        value.initialObjectStyle=pandoeditor::ObjectStyle{0x53657a,1,true};
    }
};
void apply(pandoeditor::Project& project,const pandoeditor::HistoricalInstantiationPlan& plan) {
    pandoeditor::CommandArguments args;args.action=plan;
    auto prepared=pandoeditor::CommandProcessor::prepare(project,
        pandoeditor::CommandProcessor::makeRequest(project,"historical.instantiate",args));
    if(!prepared.ok()||!prepared.preview)throw std::runtime_error(prepared.detail);
    if(!pandoeditor::CommandProcessor::confirm(project,*prepared.preview).changed())throw std::runtime_error("historical command did not apply");
}
}

class TerritorialCatalogControllerTests:public QObject {
    Q_OBJECT
private slots:
    void frozenFixturePin() {
        const auto bytes=read(indexFile());QCOMPARE(bytes.size(),qsizetype(555560));
        QCOMPARE(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex(),indexPin);
    }
    void explicitInstanceIdentityPreservesSource() {
        pandoeditor::Project project;pandoeditor::ProjectDocument empty;empty.documentId="instance-test";project.replace(empty);
        pandoeditor::HistoricalAddRequest request;request.libraryId="state:fixture";request.referenceDate="2026";
        InstanceId<decltype(request)>::set(request,"library_general_first");
        apply(project,pandoeditor::planIndependentHistorical(project.snapshot(),coreCatalog(),{request}));
        QCOMPARE(project.document().units.front().id,std::string("library_general_first"));
        QCOMPARE(project.document().units.front().sourceEntityId,std::string("state:fixture"));
        const auto first=pandoeditor::staticGeometryBinding(project.document(),"library_general_first").geometryRef;
        InstanceId<decltype(request)>::set(request,"library_general_second");
        apply(project,pandoeditor::planIndependentHistorical(project.snapshot(),coreCatalog(),{request}));
        QCOMPARE(project.document().units.size(),std::size_t(2));
        const auto second=pandoeditor::staticGeometryBinding(project.document(),"library_general_second").geometryRef;
        QVERIFY(!(first==second));QVERIFY(project.undo());QCOMPARE(project.document().units.size(),std::size_t(1));
    }
    void sourceParentRemapsOnlyInsideCurrentBatch() {
        pandoeditor::Project project;pandoeditor::ProjectDocument empty;empty.documentId="batch-parent-test";project.replace(empty);
        auto parent=*coreCatalog().get("state:fixture");parent.libraryId="state:parent";
        auto child=parent;child.libraryId="state:child";child.parentLibraryId="state:parent";
        child.geometryVersions.front().geometry={"Polygon",{},{},{{{{.2,.2},{.4,.2},{.4,.4},{.2,.4},{.2,.2}}}}};
        const pandoeditor::HistoricalLibrary catalog(2,{parent,child},{});
        pandoeditor::HistoricalAddRequest root;root.libraryId="state:parent";root.referenceDate="2026";
        InstanceId<decltype(root)>::set(root,"library_parent");
        auto nested=root;nested.libraryId="state:child";nested.parent=pandoeditor::territorialRef("state:parent");
        InstanceId<decltype(nested)>::set(nested,"library_child");
        try {apply(project,pandoeditor::planIndependentHistorical(project.snapshot(),catalog,{root,nested}));}
        catch(const std::exception& error){QFAIL(error.what());}
        catch(...){QFAIL("historical parent command threw a non-standard rejection");}
        QCOMPARE(pandoeditor::staticParentRelation(project.document(),"library_child").parentId,std::string("library_parent"));
        QCOMPARE(copies(project.document(),"state:parent").size(),std::size_t(1));
        QCOMPARE(copies(project.document(),"state:child").size(),std::size_t(1));
    }
    void typedInitialDefaultsAndSourceMetadataShareOneUndo() {
        pandoeditor::Project project;pandoeditor::ProjectDocument empty;empty.documentId="source-defaults-test";project.replace(empty);project.markSaved();
        auto plan=pandoeditor::planIndependentHistorical(project.snapshot(),coreCatalog(),{{"state:fixture","2026"}});
        auto& addition=plan.additions.front();
        addition.selection.metadata=R"({"note":"source annotation","sourceLifetime":{"validFrom":"-0044-03-15","validTo":"1945"},"sourceGeometryValidity":{"validFrom":"1900","validTo":"1945"},"sourceReferenceDate":"1945"})";
        InitialDefaults<decltype(addition)>::set(addition);
        apply(project,plan);const auto owner=pandoeditor::territorialRef(project.document().units.front().id);
        QCOMPARE(project.document().units.front().metadata,addition.selection.metadata);
        QVERIFY(project.document().countryDetails.count(owner));QCOMPARE(project.document().countryDetails.at(owner).capital,std::string("Capital"));
        QVERIFY(project.document().symbols.count(owner));QVERIFY(project.document().symbols.at(owner).policy==pandoeditor::FlagPolicy::Embedded);
        QCOMPARE(project.document().presentation.objectStyles.at(owner).color,std::uint32_t(0x53657a));
        QVERIFY(project.dirty());QVERIFY(project.undo());QVERIFY(project.document().units.empty());
        QVERIFY(project.document().symbols.empty());QVERIFY(project.document().countryDetails.empty());QVERIFY(!project.dirty());
        QVERIFY(project.redo());QCOMPARE(project.document().units.front().metadata,addition.selection.metadata);
    }
    void defaultCatalogIsFrozenMetadataOnly() {
        EditorController editor;const auto value=status(editor);
        QCOMPARE(value.value("indexSha256").toByteArray(),indexPin);
        QCOMPARE(value.value("entities").toInt(),284);QCOMPARE(value.value("lineages").toInt(),262);
        QCOMPARE(value.value("geometryVersions").toInt(),287);QCOMPARE(value.value("loadedEntities").toInt(),0);
        QCOMPARE(editor.historicalSnapshots().size(),qsizetype(2));
    }
    void indexSearchIsLazyAndLeavesProjectUntouched() {
        EditorController editor;const auto before=editor.documentBytes();const bool dirty=editor.dirty(),undo=editor.canUndo();
        QVERIFY2(load(editor),qPrintable(editor.historicalError()));
        editor.searchHistorical("Germany","","all","1900-01-01","");
        QCOMPARE(resultIds(editor),QStringList({"state:DEU","state:east-prussia","state:west-prussia"}));
        const auto east=resultRow(editor,"state:east-prussia");
        QCOMPARE(east.value("name").toString(),QString::fromUtf8("동프로이센주"));
        QCOMPARE(east.value("names").toMap().value("de").toString(),QString::fromUtf8("Ostpreußen"));
        QCOMPARE(status(editor).value("loadedEntities").toInt(),0);
        QCOMPARE(editor.documentBytes(),before);QCOMPARE(editor.dirty(),dirty);QCOMPARE(editor.canUndo(),undo);
    }
    void aliveGapIsNullAndCannotLoadPreview() {
        EditorController editor;QVERIFY2(load(editor),qPrintable(editor.historicalError()));
        editor.searchHistorical(QString::fromUtf8("독일 민주공화국"),"","all","1989-04-24","");
        QCOMPARE(resultIds(editor),QStringList({"state:deutsche-demokratische-republik"}));
        const auto row=resultRow(editor,"state:deutsche-demokratische-republik");
        QVERIFY(row.contains("selectedVersionId"));QVERIFY(row.value("selectedVersionId").isNull());
        editor.selectHistorical("state:deutsche-demokratische-republik","","1989-04-24");
        QTRY_VERIFY(!editor.historicalPreview().value("error").toString().isEmpty());
        QVERIFY(editor.historicalPreview().value("polygons").toList().isEmpty());
        QCOMPARE(status(editor).value("loadedEntities").toInt(),0);
        QVERIFY(!editor.prepareHistoricalAdd(addOptions("state:deutsche-demokratische-republik","1989-04-24")));
    }
    void coarseCursorSelectsExactFrozenVersion_data() {
        QTest::addColumn<QString>("date");QTest::addColumn<QString>("version");
        QTest::newRow("day-before")<<QString("1992-04-26")<<QString("state:sfr-yugoslavia:1945-1992-r1");
        QTest::newRow("inclusive-next")<<QString("1992-04-27")<<QString("state:federal-republic-of-yugoslavia:1992-2003-r1");
        QTest::newRow("month-end")<<QString("1992-04")<<QString("state:federal-republic-of-yugoslavia:1992-2003-r1");
        QTest::newRow("year-gap")<<QString("1943")<<QString();
    }
    void coarseCursorSelectsExactFrozenVersion() {
        QFETCH(QString,date);QFETCH(QString,version);EditorController editor;QVERIFY2(load(editor),qPrintable(editor.historicalError()));
        editor.searchHistorical("Yugoslavia","","all",date,"");const auto row=resultRow(editor,"state:yugoslavia");QVERIFY(!row.isEmpty());
        if(version.isEmpty())QVERIFY(row.value("selectedVersionId").isNull());
        else QCOMPARE(row.value("selectedVersionId").toString(),version);
        QCOMPARE(status(editor).value("loadedEntities").toInt(),0);
    }
    void selectedPreviewUsesLazyGeometryAndDoesNotDirty() {
        EditorController editor;QVERIFY2(load(editor),qPrintable(editor.historicalError()));const auto before=editor.documentBytes();
        const bool dirty=editor.dirty(),undo=editor.canUndo();editor.selectHistorical("state:KOR","","2026-10-06");
        QTRY_VERIFY_WITH_TIMEOUT(!editor.historicalPreview().value("polygons").toList().isEmpty(),15000);
        QCOMPARE(editor.historicalPreview().value("id").toString(),QString("state:KOR"));
        QCOMPARE(status(editor).value("loadedEntities").toInt(),1);
        QCOMPARE(editor.documentBytes(),before);QCOMPARE(editor.dirty(),dirty);QCOMPARE(editor.canUndo(),undo);
    }
    void failedCatalogReplacementKeepsPublishedSource() {
        EditorController editor;QVERIFY2(load(editor),qPrintable(editor.historicalError()));
        editor.searchHistorical("korea","","all","2026-10-06","");const auto before=editor.historicalResults();const auto identity=status(editor).value("indexSha256");
        QTemporaryDir temp;QVERIFY(temp.isValid());QFile file(temp.filePath("broken-index.json"));QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{broken");file.close();QVERIFY(!editor.loadHistoricalLibrary(QUrl::fromLocalFile(file.fileName())));
        QCOMPARE(status(editor).value("indexSha256"),identity);QCOMPARE(editor.historicalResults(),before);
    }
    void repeatedCatalogCopiesHaveFreshInstanceAndGeometryIds() {
        EditorController editor;QVERIFY2(load(editor),qPrintable(editor.historicalError()));
        QVERIFY(editor.prepareHistoricalAdd(addOptions("state:KOR")));
        QTRY_COMPARE_WITH_TIMEOUT(editor.historicalStage(),QString("impact"),15000);
        QVERIFY(editor.confirmHistoricalAdd(editor.historicalSession()));
        const auto original=projectcodec::decode(editor.documentBytes());const auto firstValues=copies(original,"state:KOR");QCOMPARE(firstValues.size(),std::size_t(1));
        const auto firstId=firstValues.front().id;const auto first=pandoeditor::staticGeometryBinding(original,firstId).geometryRef;
        QVERIFY(editor.prepareHistoricalAdd(addOptions("state:KOR")));
        QTRY_COMPARE_WITH_TIMEOUT(editor.historicalStage(),QString("impact"),15000);
        const auto token=editor.historicalSession();QVERIFY(editor.confirmHistoricalAdd(token));QVERIFY(!editor.confirmHistoricalAdd(token));
        // Corrected against fixed Web app-library-assembly.js: every root goes
        // through territorial-library-batch; an overlapping old root is replaced.
        // The first RED source incorrectly expected two overlapping General roots.
        const auto document=projectcodec::decode(editor.documentBytes());const auto values=copies(document,"state:KOR");QCOMPARE(values.size(),std::size_t(1));
        QVERIFY(values.front().id!="state:KOR");QVERIFY(values.front().id!=firstId);
        const auto second=pandoeditor::staticGeometryBinding(document,values.front().id).geometryRef;QVERIFY(!(first==second));
        QCOMPARE(values.front().sourceGeometryVersion,firstValues.front().sourceGeometryVersion);
        QVERIFY(document.geometries.get(first)); // Immutable archive survives donor removal.
        editor.undo();const auto restored=copies(projectcodec::decode(editor.documentBytes()),"state:KOR");
        QCOMPARE(restored.size(),std::size_t(1));QCOMPARE(restored.front().id,firstId);
        editor.redo();const auto redone=copies(projectcodec::decode(editor.documentBytes()),"state:KOR");
        QCOMPARE(redone.size(),std::size_t(1));QCOMPARE(redone.front().id,values.front().id);
    }
    void sourceParentNeverBindsAnEarlierImportedInstanceImplicitly() {
        EditorController editor;QVERIFY2(load(editor),qPrintable(editor.historicalError()));
        QVERIFY(editor.prepareHistoricalAdd(addOptions("state:NLD")));QTRY_COMPARE_WITH_TIMEOUT(editor.historicalStage(),QString("impact"),15000);
        QVERIFY(editor.confirmHistoricalAdd(editor.historicalSession()));const auto before=editor.documentBytes();
        QVERIFY(!editor.prepareHistoricalAdd(addOptions("state:ABW")));QCOMPARE(editor.historicalStage(),QString("ownership"));
        QCOMPARE(editor.historicalOwnershipNeeded().size(),qsizetype(1));QCOMPARE(editor.documentBytes(),before);
    }
    void cancelledPreparationCannotPublishImpact() {
        EditorController editor;QVERIFY2(load(editor),qPrintable(editor.historicalError()));const auto before=editor.documentBytes();
        QVERIFY(editor.prepareHistoricalAdd(addOptions("state:KOR")));const auto oldToken=editor.historicalSession();editor.cancelHistoricalAdd();
        QTRY_COMPARE_WITH_TIMEOUT(status(editor).value("pendingJobs").toULongLong(),qulonglong(0),15000);
        QVERIFY(editor.historicalStage()!=QString("impact"));QVERIFY(!editor.confirmHistoricalAdd(oldToken));
        QCOMPARE(editor.documentBytes(),before);
    }
    void projectReplacementRejectsLateHistoricalPreparation() {
        EditorController editor;QVERIFY2(load(editor),qPrintable(editor.historicalError()));
        QVERIFY(editor.prepareHistoricalAdd(addOptions("state:KOR")));const auto oldToken=editor.historicalSession();QVERIFY(editor.newProject());
        const auto after=editor.documentBytes();QTRY_COMPARE_WITH_TIMEOUT(status(editor).value("pendingJobs").toULongLong(),qulonglong(0),15000);
        QVERIFY(!editor.confirmHistoricalAdd(oldToken));
        QCOMPARE(editor.documentBytes(),after);QVERIFY(copies(projectcodec::decode(after),"state:KOR").empty());
    }
    void previewIsBoundToOriginalProjectRevision() {
        EditorController editor;QVERIFY2(load(editor),qPrintable(editor.historicalError()));
        QVERIFY(editor.prepareHistoricalAdd(addOptions("state:KOR")));QTRY_COMPARE_WITH_TIMEOUT(editor.historicalStage(),QString("impact"),15000);
        const auto oldToken=editor.historicalSession();QVERIFY(editor.newProject());const auto after=editor.documentBytes();
        QVERIFY(!editor.confirmHistoricalAdd(oldToken));QCOMPARE(editor.documentBytes(),after);
    }
    void actualChildExpandsOnlyChosenExistingRootInOneUndo() {
        EditorController editor;QVERIFY2(load(editor),qPrintable(editor.historicalError()));
        QVERIFY(editor.prepareHistoricalAdd(addOptions("state:NLD")));QTRY_COMPARE_WITH_TIMEOUT(editor.historicalStage(),QString("impact"),15000);
        QVERIFY(editor.confirmHistoricalAdd(editor.historicalSession()));
        const auto original=projectcodec::decode(editor.documentBytes());const auto roots=copies(original,"state:NLD");QCOMPARE(roots.size(),std::size_t(1));
        const auto root=QString::fromStdString(roots.front().id);const auto before=editor.documentBytes();
        // Fixed Web uses choice.parentId || choice.countryId; choosing the
        // country alone must select its root even with an explicit empty parent.
        auto options=addOptions("state:ABW");options["ownership"]=QVariantMap{{"state:ABW",QVariantMap{{"mode","child"},{"countryId",root},{"parentId",""}}}};
        QVERIFY(editor.prepareHistoricalAdd(options));QTRY_COMPARE_WITH_TIMEOUT(editor.historicalStage(),QString("impact"),15000);
        QVERIFY(!editor.historicalImpact().value("adjusted").toList().isEmpty());QCOMPARE(editor.documentBytes(),before);
        QVERIFY(editor.confirmHistoricalAdd(editor.historicalSession()));const auto result=projectcodec::decode(editor.documentBytes());
        const auto children=copies(result,"state:ABW");QCOMPARE(children.size(),std::size_t(1));
        QCOMPARE(pandoeditor::staticParentRelation(result,children.front().id).parentId,roots.front().id);
        const auto oldRef=pandoeditor::staticGeometryBinding(original,roots.front().id).geometryRef;
        const auto newRef=pandoeditor::staticGeometryBinding(result,roots.front().id).geometryRef;QVERIFY(!(oldRef==newRef));
        const auto childRef=pandoeditor::staticGeometryBinding(result,children.front().id).geometryRef;
        QVERIFY(pandoeditor::geometryContains(*result.geometries.get(newRef),*result.geometries.get(childRef)));
        editor.undo();QCOMPARE(editor.documentBytes(),before);editor.redo();QCOMPARE(copies(projectcodec::decode(editor.documentBytes()),"state:ABW").size(),std::size_t(1));
    }
    void legacyIndependentSelectionCannotSmuggleGeometryPatch() {
        pandoeditor::Project project;pandoeditor::ProjectDocument empty;empty.documentId="legacy-patch";project.replace(empty);
        apply(project,pandoeditor::planIndependentHistorical(project.snapshot(),coreCatalog(),{{"state:fixture","2026"}}));
        auto source=*coreCatalog().get("state:fixture");source.libraryId="state:other";
        source.geometryVersions.front().geometry={"Polygon",{},{},{{{{2,2},{3,2},{3,3},{2,3},{2,2}}}}};
        pandoeditor::HistoricalLibrary catalog(2,{source},{});auto plan=pandoeditor::planIndependentHistorical(project.snapshot(),catalog,{{"state:other","2026"}});
        plan.territoryReplacements.push_back({pandoeditor::territorialRef("state:fixture"),source.geometryVersions.front().geometry});
        pandoeditor::CommandArguments args;args.action=plan;const auto before=projectcodec::encode(project);
        const auto prepared=pandoeditor::CommandProcessor::prepare(project,pandoeditor::CommandProcessor::makeRequest(project,"historical.instantiate",args));
        QVERIFY(!prepared.ok());QCOMPARE(projectcodec::encode(project),before);
    }
    void absentCountryRenameTargetDoesNotRejectExactDatedImport() {
        // RED-01 used the default sample, which contains DEU. Frozen Web
        // countryUpdates must apply there; this case explicitly needs no DEU.
        QTemporaryDir temporary;QVERIFY(temporary.isValid());
        pandoeditor::Project empty;pandoeditor::ProjectDocument document(
            std::vector<pandoeditor::Country>{},std::vector<pandoeditor::Layer>{{"countries","Countries"}});
        empty.replace(std::move(document));const auto bytes=projectcodec::encode(empty);
        QFile file(temporary.filePath("empty.pando.json"));QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes),qint64(bytes.size()));file.close();
        EditorController editor;QVERIFY(editor.openFile(QUrl::fromLocalFile(file.fileName())));
        QVERIFY(projectcodec::decode(editor.documentBytes()).units.empty());
        QVERIFY2(load(editor),qPrintable(editor.historicalError()));const auto before=editor.documentBytes();
        QVERIFY(editor.prepareHistoricalAdd(addOptions("state:deutsche-demokratische-republik","1989-04-25")));
        QTRY_COMPARE_WITH_TIMEOUT(editor.historicalStage(),QString("impact"),15000);
        QVERIFY(editor.historicalImpact().value("updated").toList().isEmpty());QCOMPARE(editor.documentBytes(),before);
        QVERIFY(editor.confirmHistoricalAdd(editor.historicalSession()));const auto addedDocument=projectcodec::decode(editor.documentBytes());
        const auto values=copies(addedDocument,"state:deutsche-demokratische-republik");QCOMPARE(values.size(),std::size_t(1));
        QCOMPARE(values.front().sourceGeometryVersion,std::string("state:deutsche-demokratische-republik:1989-04-25-r1"));
        const auto metadata=QJsonDocument::fromJson(QByteArray::fromStdString(values.front().metadata)).object();
        QCOMPARE(metadata.value("sourceReferenceDate").toString(),QString("1989-04-25"));
        QCOMPARE(metadata.value("sourceLifetime").toObject().value("validFrom").toString(),QString("1949-10-07"));
        QVERIFY(!pandoeditor::staticGeometryBinding(addedDocument,values.front().id).validity.from);
        editor.undo();QCOMPARE(editor.documentBytes(),before);
    }
    void retainedCountryRenameTargetReceivesFrozenSourceUpdateInSameUndo() {
        // The fixed DDR descriptor contains countryUpdates.DEU.name; Web's
        // gis-import-transaction maps it onto the retained existing root.
        EditorController editor;QVERIFY2(load(editor),qPrintable(editor.historicalError()));
        const auto before=editor.documentBytes();const auto original=projectcodec::decode(before);
        QVERIFY(std::any_of(original.units.begin(),original.units.end(),[](const auto& unit){return unit.id=="DEU";}));
        QVERIFY(editor.prepareHistoricalAdd(addOptions("state:deutsche-demokratische-republik","1989-04-25")));
        QTRY_COMPARE_WITH_TIMEOUT(editor.historicalStage(),QString("impact"),15000);
        const auto updated=editor.historicalImpact().value("updated").toList();QCOMPARE(updated.size(),qsizetype(1));
        QCOMPARE(updated.front().toString(),QString::fromUtf8("독일 → 독일 연방공화국"));
        QCOMPARE(editor.documentBytes(),before);
        QVERIFY(editor.confirmHistoricalAdd(editor.historicalSession()));const auto document=projectcodec::decode(editor.documentBytes());
        const auto root=std::find_if(document.units.begin(),document.units.end(),[](const auto& unit){return unit.id=="DEU";});
        QVERIFY(root!=document.units.end());QCOMPARE(pandoeditor::objectDisplayName(*root),std::string("독일 연방공화국"));
        QCOMPARE(copies(document,"state:deutsche-demokratische-republik").size(),std::size_t(1));
        editor.undo();QCOMPARE(editor.documentBytes(),before);
    }
    void snapshotUsesStoredReferenceDateWhenCallerOmitsDate() {
        EditorController editor;QVERIFY2(load(editor),qPrintable(editor.historicalError()));const auto before=editor.documentBytes();
        QVERIFY(editor.prepareHistoricalAdd({{"snapshotId","pilot-1991"},{"childDepth","none"}}));
        QTRY_COMPARE_WITH_TIMEOUT(editor.historicalStage(),QString("impact"),20000);QCOMPARE(editor.documentBytes(),before);
        QVERIFY(editor.confirmHistoricalAdd(editor.historicalSession()));const auto document=projectcodec::decode(editor.documentBytes());
        for(const auto& id:{"state:czechoslovakia","state:soviet-union"}) {
            const auto values=copies(document,id);QCOMPARE(values.size(),std::size_t(1));
            QVERIFY(values.front().libraryOrigin);QVERIFY(values.front().libraryOrigin->referenceDate==std::optional<std::string>("1991"));
            const auto metadata=QJsonDocument::fromJson(QByteArray::fromStdString(values.front().metadata)).object();
            QCOMPARE(metadata.value("sourceReferenceDate").toString(),QString("1991"));
        }
        editor.undo();QCOMPARE(editor.documentBytes(),before);
    }
    void bceCoarseCursorRemainsValidWithoutInventingAResult() {
        EditorController editor;QVERIFY2(load(editor),qPrintable(editor.historicalError()));const auto before=editor.documentBytes();
        editor.searchHistorical("","","all","-0044-03","");QVERIFY(editor.historicalError().isEmpty());
        // RED-01's empty expectation was wrong: the original fixed index has
        // 258 open lifetimes. Fixed Web existsAt includes those at any BCE
        // cursor; their supplied unbounded versions remain the actual source.
        const auto indexBytes=read(indexFile());QCOMPARE(QCryptographicHash::hash(indexBytes,QCryptographicHash::Sha256).toHex(),indexPin);
        QMap<QString,QString> expected;
        for(const auto& raw:QJsonDocument::fromJson(indexBytes).object().value("entities").toArray()) {
            const auto entity=raw.toObject(),lifetime=entity.value("lifetime").toObject();
            if(!lifetime.value("validFrom").isNull()||!lifetime.value("validTo").isNull())continue;
            const auto versions=entity.value("geometryVersions").toArray();QCOMPARE(versions.size(),qsizetype(1));
            const auto version=versions.first().toObject();
            QVERIFY(version.value("validFrom").isNull()&&version.value("validTo").isNull());
            expected.insert(entity.value("entityId").toString(),version.value("versionId").toString());
        }
        QCOMPARE(expected.size(),qsizetype(258));QMap<QString,QString> actual;
        for(const auto& raw:editor.historicalResults()) {
            const auto row=raw.toMap();QVERIFY(!row.value("selectedVersionId").isNull());
            QVERIFY(!actual.contains(row.value("id").toString()));
            actual.insert(row.value("id").toString(),row.value("selectedVersionId").toString());
        }
        QCOMPARE(actual,expected);QCOMPARE(status(editor).value("loadedEntities").toInt(),0);QCOMPARE(editor.documentBytes(),before);
    }
    void flagURLnativeavailableWithoutMetadataRewrite() {try {
        EditorController editor;QVERIFY2(load(editor),qPrintable(editor.historicalError()));
        // The native sample has user-layer membership outside the common
        // exchange subset. Keep it out of this common-format metadata check;
        // separate native-field rejection gates stay intact.
        QTemporaryDir temporary;QVERIFY(temporary.isValid());pandoeditor::Project common;
        common.replace(pandoeditor::ProjectDocument({},{}));
        QFile input(temporary.filePath("common.json"));QVERIFY(input.open(QIODevice::WriteOnly));
        const auto commonBytes=projectcodec::encode(common);QCOMPARE(input.write(commonBytes),qint64(commonBytes.size()));input.close();
        QVERIFY(editor.openFile(QUrl::fromLocalFile(input.fileName())));
        QVERIFY(editor.prepareHistoricalAdd(addOptions("state:KOR")));QTRY_COMPARE_WITH_TIMEOUT(editor.historicalStage(),QString("impact"),15000);
        QVERIFY(editor.confirmHistoricalAdd(editor.historicalSession()));const auto document=projectcodec::decode(editor.documentBytes());
        const auto values=copies(document,"state:KOR");QCOMPARE(values.size(),std::size_t(1));
        const auto originalMetadata=values.front().metadata;
        const QString originalPath="assets/vendor/country-flags/c09927e63705529bbf59ca6684cd9b23225dddad/svg/kr.svg";
        const auto metadata=QJsonDocument::fromJson(QByteArray::fromStdString(originalMetadata)).object();
        QCOMPARE(metadata.value("defaultFlagDataUrl").toString(),originalPath);
        const auto resolved=resolveDefaultFlag(document,pandoeditor::territorialRef(values.front().id));
        QVERIFY(resolved.available);QCOMPARE(resolved.source,QString("qrc:/defaults/flags/native/kr.svg"));
        const auto row=resultRow(editor,"state:KOR");
        QCOMPARE(row.value("flagSource").toString(),QString("qrc:/defaults/flags/native/kr.svg"));
        QVERIFY(!row.value("lineageName").toString().isEmpty());
        QCOMPARE(row.value("metadata").toMap().value("defaultFlagDataUrl").toString(),originalPath);
        QVERIFY(QFile::exists(":/defaults/flags/native/kr.svg"));
        QCOMPARE(values.front().metadata,originalMetadata);
        pandoeditor::Project project;project.replace(document);
        const auto nativeRoundtrip=projectcodec::decode(projectcodec::encode(project));
        QCOMPARE(copies(nativeRoundtrip,"state:KOR").front().metadata,originalMetadata);
        const auto webRoundtrip=projectcodec::decodeWeb(projectcodec::encodeWeb(project.snapshot()));
        const auto restored=copies(webRoundtrip,"state:KOR");QCOMPARE(restored.size(),std::size_t(1));
        const auto restoredMetadata=QJsonDocument::fromJson(QByteArray::fromStdString(restored.front().metadata)).object();
        QCOMPARE(restoredMetadata.value("defaultFlagDataUrl").toString(),originalPath);
        QCOMPARE(restored.front().sourceEntityId,std::string("state:KOR"));
        QCOMPARE(restored.front().sourceGeometryVersion,values.front().sourceGeometryVersion);
    } catch(const std::exception& error) {QFAIL(error.what());}}
    void directDateSearchCancelsPreviouslyPreparedImpact() {
        EditorController editor;QVERIFY2(load(editor),qPrintable(editor.historicalError()));const auto before=editor.documentBytes();
        editor.searchHistorical("Korea","","all","2000-01-01","");
        QVERIFY(editor.prepareHistoricalAdd(addOptions("state:KOR","2026-10-06")));
        QTRY_COMPARE_WITH_TIMEOUT(editor.historicalStage(),QString("impact"),15000);const auto token=editor.historicalSession();
        editor.searchHistorical("Korea","","all","2000-01-01","");
        QVERIFY(editor.historicalStage()!=QString("impact"));QVERIFY(!editor.confirmHistoricalAdd(token));
        QVERIFY(editor.historicalPreview().isEmpty());QCOMPARE(editor.documentBytes(),before);
    }
    void explicitDifferentSourceDoesNotReuseCachedPreviewVersion() {
        EditorController editor;QVERIFY2(load(editor),qPrintable(editor.historicalError()));
        editor.selectHistorical("state:KOR","","2026-10-06");
        QTRY_VERIFY_WITH_TIMEOUT(!editor.historicalPreview().value("polygons").toList().isEmpty(),15000);
        QVERIFY(editor.prepareHistoricalAdd(addOptions("state:NLD","2026-10-06")));
        QTRY_COMPARE_WITH_TIMEOUT(editor.historicalStage(),QString("impact"),15000);QVERIFY(editor.confirmHistoricalAdd(editor.historicalSession()));
        const auto document=projectcodec::decode(editor.documentBytes());QVERIFY(copies(document,"state:KOR").empty());
        const auto values=copies(document,"state:NLD");QCOMPARE(values.size(),std::size_t(1));
        QCOMPARE(values.front().sourceGeometryVersion,std::string("state:NLD:natural-earth-5.1.1"));
    }
};
QTEST_MAIN(TerritorialCatalogControllerTests)
#include "territorial_catalog_controller_tests.moc"

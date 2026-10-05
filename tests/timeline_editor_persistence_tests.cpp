#include "editorcontroller.h"
#include "projectgeopackage.h"
#include "webimport.h"
#include "autosavecoordinator.h"
#include "territorial_fixture.h"
#include "defaultflagresolver.h"
#include <pandoeditor/commands.h>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>
#include <future>
#include <functional>

using namespace pandoeditor;
namespace {
QByteArray read(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("fixture read");return file.readAll();}
void write(const QString& path,const QByteArray& bytes){QFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size())throw std::runtime_error("fixture write");}
ProjectDocument fixture(const char* kind){return webimport::prepare(read(QStringLiteral(PANDOEDITOR_EXCHANGE_FIXTURES)+"/"+kind+".json")).document;}
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
    QVERIFY(editor.saveFile(QUrl::fromLocalFile(saved)));QCOMPARE(QJsonDocument::fromJson(read(saved)).object()["version"].toInt(),9);
    editor.setColor("#654321");editor.setColor("#abcdef");editor.undo();QVERIFY(editor.canUndo());QVERIFY(editor.canRedo());QVERIFY(editor.dirty());
    const auto bytes=editor.documentBytes();const auto selection=editor.primaryObject();const auto paths=editor.paths();
    const auto rejected=dir.filePath("rejected.json");Project complex;complex.replace(fixture("complex"));write(rejected,projectcodec::encode(complex));
    QVERIFY(!editor.openFile(QUrl::fromLocalFile(rejected)));QCOMPARE(editor.documentBytes(),bytes);QCOMPARE(editor.primaryObject(),selection);QCOMPARE(editor.paths(),paths);QVERIFY(editor.canUndo());QVERIFY(editor.canRedo());QVERIFY(editor.dirty());
    auto invalid=QJsonDocument::fromJson(bytes).object();invalid["geometries"]=QJsonArray{};write(rejected,QJsonDocument(invalid).toJson());
    QVERIFY(!editor.openFile(QUrl::fromLocalFile(rejected)));QCOMPARE(editor.documentBytes(),bytes);QCOMPARE(editor.primaryObject(),selection);QVERIFY(editor.canUndo());QVERIFY(editor.canRedo());QVERIFY(editor.dirty());
    QVERIFY(editor.save());QCOMPARE(read(saved),bytes);QCOMPARE(read(input),projectcodec::encode(source));
    EditorController reopened(EditorControllerConfig{false,dir.filePath("private-2.json")});QVERIFY(reopened.openFile(QUrl::fromLocalFile(saved)));QCOMPARE(reopened.documentBytes(),bytes);
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

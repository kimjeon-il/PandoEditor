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
}
class TimelineEditorPersistenceTests final:public QObject {
 Q_OBJECT
private slots:
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

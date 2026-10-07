#include "editorcontroller.h"
#include "placeruntimestore.h"
#include "autosavecoordinator.h"
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#include <stdexcept>
namespace {
const QString sourceId=QStringLiteral("builtin:place:synthetic:capital");
QVariantMap builtinRef(){return {{"domain","placeBuiltin"},{"type","placeBuiltin"},{"id",sourceId}};}
QUrl fixture(){return QUrl::fromLocalFile(QFINDTESTDATA("fixtures/web-place-runtime-source/synthetic/basic/manifest.json"));}
void origin(EditorController& editor){editor.resizeMapCamera(800,600);editor.publishMapView({{"viewportWidth",800},{"viewportHeight",600},{"centerLongitude",0},{"centerLatitude",0},{"scale",100},{"translateX",400},{"translateY",300}});}
QVariantMap row(EditorController& editor,const QString& id){for(const auto& value:editor.objectRows())if(value.toMap()["id"].toString()==id)return value.toMap();return {};}
pandoeditor::ProjectDocument saved(EditorController& editor,const QString& path){
    if(!editor.saveFile(QUrl::fromLocalFile(path)))throw std::runtime_error("controller snapshot save failed");QFile file(path);
    if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("controller snapshot read failed");return projectcodec::decode(file.readAll());
}
bool autosaveContains(const QString& path){if(!QFile::exists(path))return false;try{ProjectAutosave reader(path,path+".unused-view");const auto document=projectcodec::decode(reader.restoreDocument());return document.labels.size()==1&&document.labels[0].sourcePlaceId==sourceId.toStdString();}catch(...){return false;}}
}
class PlaceControllerTests final:public QObject {
    Q_OBJECT
private slots:
    void bundledEmptySourceIsHonestAndTransient(){
        EditorController editor;const auto status=editor.placeDataStatus();
        QVERIFY(status["loaded"].toBool());QCOMPARE(status["revision"].toString(),QString("empty-v1"));
        QCOMPARE(status["manifestTileCount"].toULongLong(),qulonglong(0));QVERIFY(status["sourceRecordCount"].isNull());
        QVERIFY(status["dataSpecificBlocked"].toBool());QVERIFY(!editor.canUndo());QVERIFY(!editor.dirty());
    }
    void explicitSyntheticSourceProducesReadonlyRowsWithoutContentMutation(){
        QTemporaryDir temporary;EditorController editor;origin(editor);const auto before=saved(editor,temporary.filePath("before.json"));
        QSignalSpy status(&editor,&EditorController::placeDataChanged);QVERIFY(editor.configurePlaceData(fixture()));
        QTRY_VERIFY(!row(editor,sourceId).isEmpty());const auto item=row(editor,sourceId);
        QCOMPARE(item["domain"].toString(),QString("placeBuiltin"));QCOMPARE(item["name"].toString(),QString::fromUtf8("서울"));
        QVERIFY(item["locked"].toBool());QVERIFY(!item["editable"].toBool());QVERIFY(status.count()>0);
        QVERIFY(editor.selectObject(builtinRef()));QCOMPARE(editor.primaryObject()["domain"].toString(),QString("placeBuiltin"));
        QCOMPARE(editor.objectProperties()["displayName"].toString(),QString::fromUtf8("서울"));QVERIFY(!editor.objectProperties()["editable"].toBool());
        QVERIFY(editor.setHoverObject(builtinRef(),"test"));QVERIFY(editor.focusObject(builtinRef()));
        QVERIFY(!editor.beginContentEdit("placeBuiltin"));QVERIFY(!editor.beginContentEdit("label"));QVERIFY(!editor.beginDeleteSelection());
        QVERIFY(!editor.setLabelPinned(builtinRef(),true));QVERIFY(!editor.beginColorEdit());
        QVERIFY(!editor.canUndo());QVERIFY(!editor.dirty());QVERIFY(pandoeditor::semanticallyEqual(before,saved(editor,temporary.filePath("after.json"))));
    }
    void failedConfigurationKeepsValidSourceAndReportsFailure(){
        EditorController editor;origin(editor);QVERIFY(editor.configurePlaceData(fixture()));QTRY_VERIFY(!row(editor,sourceId).isEmpty());
        const auto revision=editor.placeDataStatus()["revision"];QSignalSpy errors(&editor,&EditorController::errorOccurred);
        QVERIFY(!editor.configurePlaceData(QUrl::fromLocalFile("missing-places-manifest.json")));
        QCOMPARE(editor.placeDataStatus()["revision"],revision);QVERIFY(!row(editor,sourceId).isEmpty());QVERIFY(errors.count()>0);
        QVERIFY(!editor.configurePlaceData(QUrl("https://invalid.example/manifest.json")));QCOMPARE(editor.placeDataStatus()["revision"],revision);
    }
    void copyIsOneUndoAndRedoRestoresPinnedPositionProvenance(){
        QTemporaryDir temporary;EditorController editor;origin(editor);const auto before=saved(editor,temporary.filePath("before.json"));
        QVERIFY(editor.configurePlaceData(fixture()));QTRY_VERIFY(!row(editor,sourceId).isEmpty());QVERIFY(editor.selectObject(builtinRef()));
        QVERIFY(editor.copySelectedPlaceForEditing());QVERIFY(editor.dirty());QCOMPARE(editor.primaryObject()["domain"].toString(),QString("label"));
        const auto copyId=editor.selectedId();QVERIFY(copyId!=sourceId);QVERIFY(!QUuid(copyId).isNull());
        QTRY_VERIFY(row(editor,sourceId).isEmpty());editor.undo();QVERIFY(!editor.canUndo());QVERIFY(!editor.dirty());QTRY_VERIFY(!row(editor,sourceId).isEmpty());
        editor.redo();QVERIFY(editor.canUndo());QVERIFY(editor.dirty());QTRY_VERIFY(row(editor,sourceId).isEmpty());
        const auto after=saved(editor,temporary.filePath("copy.json"));QCOMPARE(after.labels.size(),before.labels.size()+1);
        const auto& copy=after.labels.back();QCOMPARE(copy.sourcePlaceId, std::optional<std::string>(sourceId.toStdString()));
        QCOMPARE(copy.name,std::string("서울"));QCOMPARE(copy.kind,std::string("capital"));
        const auto settings=after.presentation.webPresentation.labelSettings.at({"label",copy.id});QVERIFY(settings.pinned);QVERIFY(settings.manualPosition);
        const auto geometry=after.geometries.get(copy.geometry);QVERIFY(geometry&&geometry->type=="Point");
        QCOMPARE(settings.manualPosition->x,geometry->points[0].x);QCOMPARE(settings.manualPosition->y,geometry->points[0].y);
    }
    void editingCopyLeavesBuiltinRecordUnchanged(){
        EditorController editor;origin(editor);QVERIFY(editor.configurePlaceData(fixture()));QTRY_VERIFY(!row(editor,sourceId).isEmpty());
        QVERIFY(editor.selectObject(builtinRef()));QVERIFY(editor.copySelectedPlaceForEditing());QVERIFY(editor.beginContentEdit("label"));
        QVERIFY(editor.updateContentField("name",QString::fromUtf8("편집한 서울")));QVERIFY(editor.previewContentEdit());QVERIFY(editor.confirmContentEdit());
        editor.undo();editor.undo();QTRY_VERIFY(!row(editor,sourceId).isEmpty());QCOMPARE(row(editor,sourceId)["name"].toString(),QString::fromUtf8("서울"));
    }
    void searchAndFocusRetainBuiltinSelectionAfterLeavingViewport(){
        EditorController editor;origin(editor);QVERIFY(editor.configurePlaceData(fixture()));editor.setSearchQuery(QString::fromUtf8("서울"));
        QTRY_VERIFY([&]{for(const auto& value:editor.searchResults())if(value.toMap()["id"].toString()==sourceId)return true;return false;}());
        QVERIFY(editor.selectObject(builtinRef()));QVERIFY(editor.publishMapView({{"centerLongitude",150},{"scale",1000}}));
        QTest::qWait(30);QCOMPARE(editor.primaryObject()["id"].toString(),sourceId);QVERIFY(editor.focusObject(builtinRef()));QVERIFY(!editor.dirty());
    }
    void placedBuiltinIsPickableThroughCanonicalScreenFlow(){
        EditorController editor;origin(editor);QVERIFY(editor.configurePlaceData(fixture()));
        QTRY_VERIFY([&]{for(const auto& value:editor.placedLabels())if(value.toMap()["ref"].toMap()["id"].toString()==sourceId)return true;return false;}());
        QVariantMap placed;for(const auto& value:editor.placedLabels())if(value.toMap()["ref"].toMap()["id"].toString()==sourceId)placed=value.toMap();
        const auto pick=editor.pickObjectScreen(placed["x"].toDouble(),placed["y"].toDouble(),1);
        QCOMPARE(pick["domain"].toString(),QString("placeBuiltin"));QCOMPARE(pick["id"].toString(),sourceId);
        editor.beginMapSelectionScreen(placed["x"].toDouble(),placed["y"].toDouble());QCOMPARE(editor.primaryObject()["id"].toString(),sourceId);
    }
    void copyQueuesCanonicalAutosaveAndProjectSwitchDropsTransientRefs(){
        QTemporaryDir temporary;EditorControllerConfig config;config.autosaveEnabled=true;config.autosaveProjectPath=temporary.filePath("autosave.json");config.autosaveViewPath=temporary.filePath("view.json");
        EditorController editor(config);origin(editor);QVERIFY(editor.configurePlaceData(fixture()));QTRY_VERIFY(!row(editor,sourceId).isEmpty());
        QVERIFY(editor.selectObject(builtinRef()));QVERIFY(editor.copySelectedPlaceForEditing());QTRY_VERIFY_WITH_TIMEOUT(autosaveContains(config.autosaveProjectPath),5000);
        const auto oldInstance=editor.projectInstanceId();editor.undo();QVERIFY(editor.newProject());QVERIFY(editor.projectInstanceId()!=oldInstance);
        QVERIFY(editor.primaryObject().isEmpty());QVERIFY(editor.hoverObject().isEmpty());QVERIFY(editor.searchQuery().isEmpty());QVERIFY(!editor.canUndo());
    }
};
QTEST_MAIN(PlaceControllerTests)
#include "place_controller_tests.moc"

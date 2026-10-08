#include "editorcontroller.h"
#include "projectcodec.h"
#include <pandoeditor/project.h>
#include <pandoeditor/timeline-view.h>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <array>

class TimelineControllerTests final : public QObject {
    Q_OBJECT
private slots:
    void datedSharedBoundaryPreservesEarlierMonth() {
        try {
        pandoeditor::ProjectDocument document({
            {"A","Alpha",{{{{0,0},{10,0},{10,10},{0,10},{0,0}}}},0x336699},
            {"B","Beta",{{{{2,2},{5,2},{5,3},{5,4},{2,4},{2,2}}}},0x993366},
            {"D","Delta",{{{{5,2},{8,2},{8,4},{5,4},{5,3},{5,2}}}},0x993399}},
            {{"countries","Countries"}});
        pandoeditor::staticParentRelation(document,"B").parentId="A";
        pandoeditor::staticParentRelation(document,"D").parentId="A";
        const auto geometry=pandoeditor::staticGeometryBinding(document,"A").geometryRef;
        pandoeditor::staticGeometryBinding(document,"A").validity.to="1914-06";
        document.timelineRecords.geometryBindings.push_back({"A:1914-07","A",{{"1914-07"},{}},geometry});
        pandoeditor::Project project;project.replace(document);
        QTemporaryDir directory;QVERIFY(directory.isValid());
        QFile file(directory.filePath(QStringLiteral("boundary.pando.json")));
        QVERIFY(file.open(QIODevice::WriteOnly));file.write(projectcodec::encode(project));file.close();
        EditorController editor;
        QVERIFY(editor.openFile(QUrl::fromLocalFile(file.fileName())));
        QVERIFY(editor.setTimelineMonth(QStringLiteral("1915-01")));
        QVERIFY(editor.selectObject({{"domain","territorial"},{"id","B"}}));
        QVERIFY(editor.beginSharedBoundaryGeometry());
        QTRY_COMPARE_WITH_TIMEOUT(editor.geometryEditState().value("boundaryStatus").toString(),QStringLiteral("ready"),10000);
        const auto view=pandoeditor::timelineDocumentView(project.document(),"1915-01");
        MapProjection projection;projection.rebuild(view.document,view.inactiveIds);
        const auto from=projection.project({5,3}),to=projection.project({5.2,3});
        QVERIFY(editor.geometrySelectNearest(from.x,from.y,.001));
        QVERIFY(editor.geometryBeginVertexDrag());
        QVERIFY(editor.geometryMoveSelectedVertex(to.x,to.y,0));
        editor.geometryEndVertexDrag(false);
        QTRY_VERIFY_WITH_TIMEOUT(!editor.geometryEditState().value("calculating").toBool(),30000);
        QVERIFY2(editor.geometryEditState().value("previewReady").toBool(),
            qPrintable(editor.geometryEditState().value("error").toString()));
        bool confirmed=editor.confirmGeometryEdit();
        if(editor.geometryEditState().value("boundaryImpactConfirmation").toBool())confirmed=editor.geometryConfirmBoundaryImpacts();
        QVERIFY2(confirmed,qPrintable(editor.geometryEditState().value("error").toString()));
        pandoeditor::Project changed;changed.replace(projectcodec::decode(editor.documentBytes()));
        const auto past=changed.resolveWorld("1914-06"),now=changed.resolveWorld("1915-01");
        QCOMPARE(past.find("B")->geometryRef, pandoeditor::staticGeometryBinding(document,"B").geometryRef);
        QVERIFY(now.find("B")->geometryRef.version>past.find("B")->geometryRef.version);
        QVERIFY(now.find("D")->geometryRef.version>past.find("D")->geometryRef.version);
        editor.undo();
        pandoeditor::Project undone;undone.replace(projectcodec::decode(editor.documentBytes()));
        QCOMPARE(undone.resolveWorld("1915-01").find("B")->geometryRef,past.find("B")->geometryRef);
        } catch(const std::exception& error) {QFAIL(error.what());}
    }
    void datedSplitCreatesHistoricalSibling() {
        try {
        pandoeditor::ProjectDocument document({
            {"A","Alpha",{{{{0,0},{10,0},{10,10},{0,10},{0,0}}}},0x336699}},
            {{"countries","Countries"}});
        const auto ref=pandoeditor::staticGeometryBinding(document,"A").geometryRef;
        pandoeditor::staticGeometryBinding(document,"A").validity.to="1914-06";
        document.timelineRecords.geometryBindings.push_back({"A:1914-07","A",{{"1914-07"},{}},ref});
        pandoeditor::Project project;project.replace(document);
        QTemporaryDir directory;QVERIFY(directory.isValid());
        QFile file(directory.filePath(QStringLiteral("split.pando.json")));
        QVERIFY(file.open(QIODevice::WriteOnly));file.write(projectcodec::encode(project));file.close();
        EditorController editor;
        QVERIFY(editor.openFile(QUrl::fromLocalFile(file.fileName())));
        QVERIFY(editor.setTimelineMonth(QStringLiteral("1915-01")));
        QVERIFY(editor.selectObject({{"domain","territorial"},{"id","A"}}));
        QVERIFY(editor.beginSplitGeometry());
        QTRY_VERIFY_WITH_TIMEOUT(!editor.geometryEditState().value("calculating").toBool(),10000);
        QVERIFY(editor.geometryAdvanceStage());
        QVERIFY(editor.geometrySelectTerritoryMethod(QStringLiteral("line")));
        QTRY_VERIFY_WITH_TIMEOUT(!editor.geometryEditState().value("calculating").toBool(),10000);
        const auto view=pandoeditor::timelineDocumentView(project.document(),"1915-01");
        MapProjection projection;projection.rebuild(view.document,view.inactiveIds);
        for(const auto point:{pandoeditor::Point{2,-1},pandoeditor::Point{2,11}}) {
            const auto xy=projection.project(point);QVERIFY(editor.geometryAddPoint(xy.x,xy.y,0));
        }
        QVERIFY(editor.geometryFinishTerritoryDraft());
        QTRY_VERIFY_WITH_TIMEOUT(editor.geometryEditState().value("canAddPart").toBool(),15000);
        QVERIFY(editor.geometryAddTerritoryPart());
        QTRY_VERIFY_WITH_TIMEOUT(editor.geometryEditState().value("canAdvance").toBool(),15000);
        QVERIFY(editor.geometryAdvanceStage());
        QVERIFY(editor.confirmGeometryEdit());
        QTRY_VERIFY_WITH_TIMEOUT(!editor.geometryEditState().value("active").toBool(),15000);
        pandoeditor::Project changed;changed.replace(projectcodec::decode(editor.documentBytes()));
        QCOMPARE(changed.resolveWorld("1914-06").entities.size(),std::size_t(1));
        QCOMPARE(changed.resolveWorld("1915-01").entities.size(),std::size_t(2));
        editor.undo();
        pandoeditor::Project undone;undone.replace(projectcodec::decode(editor.documentBytes()));
        QCOMPARE(undone.resolveWorld("1915-01").entities.size(),std::size_t(1));
        editor.redo();
        } catch(const std::exception& error) {QFAIL(error.what());}
    }
    void datedTransferKeepsEarlierMonth() {
        try {
        pandoeditor::ProjectDocument document({
            {"A","Alpha",{{{{0,0},{10,0},{10,10},{0,10},{0,0}}}},0x336699},
            {"B","Beta",{{{{2,2},{5,2},{5,5},{2,5},{2,2}}}},0x993366},
            {"C","Gamma",{{{{20,0},{30,0},{30,10},{20,10},{20,0}}}},0x339966},
            {"D","Delta",{{{{5,2},{8,2},{8,4},{5,4},{5,2}}}},0x993399}},
            {{"countries","Countries"}});
        pandoeditor::staticParentRelation(document,"B").parentId="A";
        pandoeditor::staticParentRelation(document,"B").coverageMode="partition";
        pandoeditor::staticParentRelation(document,"D").parentId="A";
        pandoeditor::staticParentRelation(document,"D").coverageMode="partition";
        const auto geometry=pandoeditor::staticGeometryBinding(document,"A").geometryRef;
        pandoeditor::staticGeometryBinding(document,"A").validity.to="1914-06";
        document.timelineRecords.geometryBindings.push_back({"A:1914-07","A",{{"1914-07"},{}},geometry});
        pandoeditor::Project project;project.replace(document);
        QTemporaryDir directory;QVERIFY(directory.isValid());
        QFile file(directory.filePath(QStringLiteral("transfer.pando.json")));
        QVERIFY(file.open(QIODevice::WriteOnly));file.write(projectcodec::encode(project));file.close();
        EditorController editor;
        QSignalSpy errors(&editor,&EditorController::errorOccurred);
        QVERIFY(editor.openFile(QUrl::fromLocalFile(file.fileName())));
        QVERIFY(editor.setTimelineMonth(QStringLiteral("1915-01")));
        QVERIFY(editor.selectObject({{"domain","territorial"},{"id","B"}}));
        QVERIFY(editor.transferSelectedSubunit(QStringLiteral("C")));
        QTRY_VERIFY_WITH_TIMEOUT(!editor.structureState().value("calculating").toBool(),30000);
        QVERIFY2(editor.confirmStructureMutation(),errors.isEmpty()?qPrintable(editor.structureState().value("detail").toString()):qPrintable(errors.last().first().toString()));
        QCOMPARE(editor.objectProperties().value("parentId").toString(),QStringLiteral("C"));
        pandoeditor::Project changed;
        changed.replace(projectcodec::decode(editor.documentBytes()));
        const auto past=changed.resolveWorld("1914-06");
        const auto now=changed.resolveWorld("1915-01");
        QVERIFY(now.find("A")->geometryRef.version>past.find("A")->geometryRef.version);
        QVERIFY(now.find("C")->geometryRef.version>past.find("C")->geometryRef.version);
        QVERIFY(editor.setTimelineMonth(QStringLiteral("1914-06")));
        QCOMPARE(editor.objectProperties().value("parentId").toString(),QStringLiteral("A"));
        QVERIFY(editor.setTimelineMonth(QStringLiteral("1915-01")));
        editor.undo();
        QCOMPARE(editor.objectProperties().value("parentId").toString(),QStringLiteral("A"));
        editor.redo();
        QCOMPARE(editor.objectProperties().value("parentId").toString(),QStringLiteral("C"));
        EditorController merging;
        QVERIFY(merging.openFile(QUrl::fromLocalFile(file.fileName())));
        QVERIFY(merging.setTimelineMonth(QStringLiteral("1915-01")));
        QVERIFY(merging.selectObject({{"domain","territorial"},{"id","B"}}));
        QVERIFY(merging.beginMergeSelection());
        QVERIFY(merging.geometryToggleProvider({{"domain","territorial"},{"id","D"}}));
        QVERIFY(merging.requestGeometryPreview());
        QTRY_VERIFY_WITH_TIMEOUT(!merging.geometryEditState().value("calculating").toBool(),30000);
        QVERIFY2(merging.geometryEditState().value("previewReady").toBool(),
            qPrintable(merging.geometryEditState().value("error").toString()));
        QVERIFY(merging.confirmGeometryEdit());
        QVERIFY(!merging.countryVisuals().contains(QStringLiteral("D")));
        QVERIFY(merging.setTimelineMonth(QStringLiteral("1914-06")));
        QVERIFY(merging.countryVisuals().contains(QStringLiteral("D")));
        } catch(const std::exception& error) {QFAIL(error.what());}
    }
    void datedProjectUsesResolvedView() {
        try {
        QFile source(QStringLiteral(PANDOEDITOR_TIMELINE_COMPLEX_FIXTURE));
        QVERIFY(source.open(QIODevice::ReadOnly));
        pandoeditor::Project project;
        project.replace(projectcodec::decodeWeb(source.readAll()));
        QTemporaryDir directory;QVERIFY(directory.isValid());
        QFile file(directory.filePath(QStringLiteral("dated.pando.json")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        const auto bytes=projectcodec::encode(project);
        QCOMPARE(file.write(bytes),qint64(bytes.size()));file.close();

        EditorController editor;
        QSignalSpy errors(&editor,&EditorController::errorOccurred);
        QVERIFY2(editor.openFile(QUrl::fromLocalFile(file.fileName())),
            errors.isEmpty()?"open failed":qPrintable(errors.last().first().toString()));
        QVERIFY(!editor.timelineMonth().isEmpty());
        const auto initialRevision=editor.revision();
        QVERIFY(!editor.dirty());
        QVERIFY(editor.setTimelineMonth(QStringLiteral("1909-12")));
        QCOMPARE(editor.timelineMonth(),QStringLiteral("1909-12"));
        QVERIFY(!editor.countryVisuals().contains(QStringLiteral("B")));
        QCOMPARE(editor.historicalCountries().size(),2);
        QCOMPARE(editor.gisExportLayers().front().toMap().value("count").toInt(),2);
        const auto hasPacket=[&](const char* id) {
            const auto scene=static_cast<MapSceneBridge*>(editor.mapSceneBridge())->sceneSnapshot();
            return scene&&std::any_of(scene->polygons.begin(),scene->polygons.end(),
                [id](const auto& packet){return packet.object.domain=="territorial"&&packet.object.id==id;});
        };
        QVERIFY(!hasPacket("B"));
        QVERIFY(!editor.selectObject({{"domain","territorial"},{"id","B"}}));
        QCOMPARE(editor.revision(),initialRevision);
        QVERIFY(!editor.dirty());
        QVERIFY(editor.setTimelineMonth(QStringLiteral("1914-06")));
        QVERIFY(editor.countryVisuals().contains(QStringLiteral("B")));
        QVERIFY(hasPacket("B"));
        QVERIFY(editor.selectObject({{"domain","territorial"},{"id","B"}}));
        QVERIFY(editor.setTimelineMonth(QStringLiteral("1920-04")));
        QCOMPARE(editor.selectedId(),QString());
        QVERIFY(!editor.countryVisuals().contains(QStringLiteral("B")));
        QVERIFY(!hasPacket("B"));
        QVERIFY(editor.shiftTimelineMonth(-1));
        QCOMPARE(editor.timelineMonth(),QStringLiteral("1920-03"));
        QVERIFY(editor.countryVisuals().contains(QStringLiteral("B")));
        QVERIFY(hasPacket("B"));
        QVERIFY(errors.isEmpty());
        QVERIFY(!editor.setTimelineMonth(QStringLiteral("invalid")));
        QCOMPARE(editor.timelineMonth(),QStringLiteral("1920-03"));
        QCOMPARE(editor.revision(),initialRevision);
        QVERIFY(!editor.dirty());
        QVERIFY(editor.setTimelineMonth(QStringLiteral("1914-07")));
        QVERIFY(editor.selectObject({{"domain","territorial"},{"id","B"}}));
        QVERIFY(editor.changeSelectedParent(QStringLiteral("C")));
        QVERIFY(editor.structureState().value("open").toBool());
        QVERIFY(editor.confirmStructureMutation());
        QCOMPARE(editor.objectProperties().value("parentId").toString(),QStringLiteral("C"));
        QVERIFY(editor.dirty());
        QVERIFY(editor.setTimelineMonth(QStringLiteral("1914-06")));
        QCOMPARE(editor.objectProperties().value("parentId").toString(),QStringLiteral("A"));
        QVERIFY(editor.setTimelineMonth(QStringLiteral("1914-07")));
        editor.undo();
        QCOMPARE(editor.objectProperties().value("parentId").toString(),QStringLiteral("A"));
        editor.redo();
        QCOMPARE(editor.objectProperties().value("parentId").toString(),QStringLiteral("C"));
        QVERIFY(editor.beginGeometryEdit());
        const auto oldGeometry=editor.geometryDraftPaths();
        QVERIFY(editor.geometrySelectNearest(0,0,1000));
        QVERIFY(editor.geometryMoveSelectedVertex(0.1,0.1));
        const auto editedGeometry=editor.geometryDraftPaths();
        QVERIFY(editedGeometry!=oldGeometry);
        QVERIFY(editor.requestGeometryPreview());
        QVERIFY(editor.geometryEditState().value("previewReady").toBool());
        QVERIFY(editor.confirmGeometryEdit());
        QVERIFY(editor.setTimelineMonth(QStringLiteral("1914-06")));
        QVERIFY(editor.beginGeometryEdit());
        QCOMPARE(editor.geometryDraftPaths(),oldGeometry);
        editor.cancelGeometryEdit();
        QVERIFY(editor.setTimelineMonth(QStringLiteral("1914-07")));
        QVERIFY(editor.beginGeometryEdit());
        QCOMPARE(editor.geometryDraftPaths(),editedGeometry);
        editor.cancelGeometryEdit();
        QVERIFY2(editor.saveFile(QUrl::fromLocalFile(file.fileName())),
            errors.isEmpty()?"save failed without error":qPrintable(errors.last().first().toString()));
        QVERIFY(!editor.dirty());
        EditorController reopened;
        QSignalSpy reopenedErrors(&reopened,&EditorController::errorOccurred);
        QVERIFY(reopened.openFile(QUrl::fromLocalFile(file.fileName())));
        QVERIFY(reopened.setTimelineMonth(QStringLiteral("1914-06")));
        QVERIFY(reopened.selectObject({{"domain","territorial"},{"id","B"}}));
        QCOMPARE(reopened.objectProperties().value("parentId").toString(),QStringLiteral("A"));
        QVERIFY(reopened.beginGeometryEdit());
        QCOMPARE(reopened.geometryDraftPaths(),oldGeometry);
        reopened.cancelGeometryEdit();
        reopened.setNameDraft(QStringLiteral("B 개명"));
        QVERIFY2(reopened.preparePendingEdits(),reopenedErrors.isEmpty()?"prepare failed without error":qPrintable(reopenedErrors.last().first().toString()));
        QVERIFY(reopened.confirmPreview());
        QCOMPARE(reopened.objectProperties().value("displayName").toString(),QStringLiteral("B 개명"));
        QVERIFY(reopened.saveFile(QUrl::fromLocalFile(file.fileName())));
        QVERIFY(!reopened.dirty());
        QVERIFY(reopened.setTimelineMonth(QStringLiteral("1914-07")));
        QCOMPARE(reopened.objectProperties().value("parentId").toString(),QStringLiteral("C"));
        QVERIFY(reopened.beginGeometryEdit());
        QCOMPARE(reopened.geometryDraftPaths(),editedGeometry);
        reopened.cancelGeometryEdit();
        EditorController deleting;
        QVERIFY(deleting.openFile(QUrl::fromLocalFile(file.fileName())));
        QVERIFY(deleting.setTimelineMonth(QStringLiteral("1914-07")));
        QVERIFY(deleting.selectObject({{"domain","territorial"},{"id","B"}}));
        QVERIFY(deleting.beginDeleteSelection());
        QVERIFY(deleting.confirmStructureMutation());
        QVERIFY(!deleting.countryVisuals().contains(QStringLiteral("B")));
        QCOMPARE(deleting.selectedId(),QString());
        QVERIFY(deleting.setTimelineMonth(QStringLiteral("1914-06")));
        QVERIFY(deleting.countryVisuals().contains(QStringLiteral("B")));
        QVERIFY(deleting.setTimelineMonth(QStringLiteral("1914-07")));
        deleting.undo();
        QVERIFY(deleting.countryVisuals().contains(QStringLiteral("B")));
        EditorController creating;
        QVERIFY(creating.openFile(QUrl::fromLocalFile(file.fileName())));
        QVERIFY(creating.setTimelineMonth(QStringLiteral("1914-07")));
        QVERIFY(creating.beginTerritorialCreate(QStringLiteral("general")));
        QVERIFY(creating.updateTerritorialCreateSetup(QStringLiteral("New Country"),QString(),QStringLiteral("D")));
        QVERIFY(creating.beginGeometryDraw());
        const auto datedView=pandoeditor::timelineDocumentView(project.document(),"1914-07");
        MapProjection projection;projection.rebuild(datedView.document,datedView.inactiveIds);
        for(const auto point:std::array<pandoeditor::Point,3>{{{20,2},{22,2},{21,4}}}) {
            const auto display=projection.project(point);
            QVERIFY(creating.geometryAddPoint(display.x,display.y));
        }
        QVERIFY2(creating.requestGeometryPreview(),qPrintable(creating.geometryEditState().value("error").toString()));
        QVERIFY(creating.confirmGeometryEdit());
        QVERIFY(creating.countryVisuals().contains(QStringLiteral("D")));
        QVERIFY(creating.setTimelineMonth(QStringLiteral("1914-06")));
        QVERIFY(!creating.countryVisuals().contains(QStringLiteral("D")));
        QVERIFY(creating.setTimelineMonth(QStringLiteral("1914-07")));
        creating.undo();
        QVERIFY(!creating.countryVisuals().contains(QStringLiteral("D")));
        creating.redo();
        QVERIFY(creating.countryVisuals().contains(QStringLiteral("D")));
        editor.undo();
        QVERIFY(editor.beginGeometryEdit());
        QCOMPARE(editor.geometryDraftPaths(),oldGeometry);
        editor.cancelGeometryEdit();
        editor.redo();
        QVERIFY(editor.beginGeometryEdit());
        QCOMPARE(editor.geometryDraftPaths(),editedGeometry);
        editor.cancelGeometryEdit();
        } catch(const std::exception& error) { QFAIL(error.what()); }
    }
};
QTEST_MAIN(TimelineControllerTests)
#include "timeline_controller_tests.moc"

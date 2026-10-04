#include "editorcontroller.h"
#include "../renderer/maprenderitem.h"
#include "../renderer/geographicimagemesh.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QThreadPool>
#include <cmath>
#include <algorithm>

class ViewNavigationTests final : public QObject {
    Q_OBJECT
private slots:
    void colorEditingForcesDetailUntilCancelled() {
        EditorController editor;QVERIFY(editor.resizeMapCamera(800,600));editor.selectCountry("DEU");
        QVERIFY(editor.beginColorEdit());
        QCOMPARE(editor.renderQuality().value("worldDetailRequested").toString(),QString("canonical"));
        editor.cancelColorEdit();
        QCOMPARE(editor.renderQuality().value("worldDetailRequested").toString(),QString("preview"));
    }
    void propertyFieldEditingForcesDetailUntilEnded() {
        EditorController editor;QVERIFY(editor.resizeMapCamera(800,600));editor.selectCountry("DEU");
        const auto token=editor.beginPropertyEdit("name");QVERIFY(!token.isEmpty());
        QCOMPARE(editor.renderQuality().value("worldDetailRequested").toString(),QString("canonical"));
        editor.endPropertyEdit(token);
        QCOMPARE(editor.renderQuality().value("worldDetailRequested").toString(),QString("preview"));
        QVERIFY(!editor.beginPropertyEdit("name").isEmpty());editor.discardPendingEdits();
        QCOMPARE(editor.renderQuality().value("worldDetailRequested").toString(),QString("preview"));
    }
    void clampedNavigationReleasesFocusButInvalidZoomDoesNot() {
        EditorController editor;QVERIFY(editor.resizeMapCamera(800,600));
        QVERIFY(editor.zoomMapCameraAt(.001,400,300));
        QVERIFY(editor.focusMapCameraRect(0,0,10000,10000,10));
        QVERIFY(!editor.zoomMapCameraAt(-1,400,300));
        QCOMPARE(editor.renderQuality().value("worldDetailRequested").toString(),QString("canonical"));
        QVERIFY(!editor.zoomMapCameraAt(.8,400,300));
        QCOMPARE(editor.renderQuality().value("worldDetailRequested").toString(),QString("preview"));
    }
    void missingCanonicalMeshRetainsReadyPreviewAndPreciseEditing() {
        QTemporaryDir directory;QVERIFY(directory.isValid());
        const QDir source(QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).filePath("../assets/world"));
        for(const char* name:{"manifest.json","countries-canonical-v0.33.0.pcg.gz",
                "world-mesh-preview-v0.33.0.bin.gz"})
            QVERIFY(QFile::copy(source.filePath(name),directory.filePath(name)));
        EditorControllerConfig config;config.bootstrapWorld=true;config.worldDataRoot=directory.path();
        EditorController editor(config);QVERIFY(editor.resizeMapCamera(800,600));
        QVERIFY(editor.zoomMapCameraAt(3.,400,300));
        QTRY_COMPARE_WITH_TIMEOUT(editor.worldStatus(),QString("canonical-mesh-unavailable"),30000);
        auto* bridge=qobject_cast<MapSceneBridge*>(editor.mapSceneBridge());QVERIFY(bridge);
        QVERIFY(bridge->sceneSnapshot()->worldBase&&bridge->sceneSnapshot()->worldBase->mesh->preview);
        QVERIFY(bridge->sceneSnapshot()->worldBase->documentReady);
        QCOMPARE(editor.renderQuality().value("worldDetailRequested").toString(),QString("canonical"));
        const auto before=editor.documentBytes();const auto doc=projectcodec::decode(before);
        const auto unit=std::find_if(doc.units.begin(),doc.units.end(),[](const auto& u){return u.id=="DEU";});
        QVERIFY(unit!=doc.units.end());const auto exact=doc.geometries.get(unit->geometry);QVERIFY(exact);
        const auto count=[](const pandoeditor::Geometry& g){std::size_t n=0;for(const auto& p:g.polygons)for(const auto& r:p)n+=r.size();return n;};
        editor.selectCountry("DEU");QVERIFY(editor.beginGeometryEdit());
        std::size_t draftVertices=0;for(const auto& path:editor.geometryDraftPaths())draftVertices+=path.toMap().value("vertices").toList().size();
        QCOMPARE(draftVertices,count(*exact));
        editor.cancelGeometryEdit();QCOMPARE(editor.documentBytes(),before);
        QVERIFY(editor.saveFile(QUrl::fromLocalFile(directory.filePath("saved.json"))));
    }
    void visibleEditorPanelForcesDetailUntilClosed() {
        EditorControllerConfig config;config.bootstrapWorld=false;config.autosaveEnabled=false;
        EditorController editor(config);QVERIFY(editor.resizeMapCamera(800,600));
        QVERIFY(QMetaObject::invokeMethod(&editor,"setMapEditorActive",Q_ARG(bool,true)));
        QCOMPARE(editor.renderQuality().value("worldDetailRequested").toString(),QString("canonical"));
        QVERIFY(editor.fitMapCamera());
        QCOMPARE(editor.renderQuality().value("worldDetailRequested").toString(),QString("canonical"));
        QVERIFY(QMetaObject::invokeMethod(&editor,"setMapEditorActive",Q_ARG(bool,false)));
        QCOMPARE(editor.renderQuality().value("worldDetailRequested").toString(),QString("preview"));
    }
    void worldLoadUsesLatestZoomWithoutChangingCanonicalDocument() {
        EditorControllerConfig config;config.bootstrapWorld=true;config.autosaveEnabled=false;
        config.worldDataRoot=QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).filePath("../assets/world");
        EditorController editor(config);QVERIFY(editor.resizeMapCamera(800,600));
        QVERIFY(editor.zoomMapCameraAt(2.,400,300)); // Initial globe band defaults to preview.
        connect(&editor,&EditorController::worldStatusChanged,&editor,[&]{
            if(editor.worldStatus()=="canonical-pending-mesh") {
                editor.zoomMapCameraAt(3./editor.mapViewState().value("zoom").toDouble(),400,300);
                editor.fitMapCamera(); // A late canonical completion must honor this latest request.
            }
        });
        QTRY_COMPARE_WITH_TIMEOUT(editor.worldStatus(),QString("canonical"),30000);
        auto* bridge=qobject_cast<MapSceneBridge*>(editor.mapSceneBridge());QVERIFY(bridge);
        auto preview=bridge->sceneSnapshot()->worldBase;QVERIFY(preview&&preview->mesh->preview&&preview->documentReady);
        const auto document=editor.documentBytes();
        const auto zoom=[&](double target){return editor.zoomMapCameraAt(target/editor.mapViewState().value("zoom").toDouble(),400,300);};
        QVERIFY(zoom(2.21));auto canonical=bridge->sceneSnapshot()->worldBase;
        QVERIFY(canonical&&!canonical->mesh->preview);
        const auto publications=bridge->scenePublicationCount();
        for(int i=0;i<4;++i){QVERIFY(zoom(i%2?2.1:1.9));QVERIFY(bridge->sceneSnapshot()->worldBase==canonical);}
        QCOMPARE(bridge->scenePublicationCount(),publications);
        QVERIFY(zoom(1.79));QVERIFY(bridge->sceneSnapshot()->worldBase==preview);
        QCOMPARE(editor.documentBytes(),document);
        editor.selectCountry("DEU");QVERIFY(editor.beginGeometryEdit());
        QVERIFY(bridge->sceneSnapshot()->worldBase==canonical);
        editor.cancelGeometryEdit();QVERIFY(bridge->sceneSnapshot()->worldBase==preview);
        QCOMPARE(editor.documentBytes(),document);
        QTemporaryDir directory;QVERIFY(directory.isValid());
        QVERIFY(editor.saveFile(QUrl::fromLocalFile(directory.filePath("world.pando.json"))));
        EditorController reopened(config);
        QVERIFY(reopened.openFile(QUrl::fromLocalFile(directory.filePath("world.pando.json"))));
        QTRY_COMPARE_WITH_TIMEOUT(reopened.worldStatus(),QString("canonical"),30000);
        auto* reopenedBridge=qobject_cast<MapSceneBridge*>(reopened.mapSceneBridge());QVERIFY(reopenedBridge);
        QVERIFY(reopenedBridge->sceneSnapshot()->worldBase&&reopenedBridge->sceneSnapshot()->worldBase->mesh->preview);
        QCOMPARE(reopened.documentBytes(),document);QCOMPARE(reopened.fileName(),QString("world.pando.json"));
        // Recovery must also choose preview at 1x while retaining exact saved geometry.
        {ProjectAutosave saved(directory.filePath("autosave.json"),directory.filePath("view.json"));
         saved.scheduleDocument(document);QVERIFY(saved.flushNow());}
        auto recovery=config;recovery.autosaveEnabled=true;
        recovery.autosaveProjectPath=directory.filePath("autosave.json");recovery.autosaveViewPath=directory.filePath("view.json");
        EditorController restored(recovery);
        QTRY_VERIFY_WITH_TIMEOUT(!restored.startupBusy(),30000);
        auto* restoredBridge=qobject_cast<MapSceneBridge*>(restored.mapSceneBridge());QVERIFY(restoredBridge);
        QVERIFY(restoredBridge->sceneSnapshot()->worldBase&&restoredBridge->sceneSnapshot()->worldBase->mesh->preview);
        QCOMPARE(restored.documentBytes(),document);
    }
    void staleWorldLoadCannotReplaceImportedProject() {
        EditorControllerConfig config;config.bootstrapWorld=true;config.autosaveEnabled=false;
        config.worldDataRoot=QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).filePath("../assets/world");
        EditorController editor(config);
        QTRY_COMPARE_WITH_TIMEOUT(editor.worldStatus(),QString("preview"),30000);
        QTemporaryDir directory;QFile sample(":/assets/sample.pando.json");QVERIFY(sample.open(QIODevice::ReadOnly));
        QFile replacement(directory.filePath("replacement.json"));QVERIFY(replacement.open(QIODevice::WriteOnly));
        replacement.write(sample.readAll());replacement.close();
        QVERIFY(editor.importProject(QUrl::fromLocalFile(replacement.fileName())));
        const auto document=editor.documentBytes();const auto instance=editor.projectInstanceId();
        QTRY_COMPARE_WITH_TIMEOUT(QThreadPool::globalInstance()->activeThreadCount(),0,30000);
        QCoreApplication::processEvents();
        QCOMPARE(editor.documentBytes(),document);QCOMPARE(editor.projectInstanceId(),instance);
        QCOMPARE(editor.worldStatus(),QString("disabled"));
        auto* bridge=qobject_cast<MapSceneBridge*>(editor.mapSceneBridge());QVERIFY(bridge);
        QVERIFY(!bridge->sceneSnapshot()->worldBase);
    }
    void projectReplacementBeforeQueuedBootstrapStaysLoaded() {
        EditorControllerConfig config;config.bootstrapWorld=true;config.autosaveEnabled=false;
        config.worldDataRoot=QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).filePath("../assets/world");
        EditorController editor(config);
        QTemporaryDir directory;QFile sample(":/assets/sample.pando.json");QVERIFY(sample.open(QIODevice::ReadOnly));
        QFile replacement(directory.filePath("replacement.json"));QVERIFY(replacement.open(QIODevice::WriteOnly));
        replacement.write(sample.readAll());replacement.close();
        QVERIFY(editor.importProject(QUrl::fromLocalFile(replacement.fileName())));
        const auto document=editor.documentBytes();const auto instance=editor.projectInstanceId();
        QCoreApplication::processEvents();
        QTRY_COMPARE_WITH_TIMEOUT(QThreadPool::globalInstance()->activeThreadCount(),0,30000);
        QCoreApplication::processEvents();
        QCOMPARE(editor.documentBytes(),document);QCOMPARE(editor.projectInstanceId(),instance);
        QCOMPARE(editor.worldStatus(),QString("disabled"));
    }
    void worldDetailUsesProjectionThresholdsAndRetainsBand() {
        EditorControllerConfig config;config.bootstrapWorld=false;config.autosaveEnabled=false;
        EditorController editor(config);QVERIFY(editor.resizeMapCamera(800,600));
        const auto detail=[&]{return editor.renderQuality().value("worldDetailRequested").toString();};
        const auto zoom=[&](double target) {
            return editor.zoomMapCameraAt(target/editor.mapViewState().value("zoom").toDouble(),400,300);
        };
        QCOMPARE(detail(),QString("preview"));
        QVERIFY(zoom(2.));QCOMPARE(detail(),QString("preview"));
        QVERIFY(zoom(2.2));QCOMPARE(detail(),QString("canonical"));
        for(int i=0;i<6;++i){QVERIFY(zoom(i%2?2.1:1.9));QCOMPARE(detail(),QString("canonical"));}
        QVERIFY(zoom(1.8));QCOMPARE(detail(),QString("preview"));
        QVERIFY(editor.setProjectionMode("flat"));QCOMPARE(detail(),QString("preview"));
        QVERIFY(zoom(2.5));QCOMPARE(detail(),QString("preview"));
        QVERIFY(zoom(2.8));QCOMPARE(detail(),QString("canonical"));
        QVERIFY(zoom(2.5));QCOMPARE(detail(),QString("canonical"));
        QVERIFY(editor.setProjectionMode("globe"));QCOMPARE(detail(),QString("preview"));
        QVERIFY(zoom(2.));QCOMPARE(detail(),QString("preview"));
        QVERIFY(editor.setProjectionMode("flat"));QCOMPARE(detail(),QString("preview"));
        QVERIFY(zoom(2.8));QCOMPARE(detail(),QString("canonical"));
        QVERIFY(zoom(2.5));
        QVERIFY(editor.setProjectionMode("globe"));QCOMPARE(detail(),QString("canonical"));
        QVERIFY(editor.setProjectionMode("flat"));QCOMPARE(detail(),QString("canonical"));
        QVERIFY(zoom(2.2));QCOMPARE(detail(),QString("preview"));
    }
    void worldDetailEditAndFocusForceHasBoundedLifetime() {
        EditorControllerConfig config;config.bootstrapWorld=false;config.autosaveEnabled=false;
        EditorController editor(config);QVERIFY(editor.resizeMapCamera(800,600));
        QVERIFY(editor.setProjectionMode("flat"));editor.selectCountry("DEU");
        const auto detail=[&]{return editor.renderQuality().value("worldDetailRequested").toString();};
        QVERIFY(editor.beginGeometryEdit());QCOMPARE(detail(),QString("canonical"));
        QVERIFY(editor.fitMapCamera());QCOMPARE(detail(),QString("canonical"));
        editor.cancelGeometryEdit();QCOMPARE(detail(),QString("preview"));
        // A large focus can end below the usual threshold; it still promotes
        // synchronously, and the next explicit camera gesture releases it.
        QVERIFY(editor.focusMapCameraRect(0,0,10000,10000,10));
        QCOMPARE(detail(),QString("canonical"));
        QVERIFY(editor.setProjectionMode("flat"));QCOMPARE(detail(),QString("preview"));
        QVERIFY(editor.focusMapCameraRect(0,0,10000,10000,10));
        QCOMPARE(detail(),QString("canonical"));
        QVERIFY(editor.resizeMapCamera(801,601));QCOMPARE(detail(),QString("canonical"));
        QVERIFY(editor.zoomMapCameraAt(1.01,400,300));QCOMPARE(detail(),QString("preview"));
        QVERIFY(editor.beginContentEdit("territorial","capital"));QCOMPARE(detail(),QString("canonical"));
        editor.cancelContentEdit();QCOMPARE(detail(),QString("preview"));
        QVERIFY(editor.focusMapCameraRect(0,0,10000,10000,10));
        QVERIFY(editor.newProject());QCOMPARE(detail(),QString("preview"));
    }
    void presentationApplyUndoRedoRetainGeometryPreparation() {
        EditorControllerConfig config;config.bootstrapWorld=false;config.autosaveEnabled=false;
        EditorController editor(config);
        editor.selectCountry("DEU");
        auto* bridge=qobject_cast<MapSceneBridge*>(editor.mapSceneBridge());QVERIFY(bridge);
        const auto before=bridge->sceneSnapshot();QVERIFY(before);
        const auto baseline=editor.renderQuality();
        for(const char* key:{"sceneDeltaUpdateCount","scenePresentationUpdateCount","scenePreparationCount","packetCacheBuilds","sceneFullBuildCount","scenePatchCount"})
            QVERIFY(baseline.contains(key));
        const auto patches=baseline.value("scenePatchCount").toULongLong();
        QSignalSpy geometry(&editor,&EditorController::geometryChanged);
        editor.setColor("#123456");
        QVERIFY(bridge->sceneSnapshot()->revisions.presentation>before->revisions.presentation);
        QCOMPARE(editor.renderQuality().value("scenePatchCount").toULongLong(),patches+1);
        editor.undo();
        QCOMPARE(editor.renderQuality().value("scenePatchCount").toULongLong(),patches+2);
        editor.redo();
        QCOMPARE(editor.renderQuality().value("scenePatchCount").toULongLong(),patches+3);
        QCOMPARE(geometry.count(),0);
        for(const char* key:{"scenePreparationCount","packetCacheBuilds","sceneFullBuildCount"})
            QCOMPARE(editor.renderQuality().value(key),baseline.value(key));
        const auto afterStyle=editor.renderQuality();
        QCOMPARE(afterStyle.value("sceneDeltaUpdateCount").toULongLong(),baseline.value("sceneDeltaUpdateCount").toULongLong()+3);
        qInfo()<<"M93_CONTROLLER_DELTA"<<"style apply/undo/redo delta"
               <<afterStyle.value("sceneDeltaUpdateCount").toULongLong()-baseline.value("sceneDeltaUpdateCount").toULongLong()
               <<"preparation delta"<<afterStyle.value("scenePreparationCount").toULongLong()-baseline.value("scenePreparationCount").toULongLong()
               <<"cache build delta"<<afterStyle.value("packetCacheBuilds").toULongLong()-baseline.value("packetCacheBuilds").toULongLong();
        editor.clearSelection();
        QVERIFY(editor.selectionItems().isEmpty());
        editor.selectCountry("DEU");
        QCOMPARE(editor.selectedId(),QString("DEU"));
        for(const char* key:{"scenePreparationCount","packetCacheBuilds","scenePatchCount"})
            QCOMPARE(editor.renderQuality().value(key),afterStyle.value(key));
        QVERIFY(bridge->frameSnapshot()->path==FrameUpdatePath::InteractionOnly);
    }
    void presentationProcessorOpacityRetainsGeometryPreparation() {
        EditorControllerConfig config;config.bootstrapWorld=false;config.autosaveEnabled=false;
        EditorController editor(config);
        auto* bridge=qobject_cast<MapSceneBridge*>(editor.mapSceneBridge());QVERIFY(bridge);
        const auto before=bridge->sceneSnapshot();QVERIFY(before);
        const auto baseline=editor.renderQuality();
        for(const char* key:{"sceneDeltaUpdateCount","scenePresentationUpdateCount","scenePreparationCount","packetCacheBuilds","sceneFullBuildCount","scenePatchCount"})
            QVERIFY(baseline.contains(key));
        QVERIFY(editor.setPresentationOpacity("countries",0.5));
        QVERIFY(bridge->sceneSnapshot()->revisions.presentation>before->revisions.presentation);
        QCOMPARE(editor.renderQuality().value("scenePatchCount").toULongLong(),
                 baseline.value("scenePatchCount").toULongLong()+1);
        for(const char* key:{"scenePreparationCount","packetCacheBuilds","sceneFullBuildCount"})
            QCOMPARE(editor.renderQuality().value(key),baseline.value(key));
        const auto after=editor.renderQuality();
        QCOMPARE(after.value("sceneDeltaUpdateCount").toULongLong(),baseline.value("sceneDeltaUpdateCount").toULongLong()+1);
        qInfo()<<"M93_CONTROLLER_DELTA"<<"presentation processor delta"
               <<after.value("sceneDeltaUpdateCount").toULongLong()-baseline.value("sceneDeltaUpdateCount").toULongLong()
               <<"preparation delta"<<after.value("scenePreparationCount").toULongLong()-baseline.value("scenePreparationCount").toULongLong()
               <<"cache build delta"<<after.value("packetCacheBuilds").toULongLong()-baseline.value("packetCacheBuilds").toULongLong();
    }
    void cameraFramesDoNotPublishOrCopyPreparedScene() {
        EditorControllerConfig config;config.bootstrapWorld=false;config.autosaveEnabled=false;
        EditorController editor(config);
        QVERIFY(editor.resizeMapCamera(800,600));
        auto* bridge=qobject_cast<MapSceneBridge*>(editor.mapSceneBridge());QVERIFY(bridge);
        const auto prepared=bridge->sceneSnapshot();QVERIFY(prepared);
        QSignalSpy scenes(bridge,&MapSceneBridge::sceneChanged);
        QSignalSpy views(bridge,&MapSceneBridge::viewChanged);
        const auto baseline=editor.renderQuality();
        for(const char* key:{"scenePreparationCount","sceneTransientUpdateCount","packetCacheBuilds","scenePublicationCount","viewFrameCount","interactionFrameCount"})QVERIFY(baseline.contains(key));
        for(int i=0;i<8;++i) {
            QVERIFY(editor.zoomMapCameraAt(1.01,400,300));
            QVERIFY(editor.publishMapView({{"rotationLongitude",double(i+1)}}));
            QVERIFY(editor.resizeMapCamera(800+i,600+i));
        }
        QVERIFY(views.count()>0);QCOMPARE(scenes.count(),0);
        QVERIFY(bridge->sceneSnapshot()==prepared);
        QVERIFY(bridge->frameSnapshot()->scene==prepared);
        QCOMPARE(bridge->frameSnapshot()->view.revision,bridge->viewState().revision);
        QCOMPARE(editor.renderQuality().value("scenePublicationCount"),baseline.value("scenePublicationCount"));
        QVERIFY(editor.renderQuality().value("viewFrameCount").toULongLong()>baseline.value("viewFrameCount").toULongLong());
        for(const char* key:{"scenePreparationCount","sceneTransientUpdateCount","packetCacheBuilds"})QCOMPARE(editor.renderQuality().value(key),baseline.value(key));
        qInfo()<<"M92_CONTROLLER_VIEW"<<"view publications"<<views.count()
               <<"scene publications"<<scenes.count()
               <<"preparation delta"<<(editor.renderQuality().value("scenePreparationCount").toULongLong()-baseline.value("scenePreparationCount").toULongLong())
               <<"scene-copy delta"<<(editor.renderQuality().value("sceneTransientUpdateCount").toULongLong()-baseline.value("sceneTransientUpdateCount").toULongLong());
        QVERIFY(editor.setProjectionMode("flat"));QVERIFY(bridge->sceneSnapshot()!=prepared);
        const auto flat=bridge->sceneSnapshot();scenes.clear();
        editor.beginMapCameraPan();
        QVERIFY(editor.updateMapCameraPan(20,10));editor.endMapCameraPan();
        QVERIFY(editor.zoomMapCameraAt(1.02,400,300));
        QCOMPARE(scenes.count(),0);QVERIFY(bridge->sceneSnapshot()==flat);
        const auto preparations=editor.renderQuality().value("scenePreparationCount");
        editor.selectCountry("DEU");QCOMPARE(editor.selectedId(),QString("DEU"));
        QCOMPARE(editor.renderQuality().value("scenePreparationCount"),preparations);
        QVERIFY(bridge->frameSnapshot()->path==FrameUpdatePath::InteractionOnly);
        QVERIFY(bridge->sceneSnapshot()->preparationIdentity==flat->preparationIdentity);
        const auto beforeStyle=bridge->sceneSnapshot();
        QVERIFY(editor.zoomMapCameraAt(1.02,400,300));
        editor.setColor("#123456");
        QVERIFY(bridge->sceneSnapshot()->revisions.presentation>beforeStyle->revisions.presentation);
        QVERIFY(editor.newProject());
        const auto replacement=bridge->sceneSnapshot();
        QVERIFY(replacement->preparationIdentity!=beforeStyle->preparationIdentity);
        scenes.clear();
        const auto replacementCount=editor.renderQuality().value("scenePreparationCount");
        QVERIFY(editor.zoomMapCameraAt(1.02,400,300));
        QCOMPARE(scenes.count(),0);QVERIFY(bridge->sceneSnapshot()==replacement);
        QCOMPARE(editor.renderQuality().value("scenePreparationCount"),replacementCount);
    }
    void geometryPatchRemainsEligibleAfterCameraChange() {
        EditorController editor;editor.selectCountry("DEU");
        QVERIFY(editor.setProjectionMode("flat"));
        QVERIFY(editor.beginGeometryEdit());
        const auto paths=editor.geometryDraftPaths();QVERIFY(!paths.isEmpty());
        const auto vertices=paths.front().toMap().value("vertices").toList();QVERIFY(!vertices.isEmpty());
        const auto vertex=vertices.front().toMap();const auto x=vertex.value("x").toDouble(),y=vertex.value("y").toDouble();
        QVERIFY(editor.geometrySelectNearest(x,y,0.01));
        QVERIFY(editor.geometryMoveSelectedVertex(x+0.1,y+0.1));
        const auto patches=editor.renderQuality().value("scenePatchCount").toULongLong();
        QVERIFY(editor.zoomMapCameraAt(1.02,400,300));
        QVERIFY(editor.requestGeometryPreview());
        QTRY_VERIFY_WITH_TIMEOUT(editor.geometryEditState().value("previewReady").toBool(),5000);
        QVERIFY(editor.confirmGeometryEdit());
        QCOMPARE(editor.renderQuality().value("scenePatchCount").toULongLong(),patches+1);
    }
    void firstRunIsGlobeAndProjectionCamerasRemainIndependent() {
        EditorController editor;QCOMPARE(editor.projectionMode(),QStringLiteral("globe"));
        QVERIFY(editor.publishMapView({{"viewportWidth",800.},{"viewportHeight",600.},{"scale",240.},
            {"translateX",400.},{"translateY",300.},{"centerLongitude",35.},{"centerLatitude",12.}}));
        const auto globe=editor.mapViewState();QCOMPARE(globe.value("centerLongitude").toDouble(),35.);
        QVERIFY(editor.setProjectionMode("flat"));QCOMPARE(editor.projectionMode(),QStringLiteral("flat"));
        QVERIFY(editor.publishMapView({{"viewportWidth",900.},{"viewportHeight",500.},{"scale",180.},
            {"translateX",450.},{"translateY",250.},{"centerLongitude",-20.},{"centerLatitude",4.}}));
        const auto flat=editor.mapViewState();QCOMPARE(flat.value("centerLongitude").toDouble(),-20.);
        QVERIFY(editor.setProjectionMode("globe"));
        QCOMPARE(editor.mapViewState().value("centerLongitude").toDouble(),35.);
        QCOMPARE(editor.mapViewState().value("scale").toDouble(),240.);
        QVERIFY(editor.setProjectionMode("flat"));
        QCOMPARE(editor.mapViewState().value("centerLongitude").toDouble(),-20.);
        QCOMPARE(editor.mapViewState().value("viewportWidth").toDouble(),900.);
    }
    void engineCameraOwnsViewportZoomPanAndFit() {
        EditorController editor;
        QVERIFY(editor.resizeMapCamera(800,600));
        auto globe=editor.mapViewState();
        QCOMPARE(globe.value("viewportWidth").toDouble(),800.);
        QCOMPARE(globe.value("viewportHeight").toDouble(),600.);
        QVERIFY(std::abs(globe.value("globeZoom").toDouble()-1.)<1e-9);
        QVERIFY(editor.setProjectionMode("flat"));
        const auto before=editor.mapViewState();
        const double oldZoom=before.value("flatZoom").toDouble();
        QVERIFY(editor.zoomMapCameraAt(2,213,177));
        const auto zoomed=editor.mapViewState();
        QVERIFY(std::abs(zoomed.value("flatZoom").toDouble()-oldZoom*2)<1e-9);
        editor.beginMapCameraPan();
        QVERIFY(editor.updateMapCameraPan(30,-20));
        editor.endMapCameraPan();
        const auto panned=editor.mapViewState();
        QVERIFY(std::abs(panned.value("panX").toDouble()-zoomed.value("panX").toDouble()-30)<1e-8);
        QVERIFY(std::abs(panned.value("panY").toDouble()-zoomed.value("panY").toDouble()+20)<1e-8);
        QVERIFY(editor.fitMapCamera());
        const auto fitted=editor.mapViewState();
        QVERIFY(std::abs(fitted.value("flatZoom").toDouble()-1.)<1e-9);
        QVERIFY(std::abs(fitted.value("panX").toDouble())<1e-9);
        QVERIFY(std::abs(fitted.value("panY").toDouble())<1e-9);
    }
    void viewportResourcesCoalesceAndWaitForInteractionSettle() {
        EditorController editor;
        QVERIFY(editor.resizeMapCamera(800,600));
        QTRY_VERIFY_WITH_TIMEOUT(
            editor.renderQuality().value("viewportResourceIssuedRequests").toULongLong()>=1,1000);
        const auto before=editor.renderQuality().value("viewportResourceIssuedRequests").toULongLong();
        QTRY_VERIFY_WITH_TIMEOUT(editor.renderQuality().value("labelLayouts").toULongLong()>=1,1000);
        const auto labelLayouts=editor.renderQuality().value("labelLayouts").toULongLong();
        const auto labelQueries=editor.renderQuality().value("labelQueries").toULongLong();
        const auto labelReprojects=editor.renderQuality().value("labelReprojects").toULongLong();

        editor.beginMapInteraction();
        QVERIFY(editor.zoomMapCameraAt(1.1,400,300));
        QVERIFY(editor.zoomMapCameraAt(1.1,400,300));
        QTest::qWait(ViewportResourceScheduler::SettleDelayMs*3);
        QCOMPARE(editor.renderQuality().value("viewportResourceIssuedRequests").toULongLong(),before);
        QVERIFY(editor.renderQuality().value("viewportResourceDeferredUpdates").toULongLong()>=2);
        QCOMPARE(editor.renderQuality().value("labelLayouts").toULongLong(),labelLayouts);
        QCOMPARE(editor.renderQuality().value("labelQueries").toULongLong(),labelQueries);
        QVERIFY(editor.renderQuality().value("labelReprojects").toULongLong()>labelReprojects);
        editor.pickObjectScreen(400,300,1);
        QCOMPARE(editor.renderQuality().value("labelQueries").toULongLong(),labelQueries);

        editor.endMapInteraction();
        QTRY_COMPARE_WITH_TIMEOUT(
            editor.renderQuality().value("viewportResourceIssuedRequests").toULongLong(),
            before+1,1000);
        QCOMPARE(editor.renderQuality().value("viewportResourcePending").toBool(),false);
        QTRY_VERIFY_WITH_TIMEOUT(editor.renderQuality().value("labelLayouts").toULongLong()>labelLayouts,1000);
    }
    void screenPickingRejectsOutsideGlobeAndPublishesViewport() {
        EditorController editor;
        QVERIFY(editor.publishMapView({{"viewportWidth",800.},{"viewportHeight",600.},{"scale",240.},
            {"translateX",400.},{"translateY",300.},{"centerLongitude",0.},{"centerLatitude",0.}}));
        QCOMPARE(editor.mapViewState().value("viewportHeight").toDouble(),600.);
        QVERIFY(editor.pickObjectScreen(799,599,1).isEmpty());
        editor.beginMapSelectionScreen(799,599,false,1);
        QVERIFY(editor.selectionItems().isEmpty());
    }
    void cpuFallbackConsumesThePublishedSceneAndView() {
        EditorController editor;MapRenderItem renderer;
        renderer.setSceneBridge(editor.mapSceneBridge());
        QVERIFY(renderer.sceneRevision()>0);
        QCOMPARE(renderer.viewRevision(),editor.mapViewState().value("revision").toULongLong());
        QVERIFY(editor.setProjectionMode("flat"));
        QCOMPARE(renderer.viewRevision(),editor.mapViewState().value("revision").toULongLong());
    }
    void globeLabelsConsumeThePublishedView() {
        EditorController editor;
        QVERIFY(editor.publishMapView({{"viewportWidth",800.},{"viewportHeight",600.},{"scale",240.},
            {"translateX",400.},{"translateY",300.},{"centerLongitude",10.},{"centerLatitude",20.}}));
        QTRY_VERIFY_WITH_TIMEOUT(!editor.placedLabels().isEmpty(),1000);
        const auto labels=editor.placedLabels();
        for(const auto& value:labels) {
            const auto row=value.toMap();
            QVERIFY(std::hypot(row.value("x").toDouble()-400,row.value("y").toDouble()-300)<=241);
        }
    }
    void geographicImageMeshUsesOneProjectionSnapshot() {
        MapViewState flat;flat.viewportWidth=800;flat.viewportHeight=600;flat.scale=200;
        flat.translateX=400;flat.translateY=300;
        const auto flatMesh=buildGeographicImageMesh({-180,-90,180,90},flat,8,4);
        QCOMPARE(flatMesh.indices.size(),std::size_t(8*4*6));
        auto globe=flat;globe.mode=ProjectionMode::Globe;
        const auto globeMesh=buildGeographicImageMesh({-180,-90,180,90},globe,32,16);
        QVERIFY(!globeMesh.indices.empty());
        QVERIFY(globeMesh.indices.size()<std::size_t(32*16*6));
    }
    void appliedAppearancePersistsButPreviewDoesNotLeak() {
        QTemporaryDir directory;QVERIFY(directory.isValid());
        EditorControllerConfig config;config.appearancePath=directory.filePath("appearance.json");
        {
            EditorController editor(config);editor.beginAppearancePreview();
            QVERIFY(editor.previewAppearance({{"theme","dark"},{"accentPreset","pink"},
                {"statusBarVisible",false},{"smoothLines",false}}));
            QVERIFY(editor.applyAppearancePreview());
        }
        EditorController restored(config);
        const auto applied=restored.appearancePreferences();
        QCOMPARE(applied.value("theme").toString(),QString("dark"));
        QCOMPARE(applied.value("accentPreset").toString(),QString("pink"));
        QCOMPARE(applied.value("statusBarVisible").toBool(),false);
        QCOMPARE(applied.value("smoothLines").toBool(),false);
        restored.beginAppearancePreview();QVERIFY(restored.previewAppearance({{"theme","light"}}));
        restored.cancelAppearancePreview();
        QCOMPARE(restored.appearancePreferences().value("theme").toString(),QString("dark"));
    }
};

QTEST_MAIN(ViewNavigationTests)
#include "view_navigation_tests.moc"

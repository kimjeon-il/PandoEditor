#include "ui_navigation.h"
#include "editorcontroller.h"
#include "windowsframe.h"
#include <pandoeditor/geometrypredicates.h>
#include "gpumapitem.h"
#include "maprenderitem.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>
#include <QImage>
#include <QFontDatabase>
#include <functional>
#include <QFile>
#include <QDir>
#include <algorithm>
#include <cmath>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#undef near
#endif

static QQuickItem* visualItem(QQuickItem* root,const QString& name)
{
    if(root->objectName()==name) return root;
    for(auto child:root->childItems()) if(auto item=visualItem(child,name)) return item;
    return nullptr;
}
static QQuickItem* placedLabel(QQuickItem* root,const QString& id)
{
    if(root->objectName()=="mapPlacedLabel" &&
       root->property("modelData").toMap().value("ref").toMap().value("id").toString()==id) return root;
    for(auto child:root->childItems()) if(auto item=placedLabel(child,id)) return item;
    return nullptr;
}
static void exposeForTest(QQuickWindow* window)
{
    if(qEnvironmentVariable("QT_QPA_PLATFORM")=="windows") {
        window->hide(); window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    }
    QTest::qWait(200); window->grabWindow();
}
static QImage capture(QQuickWindow* window)
{
    window->grabWindow(); QTest::qWait(100);
    window->grabWindow(); QTest::qWait(100);
    return window->grabWindow();
}
static void typeText(QQuickWindow* window,const QByteArray& text)
{
    for(char character:text) QTest::keyClick(window,character);
}
static QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
static bool clickControl(QQuickWindow* window,const QString& name)
{
    enterExistingControlRoute(window,name);
    auto item=visualItem(window->contentItem(),name);
    if(!item||!item->isVisible()||!item->isEnabled()) return false;
    for(auto parent=item->parentItem();parent;parent=parent->parentItem()) {
        if(parent->property("contentY").isValid()) {
            auto content=qvariant_cast<QQuickItem*>(parent->property("contentItem"));
            if(content) {
                auto y=item->mapToItem(content,QPointF()).y();
                auto maxY=std::max(0.0,parent->property("contentHeight").toDouble()-parent->height());
                parent->setProperty("contentY",std::clamp(y-16.0,0.0,maxY));
            }
        }
    }
    window->grabWindow(); QTest::qWait(100);
    QRectF visibleRect(item->mapToScene(QPointF()),QSizeF(item->width(),item->height()));
    for(auto parent=item->parentItem();parent;parent=parent->parentItem())if(parent->clip())
        visibleRect=visibleRect.intersected(QRectF(parent->mapToScene(QPointF()),QSizeF(parent->width(),parent->height())));
    visibleRect=visibleRect.intersected(QRectF(QPointF(),window->size()));
    if(visibleRect.isEmpty())return false;
    auto center=visibleRect.center().toPoint();
    if(!QRect(QPoint(),window->size()).contains(center)) return false;
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,center);
    QTest::qWait(80); return true;
}

#include "native_performance_probe.h"

class UiTests:public QObject {
    Q_OBJECT
private slots:
    void annexSelectionPointerFlow_data() {
        QTest::addColumn<int>("width");
        QTest::newRow("desktop") << 1100;
        QTest::newRow("mobile-360") << 360;
    }
    void annexSelectionPointerFlow() {
        QFETCH(int,width);
        using namespace pandoeditor;
        QTemporaryDir dir;
        ProjectDocument document({
            {"A","Target",{{{{0,0},{10,0},{10,10},{0,10},{0,0}}}},0x112233},
            {"B","Donor",{{{{10,0},{20,0},{20,10},{10,10},{10,0}}}},0x445566}},
            {{"countries","Countries"}});
        Project project;project.replace(document);
        QFile file(dir.filePath("annex-selection.json"));QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write(projectcodec::encode(project))>0);file.close();
        EditorControllerConfig config;config.mobileMode=width==360;config.bootstrapWorld=false;config.autosaveEnabled=false;
        EditorController editor(config);QVERIFY(editor.openFile(QUrl::fromLocalFile(file.fileName())));
        editor.selectCountry("A");QVERIFY(editor.setProjectionMode("flat"));
        QQmlApplicationEngine engine;QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& error:errors)warnings<<error.toString();});
        engine.rootContext()->setContextProperty("editor",&editor);engine.load(QUrl("qrc:/common/Main.qml"));
        QVERIFY2(!engine.rootObjects().isEmpty(),qPrintable(warnings.join('\n')));
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());QVERIFY(window);
        window->resize(width,760);exposeForTest(window);
        auto* map=visualItem(window->contentItem(),"mapView");QVERIFY(map);
        MapProjection projection;projection.rebuild(document);
        // Place the donor in the uncovered map area, above the mobile sheet.
        const auto left=projection.project({10,5}),right=projection.project({20,5});
        const double desiredWidth=width==360?200:300;
        QVERIFY(editor.zoomMapCameraAt(desiredWidth/(std::abs(right.x-left.x)*editor.mapViewState().value("mapScale").toDouble()),map->width()/2,map->height()/2));
        const auto center=projection.project({15,5});auto camera=editor.mapViewState();
        editor.beginMapCameraPan();
        QVERIFY(editor.updateMapCameraPan((width==360?180:300)-(camera.value("originX").toDouble()+center.x*camera.value("mapScale").toDouble()),
                                          (width==360?190:300)-(camera.value("originY").toDouble()+center.y*camera.value("mapScale").toDouble())));
        editor.endMapCameraPan();
        const auto before=editor.documentBytes();const auto revision=editor.revision();
        auto unchanged=[&]{return editor.documentBytes()==before&&editor.revision()==revision&&!editor.canUndo();};
        auto click=[&](const QString& name){return clickControl(window,name)&&(name=="geometryConfirm"||unchanged());};
        auto tap=[&](Point geographic){
            const auto projected=projection.project(geographic);const auto state=editor.mapViewState();
            const QPointF local(state.value("originX").toDouble()+projected.x*state.value("mapScale").toDouble(),
                                state.value("originY").toDouble()+projected.y*state.value("mapScale").toDouble());
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,map->mapToScene(local).toPoint());QTest::qWait(80);
            QVERIFY(unchanged());
        };
        QVERIFY(click("editorAnnexAction"));
        QVERIFY(unchanged());
        tap({15,5});
        QTRY_COMPARE(editor.geometryEditState().value("providers").toList().size(),1);
        QVERIFY(click("geometryAdvance"));QVERIFY(unchanged());
        auto* line=visualItem(window->contentItem(),"geometryMethod_line");
        QVERIFY2(line&&line->isVisible(),"Root annex must expose line/polygon/components method controls");
        QVERIFY(click("geometryMethod_line"));
        QCOMPARE(editor.geometryEditState().value("activeMethod").toString(),QString("line"));
        tap({9,5});tap({21,5});QVERIFY(unchanged());
        QVERIFY(click("geometryFinishDraft"));
        QTRY_COMPARE_WITH_TIMEOUT(editor.geometryEditState().value("candidates").toList().size(),2,10000);
        QTRY_VERIFY_WITH_TIMEOUT(editor.geometryEditState().value("previewReady").toBool(),10000);
        QCOMPARE(editor.geometryEditState().value("stage").toString(),QString("selection"));
        QVERIFY(!visualItem(window->contentItem(),"geometryConfirm")->isVisible());
        for(const auto& candidate:editor.geometryEditState().value("candidates").toList()) {
            const auto row=candidate.toMap();
            auto* overlay=visualItem(map,"geometrySelectionOverlay_candidate_"+row.value("id").toString());
            QVERIFY(overlay&&overlay->isVisible());
            QCOMPARE(overlay->property("selectionPicked").toBool(),row.value("selected").toBool());
        }
        QVERIFY(capture(window).save(QDir::tempPath()+"/m972-ui-"+QString::number(width)+"-candidates.png"));
        auto candidates=editor.geometryEditState().value("candidates").toList();
        QCOMPARE(editor.geometryEditState().value("selectedCandidateIds").toList().size(),1);
        // A ready preview must not swallow candidate taps. Both halves can be selected.
        tap({15,2});tap({15,8});
        QCOMPARE(editor.geometryEditState().value("selectedCandidateIds").toList().size(),1);
        candidates=editor.geometryEditState().value("candidates").toList();
        const auto selected=editor.geometryEditState().value("selectedCandidateIds").toList().front().toString();
        QString other;for(const auto& candidate:candidates)if(candidate.toMap().value("id").toString()!=selected)other=candidate.toMap().value("id").toString();
        QVERIFY(click("geometryCandidate_"+other));
        QCOMPARE(editor.geometryEditState().value("selectedCandidateIds").toList().size(),2);
        QVERIFY(click("geometryCandidate_"+other));
        QCOMPARE(editor.geometryEditState().value("selectedCandidateIds").toList().size(),1);
        // Fix the archived half by an actual map pick, independent of equal-area candidate order.
        for(const auto& id:editor.geometryEditState().value("selectedCandidateIds").toList())
            QVERIFY(click("geometryCandidate_"+id.toString()));
        tap({15,2});
        QCOMPARE(editor.geometryEditState().value("selectedCandidateIds").toList().size(),1);
        QTRY_VERIFY_WITH_TIMEOUT(editor.geometryEditState().value("canAddPart").toBool(),10000);
        QVERIFY(click("geometryArchivePart"));
        QTRY_COMPARE(editor.geometryEditState().value("parts").toList().size(),1);
        QTRY_VERIFY_WITH_TIMEOUT(editor.geometryEditState().value("canAdvance").toBool(),10000);
        QVERIFY(unchanged());
        // Changing a method with a live drawing asks before discarding it, while
        // the archived first part remains independently owned by the session.
        QVERIFY(click("geometryMethod_polygon"));
        tap({11,1});tap({12,1});
        const auto draftBeforeSameMethod=editor.geometryDraftPaths();
        QVERIFY(click("geometryMethod_polygon"));
        QCOMPARE(editor.geometryDraftPaths(),draftBeforeSameMethod);QVERIFY(unchanged());
        QVERIFY(click("geometryMethod_line"));
        QCOMPARE(editor.geometryEditState().value("confirmationKind").toString(),QString("method"));
        QVERIFY(click("geometryCancelTerritoryChange"));
        QCOMPARE(editor.geometryEditState().value("activeMethod").toString(),QString("polygon"));
        QCOMPARE(editor.geometryEditState().value("parts").toList().size(),1);QVERIFY(unchanged());
        QVERIFY(click("geometryMethod_line"));
        QVERIFY(click("geometryConfirmTerritoryChange"));
        QCOMPARE(editor.geometryEditState().value("activeMethod").toString(),QString("line"));
        QCOMPARE(editor.geometryEditState().value("parts").toList().size(),1);QVERIFY(unchanged());
        QVERIFY(click("geometryMethod_polygon"));
        // Two further drawings accumulate on the actual remainder. Removing the
        // middle one must preserve the first and newest stable part IDs.
        for(const double x:{12.,16.}) {
            QVERIFY(click("geometryMethod_polygon"));
            QTRY_VERIFY_WITH_TIMEOUT(!editor.geometryEditState().value("selectionPending").toBool(),10000);
            for(const auto point:{Point{x,6},Point{x+2,6},Point{x+2,8},Point{x,8}})tap(point);
            QTRY_VERIFY_WITH_TIMEOUT(editor.geometryEditState().value("canFinishDraft").toBool(),10000);
            QVERIFY(click("geometryFinishDraft"));
            QTRY_VERIFY_WITH_TIMEOUT(editor.geometryEditState().value("canAddPart").toBool(),10000);
            QVERIFY(click("geometryArchivePart"));QVERIFY(unchanged());
        }
        QCOMPARE(editor.geometryEditState().value("parts").toList().size(),3);
        const auto parts=editor.geometryEditState().value("parts").toList();
        QVERIFY(click("geometryRemovePart_"+parts[1].toMap().value("id").toString()));
        QCOMPARE(editor.geometryEditState().value("parts").toList().size(),2);
        QCOMPARE(editor.geometryEditState().value("parts").toList()[0].toMap().value("id"),parts[0].toMap().value("id"));
        QCOMPARE(editor.geometryEditState().value("parts").toList()[1].toMap().value("id"),parts[2].toMap().value("id"));
        QTRY_VERIFY_WITH_TIMEOUT(editor.geometryEditState().value("canAdvance").toBool(),10000);
        QVERIFY(click("geometryReview"));
        QCOMPARE(editor.geometryEditState().value("stage").toString(),QString("review"));
        QVERIFY(visualItem(window->contentItem(),"geometryConfirm")->isVisible());
        QVERIFY(click("geometryBack"));
        QCOMPARE(editor.geometryEditState().value("stage").toString(),QString("selection"));
        QVERIFY(editor.geometryEditState().value("previewReady").toBool());QVERIFY(unchanged());
        QVERIFY(click("geometryBack"));
        QCOMPARE(editor.geometryEditState().value("stage").toString(),QString("setup"));
        QVERIFY(editor.geometryEditState().value("previewReady").toBool());
        QVERIFY(!visualItem(window->contentItem(),"geometryConfirm")->isVisible());
        QVERIFY(click("geometryRemoveProvider_B"));
        QCOMPARE(editor.geometryEditState().value("confirmationKind").toString(),QString("settings"));
        QVERIFY(click("geometryCancelTerritoryChange"));
        QCOMPARE(editor.geometryEditState().value("parts").toList().size(),2);
        QCOMPARE(editor.geometryEditState().value("providers").toList().size(),1);QVERIFY(unchanged());
        QVERIFY(click("geometryAdvance"));
        QVERIFY(editor.geometryEditState().value("previewReady").toBool());
        QVERIFY(click("geometryReview"));
        QVERIFY(click("geometryConfirm"));
        QTRY_VERIFY_WITH_TIMEOUT(!editor.geometryEditState().value("active").toBool(),10000);
        QCOMPARE(editor.revision(),revision+1);QVERIFY(editor.canUndo());
        const auto after=editor.documentBytes();QVERIFY(after!=before);
        QVERIFY(clickControl(window,"undoButton"));QCOMPARE(editor.documentBytes(),before);
        QVERIFY(clickControl(window,"redoButton"));QCOMPARE(editor.documentBytes(),after);
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        window->setProperty("allowClose",true);window->close();
    }
    void annexFullDonorPointerFlow_data() {
        QTest::addColumn<int>("width");QTest::addColumn<QString>("method");
        for(const auto width:{1100,360})for(const auto& method:{QString("line"),QString("polygon"),QString("components")})
            QTest::newRow(qPrintable(QString::number(width)+"-"+method)) << width << method;
    }
    void annexFullDonorPointerFlow() {
        QFETCH(int,width);QFETCH(QString,method);
        using namespace pandoeditor;
        QTemporaryDir dir;
        ProjectDocument document({
            {"A","Target",{{{{0,0},{10,0},{10,10},{0,10},{0,0}}}},0x112233},
            {"B","Donor",{{{{10,0},{20,0},{20,10},{10,10},{10,0}}}},0x445566}},
            {{"countries","Countries"}});
        Project source;source.replace(document);QFile file(dir.filePath("full-donor.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));QVERIFY(file.write(projectcodec::encode(source))>0);file.close();
        EditorControllerConfig config;config.mobileMode=width==360;config.bootstrapWorld=false;config.autosaveEnabled=false;
        EditorController editor(config);QVERIFY(editor.openFile(QUrl::fromLocalFile(file.fileName())));
        editor.selectCountry("A");QVERIFY(editor.setProjectionMode("flat"));
        QQmlApplicationEngine engine;QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& error:errors)warnings<<error.toString();});
        engine.rootContext()->setContextProperty("editor",&editor);engine.load(QUrl("qrc:/common/Main.qml"));
        QVERIFY2(!engine.rootObjects().isEmpty(),qPrintable(warnings.join('\n')));
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());QVERIFY(window);
        window->resize(width,760);exposeForTest(window);
        auto* map=visualItem(window->contentItem(),"mapView");QVERIFY(map);
        MapProjection projection;projection.rebuild(document);
        const auto left=projection.project({10,5}),right=projection.project({20,5});
        QVERIFY(editor.zoomMapCameraAt((width==360?200:300)/(std::abs(right.x-left.x)*editor.mapViewState().value("mapScale").toDouble()),map->width()/2,map->height()/2));
        const auto center=projection.project({15,5});const auto camera=editor.mapViewState();
        editor.beginMapCameraPan();
        QVERIFY(editor.updateMapCameraPan((width==360?180:300)-(camera.value("originX").toDouble()+center.x*camera.value("mapScale").toDouble()),
                                          (width==360?190:300)-(camera.value("originY").toDouble()+center.y*camera.value("mapScale").toDouble())));
        editor.endMapCameraPan();
        const auto before=editor.documentBytes();const auto revision=editor.revision();
        auto unchanged=[&]{return editor.documentBytes()==before&&editor.revision()==revision&&!editor.canUndo();};
        auto click=[&](const QString& name){return clickControl(window,name)&&(name=="geometryConfirm"||unchanged());};
        auto tap=[&](Point geographic){
            const auto projected=projection.project(geographic);const auto state=editor.mapViewState();
            const QPointF local(state.value("originX").toDouble()+projected.x*state.value("mapScale").toDouble(),
                                state.value("originY").toDouble()+projected.y*state.value("mapScale").toDouble());
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,map->mapToScene(local).toPoint());QTest::qWait(80);
            QVERIFY(unchanged());
        };
        QVERIFY(click("editorAnnexAction"));tap({15,5});
        QTRY_COMPARE(editor.geometryEditState().value("providers").toList().size(),1);QVERIFY(unchanged());
        QVERIFY(click("geometryAdvance"));
        QVERIFY(click("geometryMethod_"+method));
        QTRY_VERIFY_WITH_TIMEOUT(!editor.geometryEditState().value("selectionPending").toBool(),10000);
        if(method=="components") {
            QTRY_COMPARE_WITH_TIMEOUT(editor.geometryEditState().value("components").toList().size(),1,10000);
            tap({15,5});
            QCOMPARE(editor.geometryEditState().value("selectedComponentKeys").toList().size(),1);
            QTRY_VERIFY_WITH_TIMEOUT(editor.geometryEditState().value("canAdvance").toBool(),10000);
            const auto component=editor.geometryEditState().value("components").toList().front().toMap().value("key").toString();
            QVERIFY(visualItem(map,"geometrySelectionOverlay_component_"+component));
            QVERIFY(click("geometryComponent_"+component));
            QCOMPARE(editor.geometryEditState().value("selectedComponentKeys").toList().size(),0);
            QVERIFY(!editor.geometryEditState().value("previewReady").toBool());QVERIFY(unchanged());
            QVERIFY(click("geometryComponent_"+component));
            QTRY_VERIFY_WITH_TIMEOUT(editor.geometryEditState().value("canAddPart").toBool(),10000);
            QVERIFY(click("geometryArchivePart"));
            QCOMPARE(editor.geometryEditState().value("parts").toList().size(),1);
            QTRY_VERIFY_WITH_TIMEOUT(editor.geometryEditState().value("canAdvance").toBool(),10000);
            QVERIFY(click("geometryBack"));
            QVERIFY(click("geometryRemoveProvider_B"));
            QCOMPARE(editor.geometryEditState().value("confirmationKind").toString(),QString("settings"));
            QVERIFY(click("geometryConfirmTerritoryChange"));
            QCOMPARE(editor.geometryEditState().value("providers").toList().size(),0);
            QCOMPARE(editor.geometryEditState().value("parts").toList().size(),0);
            QVERIFY(!editor.geometryEditState().value("previewReady").toBool());QVERIFY(unchanged());
            tap({15,5});
            QTRY_COMPARE(editor.geometryEditState().value("providers").toList().size(),1);
            QVERIFY(click("geometryAdvance"));
            QVERIFY(click("geometryMethod_components"));
            QTRY_COMPARE_WITH_TIMEOUT(editor.geometryEditState().value("components").toList().size(),1,10000);
            tap({15,5});
            QCOMPARE(editor.geometryEditState().value("selectedComponentKeys").toList().size(),1);
        } else {
            if(method=="line") {tap({9,5});tap({21,5});}
            else for(const auto point:{Point{9,-1},Point{21,-1},Point{21,11},Point{9,11}})tap(point);
            QVERIFY(unchanged());QVERIFY(click("geometryFinishDraft"));
            QTRY_COMPARE_WITH_TIMEOUT(editor.geometryEditState().value("candidates").toList().size(),method=="line"?2:1,10000);
            if(method=="line") {
                const auto selected=editor.geometryEditState().value("selectedCandidateIds").toList();
                for(const auto& candidate:editor.geometryEditState().value("candidates").toList()) {
                    const auto id=candidate.toMap().value("id").toString();
                    if(!selected.contains(id))QVERIFY(click("geometryCandidate_"+id));
                }
                QCOMPARE(editor.geometryEditState().value("selectedCandidateIds").toList().size(),2);
            }
            QTRY_VERIFY_WITH_TIMEOUT(editor.geometryEditState().value("canAddPart").toBool(),10000);
            QVERIFY(!editor.geometryEditState().value("canAdvance").toBool());
            QVERIFY(!visualItem(window->contentItem(),"geometryConfirm")->isVisible());
            QVERIFY(click("geometryArchivePart"));
            QCOMPARE(editor.geometryEditState().value("parts").toList().size(),1);
        }
        QTRY_VERIFY_WITH_TIMEOUT(editor.geometryEditState().value("previewReady").toBool(),10000);
        QTRY_VERIFY_WITH_TIMEOUT(editor.geometryEditState().value("canAdvance").toBool(),10000);
        QCOMPARE(editor.geometryEditState().value("stage").toString(),QString("selection"));
        QVERIFY(unchanged());
        auto* river=visualItem(window->contentItem(),"geometryRiverBoundaries");
        QVERIFY(!river||!river->isEnabled());
        QVERIFY(click("geometryReview"));QVERIFY(unchanged());
        QVERIFY(click("geometryConfirm"));
        QTRY_VERIFY_WITH_TIMEOUT(!editor.geometryEditState().value("active").toBool(),10000);
        QCOMPARE(editor.revision(),revision+1);
        const auto after=editor.documentBytes();const auto result=projectcodec::decode(after);
        QCOMPARE(result.units.size(),std::size_t(1));
        QCOMPARE(result.units.front().id,std::string("A"));
        QVERIFY(std::abs(planarArea(*result.geometries.get(staticGeometryBinding(result,"A").geometryRef))-200.)<1e-6);
        QVERIFY(clickControl(window,"undoButton"));QCOMPARE(editor.documentBytes(),before);
        QVERIFY(clickControl(window,"redoButton"));QCOMPARE(editor.documentBytes(),after);
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        window->setProperty("allowClose",true);window->close();
    }
    void softwareWindowRecreationDropsOldWindowCallbacks() {
        EditorControllerConfig config;config.bootstrapWorld=false;config.autosaveEnabled=false;
        EditorController editor(config);QVERIFY(editor.resizeMapCamera(360,300));
        QQuickWindow first,second;first.resize(360,300);second.resize(360,300);
        first.setPersistentSceneGraph(false);second.setPersistentSceneGraph(false);
        first.setPersistentGraphics(false);second.setPersistentGraphics(false);
        GpuMapItem gpu(first.contentItem());gpu.setSceneBridge(editor.mapSceneBridge());
        MapRenderItem cpu(first.contentItem());cpu.setWidth(360);cpu.setHeight(300);
        cpu.setSceneBridge(editor.mapSceneBridge());
        first.show();exposeForTest(&first);QVERIFY(!capture(&first).isNull());
        const auto before=cpu.paintCount();QVERIFY(before>0);const auto revision=cpu.sceneRevision();
        QStringList observedDiagnostics;
        connect(&gpu,&GpuMapItem::rendererReadyChanged,&gpu,[&]{observedDiagnostics.push_back(gpu.diagnostic());});
        // A context error already queued before initialization must not affect its replacement.
        const auto contextGeneration=gpu.resourceGeneration();
        first.sceneGraphError(QQuickWindow::ContextNotAvailable,QStringLiteral("old context test error"));
        first.sceneGraphInitialized();QCOMPARE(gpu.resourceGeneration(),contextGeneration+1);
        QCoreApplication::processEvents();
        QVERIFY(std::none_of(observedDiagnostics.begin(),observedDiagnostics.end(),[](const auto& diagnostic) {
            return diagnostic.contains("old context test error");
        }));
        // Queue an old-window notification, then move before its delivery.
        const auto oldGeneration=gpu.resourceGeneration();
        QVERIFY(QMetaObject::invokeMethod(&first,"sceneGraphInvalidated",Qt::DirectConnection));
        QCOMPARE(gpu.resourceGeneration(),oldGeneration+1);
        gpu.setParentItem(second.contentItem());cpu.setParentItem(second.contentItem());
        const auto currentDiagnostic=gpu.diagnostic();
        QCoreApplication::processEvents();QCOMPARE(gpu.diagnostic(),currentDiagnostic);
        first.hide();first.releaseResources();second.show();exposeForTest(&second);
        QVERIFY(!capture(&second).isNull());QVERIFY(cpu.paintCount()>before);
        QCOMPARE(cpu.sceneRevision(),revision);QVERIFY(!gpu.rendererReady());
        const auto movedPaints=cpu.paintCount();second.hide();second.releaseResources();
        QCoreApplication::processEvents();second.show();
        QVERIFY(editor.zoomMapCameraAt(1.01,180,150));exposeForTest(&second);
        QVERIFY(!capture(&second).isNull());QVERIFY(cpu.paintCount()>movedPaints);
        QCOMPARE(cpu.sceneRevision(),revision);QVERIFY(!gpu.rendererReady());
        qInfo()<<"M94_SOFTWARE_WINDOW"<<"paint recovery"<<cpu.paintCount()-before
               <<"retained scene revision"<<revision;
        first.close();second.close();
    }
    void labelReprojectionPreservesModelRows() {
        LabelPlacementModel model;
        QVariantMap row{{"ref",QVariantMap{{"type","territorial"},{"id","DEU"}}},{"x",10.},{"y",20.}};
        model.setRows({row});
        QSignalSpy resets(&model,&QAbstractItemModel::modelReset);
        QSignalSpy changes(&model,&QAbstractItemModel::dataChanged);
        row["x"]=30.;model.setRows({row});
        QCOMPARE(resets.count(),0);QCOMPARE(changes.count(),1);
        QCOMPARE(qvariant_cast<QList<int>>(changes.at(0).at(2)),QList<int>{LabelPlacementModel::LabelX});
        QCOMPARE(model.data(model.index(0),Qt::UserRole).toMap().value("x").toDouble(),30.);
        model.setRows({row});QCOMPARE(changes.count(),1);
        row["name"]="updated";model.setRows({row});
        QCOMPARE(qvariant_cast<QList<int>>(changes.at(1).at(2)),QList<int>{LabelPlacementModel::Content});
        auto second=row;second["ref"]=QVariantMap{{"id","FRA"}};
        model.setRows({second,row});QCOMPARE(resets.count(),0);
        QPersistentModelIndex retained=model.index(1);
        model.setRows({row,second});QCOMPARE(retained.row(),0);QCOMPARE(resets.count(),0);
        model.setRows({row});QVERIFY(retained.isValid());QCOMPARE(retained.row(),0);
        model.setRows({});QCOMPARE(resets.count(),0);QCOMPARE(model.rowCount(),0);
    }
    void labelDelegateSurvivesCameraMotion() {
        QTemporaryDir dir;
        pandoeditor::ProjectDocument document({{"DEU","Germany",{{{{0,0},{10,0},{10,10},{0,10},{0,0}}}},0x112233}},{{"countries","Countries"}});
        pandoeditor::Project project;project.replace(document);
        QFile file(dir.filePath("flag-only.json"));QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(projectcodec::encode(project));file.close();
        EditorController editor({false,dir.filePath("private.json")});QQmlApplicationEngine engine;
        QVERIFY(editor.openFile(QUrl::fromLocalFile(file.fileName())));
        editor.selectCountry("DEU");
        QVERIFY(editor.setLabelPinned({{"domain","territorial"},{"id","DEU"}},true,5,5,true));
        engine.rootContext()->setContextProperty("editor",&editor);
        engine.load(QUrl("qrc:/common/Main.qml"));QVERIFY(!engine.rootObjects().isEmpty());
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());QVERIFY(window);
        window->resize(1100,760);exposeForTest(window);
        auto* map=visualItem(window->contentItem(),"mapView");QVERIFY(map);
        QVERIFY(editor.focusObject());
        QVERIFY(editor.zoomMapCameraAt(2,map->width()/2,map->height()/2));
        QVERIFY(editor.setPresentationVisibility("basemapLabels",false));
        QTRY_VERIFY(placedLabel(map,"DEU")!=nullptr);
        QPointer<QQuickItem> original=placedLabel(map,"DEU");const auto x=original->x();
        editor.beginMapCameraPan();QVERIFY(editor.updateMapCameraPan(8,0));
        QCoreApplication::processEvents();
        QVERIFY(original);QCOMPARE(placedLabel(map,"DEU"),original.data());QVERIFY(original->x()!=x);
        editor.endMapCameraPan();
        editor.selectCountry("DEU");
        QCOMPARE(editor.selectedFlagSource(),editor.countryVisuals().value("DEU").toMap().value("flagSource").toString());
        window->setProperty("allowClose",true);window->close();
    }
    void oversizedFlagSourcesStayDisplaySized() {
        EditorController editor(EditorControllerConfig{});QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("editor",&editor);
        engine.load(QUrl("qrc:/common/Main.qml"));QVERIFY(!engine.rootObjects().isEmpty());
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());QVERIFY(window);
        window->resize(1280,800);exposeForTest(window);
        editor.selectCountry("DEU");QTest::qWait(150);
        auto* flag=visualItem(window->contentItem(),"selectionCardFlag");QVERIFY(flag);
        // Eritrea's SVG viewBox is 14400x7200 (> 256 MB when unbounded).
        QVERIFY(flag->setProperty("source",QUrl("qrc:/defaults/flags/native/er.svg")));
        QTRY_COMPARE(flag->property("status").toInt(),1); // Image.Ready
        const auto size=flag->property("sourceSize").toSize();
        QVERIFY(size.width()>0&&size.width()<=std::ceil(flag->width()*window->devicePixelRatio()));
        QVERIFY(size.height()>0&&size.height()<=std::ceil(flag->height()*window->devicePixelRatio()));
        QVERIFY(!capture(window).isNull());
        window->setProperty("allowClose",true);window->close();
    }
    void flagPreviewUsesWebResponsiveDimensions() {
        QTemporaryDir dir;EditorController editor({false,dir.filePath("private.json")});editor.selectCountry("DEU");
        QQmlEngine engine;engine.rootContext()->setContextProperty("editor",&editor);
        QQmlComponent component(&engine,QUrl("qrc:/common/TerritorialSelectionToolbar.qml"));
        QScopedPointer<QObject> object(component.create());QVERIFY2(object,qPrintable(component.errorString()));
        auto* bar=qobject_cast<QQuickItem*>(object.data());QVERIFY(bar);
        QQuickWindow window;window.resize(1100,760);bar->setParentItem(window.contentItem());bar->setWidth(378);bar->setHeight(142);
        bar->setProperty("previewData",QVariantMap{{"domain","territorial"},{"id","DEU"},{"flagSource","qrc:/defaults/flags/native/de.svg"}});
        window.show();exposeForTest(&window);
        auto* flag=visualItem(bar,"selectionCardFlag");QVERIFY(flag);
        QCOMPARE(flag->width(),92.);QCOMPARE(flag->height(),68.);
        window.resize(700,760);QCoreApplication::processEvents();
        QCOMPARE(flag->width(),80.);QCOMPARE(flag->height(),60.);
        window.close();
    }
    void canonicalWorldShellCapture() {
        EditorControllerConfig config;config.bootstrapWorld=true;config.worldDataRoot=QStringLiteral(PANDOEDITOR_WORLD_ASSET_DIR);
        EditorController editor(config);QQmlApplicationEngine engine;QStringList warnings;
        QSignalSpy errors(&editor,&EditorController::errorOccurred);
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& values){for(const auto& value:values)warnings<<value.toString();});
        QVERIFY(editor.setTerrainMode("none"));
        engine.rootContext()->setContextProperty("editor",&editor);engine.load(QUrl("qrc:/common/Main.qml"));
        QVERIFY2(!engine.rootObjects().isEmpty(),qPrintable(warnings.join('\n')));
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());QVERIFY(window);
        window->resize(1280,800);exposeForTest(window);
        QTRY_COMPARE_WITH_TIMEOUT(editor.worldStatus(),QString("canonical"),30000);
        QVERIFY(editor.countryRows().size()>200);
        editor.beginAppearancePreview();QVERIFY(editor.previewAppearance({{"theme","light"}}));
        editor.selectCountry("DEU");QCOMPARE(editor.selectedId(),QString("DEU"));
        editor.focusObject();QTest::qWait(150);
        const auto captureDir=qEnvironmentVariable("PANDOEDITOR_UI_CAPTURE_DIR",QDir::tempPath());QVERIFY(QDir().mkpath(captureDir));
        QVERIFY(capture(window).save(captureDir+"/ui-world-selection-light.png"));
        QVERIFY(clickControl(window,"openObjectEditor"));
        QVERIFY(capture(window).save(captureDir+"/ui-world-editor-light.png"));
        QVERIFY(editor.previewAppearance({{"theme","dark"}}));
        QVERIFY(capture(window).save(captureDir+"/ui-world-editor-dark.png"));
        QCOMPARE(editor.revision(),qulonglong(0));QVERIFY(!editor.dirty());
        QCOMPARE(errors.count(),0);
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        editor.cancelAppearancePreview();window->setProperty("allowClose",true);window->close();
    }
    void webShellLayoutAndMetadata() {
        const auto captureDir=qEnvironmentVariable("PANDOEDITOR_UI_CAPTURE_DIR",QDir::tempPath());
        QVERIFY(QDir().mkpath(captureDir));
        for(bool mobile:{false,true}) {
            EditorController editor(EditorControllerConfig{mobile,{}});QQmlApplicationEngine engine;QStringList warnings;
            connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& error:errors)warnings<<error.toString();});
            engine.rootContext()->setContextProperty("editor",&editor);engine.load(QUrl("qrc:/common/Main.qml"));
            QVERIFY2(!engine.rootObjects().isEmpty(),qPrintable(warnings.join('\n')));
            auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());QVERIFY(window);
            window->resize(mobile?360:1100,760);exposeForTest(window);
            editor.beginAppearancePreview();QVERIFY(editor.previewAppearance({{"theme","light"}}));
            editor.selectCountry("DEU");QTest::qWait(100);
            auto item=[&](const char* name){return visualItem(window->contentItem(),name);};
            auto within=[&](QQuickItem* child,QQuickItem* parent){
                const auto r=child->mapRectToItem(parent,QRectF(0,0,child->width(),child->height()));
                return r.left()>=-.5&&r.top()>=-.5&&r.right()<=parent->width()+.5&&r.bottom()<=parent->height()+.5;
            };
            QCOMPARE(item("storageToolbar")->height(),48.0);
            QVERIFY(!item("importButton")||!item("importButton")->isVisible());
            auto* card=item("territorialToolbar");QVERIFY(card&&card->isVisible());
            QVERIFY(within(card,item("mapView")));
            const auto mode=mobile?QString("compact"):QString("desktop");
            QVERIFY(capture(window).save(captureDir+"/ui-"+mode+"-selection-light.png"));
            QVERIFY(clickControl(window,"openObjectEditor"));
            auto* panel=item("objectPropertyPanel");QVERIFY(panel&&panel->isVisible());
            QCOMPARE(panel->width(),mobile?360.0:320.0);
            QCOMPARE(panel->mapToScene({0,0}).x(),0.0);
            QVERIFY(within(panel,window->contentItem()));
            QVERIFY(item("objectInfoTab")->isVisible());
            QVERIFY(item("detailObjectName")->isVisible());
            QVERIFY(item("detailObjectNotes")->isVisible());
            const auto before=editor.documentBytes();
            const auto revision=editor.revision();
            QVERIFY(clickControl(window,"detailObjectName"));
            QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);typeText(window,"Germany UI");
            QTest::keyClick(window,Qt::Key_Return);QTest::qWait(100);
            QCOMPARE(editor.selectedName(),QString("Germany UI"));
            QCOMPARE(editor.revision(),revision+1);
            editor.undo();QCOMPARE(editor.documentBytes(),before);
            QVERIFY(clickControl(window,"detailObjectNotes"));
            typeText(window,"UI notes");
            QVERIFY(clickControl(window,"detailObjectName"));
            QCOMPARE(editor.memoDraft(),QString("UI notes"));
            QCOMPARE(editor.revision(),revision+3); // name commit, undo, notes commit
            editor.undo();QCOMPARE(editor.documentBytes(),before);
            QVERIFY(clickControl(window,"detailObjectName"));
            QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);typeText(window,"Pending UI");
            QCOMPARE(editor.nameDraft(),QString("Pending UI"));
            const auto navigationRevision=editor.revision();
            QVERIFY(clickControl(window,"fileMenuButton"));
            QCOMPARE(editor.documentBytes(),before);QCOMPARE(editor.revision(),navigationRevision);
            QCOMPARE(editor.nameDraft(),QString("Pending UI"));
            QVERIFY(QMetaObject::invokeMethod(window,"handleBack"));
            editor.discardPendingEdits();QCOMPARE(editor.documentBytes(),before);
            QVERIFY(capture(window).save(captureDir+"/ui-"+mode+"-editor-light.png"));
            QVERIFY(editor.previewAppearance({{"theme","dark"}}));
            QVERIFY(capture(window).save(captureDir+"/ui-"+mode+"-editor-dark.png"));
            QVERIFY(clickControl(window,"objectActionsTab"));
            QVERIFY(clickControl(window,"editorMergeAction"));
            QVERIFY(editor.geometryEditState().value("active").toBool());
            QVERIFY(!panel->isVisible());
            QVERIFY(!card->isVisible()); // cards must not cover the geometry dock or map picks
            auto* dock=item("geometryToolDock");QVERIFY(dock&&dock->isVisible());
            QVERIFY(within(dock,item("mapView")));
            QVERIFY(within(item("geometryCancel"),dock));
            QVERIFY(capture(window).save(captureDir+"/ui-"+mode+"-merge-dark.png"));
            QVERIFY(clickControl(window,"geometryCancel"));
            QCOMPARE(editor.documentBytes(),before);
            QVERIFY(clickControl(window,"fileMenuButton"));
            QVERIFY(item("importButton")->isVisible());
            QVERIFY(within(item("importButton"),window->contentItem()));
            QVERIFY(capture(window).save(captureDir+"/ui-"+mode+"-file-dark.png"));
            QVERIFY(QMetaObject::invokeMethod(window,"handleBack"));
            QVERIFY(!item("importButton")||!item("importButton")->isVisible());
            editor.cancelAppearancePreview();
            QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
            window->setProperty("allowClose",true);window->close();
        }
    }
    void mapFlagsUseWebSizeAndHorizontalNameLayout_data() {
        QTest::addColumn<QString>("projection");
        QTest::newRow("globe")<<QString("globe");
        QTest::newRow("flat")<<QString("flat");
    }
    void mapFlagsUseWebSizeAndHorizontalNameLayout() {
        QFETCH(QString,projection);
        QTemporaryDir dir;EditorController editor({false,dir.filePath("private.json")});
        pandoeditor::ProjectDocument document({{"DEU","Germany",{{{{0,0},{10,0},{10,10},{0,10},{0,0}}}},0x112233}},
                                              {{"countries","Countries"}});
        pandoeditor::Project project;project.replace(document);
        QFile file(dir.filePath("map.json"));QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(projectcodec::encode(project));file.close();
        QVERIFY(editor.openFile(QUrl::fromLocalFile(file.fileName())));
        QVERIFY(editor.setProjectionMode(projection));
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("editor",&editor);
        engine.load(QUrl("qrc:/common/Main.qml"));QVERIFY(!engine.rootObjects().isEmpty());
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());QVERIFY(window);
        window->resize(1100,760);exposeForTest(window);editor.selectCountry("DEU");
        QVERIFY(editor.setLabelPinned({{"domain","territorial"},{"id","DEU"}},true,5,5,true));
        QVERIFY(editor.zoomMapCameraAt(4,550,380));
        QTRY_VERIFY(placedLabel(window->contentItem(),"DEU")!=nullptr);
        auto* label=placedLabel(window->contentItem(),"DEU");
        auto* flag=visualItem(label,"mapPlacedFlag");auto* name=visualItem(label,"mapPlacedText");
        QVERIFY(flag&&name);QTRY_VERIFY(flag->isVisible());QVERIFY(name->isVisible());
        QCOMPARE(flag->width(),18.);QCOMPARE(flag->height(),12.);
        QVERIFY(std::abs(name->x()-flag->x()-flag->width()-5.)<.01);
        QVERIFY(std::abs((flag->y()+flag->height()/2)-(name->y()+name->height()/2))<.01);
        QVERIFY(capture(window).save("flag-parity-"+projection+".png"));
        flag->setProperty("source",QUrl("data:image/svg+xml;base64,YmFk"));
        QTRY_COMPARE(flag->property("status").toInt(),3); // Image.Error
        QVERIFY(!flag->isVisible());QVERIFY(name->isVisible());
        QCOMPARE(name->x(),0.);QCOMPARE(label->width(),name->implicitWidth());
        QVERIFY(editor.zoomMapCameraAt(.1,550,380));
        QTRY_VERIFY(placedLabel(window->contentItem(),"DEU")!=nullptr);
        label=placedLabel(window->contentItem(),"DEU");flag=visualItem(label,"mapPlacedFlag");
        QTRY_VERIFY(!flag->isVisible());
        window->setProperty("allowClose",true);window->close();
    }
    void objectEditorForcesWorldDetailUntilPanelCloses() {
        for(bool mobile:{false,true}) {
            EditorControllerConfig config;config.mobileMode=mobile;
            config.bootstrapWorld=false;config.autosaveEnabled=false;
            EditorController editor(config);QQmlApplicationEngine engine;QStringList warnings;
            connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){
                for(const auto& error:errors)warnings<<error.toString();
            });
            engine.rootContext()->setContextProperty("editor",&editor);
            engine.load(QUrl("qrc:/common/Main.qml"));
            QVERIFY2(!engine.rootObjects().isEmpty(),qPrintable(warnings.join('\n')));
            auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());QVERIFY(window);
            window->resize(mobile?360:1100,760);exposeForTest(window);
            QVERIFY(editor.setProjectionMode("flat"));QVERIFY(editor.fitMapCamera());
            auto requested=[&]{return editor.renderQuality().value("worldDetailRequested").toString();};
            QCOMPARE(requested(),QString("preview"));
            const auto document=editor.documentBytes();const auto revision=editor.revision();
            // Ordinary territorial metadata panels do not create a geometry or content session.
            editor.selectCountry("DEU");
            auto* panel=visualItem(window->contentItem(),"objectPropertyPanel");QVERIFY(panel);
            QTRY_VERIFY(panel->isVisible());
            QVERIFY(!editor.geometryEditState().value("active").toBool());
            QVERIFY(!editor.contentEditState().value("active").toBool());
            QCOMPARE(requested(),QString("canonical"));
            QVERIFY(editor.zoomMapCameraAt(.8,180,180));
            QVERIFY(editor.fitMapCamera());
            QCOMPARE(requested(),QString("canonical"));
            QVERIFY(clickControl(window,"toggleObjectEditor"));
            QTRY_VERIFY(!panel->isVisible());
            QTRY_COMPARE(requested(),QString("preview"));
            QVERIFY(clickControl(window,"openObjectEditor"));
            QTRY_VERIFY(panel->isVisible());
            QTRY_COMPARE(requested(),QString("canonical"));
            window->resize(mobile?390:1150,760);QCoreApplication::processEvents();
            QCOMPARE(requested(),QString("canonical"));
            QVERIFY(clickControl(window,"toggleObjectEditor"));
            QTRY_VERIFY(!panel->isVisible());
            QTRY_COMPARE(requested(),QString("preview"));
            QCOMPARE(editor.documentBytes(),document);QCOMPARE(editor.revision(),revision);
            QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
            window->setProperty("allowClose",true);window->close();
        }
    }
    void viewAndAppearanceControlsMatchDesktopAndCompact() {
        for(bool mobile:{false,true}) {
            EditorController editor(EditorControllerConfig{mobile,{}});QQmlApplicationEngine engine;QStringList warnings;
            connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& error:errors)warnings<<error.toString();});
            engine.rootContext()->setContextProperty("editor",&editor);engine.load(QUrl("qrc:/common/Main.qml"));
            QVERIFY2(!engine.rootObjects().isEmpty(),qPrintable(warnings.join('\n')));
            auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());QVERIFY(window);
            window->resize(mobile?390:1100,mobile?760:720);exposeForTest(window);
            QVERIFY(clickControl(window,"mapDisplayButton"));
            auto* display=window->findChild<QObject*>("mapDisplayPopup");QVERIFY(display);
            QVERIFY(capture(window).save(mobile?"m7-view-controls-compact.png":"m7-view-controls-desktop.png"));
            QVERIFY(QMetaObject::invokeMethod(display,"close"));
            QVERIFY(capture(window).save(mobile?"m7-globe-compact.png":"m7-globe-desktop.png"));
            QVERIFY(clickControl(window,"mapDisplayButton"));
            QVERIFY(clickControl(window,"projectionFlatButton"));QCOMPARE(editor.projectionMode(),QString("flat"));
            QVERIFY(QMetaObject::invokeMethod(display,"close"));
            QVERIFY(capture(window).save(mobile?"m7-flat-compact.png":"m7-flat-desktop.png"));
            QVERIFY(clickControl(window,"mapDisplayButton"));
            QVERIFY(clickControl(window,"projectionGlobeButton"));QCOMPARE(editor.projectionMode(),QString("globe"));
            QVERIFY(clickControl(window,"terrainNoneButton"));QCOMPARE(editor.terrainMode(),QString("none"));
            QVERIFY(clickControl(window,"terrainGrayButton"));QCOMPARE(editor.terrainMode(),QString("gray"));
            QVERIFY(clickControl(window,"terrainColorButton"));QCOMPARE(editor.terrainMode(),QString("color"));
            QVERIFY(QMetaObject::invokeMethod(display,"close"));

            QVERIFY(clickControl(window,"preferencesButton"));
            auto* dialog=window->findChild<QObject*>("appearancePreferencesDialog");QVERIFY(dialog&&dialog->property("visible").toBool());
            QVERIFY(clickControl(window,"themeLightButton"));QCOMPARE(editor.appearancePreferences().value("theme").toString(),QString("light"));
            QVERIFY(clickControl(window,"themeSystemButton"));QCOMPARE(editor.appearancePreferences().value("theme").toString(),QString("system"));
            QVERIFY(clickControl(window,"themeDarkButton"));QCOMPARE(editor.appearancePreferences().value("theme").toString(),QString("dark"));
            const QStringList accents={"Red","Orange","Green","Teal","Blue","Purple","Pink"};
            for(const auto& accent:accents) {
                QVERIFY(clickControl(window,"accent"+accent+"Button"));
                QCOMPARE(editor.appearancePreferences().value("accentPreset").toString(),accent.toLower());
            }
            QVERIFY(clickControl(window,"preferencesResetButton"));
            QCOMPARE(editor.appearancePreferences().value("theme").toString(),QString("system"));
            QCOMPARE(editor.appearancePreferences().value("accentPreset").toString(),QString("blue"));
            QVERIFY(clickControl(window,"themeDarkButton"));
            QVERIFY(clickControl(window,"accentRedButton"));
            QVERIFY(capture(window).save(mobile?"m7-preferences-compact.png":"m7-preferences-desktop.png"));
            QVERIFY(clickControl(window,"preferencesCancelButton"));
            QCOMPARE(editor.appearancePreferences().value("theme").toString(),QString("system"));
            QCOMPARE(editor.appearancePreferences().value("accentPreset").toString(),QString("blue"));

            QVERIFY(clickControl(window,"preferencesButton"));
            QVERIFY(clickControl(window,"themeDarkButton"));QVERIFY(clickControl(window,"accentRedButton"));
            QVERIFY(clickControl(window,"statusBarToggle"));QVERIFY(clickControl(window,"smoothLinesToggle"));
            QVERIFY(clickControl(window,"preferencesApplyButton"));
            const auto applied=editor.appearancePreferences();
            QCOMPARE(applied.value("theme").toString(),QString("dark"));
            QCOMPARE(applied.value("accentPreset").toString(),QString("red"));
            QCOMPARE(applied.value("statusBarVisible").toBool(),false);
            QCOMPARE(applied.value("smoothLines").toBool(),false);
            QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
            window->setProperty("allowClose",true);window->close();
        }
    }
    void flagOnlyIsIndependentFromNameChannel_data() {
        QTest::addColumn<int>("width");
        QTest::newRow("desktop")<<1100;
        QTest::newRow("compact")<<360;
    }
    void flagOnlyIsIndependentFromNameChannel() {
        QFETCH(int,width);
        EditorController editor(EditorControllerConfig{width==360,{}});
        editor.selectCountry("DEU");
        QVERIFY(editor.countryVisuals().value("DEU").toMap().value("flagAvailable").toBool());
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("editor",&editor);
        engine.load(QUrl("qrc:/common/Main.qml"));QVERIFY(!engine.rootObjects().isEmpty());
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]);QVERIFY(window);
        window->resize(width,width==360?640:760);exposeForTest(window);
        auto map=visualItem(window->contentItem(),"mapView");QVERIFY(map);
        QVERIFY(editor.focusObject());
        QVERIFY(editor.zoomMapCameraAt(4,map->width()/2,map->height()/2));
        QVERIFY(editor.setPresentationVisibility("basemapLabels",false));
        QTRY_VERIFY(placedLabel(map,"DEU")!=nullptr);
        auto label=placedLabel(map,"DEU");
        auto flag=visualItem(label,"mapPlacedFlag");auto name=visualItem(label,"mapPlacedText");
        QVERIFY(flag&&name);QVERIFY(flag->isVisible());QVERIFY(!name->isVisible());
        QTRY_COMPARE(flag->property("status").toInt(),1); // Image.Ready: the SVG decoded, not just the delegate.
        QVERIFY(capture(window).save(width==360?"m5-label-flag-only-compact.png":"m5-label-flag-only-desktop.png"));
        QVERIFY(editor.setPresentationVisibility("countryFlags",false));
        QTRY_VERIFY(placedLabel(map,"DEU")==nullptr);
        QVERIFY(editor.setPresentationVisibility("basemapLabels",true));
        QTRY_VERIFY(placedLabel(map,"DEU")!=nullptr);
        label=placedLabel(map,"DEU");flag=visualItem(label,"mapPlacedFlag");name=visualItem(label,"mapPlacedText");
        QVERIFY(flag&&name);QVERIFY(!flag->isVisible());QVERIFY(name->isVisible());
        window->close();
    }
    void labelSafeAreaVisuallyExcludesBottomControls_data() {
        QTest::addColumn<int>("width");
        QTest::newRow("desktop")<<1100;
        QTest::newRow("compact")<<360;
    }
    void labelSafeAreaVisuallyExcludesBottomControls() {
        QFETCH(int,width);
        using namespace pandoeditor;
        QTemporaryDir dir;
        ProjectDocument document({{"A","Alpha",{{{{0,0},{10,0},{10,10},{0,10},{0,0}}}},0x112233}},
                                 {{"countries","Countries"}});
        document.documentId="safe-area-ui";
        Project project;project.replace(document);
        const auto path=dir.filePath("safe-area.json");QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));QVERIFY(file.write(projectcodec::encode(project))>0);file.close();
        EditorController editor(EditorControllerConfig{width==360,dir.filePath("private.json")});
        QVERIFY(editor.openFile(QUrl::fromLocalFile(path)));
        QVERIFY(editor.setProjectionMode("flat"));
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("editor",&editor);
        engine.load(QUrl("qrc:/common/Main.qml"));QVERIFY(!engine.rootObjects().isEmpty());
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]);QVERIFY(window);
        window->resize(width,width==360?640:760);exposeForTest(window);
        auto map=visualItem(window->contentItem(),"mapView");QVERIFY(map);
        const auto geometry=editor.paths().front().toMap();
        const double centerY=geometry.value("top").toDouble()+geometry.value("height").toDouble()/2;
        auto moveLabel=[&](double screenY) {
            QVERIFY(editor.fitMapCamera());
            QTest::qWait(ViewportResourceScheduler::SettleDelayMs*2);
            const auto state=editor.mapViewState();
            const auto current=state.value("originY").toDouble()+
                centerY*state.value("mapScale").toDouble();
            editor.beginMapInteraction();editor.beginMapCameraPan();
            QVERIFY(editor.updateMapCameraPan(0,screenY-current));
            editor.endMapCameraPan();editor.endMapInteraction();
            QTest::qWait(ViewportResourceScheduler::SettleDelayMs*2);
        };
        moveLabel(map->height()-(width==360?115:50));
        QTRY_VERIFY(visualItem(map,"mapPlacedLabel")!=nullptr);
        QVERIFY(capture(window).save(width==360?"m5-label-safe-area-compact.png":"m5-label-safe-area-desktop.png"));
        moveLabel(map->height()-10);
        QTRY_VERIFY(visualItem(map,"mapPlacedLabel")==nullptr);
        editor.selectCountry("A");
        QTRY_VERIFY(visualItem(map,"mapPlacedLabel")!=nullptr);
        QVERIFY(capture(window).save(width==360?"m5-label-selected-compact.png":"m5-label-selected-desktop.png"));
        window->close();
    }
    void mapLabelsAndFlagsRemainAboveSelectionEmphasis_data() {
        QTest::addColumn<int>("width");
        QTest::newRow("desktop")<<1100;
        QTest::newRow("compact")<<360;
    }
    void mapLabelsAndFlagsRemainAboveSelectionEmphasis() {
        QFETCH(int,width);
        EditorController editor(EditorControllerConfig{width==360,{}});
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("editor",&editor);
        engine.load(QUrl("qrc:/common/Main.qml"));QVERIFY(!engine.rootObjects().isEmpty());
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]);QVERIFY(window);
        window->resize(width,width==360?640:760);exposeForTest(window);
        editor.selectCountry("DEU");
        auto map=visualItem(window->contentItem(),"mapView");QVERIFY(map);
        auto painter=visualItem(map,"canonicalMapRenderer");QVERIFY(painter);
        QTRY_VERIFY(painter->property("sceneRevision").toULongLong()>0);
        QCOMPARE(editor.selectionItems().size(),1);
        QTRY_VERIFY(visualItem(map,"mapPlacedLabel")!=nullptr);
        auto label=visualItem(map,"mapPlacedLabel");
        auto flag=visualItem(label,"mapPlacedFlag");
        auto text=visualItem(label,"mapPlacedText");
        QVERIFY(flag&&text);
        QVERIFY(label->z()>painter->z());
        QCOMPARE(flag->parentItem(),label);
        QCOMPARE(text->parentItem(),label);
        QVERIFY(label->childItems().indexOf(flag)<label->childItems().indexOf(text));
        window->close();
    }
    void gisExportPanelAtDesktopAnd360px() {
        for(bool mobile:{false,true}) {
            EditorController editor(EditorControllerConfig{mobile,{}});
            QQmlApplicationEngine engine;QStringList warnings;
            connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){
                for(const auto& error:errors)warnings<<error.toString();
            });
            engine.rootContext()->setContextProperty("editor",&editor);
            engine.load(QUrl("qrc:/common/Main.qml"));
            QVERIFY2(!engine.rootObjects().isEmpty(),qPrintable(warnings.join('\n')));
            auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]);QVERIFY(window);
            window->resize(mobile?360:1100,mobile?640:760);exposeForTest(window);
            QVERIFY(clickControl(window,"gisExportButton"));
            auto* panel=window->findChild<QObject*>("gisExportPanel");QVERIFY(panel);
            QTRY_VERIFY(panel->property("visible").toBool());
            QVERIFY(panel->property("width").toDouble()<=window->width());
            QVERIFY(visualItem(window->contentItem(),"gisExportFormat"));
            auto* country=visualItem(window->contentItem(),"gisExportLayer_countries");
            QVERIFY(country&&country->isVisible());
            auto* confirm=visualItem(window->contentItem(),"gisExportConfirm");
            QVERIFY(confirm&&confirm->isEnabled());
            QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
            QMetaObject::invokeMethod(panel,"close");
            QTest::qWait(150);
            enterExistingControlRoute(window,"projectGpkgExportButton");
            QVERIFY(visualItem(window->contentItem(),"projectGpkgExportButton"));
            QVERIFY(window->findChild<QObject*>("projectGpkgSaveDialog"));
            QVERIFY(window->findChild<QObject*>("openDialog"));
            window->close();
        }
    }
    void gisImportPanelAtDesktopAnd360px() {
        for(bool mobile:{false,true}) {
            EditorController editor(EditorControllerConfig{mobile,{}});
            QQmlApplicationEngine engine;QStringList warnings;
            connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){
                for(const auto& error:errors)warnings<<error.toString();
            });
            engine.rootContext()->setContextProperty("editor",&editor);
            engine.load(QUrl("qrc:/common/Main.qml"));
            QVERIFY2(!engine.rootObjects().isEmpty(),qPrintable(warnings.join('\n')));
            auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]);QVERIFY(window);
            window->resize(mobile?360:1100,mobile?640:760);exposeForTest(window);
            QVERIFY(clickControl(window,"gisImportButton"));
            auto* panel=window->findChild<QObject*>("gisImportPanel");QVERIFY(panel);
            QTRY_VERIFY(panel->property("visible").toBool());
            QVERIFY(panel->property("width").toDouble()<=window->width());
            QVERIFY(visualItem(window->contentItem(),"gisLayerChoice"));
            QVERIFY(visualItem(window->contentItem(),"gisConfirm"));
            QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
            QMetaObject::invokeMethod(panel,"close");
            window->close();
        }
    }
    void historicalLibraryPanelAtDesktopAnd360px() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        QFile file(dir.filePath("historical.json"));QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray json=R"({"schemaVersion":2,"entities":[{"libraryId":"historical-country:fixture","type":"country","canonicalName":"Fixture","geometryVersions":[{"id":"v1","geometry":{"type":"Polygon","coordinates":[[[70,0],[72,0],[72,2],[70,2],[70,0]]]}}]}],"snapshots":[]})";
        QCOMPARE(file.write(json),json.size());file.close();
        for(bool mobile:{false,true}) {
            EditorController editor(EditorControllerConfig{mobile,{}});
            QVERIFY(editor.loadHistoricalLibrary(QUrl::fromLocalFile(file.fileName())));
            QQmlApplicationEngine engine;QStringList warnings;
            connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){
                for(const auto& e:errors)warnings<<e.toString();
            });
            engine.rootContext()->setContextProperty("editor",&editor);
            engine.load(QUrl("qrc:/common/Main.qml"));
            QVERIFY2(!engine.rootObjects().isEmpty(),qPrintable(warnings.join('\n')));
            auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]);QVERIFY(window);
            window->resize(mobile?360:1100,mobile?640:760);exposeForTest(window);
            QVERIFY(clickControl(window,"historicalLibraryButton"));
            auto* panel=window->findChild<QObject*>("historicalLibraryPanel");QVERIFY(panel);
            QTRY_VERIFY(panel->property("visible").toBool());
            QVERIFY(panel->property("width").toDouble()<=window->width());
            QVERIFY(editor.historicalResults().size()==1);
            QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
            QMetaObject::invokeMethod(panel,"close");
            window->close();
        }
    }
    void frameHitTargetsAtFractionalScale() {
        // Losing local DPI conversion would route a scaled maximize click to
        // the map; treating maximized corners as resize would break snapping.
        const std::array<QRectF,3> buttons{QRectF(862,0,46,32),
                                         QRectF(908,0,46,32),QRectF(954,0,46,32)};
        for(qreal ratio : {1.0,1.25,1.5}) {
            auto hit=[&](QPointF pixel,bool max=false) {
                return WindowsFrame::hitTest(WindowsFrame::logicalPoint(pixel,ratio),
                    QSizeF(1000,720),8,max,QRectF(0,0,1000,32),buttons);
            };
            QCOMPARE(hit(QPointF(931,16)*ratio),WindowsFrame::Maximize);
            QCOMPARE(hit(QPointF(977,16)*ratio),WindowsFrame::Close);
            QCOMPARE(hit(QPointF(885,16)*ratio),WindowsFrame::Minimize);
            QCOMPARE(hit(QPointF(300,16)*ratio),WindowsFrame::Caption);
            QCOMPARE(hit(QPointF(300,60)*ratio),WindowsFrame::Client);
            QCOMPARE(hit(QPointF(2,2)*ratio),WindowsFrame::TopLeft);
            QCOMPARE(hit(QPointF(998,2)*ratio),WindowsFrame::TopRight);
            QCOMPARE(hit(QPointF(2,718)*ratio),WindowsFrame::BottomLeft);
            QCOMPARE(hit(QPointF(998,718)*ratio),WindowsFrame::BottomRight);
            QCOMPARE(hit(QPointF(2,300)*ratio),WindowsFrame::Left);
            QCOMPARE(hit(QPointF(998,300)*ratio),WindowsFrame::Right);
            QCOMPARE(hit(QPointF(500,2)*ratio),WindowsFrame::Top);
            QCOMPARE(hit(QPointF(500,718)*ratio),WindowsFrame::Bottom);
            QCOMPARE(hit(QPointF(2,2)*ratio,true),WindowsFrame::Caption);
            QCOMPARE(hit(QPointF(998,2)*ratio,true),WindowsFrame::Close);
            QCOMPARE(hit(QPointF(500,718)*ratio,true),WindowsFrame::Client);
        }
    }
    void nativeFrameAdapterHasSafeFallback() {
        EditorController editor;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("editor", &editor);
        engine.load(QUrl("qrc:/common/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]); QVERIFY(window);
        auto frame=window->findChild<QObject*>("windowsFrame");
        QVERIFY2(frame,"Main window must own the native frame lifecycle adapter");
        if(QGuiApplication::platformName()!="windows") {
            QVERIFY(!frame->property("active").toBool());
            QVERIFY(!window->flags().testFlag(Qt::FramelessWindowHint));
            QVERIFY(!visualItem(window->contentItem(),"desktopTitleBar")->isVisible());
        }
        window->close();
    }
    void desktopOpenRequestDoesNotCommitDraftBeforeFileValidation() {
        EditorController editor;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("editor",&editor);
        engine.load(QUrl("qrc:/common/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]); QVERIFY(window);
        exposeForTest(window);
        editor.selectCountry("DEU"); editor.setNameDraft("uncommitted");
        const auto original=editor.selectedName();
        QVERIFY(QMetaObject::invokeMethod(window,"requestAction",Q_ARG(QVariant,QVariant("open"))));
        QCOMPARE(editor.selectedName(),original);
        QCOMPARE(editor.nameDraft(),QString("uncommitted")); QVERIFY(!editor.canUndo());
        QVERIFY(clickControl(window,"cancelUnsaved"));
        auto notice=visualItem(window->contentItem(),"documentFormatNotice"); QVERIFY(notice);
        QVERIFY(notice->property("text").toString().contains("Qt v9"));
        QVERIFY(clickControl(window,"documentFormatNotice"));
        QVERIFY(notice->property("expanded").toBool());
        window->setProperty("allowClose",true); window->close();
    }
    void desktopTitleBarKeepsWorkspaceBelowLargeWindowFrame() {
        EditorController editor;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("editor", &editor);
        engine.load(QUrl("qrc:/common/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]); QVERIFY(window);
        exposeForTest(window);
        auto titleBar=visualItem(window->contentItem(),"desktopTitleBar"); QVERIFY(titleBar);
        auto toolbar=visualItem(window->contentItem(),"storageToolbar"); QVERIFY(toolbar);
        if(QGuiApplication::platformName()!="windows") {
        QVERIFY(!titleBar->isVisible());
        QCOMPARE(toolbar->mapToScene(QPointF()).y(),0.0);
        window->close();
        return;
        }
#ifdef Q_OS_WIN
        auto frame=window->findChild<WindowsFrame*>("windowsFrame"); QVERIFY(frame);
        QTRY_VERIFY(frame->active());
        QCOMPARE(titleBar->height(),32.0);
        QCOMPARE(toolbar->mapToScene(QPointF()).y(),32.0);
        QVERIFY(visualItem(window->contentItem(),"minimizeWindowButton"));
        QVERIFY(visualItem(window->contentItem(),"maximizeWindowButton"));
        QVERIFY(visualItem(window->contentItem(),"closeWindowButton"));
        auto maxButton=visualItem(window->contentItem(),"maximizeWindowButton");
        QCOMPARE(maxButton->width(),46.0);
        QCOMPARE(maxButton->height(),32.0);
        const auto hwnd=reinterpret_cast<HWND>(window->winId());
        auto nativePoint=[&](QPointF local) {
            POINT point{qRound(local.x()*window->devicePixelRatio()),qRound(local.y()*window->devicePixelRatio())};
            ClientToScreen(reinterpret_cast<HWND>(window->winId()),&point);
            return MAKELPARAM(point.x,point.y);
        };
        QCOMPARE(SendMessage(hwnd,WM_NCHITTEST,0,nativePoint(maxButton->mapToScene(QPointF(23,16)))),LRESULT(HTMAXBUTTON));
        QCOMPARE(SendMessage(hwnd,WM_NCHITTEST,0,nativePoint(QPointF(250,16))),LRESULT(HTCAPTION));
        QCOMPARE(SendMessage(hwnd,WM_NCHITTEST,0,nativePoint(QPointF(1,1))),LRESULT(HTTOPLEFT));
        MSG queuedClick{};
        queuedClick.hwnd=hwnd;
        queuedClick.message=WM_NCLBUTTONDOWN;
        queuedClick.wParam=HTMAXBUTTON;
        queuedClick.lParam=nativePoint(maxButton->mapToScene(QPointF(23,16)));
        QVERIFY(frame->nativeEventFilter("windows_generic_MSG",&queuedClick,nullptr));
        QCOMPARE(frame->pressedButton(),2);
        const auto releasePoint=maxButton->mapToScene(QPointF(23,16))*window->devicePixelRatio();
        PostMessage(hwnd,WM_LBUTTONUP,0,MAKELPARAM(qRound(releasePoint.x()),qRound(releasePoint.y())));
        QTRY_COMPARE(window->visibility(),QWindow::Maximized);
        QVERIFY(clickControl(window,"maximizeWindowButton"));
        QTRY_COMPARE(window->visibility(),QWindow::Windowed);
        QVERIFY(capture(window).save("titlebar-normal.png"));
        const auto normalSize=window->size();
        QVERIFY(clickControl(window,"maximizeWindowButton"));
        QTRY_COMPARE(window->visibility(),QWindow::Maximized);
        QCOMPARE(titleBar->height(),32.0);
        QCOMPARE(toolbar->mapToScene(QPointF()).y(),32.0);
        QVERIFY(capture(window).save("titlebar-maximized.png"));
        RECT client{}; GetClientRect(hwnd,&client);
        POINT origin{0,0}; ClientToScreen(hwnd,&origin);
        MONITORINFO monitor{sizeof(MONITORINFO)};
        QVERIFY(GetMonitorInfo(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor));
        QCOMPARE(origin.x,monitor.rcWork.left);
        QCOMPARE(origin.y,monitor.rcWork.top);
        QCOMPARE(client.right,monitor.rcWork.right-monitor.rcWork.left);
        QCOMPARE(client.bottom,monitor.rcWork.bottom-monitor.rcWork.top);
        QVERIFY(clickControl(window,"maximizeWindowButton"));
        QTRY_COMPARE(window->visibility(),QWindow::Windowed);
        QTRY_COMPARE(window->size(),normalSize);
        SendMessage(hwnd,WM_NCLBUTTONDBLCLK,HTCAPTION,nativePoint(QPointF(250,16)));
        QTRY_COMPARE(window->visibility(),QWindow::Maximized);
        SendMessage(hwnd,WM_NCLBUTTONDBLCLK,HTCAPTION,nativePoint(QPointF(250,16)));
        QTRY_COMPARE(window->visibility(),QWindow::Windowed);
        QVERIFY(clickControl(window,"minimizeWindowButton"));
        QTRY_COMPARE(window->visibility(),QWindow::Minimized);
        window->showNormal(); exposeForTest(window);
        editor.selectCountry("DEU"); editor.setColor("#123456");
        QVERIFY(editor.dirty());
        QVERIFY(clickControl(window,"closeWindowButton"));
        auto unsaved=window->findChild<QObject*>("unsavedDialog"); QVERIFY(unsaved);
        QTRY_VERIFY(unsaved->property("visible").toBool());
        QVERIFY(clickControl(window,"cancelUnsaved"));
        QVERIFY(window->isVisible());
        // SC_CLOSE is shared by Alt+F4 and system-menu Close, never DestroyWindow.
        SendMessage(hwnd,WM_SYSCOMMAND,SC_CLOSE,0);
        QTRY_VERIFY(unsaved->property("visible").toBool());
        QVERIFY(clickControl(window,"cancelUnsaved"));
        // The native frame can be detached/re-attached without a frameless orphan.
        frame->setEnabled(false);
        QVERIFY(!frame->active());
        QVERIFY(!titleBar->isVisible());
        QCOMPARE(toolbar->mapToScene(QPointF()).y(),0.0);
        frame->setEnabled(true);
        QTRY_VERIFY(frame->active());
        QCOMPARE(titleBar->height(),32.0);
        window->hide();
        window->destroy();
        QVERIFY(!frame->active());
        QTest::qWait(100);
        QVERIFY(!frame->active());
        window->create(); window->show();
        QTRY_VERIFY(frame->active());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QCOMPARE(SendMessage(reinterpret_cast<HWND>(window->winId()),WM_NCHITTEST,0,
                   nativePoint(maxButton->mapToScene(QPointF(23,16)))),LRESULT(HTMAXBUTTON));
        window->setProperty("allowClose",true);
        QTest::qWait(250);
        QVERIFY(clickControl(window,"closeWindowButton"));
        QTRY_VERIFY(!window->isVisible());
#endif
    }
    void mobileStorageFlow() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto privatePath=directory.filePath("private.pando.json");
        EditorController editor(EditorControllerConfig{true,privatePath});
        QQmlApplicationEngine engine;
        QStringList warnings;
        QSignalSpy errors(&editor,&EditorController::errorOccurred);
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
        engine.rootContext()->setContextProperty("editor",&editor);
        engine.load(QUrl("qrc:/common/Main.qml"));
        QVERIFY2(!engine.rootObjects().isEmpty(),qPrintable(warnings.join('\n')));
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]); QVERIFY(window);
        exposeForTest(window);
        window->resize(360,640); QTest::qWait(200);
        QCOMPARE(window->minimumWidth(),0);
        QCOMPARE(window->minimumHeight(),0);
        auto desktopTitleBar=visualItem(window->contentItem(),"desktopTitleBar"); QVERIFY(desktopTitleBar);
        QVERIFY(!desktopTitleBar->isVisible());
        QVERIFY(!window->flags().testFlag(Qt::FramelessWindowHint));

        auto toolbar=visualItem(window->contentItem(),"storageToolbar"); QVERIFY(toolbar);
        QCOMPARE(toolbar->height(),48.0);
        const QStringList buttonNames{"fileMenuButton","mapDisplayButton","preferencesButton","undoButton","redoButton"};
        QList<QQuickItem*> buttons;
        for(const auto& name:buttonNames) {
            auto button=visualItem(window->contentItem(),name); QVERIFY2(button,qPrintable(name));
            QVERIFY(button->isVisible());
            const auto bounds=button->mapRectToItem(toolbar,QRectF(0,0,button->width(),button->height()));
            QVERIFY2(bounds.left()>=-0.5 && bounds.right()<=toolbar->width()+0.5,qPrintable(name));
            buttons.append(button);
        }
        QVERIFY(clickControl(window,"fileMenuButton"));
        for(const auto& name:QStringList{"importButton","deviceSaveButton","exportButton"}) {
            auto button=visualItem(window->contentItem(),name);QVERIFY(button&&button->isVisible());
            const auto bounds=button->mapRectToItem(window->contentItem(),QRectF(0,0,button->width(),button->height()));
            QVERIFY(bounds.left()>=0&&bounds.right()<=window->width());
        }
        QCOMPARE(visualItem(window->contentItem(),"importButton")->property("text").toString(),QString("가져오기…"));
        QCOMPARE(visualItem(window->contentItem(),"deviceSaveButton")->property("text").toString(),QString("기기에 저장"));
        QCOMPARE(visualItem(window->contentItem(),"exportButton")->property("text").toString(),QString("내보내기…"));
        QVERIFY(QMetaObject::invokeMethod(window,"handleBack"));

        editor.selectCountry("DEU");
        editor.setMemoDraft("rotation draft");
        window->resize(720,360); QTest::qWait(120);
        window->resize(360,720); QTest::qWait(120);
        QCOMPARE(editor.selectedId(),QString("DEU"));
        QCOMPARE(editor.memoDraft(),QString("rotation draft"));
        QVERIFY(!editor.canUndo());
        auto unsaved=window->findChild<QObject*>("unsavedDialog"); QVERIFY(unsaved);
        QVERIFY(clickControl(window,"countryPicker"));
        auto picker=visualItem(window->contentItem(),"countryPicker"); QVERIFY(picker);
        auto pickerPopup=qvariant_cast<QObject*>(picker->property("popup")); QVERIFY(pickerPopup);
        QTRY_VERIFY(pickerPopup->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(window,"handleBack"));
        QTRY_VERIFY(!pickerPopup->property("visible").toBool());
        QVERIFY(!unsaved->property("visible").toBool());
        QVERIFY(clickControl(window,"countryColor"));
        QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); typeText(window,"#bad");
        QCOMPARE(editor.colorDraft(),QString("#bad"));
        QVERIFY(clickControl(window,"importButton"));
        QTRY_VERIFY(unsaved->property("visible").toBool());
        QCOMPARE(editor.memoDraft(),QString("rotation draft"));
        QCOMPARE(editor.colorDraft(),QString("#bad"));
        QVERIFY(!editor.canUndo());
        QCOMPARE(errors.count(),0);
        QVERIFY(clickControl(window,"cancelUnsaved"));
        QCOMPARE(editor.memoDraft(),QString("rotation draft"));
        QCOMPARE(editor.colorDraft(),QString("#bad"));
        QVERIFY(!editor.canUndo());
        editor.setColorDraft(editor.colors()["DEU"].toString());

        QVERIFY(clickControl(window,"deviceSaveButton"));
        QVERIFY(QFile::exists(privatePath));
        QVERIFY(!editor.dirty());
        QVERIFY(capture(window).save("mobile-storage.png"));
        editor.setColor("#123456");
        QVERIFY(editor.dirty());
        QVERIFY(QMetaObject::invokeMethod(window,"requestExport"));
        QVERIFY(QFile::exists(privatePath));
        QVERIFY(!editor.dirty());
        auto exportDialog=window->findChild<QObject*>("exportDialog");QVERIFY(exportDialog);
        QTRY_VERIFY(exportDialog->property("visible").toBool());
        // Let the platform process its deferred native dialog creation before Back.
        QCoreApplication::processEvents();
        QVERIFY(QMetaObject::invokeMethod(window,"handleBack"));
        QTRY_VERIFY(!exportDialog->property("visible").toBool());

        editor.setColor("#654321");
        QVERIFY(QMetaObject::invokeMethod(window,"handleBack"));
        auto properties=visualItem(window->contentItem(),"objectPropertyPanel");QVERIFY(properties);
        QVERIFY(!properties->isVisible());
        QVERIFY(!unsaved->property("visible").toBool());
        QVERIFY(editor.dirty());
        QVERIFY(QMetaObject::invokeMethod(window,"handleBack"));
        QTRY_VERIFY(unsaved->property("visible").toBool());
        QVERIFY(clickControl(window,"cancelUnsaved"));
        QVERIFY(window->isVisible());
        QVERIFY(editor.dirty());
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        window->setProperty("allowClose",true); window->close();
    }

    void mobileCorruptRecoveryCanBeReopened() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto privatePath=directory.filePath("private.pando.json");
        QFile corrupt(privatePath);
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        QCOMPARE(corrupt.write("corrupt project"),qint64(15));
        corrupt.close();

        EditorController editor(EditorControllerConfig{true,privatePath});
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& values){for(const auto& e:values) warnings<<e.toString();});
        engine.rootContext()->setContextProperty("editor",&editor);
        engine.load(QUrl("qrc:/common/Main.qml"));
        QVERIFY2(!engine.rootObjects().isEmpty(),qPrintable(warnings.join('\n')));
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]); QVERIFY(window);
        exposeForTest(window);
        window->resize(360,640); QTest::qWait(120);
        auto recovery=window->findChild<QObject*>("recoveryDialog"); QVERIFY(recovery);
        QTRY_VERIFY(recovery->property("visible").toBool());
        QVERIFY(clickControl(window,"cancelRecovery"));
        QTRY_VERIFY(!recovery->property("visible").toBool());
        QTest::qWait(250); // let the modal exit transition release its input overlay
        QCOMPARE(readFile(privatePath),QByteArray("corrupt project"));

        editor.selectCountry("DEU"); editor.setColor("#123456");
        QVERIFY(clickControl(window,"deviceSaveButton"));
        QTRY_VERIFY(recovery->property("visible").toBool());
        QVERIFY(clickControl(window,"confirmRecovery"));
        QTRY_VERIFY(!recovery->property("visible").toBool());
        QCOMPARE(readFile(privatePath+".corrupt"),QByteArray("corrupt project"));
        QVERIFY(clickControl(window,"deviceSaveButton"));
        QVERIFY(!editor.dirty());
        QVERIFY(readFile(privatePath).startsWith('{'));
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        window->setProperty("allowClose",true); window->close();
    }

    void attributesAndLayers() {
        EditorController editor;
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
        engine.rootContext()->setContextProperty("editor",&editor);
        engine.load(QUrl("qrc:/common/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]); QVERIFY(window);
        exposeForTest(window);
        editor.selectCountry("DEU");
        QVERIFY(clickControl(window,"detailObjectName"));
        QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); typeText(window,"Draft name");
        QCOMPARE(editor.nameDraft(),QString("Draft name"));
        window->resize(390,760); QTest::qWait(200);
        QCOMPARE(editor.nameDraft(),QString("Draft name"));
        QTest::keyClick(window,Qt::Key_Return);
        QCOMPARE(editor.selectedName(),QString("Draft name"));
        QVERIFY(!editor.hasPendingEdits()); // web name change commits independently, no whole-form Apply.
        QVERIFY(clickControl(window,"countryColor"));
        QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); typeText(window,"#123456");
        QTest::keyClick(window,Qt::Key_Return);
        QVERIFY(clickControl(window,"applyEdits"));
        QTRY_COMPARE(editor.colors()["DEU"].toString(),QString("#123456"));
        // Position the slider in the scroll area, then discard the positioning
        // click's draft before measuring the actual drag's baseline.
        QVERIFY(clickControl(window,"countryOpacity"));
        editor.discardPendingEdits();
        // A drag remains a draft until Apply; Undo restores its baseline.
        auto slider=visualItem(window->contentItem(),"countryOpacity"); QVERIFY(slider);
        double before=editor.countryOpacity();
        window->grabWindow(); QTest::qWait(100); // settle the reset handle position
        auto handle=qvariant_cast<QQuickItem*>(slider->property("handle")); QVERIFY(handle);
        auto start=handle->mapToScene(QPointF(handle->width()/2,handle->height()/2)).toPoint();
        auto end=slider->mapToScene(QPointF(slider->width()*0.2,slider->height()/2)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
        QTest::mouseMove(window,(start+end)/2,20); QTest::mouseMove(window,end,20);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,end);
        QVERIFY(editor.countryOpacity()<before);
        QVERIFY(clickControl(window,"applyEdits"));
        QTRY_VERIFY(!editor.jobBusy());
        editor.undo(); QCOMPARE(editor.countryOpacity(),before); editor.redo();
        QVERIFY(clickControl(window,"layersTab"));
        QVERIFY(clickControl(window,"addLayer"));
        auto id=editor.selectedLayerId(); QVERIFY(id!="countries");
        QVERIFY(clickControl(window,"layerName"));
        QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); typeText(window,"Upper");
        QTest::keyClick(window,Qt::Key_Return);
        QCOMPARE(editor.layerNameDraft(),QString("Upper"));
        editor.moveCountry(id); QCOMPARE(editor.countryLayerId(),id);
        QVERIFY(!editor.canDeleteLayer());
        QVERIFY(clickControl(window,"layerLocked"));
        QCOMPARE(editor.selectedId(),QString("DEU"));
        QVERIFY(!editor.selectedEditable());
        editor.selectCountry("DEU"); QVERIFY(!editor.selectedEditable());
        QVERIFY(clickControl(window,"layerLocked"));
        editor.selectCountry("DEU"); QVERIFY(editor.selectedEditable());
        QVERIFY(clickControl(window,"layerVisible")); QCOMPARE(editor.selectedId(),QString("DEU"));
        QVERIFY(!editor.countryVisuals()["DEU"].toMap()["visible"].toBool());
        QVERIFY(clickControl(window,"layerVisible"));
        editor.selectCountry("DEU");
        editor.previewCountryOpacity(0); QVERIFY(editor.commitPendingEdits());
        editor.selectCountry("DEU"); QVERIFY(editor.selectedEditable());
        QTemporaryDir dir; auto path=QUrl::fromLocalFile(dir.path()+QString::fromUtf8("/속성 레이어.pando.json"));
        QVERIFY(editor.saveFile(path)); QVERIFY(editor.openFile(path));
        editor.selectCountry("DEU"); QCOMPARE(editor.selectedName(),QString("Draft name"));
        QCOMPARE(editor.countryLayerId(),id); QCOMPARE(editor.countryOpacity(),0.0);
        capture(window).save("layers-compact.png");
        window->resize(1100,720); QTest::qWait(150); capture(window).save("layers-desktop.png");
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        window->close();
    }
    void webFieldsAndAsyncApplyCancelAcrossPcAnd360px() {
        for(bool mobile:{false,true}) {
            QTemporaryDir dir; QVERIFY(dir.isValid());
            EditorController editor(EditorControllerConfig{mobile,dir.filePath("private.json")});
            QQmlApplicationEngine engine; QStringList warnings;
            connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
            engine.rootContext()->setContextProperty("editor",&editor);
            engine.load(QUrl("qrc:/common/Main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
            auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]); QVERIFY(window);
            window->resize(mobile?360:1100,mobile?640:760); exposeForTest(window);
            editor.selectCountry("DEU");
            const auto name=editor.selectedName(),layerName=editor.layerNameDraft(),color=editor.colorDraft();
            QVERIFY(clickControl(window,"detailObjectName"));
            QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); typeText(window,"Web name");
            QVERIFY(clickControl(window,"detailObjectNotes"));
            // Web change events commit each text field independently, without Apply.
            QCOMPARE(editor.selectedName(),QString("Web name")); QCOMPARE(editor.revision(),qulonglong(1));
            typeText(window,"Web notes"); QVERIFY(clickControl(window,"detailObjectName")); QVERIFY(clickControl(window,"countryColor"));
            QCOMPARE(editor.revision(),qulonglong(2));
            QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); typeText(window,"#102030");
            QVERIFY(clickControl(window,"layersTab"));
            QVERIFY(clickControl(window,"layerName"));
            QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); typeText(window,"Atomic layer");
            QVERIFY(clickControl(window,"applyEdits")); QTRY_COMPARE(editor.revision(),qulonglong(3));
            QCOMPARE(editor.colorDraft(),QString("#102030")); QCOMPARE(editor.layerNameDraft(),QString("Atomic layer"));
            QVERIFY(!editor.hasPendingEdits());
            QVERIFY(clickControl(window,"undoButton"));
            QCOMPARE(editor.selectedName(),QString("Web name")); QCOMPARE(editor.memoDraft(),QString("Web notes"));
            QCOMPARE(editor.colorDraft(),color); QCOMPARE(editor.layerNameDraft(),layerName);
            QVERIFY(clickControl(window,"undoButton")); QCOMPARE(editor.memoDraft(),QString());
            QVERIFY(clickControl(window,"undoButton")); QCOMPARE(editor.selectedName(),name);
            QCOMPARE(editor.revision(),qulonglong(6)); QVERIFY(!editor.canUndo()); QVERIFY(!editor.dirty());
            for(int i=0;i<3;++i) {
                auto* redo=visualItem(window->contentItem(),"redoButton");QVERIFY(redo);
                const auto center=redo->mapToScene(QPointF(redo->width()/2,redo->height()/2));
                QVERIFY2(clickControl(window,"redoButton"),qPrintable(QStringLiteral(
                    "redo click failed: mobile=%1 step=%2 canRedo=%3 enabled=%4 center=(%5,%6) window=%7x%8")
                    .arg(mobile).arg(i).arg(editor.canRedo()).arg(redo->isEnabled())
                    .arg(center.x()).arg(center.y()).arg(window->width()).arg(window->height())));
            }
            QCOMPARE(editor.revision(),qulonglong(9));
            QVERIFY(clickControl(window,"layerName"));
            QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); typeText(window,"Discard me");
            QVERIFY(clickControl(window,"cancelEdits"));
            QCOMPARE(editor.layerNameDraft(),QString("Atomic layer")); QCOMPARE(editor.revision(),qulonglong(9));
            editor.setLayerNameDraft("Keep cancelled draft"); QVERIFY(editor.applyPendingEditsAsync()); QVERIFY(editor.jobBusy());
            auto cancel=visualItem(window->contentItem(),"cancelBackgroundWork"); QVERIFY(cancel);
            // Directly dispatch the real button signal before processing queued
            // completion events, so fast machines cannot race this cancellation.
            QVERIFY(QMetaObject::invokeMethod(cancel,"clicked")); QVERIFY(!editor.jobBusy());
            QCoreApplication::processEvents(); QCOMPARE(editor.revision(),qulonglong(9));
            QCOMPARE(editor.layerNameDraft(),QString("Keep cancelled draft")); editor.discardPendingEdits();
            QVERIFY(capture(window).save(mobile?"jobs-mobile-360.png":"jobs-desktop.png"));
            QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
            window->setProperty("allowClose",true); window->close();
        }
    }
    void territorialStructureDeleteDialogAcrossPcAnd360px() {
        for(bool mobile:{false,true}) {
            EditorController editor(EditorControllerConfig{mobile,{}}); QQmlApplicationEngine engine; QStringList warnings;
            connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
            engine.rootContext()->setContextProperty("editor",&editor);engine.load(QUrl("qrc:/common/Main.qml"));QVERIFY(!engine.rootObjects().isEmpty());
            auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]);QVERIFY(window);window->resize(mobile?360:1100,mobile?640:760);exposeForTest(window);QVERIFY(clickControl(window,"countryTab"));editor.selectCountry("DEU");
            auto panel=visualItem(window->contentItem(),"territorialStructurePanel");QVERIFY(panel&&panel->isVisible());
            auto deleteButton=visualItem(window->contentItem(),"deleteTerritorial");QVERIFY(deleteButton&&deleteButton->isVisible()&&deleteButton->isEnabled());
            QVERIFY(QMetaObject::invokeMethod(deleteButton,"clicked"));QTRY_VERIFY(editor.structureDialogOpen());
            auto dialog=window->findChild<QObject*>("territorialStructureDialog");QVERIFY(dialog&&dialog->property("visible").toBool());
            QVERIFY(capture(window).save(mobile?"structure-mobile-360.png":"structure-desktop-1100.png"));
            QVERIFY(clickControl(window,"confirmTerritorialStructure"));QTRY_VERIFY(!editor.structureDialogOpen());editor.undo();editor.selectCountry("DEU");QCOMPARE(editor.selectedId(),QString("DEU"));
            QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));window->setProperty("allowClose",true);window->close();
        }
    }
    void territorialConversionSetupDoesNotMutateDocumentAcrossPcAnd360px() {
        for(bool mobile:{false,true}) {
            EditorController editor(EditorControllerConfig{mobile,{}}); QQmlApplicationEngine engine; QStringList warnings;
            connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
            engine.rootContext()->setContextProperty("editor",&editor);engine.load(QUrl("qrc:/common/Main.qml"));QVERIFY(!engine.rootObjects().isEmpty());
            auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]);QVERIFY(window);window->resize(mobile?360:1100,mobile?640:760);exposeForTest(window);QVERIFY(clickControl(window,"countryTab"));editor.selectCountry("DEU");
            const auto revision=editor.revision();auto button=visualItem(window->contentItem(),"convertTerritorial");QVERIFY(button&&button->isVisible());QVERIFY(QMetaObject::invokeMethod(button,"clicked"));QTRY_VERIFY(editor.structureDialogOpen());
            QCOMPARE(editor.revision(),revision);QVERIFY(editor.structureState().value("conversionSetup").toBool());QVERIFY(!editor.structureState().value("generatedId").toString().isEmpty());
            auto dialog=window->findChild<QObject*>("territorialStructureDialog");QVERIFY(dialog&&dialog->property("visible").toBool());auto confirm=visualItem(window->contentItem(),"confirmTerritorialStructure");QVERIFY(confirm&&!confirm->isEnabled());
            editor.cancelStructureMutation();QTRY_VERIFY(!editor.structureDialogOpen());QCOMPARE(editor.revision(),revision);
            QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));window->setProperty("allowClose",true);window->close();
        }
    }
    void territorialCreateSetupWaitsForPreparedGeometryAcrossPcAnd360px() {
        using namespace pandoeditor;
        for(bool mobile:{false,true}) {
            EditorController editor(EditorControllerConfig{mobile,{}});QQmlApplicationEngine engine;QStringList warnings;connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors)warnings<<e.toString();});engine.rootContext()->setContextProperty("editor",&editor);engine.load(QUrl("qrc:/common/Main.qml"));QVERIFY(!engine.rootObjects().isEmpty());auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]);QVERIFY(window);window->resize(mobile?360:1100,mobile?640:760);exposeForTest(window);QVERIFY(clickControl(window,"countryTab"));editor.selectCountry("DEU");
            const auto revision=editor.revision();auto add=visualItem(window->contentItem(),"createRegional");QVERIFY(add&&add->isVisible());QVERIFY(QMetaObject::invokeMethod(add,"clicked"));QTRY_VERIFY(editor.structureDialogOpen());QVERIFY(editor.structureState().value("createSetup").toBool());QVERIFY(editor.structureState().value("geometryRequired").toBool());QCOMPARE(editor.revision(),revision);auto confirm=visualItem(window->contentItem(),"confirmTerritorialStructure");QVERIFY(confirm&&!confirm->isEnabled());editor.cancelStructureMutation();
            Geometry geometry;geometry.type="Polygon";geometry.polygons.push_back(pandoeditor::Polygon{Ring{{30,30},{31,30},{31,31},{30,31},{30,30}}});CreateTerritorialIntent intent;intent.kind=UnitKind::Regional;intent.id=mobile?"prepared-mobile":"prepared-desktop";intent.name="Prepared";intent.geometry=geometry;QVERIFY(editor.beginTerritorialCreatePrepared(intent));QVERIFY(!editor.structureState().value("geometryRequired").toBool());QVERIFY(editor.confirmStructureMutation());QCOMPARE(editor.revision(),revision+1);editor.undo();QCOMPARE(editor.revision(),revision+2);
            QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));window->setProperty("allowClose",true);window->close();
        }
    }
    void canonicalTerritorialCreateMenuAcrossPcAnd360px() {
        for(bool mobile:{false,true}) {
            EditorController editor(EditorControllerConfig{mobile,{}});QQmlApplicationEngine engine;
            engine.rootContext()->setContextProperty("editor",&editor);engine.load(QUrl("qrc:/common/Main.qml"));QVERIFY(!engine.rootObjects().isEmpty());
            auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]);QVERIFY(window);window->resize(mobile?360:1100,mobile?640:760);exposeForTest(window);
            const auto before=editor.documentBytes();QSignalSpy errors(&editor,&EditorController::errorOccurred);
            for(const auto* name:{"addGeneral","addRegional"}) {
                QVERIFY(clickControl(window,"createMenuButton"));QVERIFY(clickControl(window,name));
                QTRY_VERIFY(editor.structureDialogOpen());QVERIFY(editor.structureState().value("createSetup").toBool());
                QCOMPARE(editor.documentBytes(),before);editor.cancelStructureMutation();
            }
            QCOMPARE(errors.count(),0);window->setProperty("allowClose",true);window->close();
        }
    }
    void compositing() {
        using namespace pandoeditor;
        Project project;
        project.replace(ProjectDocument{
            {{"A","Red",{{{{0,0},{4,0},{4,4},{0,4},{0,0}}}},0xff0000},
             {"B","Blue",{{{{2,0},{6,0},{6,4},{2,4},{2,0}}}},0x0000ff}},
            {{"countries","Base",true,false,0.5}}});
        QTemporaryDir dir;
        auto path=QUrl::fromLocalFile(dir.path()+"/overlap.pando.json");
        QFile file(path.toLocalFile()); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(projectcodec::encode(project)); file.close();
        EditorController editor; QVERIFY(editor.openFile(path)); QVERIFY(editor.setProjectionMode("flat"));
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("editor",&editor);
        engine.load(QUrl("qrc:/common/Main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]); exposeForTest(window);
        auto map=window->findChild<QQuickItem*>("mapView"); QVERIFY(map);
        auto pixel=[&](double lon,double lat) {
            auto image=capture(window);
            auto scale=map->property("mapScale").toDouble();
            auto point=map->mapToScene(QPointF(map->property("originX").toDouble()+lon/6*editor.mapWidth()*scale,
                                              map->property("originY").toDouble()+(4-lat)/4*editor.mapHeight()*scale));
            return image.pixelColor((point*image.devicePixelRatio()).toPoint());
        };
        auto near=[](QColor a,QColor b) {return std::abs(a.red()-b.red())<=3 && std::abs(a.green()-b.green())<=3 && std::abs(a.blue()-b.blue())<=3;};
        // Two overlapping opaque countries are composited once at 50% layer opacity.
        auto overlap=pixel(3,2); QVERIFY2(near(overlap,QColor(116,120,250)),qPrintable(overlap.name()));
        auto red=pixel(1,2); QVERIFY2(near(red,QColor(244,120,122)),qPrintable(red.name()));
        editor.selectCountry("A"); editor.previewCountryOpacity(0.5); QVERIFY(editor.commitPendingEdits());
        editor.clearSelection();
        auto quarter=pixel(1,2); QVERIFY2(near(quarter,QColor(238,179,183)),qPrintable(quarter.name()));
        editor.selectCountry("A"); editor.previewCountryOpacity(1); QVERIFY(editor.commitPendingEdits());
        editor.selectLayer("countries"); editor.previewLayerOpacity(1); QVERIFY(editor.commitPendingEdits());
        editor.addLayer(); auto top=editor.selectedLayerId();
        editor.selectCountry("B"); editor.moveCountry(top);
        editor.previewLayerOpacity(0.5); QVERIFY(editor.commitPendingEdits()); editor.clearSelection();
        auto purple=pixel(3,2); QVERIFY2(near(purple,QColor(127,0,128)),qPrintable(purple.name()));
        editor.selectCountry("B"); editor.previewCountryOpacity(0.5); QVERIFY(editor.commitPendingEdits()); editor.clearSelection();
        auto nested=pixel(3,2); QVERIFY2(near(nested,QColor(191,0,64)),qPrintable(nested.name()));
        editor.moveLayer(-1);
        auto reordered=pixel(3,2); QVERIFY2(near(reordered,QColor(255,0,0)),qPrintable(reordered.name()));
        editor.undo(); QVERIFY(near(pixel(3,2),QColor(191,0,64)));
        editor.setLayerVisible(false); QVERIFY(near(pixel(3,2),QColor(255,0,0)));
        editor.setLayerVisible(true); editor.setLayerLocked(true);
        QVERIFY(near(pixel(3,2),QColor(191,0,64)));
        window->setProperty("allowClose",true); window->close();
    }
    void editingFlow() {
        EditorController editor;
        QVERIFY(editor.setProjectionMode("flat"));
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){ for(const auto& e:errors) warnings<<e.toString(); });
        engine.rootContext()->setContextProperty("editor",&editor);
        engine.load(QUrl("qrc:/common/Main.qml"));
        QVERIFY2(!engine.rootObjects().isEmpty(),qPrintable(warnings.join('\n')));
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]); QVERIFY(window);
        if (qEnvironmentVariable("QT_QPA_PLATFORM")=="windows") {
            window->hide(); window->show();
            QVERIFY(QTest::qWaitForWindowExposed(window));
        }
        QTest::qWait(500);
        // Hidden Windows launches defer scene polish until the first render.
        QVERIFY(!window->grabWindow().isNull());
        auto map=window->findChild<QQuickItem*>("mapView"); QVERIFY(map);
        QVERIFY(!window->findChild<QQuickItem*>("editorPanel")); // optional forms are lazy
        auto clickItem=[&](const char* name) {
            enterExistingControlRoute(window,QString::fromLatin1(name));
            QTest::qWait(80); // settle layout before reading delegate coordinates
            window->grabWindow();
            std::function<QQuickItem*(QQuickItem*)> find=[&](QQuickItem* node)->QQuickItem* {
                if (node->objectName()==QString::fromLatin1(name)) return node;
                for(auto child:node->childItems()) if(auto result=find(child)) return result;
                return nullptr;
            };
            auto item=find(window->contentItem());
            if (!item) return false;
            const auto center=item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint();
            if (!QRect(QPoint(0,0),window->size()).contains(center)) return false;
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,center);
            QTest::qWait(50); return true;
        };
        // Pointer hit-testing and chooser routing are covered exhaustively by
        // selection_ui_tests. This broader editing-flow test only needs a stable
        // selected country before exercising zoom/pan/edit/undo/redo.
        editor.selectCountry("DEU");
        QCOMPARE(editor.selectedId(),QString("DEU"));
        QVERIFY(QMetaObject::invokeMethod(map,"zoomAt",Q_ARG(QVariant,1.5),Q_ARG(QVariant,map->width()/2),Q_ARG(QVariant,map->height()/2)));
        QCOMPARE(map->property("zoom").toDouble(),1.5);
        QVERIFY(QMetaObject::invokeMethod(map,"fit"));
        auto dragStart=map->mapToScene(QPointF(10,100)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,dragStart);
        QTest::mouseMove(window,dragStart+QPoint(25,0),30);
        QTest::mouseMove(window,dragStart+QPoint(60,20),30);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,dragStart+QPoint(60,20));
        QVERIFY(map->property("panX").toDouble()!=0);
        QCOMPARE(editor.selectedId(),QString("DEU"));
        editor.clearSelection(); editor.selectCountry("DEU");
        QCOMPARE(editor.selectedId(),QString("DEU"));
        QVERIFY(QMetaObject::invokeMethod(map,"fit"));
        QVERIFY(clickItem("swatche56b6f")); QVERIFY(editor.dirty());
        auto panel=navigationItem(window->contentItem(),"objectPropertyPanel"); QVERIFY(panel);
        QVERIFY(!panel->property("compact").toBool());
        QCOMPARE(editor.colors()["DEU"].toString(),QString("#e56b6f"));
        QVERIFY(clickItem("undoButton")); QVERIFY(!editor.dirty());
        QVERIFY(clickItem("redoButton")); QVERIFY(editor.dirty());
        QTest::qWait(150);
        window->grabWindow(); QTest::qWait(100);
        auto desktop=window->grabWindow(); QVERIFY(!desktop.isNull()); QVERIFY(desktop.save("desktop.png"));
        window->resize(390,760); QTest::qWait(300);
        QVERIFY(panel->property("compact").toBool());
        QCOMPARE(editor.selectedId(),QString("DEU"));
        QVERIFY(editor.dirty());
        QVERIFY(map->height()>200);
        QVERIFY(clickItem("swatch499c91"));
        QCOMPARE(editor.colors()["DEU"].toString(),QString("#499c91"));
        QVERIFY(clickItem("undoButton"));
        QCOMPARE(editor.colors()["DEU"].toString(),QString("#e56b6f"));
        QVERIFY(clickItem("redoButton"));
        QCOMPARE(editor.colors()["DEU"].toString(),QString("#499c91"));
        QTest::qWait(150);
        window->grabWindow(); QTest::qWait(100);
        auto mobile=window->grabWindow(); QVERIFY(!mobile.isNull()); QVERIFY(mobile.save("compact.png"));
        // Exact map raster/color composition is covered by compositing() and
        // map_render_tests. The editing flow only owns the persisted style state.
        QCOMPARE(editor.colors()["DEU"].toString(),QString("#499c91"));
        QTemporaryDir temporary;
        auto path=QUrl::fromLocalFile(temporary.path()+QString::fromUtf8("/화면 테스트.pando.json"));
        QVERIFY(editor.saveFile(path)); QVERIFY(!editor.dirty());
        editor.setColor("#a8c7db"); QVERIFY(editor.dirty());
        QVERIFY(QMetaObject::invokeMethod(window,"requestAction",Q_ARG(QVariant,QVariant("open"))));
        auto unsaved=window->findChild<QObject*>("unsavedDialog"); QVERIFY(unsaved);
        QTRY_VERIFY(unsaved->property("visible").toBool());
        QTest::qWait(200);
        QVERIFY(clickItem("cancelUnsaved"));
        QTRY_VERIFY(!unsaved->property("visible").toBool());
        QVERIFY(editor.dirty());
        // Cancelling a Save As dialog also cancels the deferred destructive action.
        auto saveDialog=window->findChild<QObject*>("saveDialog"); QVERIFY(saveDialog);
        window->setProperty("pendingAction","close");
        QVERIFY(QMetaObject::invokeMethod(saveDialog,"rejected"));
        QCOMPARE(window->property("pendingAction").toString(),QString());
        QVERIFY(window->isVisible() && editor.dirty());
        window->close(); QTRY_VERIFY(unsaved->property("visible").toBool());
        QTest::qWait(200); QVERIFY(clickItem("cancelUnsaved"));
        QVERIFY(window->isVisible());
        QVERIFY(editor.openFile(path));
        QCOMPARE(editor.colors()["DEU"].toString(),QString("#499c91"));
        QVERIFY(editor.selectedId().isEmpty());
        QVERIFY(!editor.canUndo());
        QCOMPARE(map->property("zoom").toDouble(),1.0);
        QTest::qWait(250); // let the modal exit transition release its input overlay
        editor.selectCountry("DEU");
        QCOMPARE(editor.selectedId(),QString("DEU"));
        editor.setColor("#e56b6f");
        window->close(); QTRY_VERIFY(unsaved->property("visible").toBool());
        QTest::qWait(200); QVERIFY(clickItem("saveUnsaved"));
        QVERIFY(!editor.dirty());
        QVERIFY(!window->isVisible());
        window->setProperty("allowClose",false); window->show();
        editor.setColor("#a8c7db");
        window->close(); QTRY_VERIFY(unsaved->property("visible").toBool());
        QTest::qWait(200); QVERIFY(clickItem("discardUnsaved"));
        QVERIFY(!window->isVisible());
        QVERIFY(editor.openFile(path));
        QCOMPARE(editor.colors()["DEU"].toString(),QString("#e56b6f"));
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        window->close();
    }
};
// Device measurements are explicitly selected by the native runner, not part
// of the headless functional suite. Keep them separate so CI neither skips a
// functional test nor pretends to validate GPU performance without a device.
class NativePerformanceTests : public QObject {
    Q_OBJECT
private slots:
    void nativePerformanceProbe() { runNativePerformanceProbe(); }
};
int main(int argc,char** argv) {
    QQuickStyle::setStyle("Basic");
    QGuiApplication app(argc,argv);
    registerWindowsFrameType();
    if (qEnvironmentVariable("QT_QPA_PLATFORM")=="offscreen") {
        int font=QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR")+"/Fonts/malgun.ttf");
        if (font>=0) app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).first()));
    }
    for(int i=1;i<argc;++i) {
        if(QString::fromLocal8Bit(argv[i])==QStringLiteral("nativePerformanceProbe")) {
            NativePerformanceTests test;return QTest::qExec(&test,argc,argv);
        }
    }
    UiTests test; return QTest::qExec(&test,argc,argv);
}
#include "ui_tests.moc"

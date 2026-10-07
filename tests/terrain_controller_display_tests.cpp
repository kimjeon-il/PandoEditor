#include "editorcontroller.h"
#include "windowsframe.h"
#include "../renderer/terrainlandmaskitem.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QTest>
#include <QFile>
#include <QDir>
#include <QSGRendererInterface>
#include <QJsonDocument>
#include <QScopeGuard>

// Actual pinned DEM + application controller/QML/QSG path. These are device
// diagnostics; full corpus pixel parity and performance acceptance are separate.
class TerrainControllerDisplayTests final : public QObject {
    Q_OBJECT
private slots:
    void actualDemTransitionsRetainDisplayAndRejectObsoleteWork() {
        QCOMPARE(qEnvironmentVariable("QT_QPA_PLATFORM"),QString("windows"));
        QTemporaryDir settings;QVERIFY(settings.isValid());
        EditorControllerConfig config;config.bootstrapWorld=true;
        config.worldDataRoot=QStringLiteral(PANDOEDITOR_WORLD_ASSET_DIR);
        config.appearancePath=settings.filePath("appearance.json");
        QStringList warnings;EditorController editor(config);QQmlApplicationEngine engine;
        connect(&engine,&QQmlEngine::warnings,this,[&](const auto& rows){for(const auto& row:rows)warnings<<row.toString();});
        engine.rootContext()->setContextProperty("editor",&editor);
        engine.load(QUrl("qrc:/common/Main.qml"));
        QVERIFY2(!engine.rootObjects().isEmpty(),qPrintable(warnings.join('\n')));
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());QVERIFY(window);
        window->resize(1100,760);window->hide();window->show();QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY_WITH_TIMEOUT(editor.worldStatus()=="canonical",30000);
        QCOMPARE(window->rendererInterface()->graphicsApi(),QSGRendererInterface::Direct3D11);
        int processed=0;const int expected=12;
        const auto status=[&]{auto s=editor.terrainDataStatus();s["viewRevision"]=qulonglong(qobject_cast<MapSceneBridge*>(editor.mapSceneBridge())->viewState().revision);
            QVariantList masks;for(auto* mask:window->findChildren<TerrainLandMaskItem*>())masks.append(QVariantMap{{"ready",mask->maskReady()},{"view",qulonglong(mask->maskViewRevision())},{"scene",qulonglong(mask->maskSceneRevision())}});
            s["masks"]=masks;return s;};
        const auto finalState=qScopeGuard([&]{qInfo().noquote()<<"TERRAIN_FINAL_STATE"<<QJsonDocument::fromVariant(status()).toJson(QJsonDocument::Compact);});
        const auto receipt=[&]{return status().value("displayReceiptAccepted").toBool()&&status().value("displayedDraws").toULongLong()>0;};
        const auto settled=[&]{const auto s=status();return receipt()&&!s.value("cpuPreparationPending").toBool()&&!s.value("cpuPreparationScheduled").toBool()&&s.value("pendingJobs").toULongLong()==0&&!s.value("uploadWorkPending").toBool();};
        QTRY_VERIFY_WITH_TIMEOUT(receipt(),15000);
        QCOMPARE(status().value("representation").toString(),QString("dem-relief-v1"));
        QVERIFY(status().value("uploadOperations").toULongLong()>=3);++processed;
        const auto captureDir=qEnvironmentVariable("PANDOEDITOR_UI_CAPTURE_DIR");QVERIFY(!captureDir.isEmpty());
        QVERIFY(QDir().mkpath(captureDir));
        const auto shot=[&](const QString& name){auto image=window->grabWindow();return !image.isNull()&&image.save(captureDir+"/"+name+".png");};
        QVERIFY(shot("01-real-dem-gray"));++processed;
        const auto document=editor.documentBytes();const auto dirty=editor.dirty(),undo=editor.canUndo(),redo=editor.canRedo();
        // First complete base coverage can precede ordinary detail/prefetch
        // work. Observe that work settling before isolating style-only uploads.
        QTRY_VERIFY_WITH_TIMEOUT(settled(),15000);
        const auto uploads=status().value("uploadOperations").toULongLong();
        QVERIFY(editor.setTerrainMode("color"));QTest::qWait(150);QVERIFY(shot("02-real-dem-color"));
        QCOMPARE(status().value("uploadOperations").toULongLong(),uploads);++processed;
        editor.beginAppearancePreview();QVERIFY(editor.previewAppearance({{"theme","dark"}}));
        QTest::qWait(150);QVERIFY(shot("03-real-dem-dark"));
        QCOMPARE(status().value("uploadOperations").toULongLong(),uploads);editor.cancelAppearancePreview();++processed;
        editor.beginMapInteraction();const auto view=editor.mapViewState();
        QVERIFY(editor.publishMapView({{"scale",view.value("scale").toDouble()*3},{"translateX",350.}}));
        QTest::qWait(650);QCOMPARE(status().value("uploadOperations").toULongLong(),uploads);
        QCOMPARE(status().value("cpuDecodeReservedBytes").toULongLong(),qulonglong(0));QVERIFY(shot("04-navigation-fallback"));++processed;
        editor.endMapInteraction();QTRY_VERIFY_WITH_TIMEOUT(settled(),15000);QVERIFY(shot("05-settled-detail"));++processed;
        const auto beforeReverse=status().value("uploadOperations").toULongLong();
        editor.beginMapInteraction();
        QVERIFY(editor.publishMapView({{"scale",view.value("scale").toDouble()*5}}));
        QVERIFY(editor.publishMapView({{"scale",view.value("scale").toDouble()}}));
        QTest::qWait(550);QCOMPARE(status().value("uploadOperations").toULongLong(),beforeReverse);
        editor.endMapInteraction();QTRY_VERIFY2_WITH_TIMEOUT(receipt(),qPrintable(QJsonDocument::fromVariant(status()).toJson(QJsonDocument::Compact)),15000);++processed;
        QVERIFY(editor.publishMapView({{"centerLongitude",180.},{"translateX",550.}}));
        QTRY_VERIFY_WITH_TIMEOUT(receipt(),15000);QVERIFY(shot("06-dateline"));++processed;
        QVERIFY(editor.setProjectionMode("globe"));QTRY_VERIFY_WITH_TIMEOUT(receipt(),15000);
        QVERIFY(shot("07-globe"));++processed;
        const auto beforeHide=status().value("uploadOperations").toULongLong();
        window->hide();QTest::qWait(100);
        QVERIFY(editor.publishMapView({{"scale",5000.}}));QTest::qWait(650);
        QCOMPARE(status().value("uploadOperations").toULongLong(),beforeHide);
        QCOMPARE(status().value("cpuDecodeReservedBytes").toULongLong(),qulonglong(0));
        QVERIFY(!receipt());window->show();QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(editor.publishMapView({{"scale",view.value("scale").toDouble()}}));
        QTRY_VERIFY_WITH_TIMEOUT(receipt(),15000);++processed;
        QVERIFY(editor.setTerrainMode("none"));QTest::qWait(200);QVERIFY(shot("08-none"));
        QTRY_COMPARE_WITH_TIMEOUT(status().value("qsgTextureNominalBytes").toULongLong(),qulonglong(0),5000);++processed;
        QVERIFY(editor.setTerrainMode("gray"));QTRY_VERIFY_WITH_TIMEOUT(receipt(),15000);
        QCOMPARE(editor.documentBytes(),document);QCOMPARE(editor.dirty(),dirty);
        QCOMPARE(editor.canUndo(),undo);QCOMPARE(editor.canRedo(),redo);++processed;
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        qInfo().noquote()<<QString("TERRAIN_CONTROLLER_DISPLAY expected=%1 processed=%2 fail=0 skip=0 actualDEM=true backend=D3D11").arg(expected).arg(processed);
        QCOMPARE(processed,expected);window->setProperty("allowClose",true);window->close();
    }
};
int main(int argc,char** argv){QQuickStyle::setStyle("Basic");QGuiApplication app(argc,argv);registerWindowsFrameType();TerrainControllerDisplayTests tests;return QTest::qExec(&tests,argc,argv);}
#include "terrain_controller_display_tests.moc"

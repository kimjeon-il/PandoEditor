#include "territorial_fixture.h"
#include "autosavecoordinator.h"
#include "editorcontroller.h"
#include "worlddatasetloader.h"
#include "mapscenebridge.h"
#include "losslessjson.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QSignalSpy>
#include <algorithm>

class AutosaveTests final : public QObject {
    Q_OBJECT
private slots:
    void debounceEnvelopeAndViewSeparation() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto projectPath=dir.filePath("autosave-project.json");
        const auto viewPath=dir.filePath("autosave-view.json");
        ProjectAutosave autosave(projectPath,viewPath);
        const QByteArray first=R"({"version":7,"documentId":"first"})";
        const QByteArray latest=R"({"version":7,"documentId":"latest"})";
        autosave.scheduleDocument(first);
        QTest::qWait(ProjectAutosave::SaveDelayMs-100);
        QVERIFY(!QFile::exists(projectPath));
        autosave.scheduleDocument(latest);
        QTest::qWait(ProjectAutosave::SaveDelayMs-100);
        QVERIFY(!QFile::exists(projectPath));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(projectPath),500);
        QCOMPARE(autosave.restoreDocument(),latest);

        MapViewState view;view.mode=ProjectionMode::Globe;view.centerLongitude=32;
        view.centerLatitude=-12;view.scale=2.5;view.revision=9;
        autosave.scheduleView(view);
        QVERIFY(!QFile::exists(viewPath));
        QVERIFY(autosave.flushNow());
        QVERIFY(QFile::exists(viewPath));
        const auto restored=autosave.restoreView();
        QVERIFY(restored.has_value());
        QCOMPARE(restored->mode,ProjectionMode::Globe);
        QCOMPARE(restored->centerLongitude,32.0);
        QCOMPARE(restored->centerLatitude,-12.0);
        QCOMPARE(restored->scale,2.5);
        QCOMPARE(autosave.restoreDocument(),latest);
    }
    void failedReplacementKeepsLastGoodAndCorruptInputIsPreserved() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto projectPath=dir.filePath("autosave-project.json");
        const auto viewPath=dir.filePath("autosave-view.json");
        ProjectAutosave autosave(projectPath,viewPath);
        const QByteArray good=R"({"version":7,"documentId":"good"})";
        autosave.scheduleDocument(good);QVERIFY(autosave.flushNow());
        QCOMPARE(autosave.restoreDocument(),good);
        autosave.scheduleDocument(QByteArray(ProjectStorage::MaximumProjectBytes+1,'x'));
        QVERIFY(!autosave.flushNow());
        QCOMPARE(autosave.restoreDocument(),good);

        QFile corrupt(projectPath);QVERIFY(corrupt.open(QIODevice::WriteOnly|QIODevice::Truncate));
        QCOMPARE(corrupt.write("not an envelope"),qint64(15));corrupt.close();
        QVERIFY(autosave.restoreDocument().isEmpty());
        QCOMPARE(QFile(projectPath).exists(),true);
        QVERIFY(QFile::exists(projectPath+".corrupt"));
    }
    void controllerRestoresCommittedDocumentWithoutTouchingSource() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        EditorControllerConfig config;config.autosaveEnabled=true;
        config.privateProjectPath=dir.filePath("private.json");
        config.autosaveProjectPath=dir.filePath("autosave-project.json");
        config.autosaveViewPath=dir.filePath("autosave-view.json");
        const auto sourcePath=dir.filePath("opened-project.json");
        QFile sample(":/assets/sample.pando.json");QVERIFY(sample.open(QIODevice::ReadOnly));
        const auto original=sample.readAll();sample.close();
        QFile source(sourcePath);QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write(original),qint64(original.size()));source.close();
        QByteArray expected;
        {
            EditorController editor(config);
            QVERIFY(editor.openFile(QUrl::fromLocalFile(sourcePath)));
            editor.selectCountry("DEU");editor.setColor("#123456");
            expected=editor.documentBytes();
            QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(config.autosaveProjectPath),1500);
        }
        QVERIFY(source.open(QIODevice::ReadOnly));QCOMPARE(source.readAll(),original);source.close();
        EditorController restored(config);
        QVERIFY(restored.startupBusy());
        QTRY_VERIFY_WITH_TIMEOUT(!restored.startupBusy(),10000);
        QCOMPARE(restored.documentBytes(),expected);
    }
    void immutableSnapshotsCoalesceAndFlushInOrder() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto path=dir.filePath("autosave-project.json");
        QFile sample(":/assets/sample.pando.json");QVERIFY(sample.open(QIODevice::ReadOnly));
        pandoeditor::Project project;project.replace(projectcodec::decode(sample.readAll()));
        const auto first=project.snapshot();
        QVERIFY(project.setColor("DEU",0x123456));
        const auto second=project.snapshot();
        QVERIFY(project.setColor("DEU",0xabcdef));
        const auto latest=projectcodec::encode(project);
        {
            ProjectAutosave save(path,dir.filePath("view.json"));
            save.scheduleDocument(first);
            QTest::qWait(ProjectAutosave::SaveDelayMs+10);
            save.scheduleDocument(second);
            save.scheduleDocument(project.snapshot());
            QVERIFY(save.flushNow());
            QCOMPARE(save.restoreDocument(),latest);
            // Destruction also drains a queued latest revision without losing edits.
            save.scheduleDocument(first);
        }
        ProjectAutosave check(path,dir.filePath("view.json"));
        QCOMPARE(check.restoreDocument(),projectcodec::encode(first));
    }
    void fullWorldRecoveryKeepsEventLoopResponsiveAndReusesMesh() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto root=QStringLiteral(PANDOEDITOR_WORLD_ASSET_DIR);
        pandoeditor::Project world;world.replace(*WorldDatasetLoader::canonical(root).document);
        QVERIFY(world.setColor("DEU",0x123456));
        const auto expected=projectcodec::encode(world);
        EditorControllerConfig config;config.autosaveEnabled=true;config.bootstrapWorld=true;
        config.privateProjectPath=dir.filePath("private.json");
        config.worldDataRoot=root;
        config.autosaveProjectPath=dir.filePath("autosave-project.json");
        config.autosaveViewPath=dir.filePath("autosave-view.json");
        MapViewState view;view.mode=ProjectionMode::Globe;view.scale=400;
        view.viewportWidth=800;view.viewportHeight=600;view.rotationLongitude=31;
        {
            ProjectAutosave save(config.autosaveProjectPath,config.autosaveViewPath);
            save.scheduleDocument(expected);save.scheduleView(view);QVERIFY(save.flushNow());
        }
        const auto read=[](const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return QByteArray{};return file.readAll();};
        const auto original=read(config.autosaveProjectPath),originalView=read(config.autosaveViewPath);
        int beats=0;qint64 maxGap=0,lastBeat=0;QElapsedTimer clock;clock.start();
        QTimer heartbeat;heartbeat.setInterval(5);
        connect(&heartbeat,&QTimer::timeout,this,[&]{
            const auto now=clock.elapsed();maxGap=std::max(maxGap,now-lastBeat);lastBeat=now;++beats;
        });heartbeat.start();
        EditorController restored(config);
        const auto constructorMs=clock.elapsed();
        QSignalSpy errors(&restored,&EditorController::errorOccurred);
        QVERIFY(restored.startupBusy());
        QVERIFY(!restored.saveFile(QUrl::fromLocalFile(dir.filePath("premature.json"))));
        QTRY_VERIFY_WITH_TIMEOUT(beats>0,1000);
        QTRY_VERIFY_WITH_TIMEOUT(!restored.startupBusy(),60000);
        const auto readyMs=clock.elapsed();heartbeat.stop();
        QVERIFY2(restored.worldStatus()==QStringLiteral("canonical"),
            errors.isEmpty()?qPrintable(restored.worldStatus()):qPrintable(errors.first().first().toString()));
        auto* bridge=qobject_cast<MapSceneBridge*>(restored.mapSceneBridge());QVERIFY(bridge);
        const auto scene=bridge->sceneSnapshot();QVERIFY(scene);QVERIFY(scene->worldBase);
        QCOMPARE(scene->worldCountries.size(),std::size_t(258));
        QVERIFY(scene->polygons.empty());
        QCOMPARE(restored.documentBytes(),expected);
        QTest::qWait(ProjectAutosave::SaveDelayMs+100);
        QCOMPARE(read(config.autosaveProjectPath),original);
        QCOMPARE(read(config.autosaveViewPath),originalView);
        qInfo()<<"Recovery constructor ms:"<<constructorMs<<"ready ms:"<<readyMs
               <<"UI heartbeats:"<<beats<<"max gap ms:"<<maxGap;
        beats=0;maxGap=0;lastBeat=0;clock.restart();heartbeat.start();
        QVERIFY(restored.setPresentationOpacity("countries",.6));
        QTRY_VERIFY_WITH_TIMEOUT(restored.presentationRecoveryAvailable(),60000);
        heartbeat.stop();
        qInfo()<<"Presentation recovery ms:"<<clock.elapsed()<<"UI heartbeats:"<<beats
               <<"max gap ms:"<<maxGap;
        QVERIFY(beats>0);
        QVERIFY(restored.discardPresentationRecovery());
        QTest::qWait(100);
        QVERIFY(!restored.presentationRecoveryAvailable());
        QVERIFY(restored.setPresentationOpacity("countries",.7));
        QTest::qWait(550);
        QVERIFY(restored.discardPresentationRecovery());
        QTest::qWait(2000);
        QVERIFY(!restored.presentationRecoveryAvailable());
    }
    void suppliedRecoveryCopy() {
        const auto fixture=qEnvironmentVariable("PANDOEDITOR_RECOVERY_FIXTURE");
        QTemporaryDir dir;QVERIFY(dir.isValid());
        EditorControllerConfig config;config.autosaveEnabled=true;config.bootstrapWorld=true;
        config.worldDataRoot=QStringLiteral(PANDOEDITOR_WORLD_ASSET_DIR);
        config.privateProjectPath=dir.filePath("private.json");
        config.autosaveProjectPath=dir.filePath("autosave-project.json");
        config.autosaveViewPath=dir.filePath("autosave-view.json");
        if(fixture.isEmpty()) {
            // Old development saves are refused without rewriting the recovery copy.
            pandoeditor::Project world;
            world.replace(*WorldDatasetLoader::canonical(config.worldDataRoot).document);
            auto legacy=losslessjson::parse(projectcodec::encode(world));
            legacy.object.at("version")=losslessjson::Value::num(7);
            auto& distributionSettings=legacy.object.at("presentation").object.at("webPresentation")
                .object.at("distributionSettings");
            distributionSettings.object.at("renderMode")=losslessjson::Value::str("dominant");
            distributionSettings.object.erase("activeLayerId");
            int affected=0;
            for(auto& unit:legacy.object.at("units").array) {
                if(pandoeditor::staticParentRelation(world.document(),unit.object.at("id").string).parentId.empty())continue;
                unit.object.at("baseName")=unit.object.at("name");++affected;
            }
            QCOMPARE(affected,47);
            ProjectAutosave seed(config.autosaveProjectPath,config.autosaveViewPath);
            seed.scheduleDocument(legacy.encode());QVERIFY(seed.flushNow());
        } else {
            QVERIFY(QFile::copy(fixture,config.autosaveProjectPath));
            const auto savedView=QFileInfo(fixture).dir().filePath("autosave-view.json");
            if(QFile::exists(savedView))QVERIFY(QFile::copy(savedView,config.autosaveViewPath));
        }
        const auto fingerprint=[](const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return QByteArray{};return QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256);};
        const auto before=fingerprint(config.autosaveProjectPath);
        const auto viewBefore=fingerprint(config.autosaveViewPath);
        int beats=0;qint64 maxGap=0,lastBeat=0;QElapsedTimer clock;clock.start();
        QTimer pulse;pulse.setInterval(5);
        connect(&pulse,&QTimer::timeout,this,[&]{const auto now=clock.elapsed();maxGap=std::max(maxGap,now-lastBeat);lastBeat=now;++beats;});pulse.start();
        EditorController restored(config);const auto constructorMs=clock.elapsed();
        QSignalSpy errors(&restored,&EditorController::errorOccurred);
        QVERIFY(restored.startupBusy());
        QTRY_VERIFY_WITH_TIMEOUT(!restored.startupBusy(),60000);
        pulse.stop();
        QVERIFY2(restored.worldStatus()==QStringLiteral("canonical"),
            errors.isEmpty()?qPrintable(restored.worldStatus()):qPrintable(errors.first().first().toString()));
        QVERIFY(beats>0);
        auto* bridge=qobject_cast<MapSceneBridge*>(restored.mapSceneBridge());QVERIFY(bridge);
        QVERIFY(bridge->sceneSnapshot()->worldBase);
        QCOMPARE(fingerprint(config.autosaveProjectPath),before);
        QCOMPARE(fingerprint(config.autosaveViewPath),viewBefore);
        qInfo()<<"Supplied copy constructor ms:"<<constructorMs<<"ready ms:"<<clock.elapsed()
               <<"UI heartbeats:"<<beats<<"max gap ms:"<<maxGap;
        const auto recovered=restored.documentBytes();
        pandoeditor::Project roundtrip;roundtrip.replace(projectcodec::decode(recovered));
        QCOMPARE(projectcodec::encode(roundtrip),recovered);
    }
    void canonicalIdsDoNotReplaceEditedCoordinates() {
        const auto root=QStringLiteral(PANDOEDITOR_WORLD_ASSET_DIR);
        auto document=*WorldDatasetLoader::canonical(root).document;
        const auto ref=staticGeometryBinding(document,document.units.front().id).geometryRef;
        auto changed=*document.geometries.get(ref);
        changed.polygons.front().front().front().x+=0.000001;
        changed.polygons.front().front().back()=changed.polygons.front().front().front();
        pandoeditor::GeometryStore replacement;
        for(const auto& [key,geometry]:document.geometries.versions())
            replacement.insert(key,key==ref?changed:*geometry);
        document.geometries=std::move(replacement);
        QVERIFY(!WorldDatasetLoader::matchingBaseFrame(document,root));
    }
    void obsoleteEntityKindRemainsRejected() {
        QFile sample(":/assets/sample.pando.json");QVERIFY(sample.open(QIODevice::ReadOnly));
        pandoeditor::Project project;project.replace(projectcodec::decode(sample.readAll()));
        auto encoded=losslessjson::parse(projectcodec::encode(project));
        auto& unit=encoded.object.at("units").array.front();
        unit.object.at("kind")=losslessjson::Value::str("subunit");
        unit.object.at("baseName")=losslessjson::Value::str("invalid country-only field");
        try {
            projectcodec::decode(encoded.encode());
            QFAIL("Obsolete entity kind must be rejected");
        } catch(const std::invalid_argument& error) {
            QCOMPARE(QByteArray(error.what()),QByteArray("INVALID_ENTITY_KIND"));
        }
    }
    void failedRecoveryDoesNotOverwriteAutosave() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        EditorControllerConfig config;config.autosaveEnabled=true;
        config.privateProjectPath=dir.filePath("private.json");
        config.autosaveProjectPath=dir.filePath("autosave-project.json");
        config.autosaveViewPath=dir.filePath("autosave-view.json");
        QFile corrupt(config.autosaveProjectPath);QVERIFY(corrupt.open(QIODevice::WriteOnly));
        corrupt.write("broken recovery");corrupt.close();
        {
            EditorController restored(config);
            QTRY_VERIFY_WITH_TIMEOUT(!restored.startupBusy(),10000);
            QCOMPARE(restored.worldStatus(),QStringLiteral("recovery-failed"));
            QVERIFY(!restored.saveFile(QUrl::fromLocalFile(config.autosaveProjectPath)));
        }
        QVERIFY(corrupt.open(QIODevice::ReadOnly));QCOMPARE(corrupt.readAll(),QByteArray("broken recovery"));
        QVERIFY(!QFile::exists(config.autosaveViewPath));
    }
};

QTEST_GUILESS_MAIN(AutosaveTests)
#include "autosave_tests.moc"

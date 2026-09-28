#include "autosavecoordinator.h"
#include "editorcontroller.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

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
        QCOMPARE(restored.documentBytes(),expected);
    }
};

QTEST_GUILESS_MAIN(AutosaveTests)
#include "autosave_tests.moc"

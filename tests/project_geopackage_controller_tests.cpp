#include "editorcontroller.h"
#include "gisgeopackage.h"
#include <QtTest>
#include <QFile>
#include <QTemporaryDir>

class ProjectGeoPackageControllerTests:public QObject {
    Q_OBJECT
private slots:
    void exportOpenAndInvalidSource() {
        QTemporaryDir directory;QVERIFY(directory.isValid());
        EditorController editor;
        const auto original=editor.documentBytes();
        const auto sourceRevision=editor.revision();
        const auto path=directory.filePath("project.gpkg");
        QVERIFY(editor.exportProjectGeoPackage(QUrl::fromLocalFile(path)));
        QTRY_COMPARE_WITH_TIMEOUT(editor.projectGpkgState().value("stage").toString(),QString("done"),15000);
        QVERIFY(QFile::exists(path));
        QVERIFY(pandoeditor::readGisGeoPackage(path).projectPackage);
        QCOMPARE(editor.documentBytes(),original);
        QCOMPARE(editor.revision(),sourceRevision);

        EditorController restored;
        QVERIFY(restored.openProjectGeoPackage(QUrl::fromLocalFile(path)));
        QCOMPARE(restored.documentBytes(),original);
        QVERIFY(restored.dirty());
        QVERIFY(!restored.hasFile()); // An ordinary Save must not overwrite SQLite with JSON.
        const auto restoredRevision=restored.revision();
        const auto broken=directory.filePath("broken.gpkg");
        QFile bad(broken);QVERIFY(bad.open(QIODevice::WriteOnly));
        QVERIFY(bad.write("invalid")>0);bad.close();
        QVERIFY(!restored.openProjectGeoPackage(QUrl::fromLocalFile(broken)));
        QCOMPARE(restored.documentBytes(),original);
        QCOMPARE(restored.revision(),restoredRevision);
        QVERIFY(!restored.hasFile());

        QVERIFY(!editor.exportProjectGeoPackage(QUrl::fromLocalFile(directory.filePath("incorrect.json"))));
        QVERIFY(!QFile::exists(directory.filePath("incorrect.json")));
    }
};
QTEST_MAIN(ProjectGeoPackageControllerTests)
#include "project_geopackage_controller_tests.moc"

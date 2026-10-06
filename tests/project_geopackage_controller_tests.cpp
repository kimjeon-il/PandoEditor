#include "editorcontroller.h"
#include "gisgeopackage.h"
#include "projectcodec.h"
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

        const auto web=QString::fromUtf8(WEB_GPKG_FIXTURE)+"/../lineage-v10-content/content.gpkg";
        QVERIFY(restored.openProjectGeoPackage(QUrl::fromLocalFile(web)));
        const auto webDocument=projectcodec::decode(restored.documentBytes());
        QCOMPARE(webDocument.units.size(),std::size_t(4));
        QCOMPARE(webDocument.distributionEntries.size(),std::size_t(1));
        QVERIFY(restored.dirty());
        QVERIFY(!restored.hasFile());
        const auto webBytes=restored.documentBytes();
        const auto webRevision=restored.revision();
        QVERIFY(!restored.openProjectGeoPackage(QUrl::fromLocalFile(broken)));
        QCOMPARE(restored.documentBytes(),webBytes);
        QCOMPARE(restored.revision(),webRevision);

        QVERIFY(!editor.exportProjectGeoPackage(QUrl::fromLocalFile(directory.filePath("incorrect.json"))));
        QVERIFY(!QFile::exists(directory.filePath("incorrect.json")));
    }
};
QTEST_MAIN(ProjectGeoPackageControllerTests)
#include "project_geopackage_controller_tests.moc"

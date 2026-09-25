#include "editorcontroller.h"
#include "giszip.h"
#include "gisgeopackage.h"
#include <QtTest>
#include <QFile>
#include <QTemporaryDir>

using namespace pandoeditor;
class GisExportControllerTests:public QObject {
    Q_OBJECT
private slots:
    void selectedOnlyAndCancellation() {
        QTemporaryDir directory;QVERIFY(directory.isValid());
        EditorController editor;
        const auto before=editor.documentBytes();
        const auto revision=editor.revision();
        const auto layers=editor.gisExportLayers();
        QCOMPARE(layers.first().toMap().value("category").toString(),QString("countries"));
        QVERIFY(layers.first().toMap().value("count").toInt()>0);

        const auto zipPath=directory.filePath("gis.zip");
        QVERIFY(editor.exportGisData(QUrl::fromLocalFile(zipPath),"geojson-zip",{"countries"}));
        QTRY_COMPARE_WITH_TIMEOUT(editor.gisExportState().value("stage").toString(),QString("done"),15000);
        QFile zip(zipPath);QVERIFY(zip.open(QIODevice::ReadOnly));
        const auto archive=parseGisGeoJsonZip(zip.readAll());
        QVERIFY(archive.webManifest);QCOMPARE(archive.layers.size(),std::size_t(1));
        QCOMPARE(archive.layers.front().targetType,std::string("country"));
        QCOMPARE(editor.documentBytes(),before);
        QCOMPARE(editor.revision(),revision);

        const auto gpkgPath=directory.filePath("gis.gpkg");
        QVERIFY(editor.exportGisData(QUrl::fromLocalFile(gpkgPath),"geopackage",{"countries"}));
        QTRY_COMPARE_WITH_TIMEOUT(editor.gisExportState().value("stage").toString(),QString("done"),15000);
        const auto gpkg=readGisGeoPackage(gpkgPath);
        QVERIFY(!gpkg.projectPackage);QCOMPARE(gpkg.layers.size(),std::size_t(1));
        QCOMPARE(gpkg.layers.front().tableName,std::string("countries"));
        QCOMPARE(editor.documentBytes(),before);

        const auto rejected=directory.filePath("rejected.gpkg");
        QVERIFY(!editor.exportGisData(QUrl::fromLocalFile(rejected),"geopackage",{}));
        QVERIFY(!QFile::exists(rejected));
        const auto cancelled=directory.filePath("cancelled.zip");
        QVERIFY(editor.exportGisData(QUrl::fromLocalFile(cancelled),"geojson-zip",{"countries"}));
        editor.cancelGisExport();
        QTest::qWait(250);
        QVERIFY(!QFile::exists(cancelled));
        QCOMPARE(editor.documentBytes(),before);
    }
};
QTEST_MAIN(GisExportControllerTests)
#include "gis_export_controller_tests.moc"

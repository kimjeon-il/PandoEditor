#include "editorcontroller.h"
#include <QtTest>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

class GisFailureMatrixTests:public QObject {
    Q_OBJECT
private slots:
    void corruptedInputsAndStaleConfirmationKeepLiveState() {
        QTemporaryDir directory;QVERIFY(directory.isValid());
        EditorController editor;
        const auto country=editor.countryRows().front().toMap().value("id").toString();
        editor.selectCountry(country);
        const auto before=editor.documentBytes();
        const auto revision=editor.revision();
        const auto selected=editor.selectedId();
        const auto dirty=editor.dirty();

        auto write=[&](const QString& name,const QByteArray& bytes) {
            const auto path=directory.filePath(name);
            QFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size())return QString();
            file.close();return path;
        };
        const auto malformed=write("malformed.geojson","{broken");
        const auto brokenZip=write("broken.zip","not a ZIP archive");
        const auto brokenPackage=write("broken.gpkg","not SQLite");
        QVERIFY(!malformed.isEmpty()&&!brokenZip.isEmpty()&&!brokenPackage.isEmpty());

        const auto source=QString::fromUtf8(WEB_GIS_FIXTURE)+"/web-gis.gpkg";
        const auto badCrs=directory.filePath("bad-crs.gpkg");
        QVERIFY(QFile::copy(source,badCrs));
        {
            auto db=QSqlDatabase::addDatabase("QSQLITE","gis-failure-matrix");
            db.setDatabaseName(badCrs);QVERIFY(db.open());
            {QSqlQuery query(db);QVERIFY(query.exec(
                "UPDATE gpkg_geometry_columns SET srs_id=3857 WHERE table_name='countries'"));}
            db.close();db={};
        }
        QSqlDatabase::removeDatabase("gis-failure-matrix");

        for(const auto& path:{malformed,brokenZip,brokenPackage,badCrs}) {
            QVERIFY(editor.loadGisSource(QUrl::fromLocalFile(path)));
            QTRY_COMPARE_WITH_TIMEOUT(editor.gisImportState().value("stage").toString(),QString("error"),15000);
            QCOMPARE(editor.documentBytes(),before);
            QCOMPARE(editor.revision(),revision);
            QCOMPARE(editor.selectedId(),selected);
            QCOMPARE(editor.dirty(),dirty);
            editor.cancelGisImport();
        }

        const auto point=write("point.geojson",R"({"type":"FeatureCollection","features":[{"type":"Feature","id":"late:point","properties":{"name":"Late"},"geometry":{"type":"Point","coordinates":[1,1]}}]})");
        QVERIFY(editor.loadGisSource(QUrl::fromLocalFile(point)));
        QTRY_COMPARE_WITH_TIMEOUT(editor.gisImportState().value("stage").toString(),QString("mapping"),15000);
        QVERIFY(editor.prepareGisImport(0,{{"target","generic"},{"idField","__fid__"}}));
        QTRY_COMPARE_WITH_TIMEOUT(editor.gisImportState().value("stage").toString(),QString("impact"),15000);
        const auto token=editor.gisImportState().value("session").toULongLong();
        editor.addLayer(); // The prepared impact is now based on an older revision.
        const auto changed=editor.documentBytes();
        const auto changedRevision=editor.revision();
        const auto changedSelection=editor.selectedId();
        QVERIFY(!editor.confirmGisImport(token));
        QCOMPARE(editor.documentBytes(),changed);
        QCOMPARE(editor.revision(),changedRevision);
        QCOMPARE(editor.selectedId(),changedSelection);
    }
};
QTEST_MAIN(GisFailureMatrixTests)
#include "gis_failure_matrix_tests.moc"

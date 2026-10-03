#include "editorcontroller.h"
#include "projectcodec.h"
#include <QtTest>
#include <QFile>
#include <QTemporaryDir>

class GisImportControllerTests:public QObject {
    Q_OBJECT
private slots:
    void genericAndTerritorialDistributionConfirmation() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        auto write=[&](const QString& name,const QByteArray& bytes) {
            const auto path=dir.filePath(name);QFile file(path);
            if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size())return QString();
            file.close();return path;
        };
        EditorController editor;
        const auto original=projectcodec::decode(editor.documentBytes());
        const auto point=write("point.geojson",R"({"type":"FeatureCollection","features":[{"type":"Feature","id":"gis:point","properties":{"name":"Imported","custom":42},"geometry":{"type":"Point","coordinates":[1,1]}}]})");
        QVERIFY(!point.isEmpty());
        QVERIFY(editor.loadGisSource(QUrl::fromLocalFile(point)));
        QTRY_COMPARE_WITH_TIMEOUT(editor.gisImportState().value("stage").toString(),QString("mapping"),15000);
        QVERIFY(editor.prepareGisImport(0,{{"target","generic"},{"idField","__fid__"}}));
        QTRY_COMPARE_WITH_TIMEOUT(editor.gisImportState().value("stage").toString(),QString("impact"),15000);
        QCOMPARE(projectcodec::decode(editor.documentBytes()).genericFeatures.size(),original.genericFeatures.size());
        const auto token=editor.gisImportState().value("session").toULongLong();
        QVERIFY(editor.confirmGisImport(token));
        QVERIFY(!editor.confirmGisImport(token));
        auto after=projectcodec::decode(editor.documentBytes());
        QCOMPARE(after.genericFeatures.size(),original.genericFeatures.size()+1);
        QCOMPARE(QString::fromStdString(after.genericFeatures.back().id),QString("gis:point"));
        QVERIFY(QString::fromStdString(after.genericFeatures.back().source.details).contains("custom"));
        editor.undo();
        QCOMPARE(projectcodec::decode(editor.documentBytes()).genericFeatures.size(),original.genericFeatures.size());

        const auto country=editor.countryRows().front().toMap().value("id").toString();
        QVERIFY(!country.isEmpty());
        const auto polygon=write("distribution.geojson",QString(R"({"type":"FeatureCollection","features":[{"type":"Feature","id":"entry:fixture","properties":{"entry_id":"entry:fixture","layer_id":"lang:fixture","name":"Fixture values","unit":"people","source_mode":"territorial","territorial_unit_id":"%1","value":600},"geometry":{"type":"Polygon","coordinates":[[[0,0],[1,0],[1,1],[0,1],[0,0]]]}}]})").arg(country).toUtf8());
        QVERIFY(!polygon.isEmpty());
        QVERIFY(editor.loadGisSource(QUrl::fromLocalFile(polygon)));
        QTRY_COMPARE_WITH_TIMEOUT(editor.gisImportState().value("stage").toString(),QString("mapping"),15000);
        QVERIFY(editor.prepareGisImport(0,{{"target","distribution"},
            {"idField","entry_id"}}));
        QTRY_COMPARE_WITH_TIMEOUT(editor.gisImportState().value("stage").toString(),QString("impact"),15000);
        QCOMPARE(projectcodec::decode(editor.documentBytes()).distributionEntries.size(),original.distributionEntries.size());
        const auto distributionToken=editor.gisImportState().value("session").toULongLong();
        QVERIFY(editor.confirmGisImport(distributionToken));
        after=projectcodec::decode(editor.documentBytes());
        QCOMPARE(after.distributionEntries.size(),original.distributionEntries.size()+1);
        QCOMPARE(QString::fromStdString(after.distributionEntries.back().territory->id),country);
        QVERIFY(!after.distributionEntries.back().geometry);
        editor.undo();
        QCOMPARE(projectcodec::decode(editor.documentBytes()).distributionEntries.size(),original.distributionEntries.size());

        QVERIFY(editor.loadGisSource(QUrl::fromLocalFile(point)));
        editor.cancelGisImport();
        QTest::qWait(100);
        QCOMPARE(editor.gisImportState().value("stage").toString(),QString("empty"));
        QCOMPARE(projectcodec::decode(editor.documentBytes()).genericFeatures.size(),original.genericFeatures.size());
    }
};
QTEST_MAIN(GisImportControllerTests)
#include "gis_import_controller_tests.moc"

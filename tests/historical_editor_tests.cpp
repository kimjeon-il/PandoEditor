#include "editorcontroller.h"
#include "projectcodec.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>

class HistoricalEditorTests:public QObject {
    Q_OBJECT
private slots:
    void bundledPilotIsDefaultCatalog() {
        EditorController editor;
        QCOMPARE(editor.historicalStage(),QStringLiteral("ready"));
        QCOMPARE(editor.historicalCatalogName(),QStringLiteral("내장 pilot"));
        QVERIFY(editor.historicalSnapshots().size()>=1);
        bool pilot=false;for(const auto& row:editor.historicalSnapshots())
            if(row.toMap().value("id")==QStringLiteral("pilot-1991"))pilot=true;
        QVERIFY(pilot);
        editor.searchHistorical(QString(),QString(),QStringLiteral("all"),QString(),QString());
        QVERIFY(!editor.historicalResults().isEmpty());
    }
    void catalogSearchPreviewAndConfirm() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        QFile file(dir.filePath("library.json"));QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray json=R"({"schemaVersion":2,"entities":[{"libraryId":"historical-country:fixture","type":"country","canonicalName":"Fixture","displayNames":{"ko":"예시"},"geometryVersions":[{"id":"v1","geometry":{"type":"Polygon","coordinates":[[[70,0],[72,0],[72,2],[70,2],[70,0]]]}}]}],"snapshots":[{"id":"fixture-snapshot","name":"Example","referenceDate":"1945","entityRefs":["historical-country:fixture"]}]})";
        QCOMPARE(file.write(json),json.size());file.close();
        EditorController editor;
        const auto before=projectcodec::decode(editor.documentBytes()).units.size();
        QVERIFY(editor.loadHistoricalLibrary(QUrl::fromLocalFile(file.fileName())));
        QVERIFY(editor.historicalCatalogName().startsWith(QStringLiteral("로컬: ")));
        QCOMPARE(editor.historicalSnapshots().size(),qsizetype(1));
        editor.searchHistorical(QStringLiteral("예시"),QString(),QStringLiteral("all"),QStringLiteral("1945"),QString());
        QCOMPARE(editor.historicalResults().size(),qsizetype(1));
        editor.selectHistorical(QStringLiteral("historical-country:fixture"),QString(),QStringLiteral("1945"));
        QCOMPARE(editor.historicalPreview().value("geometryVersionId").toString(),QStringLiteral("v1"));
        QVERIFY(editor.prepareHistoricalAdd({{"libraryId",QStringLiteral("historical-country:fixture")},
                                           {"referenceDate",QStringLiteral("1945")}}));
        QTRY_COMPARE_WITH_TIMEOUT(editor.historicalStage(),QStringLiteral("impact"),15000);
        const auto token=editor.historicalSession();
        QVERIFY(editor.confirmHistoricalAdd(token));
        QVERIFY(!editor.confirmHistoricalAdd(token));
        QCOMPARE(projectcodec::decode(editor.documentBytes()).units.size(),before+1);
        editor.undo();
        QCOMPARE(projectcodec::decode(editor.documentBytes()).units.size(),before);
        QVERIFY(editor.prepareHistoricalAdd({{"libraryId",QStringLiteral("historical-country:fixture")}}));
        editor.cancelHistoricalAdd();
        QTest::qWait(100);
        QCOMPARE(editor.historicalStage(),QStringLiteral("ready"));
        QCOMPARE(projectcodec::decode(editor.documentBytes()).units.size(),before);
    }
};
QTEST_MAIN(HistoricalEditorTests)
#include "historical_editor_tests.moc"

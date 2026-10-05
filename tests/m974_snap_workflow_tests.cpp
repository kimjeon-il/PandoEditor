#include "editorcontroller.h"
#include "projectcodec.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <cmath>
using namespace pandoeditor;
class M974SnapWorkflowTests : public QObject {
    Q_OBJECT
private slots:
    void coldCandidateCachePreservesFirstPoint() {
        QTemporaryDir directory;
        Project p; p.replace(std::vector<Country>{{"A","A",{{{{0,0},{2,0},{2,2},{0,2},{0,0}}}},0xabcdef}});
        QFile file(directory.filePath("input.json"));QVERIFY(file.open(QIODevice::WriteOnly));file.write(projectcodec::encode(p));file.close();
        EditorController controller({false,directory.filePath("private.json")});QVERIFY(controller.openFile(QUrl::fromLocalFile(file.fileName())));
        QVERIFY(controller.selectObject({{"domain","territorial"},{"id","A"}},"replace"));
        controller.setProjectionMode("flat");controller.resizeMapCamera(800,600);
        QVERIFY(controller.beginGeometryDraw());MapProjection projection;projection.rebuild(p.document());
        const auto raw=projection.project({.005,.005});
        QVERIFY2(!controller.geometryHoverSnap(raw.x,raw.y,"mouse"),"Pinned web ignores hover before the first draft point");
        QTest::qWait(50);QCOMPARE(controller.geometrySnapState().value("submitted").toULongLong(),qulonglong(0));
        QVERIFY(controller.geometryAddPoint(raw.x,raw.y,.1));
        QVERIFY2(!std::isfinite(controller.geometryEditState().value("snapX").toDouble()),"A cold web snap cache returns the raw input, not a synchronous snapped vertex");
        const auto paths=controller.geometryDraftPaths();QVERIFY(!paths.isEmpty());const auto vertices=paths.front().toMap().value("vertices").toList();QVERIFY(!vertices.isEmpty());
        QCOMPARE(vertices.front().toMap().value("x").toDouble(),raw.x);
        QCOMPARE(vertices.front().toMap().value("y").toDouble(),raw.y);
        QTRY_COMPARE(controller.geometrySnapState().value("status").toString(),QString("ready"));
        // Readying candidates never rewrites the first accepted point.
        QCOMPARE(controller.geometryDraftPaths().front().toMap().value("vertices").toList().front().toMap().value("x").toDouble(),raw.x);
        QVERIFY(controller.geometryAddPoint(raw.x,raw.y,.1,"mouse"));
        QCOMPARE(controller.geometrySnapState().value("indicator").toMap().value("kind").toString(),QString("edge"));
        const auto after=controller.geometryDraftPaths().front().toMap().value("vertices").toList();
        QCOMPARE(after.front().toMap().value("x").toDouble(),raw.x);
        QVERIFY(after[1].toMap().value("x").toDouble()!=raw.x||after[1].toMap().value("y").toDouble()!=raw.y);
        controller.cancelGeometryEdit();QCOMPARE(controller.geometrySnapState().value("status").toString(),QString("empty"));
    }
};
QTEST_MAIN(M974SnapWorkflowTests)
#include "m974_snap_workflow_tests.moc"

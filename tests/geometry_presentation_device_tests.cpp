#include "editorcontroller.h"
#include "projectcodec.h"
#include "gpumapitem.h"
#include "maprenderitem.h"
#include <pandoeditor/project.h>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <QFile>
#include <QtTest>

// Actual Windows renderer receipts. No manually emitted presentation signals,
// mocked upload, GPU fence or driver-memory claim is made by this test.
class GeometryPresentationDeviceTests:public QObject {
    Q_OBJECT
private slots:
    void matchingPresentedSuccessorRetiresHeldStroke_data() {
        QTest::addColumn<bool>("gpu");QTest::newRow("gpu")<<true;QTest::newRow("painted")<<false;
    }
    void matchingPresentedSuccessorRetiresHeldStroke() {
        QFETCH(bool,gpu);QCOMPARE(qEnvironmentVariable("QT_QPA_PLATFORM"),QString("windows"));
        QTemporaryDir directory;QVERIFY(directory.isValid());
        EditorControllerConfig config;config.appearancePath=directory.filePath("appearance.json");
        EditorController editor(config);
        using namespace pandoeditor;
        ProjectDocument document({{"A","Alpha",{{{{0,0},{10,0},{10,10},{0,10},{0,0}}}},0xabcdef}},{{"countries","Countries"}});
        Geometry unused;unused.type="Polygon";unused.polygons={{{{30,0},{31,0},{31,1},{30,1},{30,0}}}};
        document.geometries.insert({"unreferenced",77},unused);
        Project project;project.replace(document);QFile file(directory.filePath("input.json"));QVERIFY(file.open(QIODevice::WriteOnly));
        const auto bytes=projectcodec::encode(project);QCOMPARE(file.write(bytes),qint64(bytes.size()));file.close();
        QVERIFY(editor.openFile(QUrl::fromLocalFile(file.fileName())));QVERIFY(editor.setProjectionMode("flat"));QVERIFY(editor.resizeMapCamera(800,600,1));
        auto* bridge=qobject_cast<MapSceneBridge*>(editor.mapSceneBridge());QVERIFY(bridge);
        QQuickWindow window;window.resize(800,600);
        QQuickItem* item=nullptr;int actualReceipts=0;QVariantList actualInventory;std::shared_ptr<const MapFrame> actualFrame;
        const auto received=[&](const std::shared_ptr<const MapFrame>& frame,const QVariantList& inventory){++actualReceipts;actualFrame=frame;actualInventory=inventory;};
        if(gpu) {
            auto* renderer=new GpuMapItem(window.contentItem());renderer->setSceneBridge(bridge);item=renderer;
            editor.recordMapPresentationSource(renderer);connect(renderer,&GpuMapItem::framePresented,this,received);
        } else {
            auto* renderer=new MapRenderItem(window.contentItem());renderer->setSceneBridge(bridge);item=renderer;
            editor.recordMapPresentationSource(renderer);connect(renderer,&MapRenderItem::framePresented,this,received);
        }
        item->setSize(QSizeF(800,600));window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCOMPARE(window.rendererInterface()->graphicsApi(),QSGRendererInterface::Direct3D11);
        QTRY_VERIFY_WITH_TIMEOUT(actualReceipts>0,5000);
        QVERIFY(editor.selectObject({{"domain","territorial"},{"id","A"}},"replace"));QVERIFY(editor.beginGeometryEdit());
        const auto vertex=editor.geometryDraftPaths().front().toMap().value("vertices").toList().front().toMap();
        QVERIFY(editor.geometrySelectNearest(vertex.value("x").toDouble(),vertex.value("y").toDouble(),.001));
        QVERIFY(editor.geometryMoveSelectedVertex(vertex.value("x").toDouble()+.01,vertex.value("y").toDouble()+.01,0));
        QVERIFY(editor.requestGeometryPreview());QVERIFY(editor.confirmGeometryEdit());
        QVERIFY(!editor.geometryPresentationPaths().isEmpty());
        const auto committed=editor.documentBytes();const auto revision=editor.revision();const int before=actualReceipts;
        QTRY_VERIFY_WITH_TIMEOUT(actualReceipts>before&&editor.geometryPresentationPaths().isEmpty(),5000);
        QVERIFY(actualFrame==bridge->frameSnapshot());QVERIFY(!actualInventory.isEmpty());
        QCOMPARE(editor.documentBytes(),committed);QCOMPARE(editor.revision(),revision);
        const auto restored=projectcodec::decode(committed);QVERIFY(restored.geometries.get({"unreferenced",77}));
        QVERIFY(!window.grabWindow().isNull());
        qInfo()<<"Actual presentation backend"<<(gpu?"gpu":"painted")<<"receipts"<<actualReceipts<<"inventory"<<actualInventory.size()<<"processed=1 mismatch=0 skip=0";
        window.hide();
    }
};
QTEST_MAIN(GeometryPresentationDeviceTests)
#include "geometry_presentation_device_tests.moc"

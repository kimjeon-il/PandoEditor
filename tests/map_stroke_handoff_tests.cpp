#include "scenegraph/mapscenenode.h"
#include <QSGGeometryNode>
#include <QtTest>
#include <limits>

namespace {
// Fixed Web 5b11879f82efe8aac324e31d60be2bccfbcf7aee stages scene
// stroke domains until their matching successor presentation. These tests
// exercise the earlier CPU/QSG admission boundary only: a QSGGeometry
// allocation is not a driver upload or a displayed-frame receipt.
std::shared_ptr<RenderScene> strokes(const std::string& domain, unsigned version) {
    auto scene=std::make_shared<RenderScene>();scene->revision=version;
    scene->worldPlan.worldOffsets={0};
    for(unsigned i=0;i<2;++i) {
        StrokeDrawPacket draw;draw.key="stroke/"+std::to_string(i);
        draw.object={domain,"owner-"+std::to_string(i)};
        draw.geometry={"shape-"+std::to_string(i),version};
        draw.geometryRevision=version;draw.style.color=0x123456;
        const float x=float(i*20+version);
        draw.geometryPacket.startsEnds=std::make_shared<const std::vector<float>>(
            std::vector<float>{x,0,x+10,0});
        draw.geometryPacket.segmentCount=1;
        scene->strokes.push_back(std::move(draw));
        scene->drawSequence.push_back({PrimitiveKind::Stroke,i,{},-1});
    }
    return scene;
}
float firstLongitude(QSGNode* node) {
    const auto* geometry=static_cast<QSGGeometryNode*>(node)->geometry();
    return *static_cast<const float*>(geometry->vertexData());
}
}

class MapStrokeHandoffTests final:public QObject {
    Q_OBJECT
private slots:
    void deferredSuccessorKeepsPreviousStroke_data() {
        QTest::addColumn<QString>("domain");
        QTest::newRow("generic")<<QStringLiteral("generic");
        QTest::newRow("hydroBuiltin")<<QStringLiteral("hydroBuiltin");
        QTest::newRow("territorial")<<QStringLiteral("territorial");
    }
    void deferredSuccessorKeepsPreviousStroke() {
        QFETCH(QString,domain);
        MapSceneNode node;MapGpuStats stats;MapFlatViewport flat;
        MapViewState view;view.mode=ProjectionMode::Flat;
        view.viewportWidth=100;view.viewportHeight=100;view.revision=1;
        auto previous=strokes(domain.toStdString(),1);
        node.sync(previous,view,flat,stats,std::numeric_limits<std::size_t>::max());
        QCOMPARE(node.childCount(),2);QVERIFY(!stats.uploadsPending);
        auto* oldSecond=node.lastChild();
        QCOMPARE(firstLongitude(oldSecond),21.f);
        const auto oneNodeBytes=std::size_t(stats.geometryBytes/2);
        const auto uploadsBefore=stats.geometryUploadCount;

        auto successor=strokes(domain.toStdString(),2);
        node.sync(successor,view,flat,stats,oneNodeBytes);
        QVERIFY(stats.uploadsPending);
        QCOMPARE(stats.geometryUploadCount,uploadsBefore+1);
        // The budget admits only the first changed stroke. The second still
        // draws its real previous geometry rather than disappearing.
        QCOMPARE(node.childCount(),2);
        QCOMPARE(node.lastChild(),oldSecond);
        QCOMPARE(firstLongitude(node.firstChild()),2.f);
        QCOMPARE(firstLongitude(node.lastChild()),21.f);

        node.sync(successor,view,flat,stats,oneNodeBytes);
        QVERIFY(!stats.uploadsPending);QCOMPARE(node.childCount(),2);
        QCOMPARE(stats.geometryUploadCount,uploadsBefore+2);
        QCOMPARE(firstLongitude(node.firstChild()),2.f);
        QCOMPARE(firstLongitude(node.lastChild()),22.f);
        const auto completedUploads=stats.geometryUploadCount;
        node.sync(successor,view,flat,stats,oneNodeBytes);
        QCOMPARE(stats.geometryUploadCount,completedUploads);
    }
};

QTEST_MAIN(MapStrokeHandoffTests)
#include "map_stroke_handoff_tests.moc"

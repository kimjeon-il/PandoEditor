#include "maprenderitem.h"
#include "scenegraph/mapscenenode.h"
#include <QSGGeometryNode>
#include <pandoeditor/map/renderpacket.h>
#include <QtTest>
#include <QImage>
#include <QPainter>
#include <QQuickWindow>
#include <cmath>
#include <memory>
#include <vector>

namespace {
constexpr double DegreesPerRadian=180.0/3.14159265358979323846;

MapViewState viewFor(int width,int height) {
    MapViewState view;
    view.mode=ProjectionMode::Flat;
    view.viewportWidth=width;
    view.viewportHeight=height;
    view.scale=DegreesPerRadian; // one longitude/latitude degree per screen pixel
    view.translateX=0;
    view.translateY=height;
    return view;
}

pandoeditor::Point screenPoint(double x,double y,double height) {
    return {x,height-y};
}

pandoeditor::Geometry rectangle(double left,double top,double right,double bottom,double height) {
    pandoeditor::Geometry geometry;
    geometry.type="Polygon";
    geometry.polygons={{{screenPoint(left,top,height),screenPoint(right,top,height),
                         screenPoint(right,bottom,height),screenPoint(left,bottom,height),
                         screenPoint(left,top,height)}}};
    return geometry;
}

pandoeditor::Geometry polygonWithHole(double height) {
    pandoeditor::Geometry geometry;
    geometry.type="Polygon";
    geometry.polygons={{
        {screenPoint(5,5,height),screenPoint(35,5,height),screenPoint(35,35,height),
         screenPoint(5,35,height),screenPoint(5,5,height)},
        {screenPoint(15,15,height),screenPoint(15,25,height),screenPoint(25,25,height),
         screenPoint(25,15,height),screenPoint(15,15,height)}
    }};
    return geometry;
}

PolygonDrawPacket polygonDraw(const std::string& id,const pandoeditor::Geometry& geometry,
                              std::uint32_t color,BlendMode blend=BlendMode::Normal) {
    PolygonDrawPacket draw;
    draw.key=id;
    draw.object={"generic",id};
    draw.geometry={id,1};
    draw.style.color=color;
    draw.style.blendMode=blend;
    draw.geometryPacket=makePolygonGeometryPacket(geometry);
    return draw;
}

StrokeDrawPacket strokeDraw(const std::string& id,const std::vector<pandoeditor::Point>& points,
                            std::uint32_t color,float width=1) {
    pandoeditor::Geometry geometry;
    geometry.type="LineString";
    geometry.lines={points};
    StrokeDrawPacket draw;
    draw.key=id;
    draw.object={"generic",id};
    draw.geometry={id,1};
    draw.style.color=color;
    draw.style.width=width;
    draw.geometryPacket=makeStrokeGeometryPacket(geometry);
    return draw;
}

PointDrawPacket pointDraw(const std::string& id,pandoeditor::Point point,std::uint32_t color) {
    pandoeditor::Geometry geometry;
    geometry.type="Point";
    geometry.points={point};
    PointDrawPacket draw;
    draw.key=id;
    draw.object={"generic",id};
    draw.geometry={id,1};
    draw.style.color=color;
    draw.geometryPacket=makePointGeometryPacket(geometry);
    return draw;
}

QImage paint(MapRenderItem& item,int width,int height,QColor background=Qt::white) {
    QImage image(width,height,QImage::Format_ARGB32_Premultiplied);
    image.fill(background);
    QPainter painter(&image);
    item.paint(&painter);
    painter.end();
    return image;
}

std::shared_ptr<RenderScene> scene() {
    auto result=std::make_shared<RenderScene>();
    result->revision=1;
    result->worldPlan.worldOffsets={0};
    return result;
}
}

class MapRenderTests:public QObject {
    Q_OBJECT
private slots:
    void worldMeshSlotsWithOneOwnerNeverAliasOrUploadForever() {
        auto mesh=std::make_shared<CountryBaseMesh>();
        mesh->positionsMicrodegrees={0,0,1000000,0,0,1000000,10000000,0,11000000,0,10000000,1000000};
        mesh->countryIndices={0,0,0,1,1,1};mesh->triangleIndices={0,1,2,3,4,5};
        mesh->lineIndices={0,1,3,4};mesh->countryTriangleRanges={0,3,3,3};mesh->countryBoundaryRanges={0,2,2,2};
        auto base=std::make_shared<WorldBaseFrame>();base->mesh=mesh;
        auto s=scene();s->worldBase=base;s->worldCountries.resize(2);
        s->worldCountries[0].id=s->worldCountries[1].id="owner";
        s->worldPlan.fills.visible={true,true};s->worldPlan.strokes.visible={true,true};
        s->drawSequence={{PrimitiveKind::WorldFill,0,{},-1},{PrimitiveKind::WorldFill,1,{},-1},
                         {PrimitiveKind::WorldStroke,0,{},-1},{PrimitiveKind::WorldStroke,1,{},-1}};
        MapSceneNode node;MapGpuStats stats;MapFlatViewport flat;
        node.sync(s,viewFor(40,40),flat,stats,1024*1024);
        QCOMPARE(stats.geometryUploadCount,std::uint64_t(4));
        for(int i=0;i<8;++i) {
            auto changed=std::make_shared<RenderScene>(*s);changed->revision=2+i;changed->revisions.selection=2+i;
            node.sync(changed,viewFor(40,40),flat,stats,1024*1024);
            QCOMPARE(node.childCount(),4);QVERIFY(!stats.uploadsPending);
            QCOMPARE(stats.geometryUploadCount,std::uint64_t(4));
            QCOMPARE(stats.nodeAttachmentCount,std::uint64_t(4));
        }
        auto globe=viewFor(40,40);globe.mode=ProjectionMode::Globe;
        node.sync(s,globe,flat,stats,1024*1024);
        QCOMPARE(node.childCount(),2); // adjacent equal fill and stroke groups
        auto* fill=static_cast<QSGGeometryNode*>(node.firstChild());
        QCOMPARE(fill->geometry()->vertexCount(),6);QCOMPARE(fill->geometry()->indexCount(),6);
        QCOMPARE(fill->geometry()->indexDataAsUInt()[3],std::uint32_t(3));
        const auto uploads=stats.geometryUploadCount,attachments=stats.nodeAttachmentCount;
        globe.centerLongitude=15;node.sync(s,globe,flat,stats,1024*1024);
        QCOMPARE(stats.geometryUploadCount,uploads);QCOMPARE(stats.nodeAttachmentCount,attachments);
        auto recolored=std::make_shared<RenderScene>(*s);++recolored->revision;
        recolored->worldCountries[1].fill.color=0xff0000;
        node.sync(recolored,globe,flat,stats,1024*1024);
        QCOMPARE(node.childCount(),3); // color mismatch must split the batch
        auto interleaved=std::make_shared<RenderScene>(*s);interleaved->revision=3;
        std::swap(interleaved->drawSequence[1],interleaved->drawSequence[2]);
        node.sync(interleaved,globe,flat,stats,1024*1024);
        QCOMPARE(node.childCount(),4); // never merge across intervening draws
    }
    void sceneGraphReconciliationRetainsUnchangedAttachments() {
        MapSceneNode node;MapGpuStats stats;MapFlatViewport flat;
        auto s=scene();
        s->polygons={polygonDraw("a",rectangle(2,2,12,12,40),0xff0000),
                     polygonDraw("b",rectangle(8,8,18,18,40),0x0000ff)};
        s->drawSequence={{PrimitiveKind::Polygon,0,{0,0,0},-1},{PrimitiveKind::Polygon,1,{1,0,0},-1}};
        node.sync(s,viewFor(40,40),flat,stats,1024*1024);
        auto* a=node.firstChild();auto* b=a->nextSibling();QVERIFY(b);
        QCOMPARE(stats.nodeAttachmentCount,std::uint64_t(2));
        auto changed=std::make_shared<RenderScene>(*s);++changed->revision;++changed->revisions.selection;
        node.sync(changed,viewFor(40,40),flat,stats,1024*1024);
        QCOMPARE(node.firstChild(),a);QCOMPARE(a->nextSibling(),b);
        QCOMPARE(stats.nodeAttachmentCount,std::uint64_t(2));QCOMPARE(stats.geometryUploadCount,std::uint64_t(2));
        auto reordered=std::make_shared<RenderScene>(*changed);++reordered->revision;
        std::reverse(reordered->drawSequence.begin(),reordered->drawSequence.end());
        node.sync(reordered,viewFor(40,40),flat,stats,1024*1024);
        QCOMPARE(node.firstChild(),b);QCOMPARE(b->nextSibling(),a);
        QCOMPARE(stats.nodeAttachmentCount,std::uint64_t(3));QCOMPARE(stats.geometryUploadCount,std::uint64_t(2));
        auto culled=std::make_shared<RenderScene>(*reordered);++culled->revision;culled->drawSequence.resize(1);
        node.sync(culled,viewFor(40,40),flat,stats,1024*1024);
        QCOMPARE(node.firstChild(),b);QVERIFY(!b->nextSibling());
        QCOMPARE(stats.nodeAttachmentCount,std::uint64_t(3));
    }
    void hiddenFallbackDoesNotRasterizeAndResumesWhenShown() {
        QQuickWindow window;window.resize(48,48);window.show();
        MapRenderItem item(window.contentItem());item.setWidth(48);item.setHeight(48);
        auto s=scene();
        s->polygons.push_back(polygonDraw("red",rectangle(5,5,15,15,48),0xff0000));
        s->drawSequence.push_back({PrimitiveKind::Polygon,0,{0,0,0},-1});
        item.setSceneSnapshot(s,viewFor(48,48));item.setVisible(false);
        QCOMPARE(paint(item,48,48).pixelColor(10,10),QColor(Qt::white));
        QCOMPARE(item.paintCount(),std::uint64_t(0));
        item.setVisible(true);
        QVERIFY(item.isVisible());
        QCOMPARE(item.sceneRevision(),qulonglong(1));
        QCOMPARE(paint(item,48,48).pixelColor(10,10),QColor("#ff0000"));
        QCOMPARE(item.paintCount(),std::uint64_t(1));
    }
    void typedSceneIsTheOnlyCpuRendererDataContract() {
        MapRenderItem item;
        const auto* meta=item.metaObject();
        QVERIFY(meta->indexOfProperty("sceneBridge")>=0);
        QVERIFY(meta->indexOfProperty("smoothLines")>=0);
        for(const char* legacy:{"paths","visuals","selectedPaths","primaryId",
                                "originX","originY","mapScale","hydroSource",
                                "hydroProjection","hydroStyle","hiddenHydroIds",
                                "selectedHydroId"})
            QVERIFY2(meta->indexOfProperty(legacy)<0,legacy);
    }

    void publishedViewIsTheOnlyCpuViewportState() {
        MapRenderItem item;item.setWidth(48);item.setHeight(48);
        auto s=scene();
        s->polygons.push_back(polygonDraw("red",rectangle(5,5,15,15,48),0xff0000));
        s->drawSequence.push_back({PrimitiveKind::Polygon,0,{0,0,0},-1});

        auto view=viewFor(48,48);
        item.setSceneSnapshot(s,view);
        auto image=paint(item,48,48);
        QCOMPARE(image.pixelColor(10,10),QColor("#ff0000"));

        view.translateX=20;view.revision=1;
        item.setSceneSnapshot(s,view);
        image=paint(item,48,48);
        QCOMPARE(image.pixelColor(30,10),QColor("#ff0000"));
        QCOMPARE(image.pixelColor(10,10),QColor(Qt::white));

        view.translateY=68;view.revision=2;
        item.setSceneSnapshot(s,view);
        image=paint(item,48,48);
        QCOMPARE(image.pixelColor(30,30),QColor("#ff0000"));
        QCOMPARE(item.sceneRevision(),qulonglong(1));
        QCOMPARE(item.viewRevision(),qulonglong(2));
    }

    void cpuConsumesEngineDrawSequenceForOverlaps() {
        MapRenderItem item;item.setWidth(40);item.setHeight(40);
        auto s=scene();
        s->polygons.push_back(polygonDraw("country",rectangle(5,5,35,35,40),0xff0000));
        s->polygons.push_back(polygonDraw("subunit",rectangle(10,10,30,30,40),0x00ff00));
        s->polygons.push_back(polygonDraw("region",rectangle(15,15,25,25,40),0x0000ff));
        s->drawSequence={
            {PrimitiveKind::Polygon,0,{0,0,0},-1},
            {PrimitiveKind::Polygon,1,{10,0,0},-1},
            {PrimitiveKind::Polygon,2,{20,0,0},-1}
        };
        item.setSceneSnapshot(s,viewFor(40,40));
        const auto image=paint(item,40,40);
        QCOMPARE(image.pixelColor(7,7),QColor("#ff0000"));
        QCOMPARE(image.pixelColor(12,12),QColor("#00ff00"));
        QCOMPARE(image.pixelColor(20,20),QColor("#0000ff"));
    }

    void typedHydroSharesTheSamePassOrdering() {
        MapRenderItem item;item.setWidth(40);item.setHeight(40);
        auto s=scene();
        auto country=polygonDraw("country",rectangle(5,5,35,35,40),0xff0000);
        country.object={"territorial","country"};
        auto lake=polygonDraw("lake",rectangle(10,10,30,30,40),0x0000ff);
        lake.object={"hydroBuiltin","lake"};
        auto river=strokeDraw("river",{screenPoint(10,25,40),screenPoint(30,25,40)},0xffff00,0);
        river.object={"hydroBuiltin","river"};
        river.geometryPacket.endpointWidths=std::make_shared<const std::vector<float>>(
            std::vector<float>{3,3});
        auto overlay=strokeDraw("overlay",{screenPoint(10,20,40),screenPoint(30,20,40)},0x00ff00,2);

        s->polygons={country,lake};
        s->strokes={river,overlay};
        s->drawSequence={
            {PrimitiveKind::Polygon,0,{0,0,0},-1},
            {PrimitiveKind::Polygon,1,{30,0,0},-1},
            {PrimitiveKind::Stroke,0,{32,0,0},-1},
            {PrimitiveKind::Stroke,1,{60,0,0},-1}
        };
        item.setSceneSnapshot(s,viewFor(40,40));
        const auto image=paint(item,40,40);
        QCOMPARE(image.pixelColor(15,15),QColor("#0000ff"));
        QCOMPARE(image.pixelColor(15,20),QColor("#00ff00"));
        QCOMPARE(image.pixelColor(20,25),QColor("#ffff00"));
        QCOMPARE(image.pixelColor(7,7),QColor("#ff0000"));
    }

    void variableWidthHydroAndLakeHoleUseTypedPackets() {
        MapRenderItem item;item.setWidth(64);item.setHeight(64);
        auto s=scene();

        auto lake=polygonDraw("lake",polygonWithHole(64),0x0000ff);
        lake.object={"hydroBuiltin","lake"};
        lake.style.alpha=.5f;

        auto river=strokeDraw("river",{screenPoint(10,50,64),screenPoint(45,50,64)},0xff0000,0);
        river.object={"hydroBuiltin","river"};
        river.style.alpha=.5f;
        river.geometryPacket.endpointWidths=std::make_shared<const std::vector<float>>(
            std::vector<float>{2,10});

        s->polygons={lake};s->strokes={river};
        s->drawSequence={
            {PrimitiveKind::Polygon,0,{30,0,0},-1},
            {PrimitiveKind::Stroke,0,{32,0,0},-1}
        };
        item.setSceneSnapshot(s,viewFor(64,64));
        const auto image=paint(item,64,64);
        QVERIFY(image.pixelColor(10,10).red()>120&&image.pixelColor(10,10).red()<140);
        QCOMPARE(image.pixelColor(20,20),QColor(Qt::white));
        const auto wide=image.pixelColor(43,53);
        QVERIFY(wide.red()>200&&wide.green()>100&&wide.green()<160);
        QCOMPARE(image.pixelColor(12,53),QColor(Qt::white));
    }

    void pointAndOpenLineRemainTypedPrimitives() {
        MapRenderItem item;item.setWidth(40);item.setHeight(40);
        auto s=scene();
        auto line=strokeDraw("line",{screenPoint(5,5,40),screenPoint(30,5,40),
                                     screenPoint(30,30,40)},0x0000ff,1);
        auto point=pointDraw("point",screenPoint(10,30,40),0xff0000);
        s->strokes={line};s->points={point};
        s->drawSequence={
            {PrimitiveKind::Stroke,0,{60,0,0},-1},
            {PrimitiveKind::Point,0,{70,0,0},-1}
        };
        item.setSceneSnapshot(s,viewFor(40,40));
        const auto image=paint(item,40,40);
        QCOMPARE(image.pixelColor(20,12),QColor(Qt::white));
        QVERIFY(image.pixelColor(20,5).blue()>200);
        QVERIFY(image.pixelColor(10,30).red()>200);
    }

    void multiplyAndInteractionOutlineComeFromRenderScene() {
        MapRenderItem item;item.setWidth(40);item.setHeight(30);
        auto s=scene();
        auto red=polygonDraw("red",rectangle(2,2,22,22,30),0xff0000);
        red.object={"territorial","red"};
        auto blue=polygonDraw("blue",rectangle(12,2,32,22,30),0x0000ff,BlendMode::Multiply);
        blue.object={"territorial","blue"};
        auto redBoundary=strokeDraw("red-boundary",
            {screenPoint(2,2,30),screenPoint(22,2,30),screenPoint(22,22,30),
             screenPoint(2,22,30),screenPoint(2,2,30)},0x61778a,1);
        redBoundary.object={"territorial","red"};

        s->polygons={red,blue};s->strokes={redBoundary};
        s->drawSequence={
            {PrimitiveKind::Polygon,0,{0,0,0},-1},
            {PrimitiveKind::Polygon,1,{10,0,0},-1}
        };
        s->interaction.selected={{"territorial","red"}};
        s->interaction.primary=pandoeditor::ObjectRef{"territorial","red"};

        item.setSceneSnapshot(s,viewFor(40,30));
        const auto image=paint(item,40,30);
        const auto overlap=image.pixelColor(16,10);
        QVERIFY(overlap.red()<20&&overlap.green()<20&&overlap.blue()<20);
        QVERIFY(image.pixelColor(2,10).blue()>40);
    }

    void candidateHoverAndPrimaryUseSameFinalInteractionPass() {
        MapRenderItem item;item.setWidth(40);item.setHeight(40);
        auto s=scene();
        auto poly=polygonDraw("object",rectangle(5,5,35,35,40),0xdddddd);
        poly.object={"generic","object"};
        auto boundary=strokeDraw("object-boundary",
            {screenPoint(5,5,40),screenPoint(35,5,40),screenPoint(35,35,40),
             screenPoint(5,35,40),screenPoint(5,5,40)},0x888888,1);
        boundary.object=poly.object;
        s->polygons={poly};s->strokes={boundary};
        s->drawSequence={{PrimitiveKind::Polygon,0,{20,0,0},-1}};
        s->interaction.candidates={poly.object};

        item.setSceneSnapshot(s,viewFor(40,40));
        auto candidate=paint(item,40,40);
        QVERIFY(candidate.pixelColor(5,20).blue()>candidate.pixelColor(5,20).red());

        s->interaction.candidates.clear();
        s->interaction.hover=poly.object;
        item.setSceneSnapshot(s,viewFor(40,40));
        auto hover=paint(item,40,40);
        QVERIFY(hover.pixelColor(5,20).blue()>hover.pixelColor(5,20).red());

        s->interaction.hover.reset();
        s->interaction.selected={poly.object};
        s->interaction.primary=poly.object;
        item.setSceneSnapshot(s,viewFor(40,40));
        auto primary=paint(item,40,40);
        QVERIFY(primary.pixelColor(5,20).blue()>primary.pixelColor(5,20).red());
    }
};

QTEST_MAIN(MapRenderTests)
#include "map_render_tests.moc"

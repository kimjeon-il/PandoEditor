#include "maprenderitem.h"
#include "scenegraph/mapscenenode.h"
#include "scenegraph/mapmaterial.h"
#include <QSGGeometryNode>
#include <pandoeditor/map/renderpacket.h>
#include <pandoeditor/map/mapscenebuilder.h>
#include <pandoeditor/map/framepipeline.h>
#include <QtTest>
#include <QImage>
#include <QPainter>
#include <QQuickWindow>
#include <QDebug>
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
void logResourceCounters(const char* action,const MapGpuStats& before,const MapGpuStats& after) {
    qInfo().nospace()<<"M94_RESOURCE_COUNTERS action="<<action
        <<" base-upload-delta="<<after.baseGeometryUploadCount-before.baseGeometryUploadCount
        <<" interaction-upload-delta="<<after.interactionGeometryUploadCount-before.interactionGeometryUploadCount
        <<" create-delta="<<after.resourceCreationCount-before.resourceCreationCount
        <<" retire-delta="<<after.resourceRetirementCount-before.resourceRetirementCount
        <<" live="<<after.liveResourceCount<<" live-bytes="<<after.liveResourceBytes;
}
std::shared_ptr<RenderScene> readyPreviewScene() {
    auto mesh=std::make_shared<CountryBaseMesh>();mesh->preview=true;
    // Two bounded rectangles, projected one geographic degree per pixel.
    mesh->positionsMicrodegrees={5000000,43000000,20000000,43000000,
        20000000,28000000,5000000,28000000,25000000,43000000,
        40000000,43000000,40000000,28000000,25000000,28000000};
    mesh->countryIndices={0,0,0,0,1,1,1,1};
    mesh->triangleIndices={0,1,2,0,2,3,4,5,6,4,6,7};
    mesh->lineIndices={0,1,1,2,2,3,3,0,4,5,5,6,6,7,7,4};
    mesh->countryTriangleRanges={0,6,6,6};mesh->countryBoundaryRanges={0,8,8,8};
    auto base=std::make_shared<WorldBaseFrame>();base->mesh=mesh;base->documentReady=true;
    base->ranges={{"source-visible","visible","world-country-visible"},
                  {"source-hidden","hidden","world-country-hidden"}};
    auto prepared=scene();prepared->worldBase=base;prepared->worldCountries.resize(2);
    prepared->worldCountries[0].id="visible";prepared->worldCountries[0].fill.color=0xff0000;
    prepared->worldCountries[0].boundary.color=0x00ff00;
    prepared->worldCountries[0].boundary.width=4;
    prepared->worldCountries[1].id="hidden";prepared->worldCountries[1].visible=false;
    prepared->worldCountries[1].fill.color=0x00ff00;
    prepared->worldPlan.fills.visible={true,true};prepared->worldPlan.strokes.visible={true,true};
    prepared->polygons={polygonDraw("overlay",rectangle(10,8,15,17,48),0x0000ff)};
    // Intentional interleaving: the later world fill must cover the overlay.
    prepared->drawSequence={{PrimitiveKind::Polygon,0,{},-1},
                            {PrimitiveKind::WorldFill,0,{},-1}};
    return prepared;
}
}

class MapRenderTests:public QObject {
    Q_OBJECT
private slots:
    void readyPreviewUsesOrderedDrawsAndKeepsHiddenBoundariesHidden() {
        const auto view=viewFor(48,48);auto prepared=readyPreviewScene();
        QVERIFY(prepared->worldBase->mesh->preview);QVERIFY(!prepared->worldBase->startupPreview());
        MapRenderItem item;item.setWidth(48);item.setHeight(48);item.setSmoothLines(false);
        item.setSceneSnapshot(prepared,view);const auto image=paint(item,48,48);
        QCOMPARE(image.pixelColor(12,12),QColor("#ff0000")); // ordered world fill covers overlay
        QCOMPARE(image.pixelColor(4,12),QColor(Qt::white)); // no unrequested boundary stroke
        QCOMPARE(image.pixelColor(30,12),QColor(Qt::white)); // hidden source slot
        MapSceneNode node;MapGpuStats stats;MapFlatViewport flat;
        node.sync(prepared,view,flat,stats,1024*1024);
        QCOMPARE(node.childCount(),2);QVERIFY(!stats.uploadsPending);
        auto* first=static_cast<QSGGeometryNode*>(node.firstChild());
        auto* second=static_cast<QSGGeometryNode*>(first->nextSibling());
        QCOMPARE(static_cast<MapMaterial*>(first->material())->color,QVector4D(0,0,1,1));
        QCOMPARE(static_cast<MapMaterial*>(second->material())->color,QVector4D(1,0,0,1));
        QCOMPARE(static_cast<MapMaterial*>(first->material())->primitive,MapPrimitive::Fill);
        QCOMPARE(static_cast<MapMaterial*>(second->material())->primitive,MapPrimitive::Fill);
        // An explicit boundary command exposes the same green stroke in both adapters.
        auto boundaries=std::make_shared<RenderScene>(*prepared);++boundaries->revision;
        boundaries->drawSequence.push_back({PrimitiveKind::WorldStroke,0,{},-1});
        item.setSceneSnapshot(boundaries,view);
        QCOMPARE(paint(item,48,48).pixelColor(4,12),QColor("#00ff00"));
        node.sync(boundaries,view,flat,stats,1024*1024);QCOMPARE(node.childCount(),3);
        auto* last=static_cast<QSGGeometryNode*>(node.lastChild());
        QCOMPARE(static_cast<MapMaterial*>(last->material())->primitive,MapPrimitive::Stroke);
        QCOMPARE(static_cast<MapMaterial*>(last->material())->color,QVector4D(0,1,0,1));
    }
    void readyPreviewHoverOutlineAndVisibilityAgreeAcrossAdapters() {
        const auto view=viewFor(48,48);auto prepared=readyPreviewScene();
        prepared->interaction.hover=pandoeditor::territorialRef("visible");
        MapRenderItem item;item.setWidth(48);item.setHeight(48);item.setSmoothLines(false);
        item.setSceneSnapshot(prepared,view);
        QCOMPARE(paint(item,48,48).pixelColor(5,12),QColor("#4083bc"));
        MapSceneNode node;MapGpuStats stats;MapFlatViewport flat;
        node.sync(prepared,view,flat,stats,1024*1024);QCOMPARE(node.childCount(),3);
        auto* highlight=static_cast<QSGGeometryNode*>(node.lastChild());
        auto* material=static_cast<MapMaterial*>(highlight->material());
        MapMaterial expected(MapPrimitive::Stroke,BlendMode::Normal);
        RenderStyle style;style.color=0x4083bc;style.alpha=1;style.width=2;expected.setStyle(style);
        QCOMPARE(material->primitive,MapPrimitive::Stroke);QCOMPARE(material->color,expected.color);
        QCOMPARE(material->effects.x(),2.f);QCOMPARE(stats.interactionGeometryUploadCount,std::uint64_t(1));
        auto hidden=std::make_shared<RenderScene>(*prepared);++hidden->revision;
        hidden->worldCountries[0].visible=false;
        item.setSceneSnapshot(hidden,view);const auto image=paint(item,48,48);
        QCOMPARE(image.pixelColor(5,12),QColor(Qt::white));
        QCOMPARE(image.pixelColor(8,12),QColor(Qt::white));
        QCOMPARE(image.pixelColor(12,12),QColor("#0000ff"));
        node.sync(hidden,view,flat,stats,1024*1024);QCOMPARE(node.childCount(),1);
        material=static_cast<MapMaterial*>(static_cast<QSGGeometryNode*>(node.firstChild())->material());
        QCOMPARE(material->primitive,MapPrimitive::Fill);QCOMPARE(material->color,QVector4D(0,0,1,1));
    }
    void canonicalWorldSamePartitionStyleChangesRetainResources() {
        auto mesh=std::make_shared<CountryBaseMesh>();
        mesh->positionsMicrodegrees={0,0,1000000,0,0,1000000,10000000,0,11000000,0,10000000,1000000};
        mesh->countryIndices={0,0,0,1,1,1};mesh->triangleIndices={0,1,2,3,4,5};
        mesh->lineIndices={0,1,3,4};mesh->countryTriangleRanges={0,3,3,3};mesh->countryBoundaryRanges={0,2,2,2};
        auto world=std::make_shared<WorldBaseFrame>();world->mesh=mesh;
        auto prepared=scene();prepared->worldBase=world;prepared->worldCountries.resize(2);
        prepared->worldCountries[0].id="a";prepared->worldCountries[1].id="b";
        prepared->worldPlan.fills.visible={true,true};prepared->worldPlan.strokes.visible={true,true};
        prepared->drawSequence={{PrimitiveKind::WorldFill,0,{},-1},{PrimitiveKind::WorldFill,1,{},-1},
                                {PrimitiveKind::WorldStroke,0,{},-1},{PrimitiveKind::WorldStroke,1,{},-1}};
        MapSceneNode node;MapGpuStats stats;MapFlatViewport flat;auto view=viewFor(40,40);view.mode=ProjectionMode::Globe;
        node.sync(prepared,view,flat,stats,1024*1024);QCOMPARE(node.childCount(),2);
        auto* fill=static_cast<QSGGeometryNode*>(node.firstChild());
        auto* stroke=static_cast<QSGGeometryNode*>(fill->nextSibling());
        auto* fillBuffer=fill->geometry();auto* strokeBuffer=stroke->geometry();
        for(int change=0;change<3;++change) {
            const auto before=stats;auto styled=std::make_shared<RenderScene>(*prepared);
            ++styled->revision;++styled->revisions.presentation;
            for(auto& country:styled->worldCountries) {
                if(change==0){country.fill.alpha=.25f;country.boundary.alpha=.5f;}
                if(change==1){country.fill.color=0x0000ff;country.boundary.color=0x00ff00;}
                if(change==2){country.fill.blendMode=BlendMode::Multiply;country.boundary.blendMode=BlendMode::Multiply;}
            }
            // All slots keep the same adjacent style partition throughout these changes.
            node.sync(styled,view,flat,stats,1024*1024);
            QCOMPARE(node.childCount(),2);QCOMPARE(node.firstChild(),static_cast<QSGNode*>(fill));
            QCOMPARE(fill->nextSibling(),static_cast<QSGNode*>(stroke));
            QCOMPARE(fill->geometry(),fillBuffer);QCOMPARE(stroke->geometry(),strokeBuffer);
            QCOMPARE(stats.baseGeometryUploadCount,before.baseGeometryUploadCount);
            QCOMPARE(stats.interactionGeometryUploadCount,before.interactionGeometryUploadCount);
            QCOMPARE(stats.resourceCreationCount,before.resourceCreationCount);QCOMPARE(stats.resourceRetirementCount,before.resourceRetirementCount);
            QCOMPARE(stats.liveResourceCount,std::size_t(2));QVERIFY(stats.materialUpdateCount>before.materialUpdateCount);
            MapMaterial expectedFill(MapPrimitive::Fill,styled->worldCountries[0].fill.blendMode);
            expectedFill.setStyle(styled->worldCountries[0].fill);
            MapMaterial expectedStroke(MapPrimitive::Stroke,styled->worldCountries[0].boundary.blendMode);
            expectedStroke.setStyle(styled->worldCountries[0].boundary);
            const auto* fillMaterial=static_cast<MapMaterial*>(fill->material());
            const auto* strokeMaterial=static_cast<MapMaterial*>(stroke->material());
            QCOMPARE(fillMaterial->color,expectedFill.color);QCOMPARE(fillMaterial->blend,expectedFill.blend);
            QCOMPARE(strokeMaterial->color,expectedStroke.color);QCOMPARE(strokeMaterial->blend,expectedStroke.blend);
            logResourceCounters(change==0?"world-group-opacity":change==1?"world-group-color":"world-group-blend",before,stats);
            prepared=styled;
        }
        // Splitting or merging slot partitions is covered by the existing world batch test.
    }
    void strokeEndpointWidthReplacementUploadsChangedVertexWidths() {
        auto original=scene();
        original->strokes={strokeDraw("river",{{0,0},{10,10}},0x123456,0)};
        original->strokes[0].geometryPacket.endpointWidths=std::make_shared<const std::vector<float>>(std::vector<float>{2,4});
        original->drawSequence={{PrimitiveKind::Stroke,0,{},-1}};
        MapSceneNode node;MapGpuStats stats;MapFlatViewport flat;const auto view=viewFor(40,40);
        node.sync(original,view,flat,stats,1024*1024);
        const auto beforeWidthChange=stats;
        const auto checkWidths=[&](float start,float end) {
            QCOMPARE(node.childCount(),1);
            const auto* geometry=static_cast<QSGGeometryNode*>(node.firstChild())->geometry();
            QCOMPARE(geometry->vertexCount(),12);QCOMPARE(geometry->sizeOfVertex(),int(sizeof(float)*14));
            const auto* vertex=static_cast<const float*>(geometry->vertexData());
            for(int i=0;i<12;++i)QCOMPARE(vertex[i*14+6],i<2||(i>=4&&i<8)?start:end);
        };
        checkWidths(2,4);
        auto replacement=std::make_shared<RenderScene>(*original);++replacement->revision;++replacement->revisions.geometry;
        replacement->strokes[0].geometryPacket.endpointWidths=std::make_shared<const std::vector<float>>(std::vector<float>{6,8});
        QCOMPARE(replacement->strokes[0].geometryPacket.startsEnds,original->strokes[0].geometryPacket.startsEnds);
        node.sync(replacement,view,flat,stats,1024*1024);
        QCOMPARE(stats.geometryUploadCount,std::uint64_t(2));checkWidths(6,8);
        QCOMPARE(stats.baseGeometryUploadCount,std::uint64_t(2));QCOMPARE(stats.interactionGeometryUploadCount,std::uint64_t(0));
        QCOMPARE(stats.resourceCreationCount,std::uint64_t(2));QCOMPARE(stats.resourceRetirementCount,std::uint64_t(1));
        QCOMPARE(stats.liveResourceCount,std::size_t(1));
        logResourceCounters("endpoint-width-replacement",beforeWidthChange,stats);
    }
    void geometryRevisionReplacementInvalidatesOnlyChangedResource() {
        auto original=scene();original->polygons={polygonDraw("a",rectangle(2,2,12,12,40),0xff0000),
                                                  polygonDraw("b",rectangle(20,20,30,30,40),0x0000ff)};
        original->drawSequence={{PrimitiveKind::Polygon,0,{},-1},{PrimitiveKind::Polygon,1,{},-1}};
        MapSceneNode node;MapGpuStats stats;MapFlatViewport flat;const auto view=viewFor(40,40);
        node.sync(original,view,flat,stats,1024*1024);
        auto* unchanged=node.firstChild()->nextSibling();
        auto* unchangedBuffer=static_cast<QSGGeometryNode*>(unchanged)->geometry();
        auto revised=std::make_shared<RenderScene>(*original);++revised->revision;++revised->revisions.geometry;
        ++revised->polygons[0].geometryRevision;
        QCOMPARE(revised->polygons[0].geometryPacket.positions,original->polygons[0].geometryPacket.positions);
        node.sync(revised,view,flat,stats,1024*1024);
        QCOMPARE(stats.geometryUploadCount,std::uint64_t(3));
        QCOMPARE(node.firstChild()->nextSibling(),unchanged);
        QCOMPARE(static_cast<QSGGeometryNode*>(unchanged)->geometry(),unchangedBuffer);
        QCOMPARE(stats.resourceCreationCount,std::uint64_t(3));QCOMPARE(stats.resourceRetirementCount,std::uint64_t(1));
        QCOMPARE(stats.liveResourceCount,std::size_t(2));
    }
    void pendingUploadsCompleteAcrossCameraChangesWithoutReplacingWarmBuffers() {
        auto s=scene();s->preparationIdentity=std::make_shared<RenderScene::PreparationIdentity>();
        for(int i=0;i<3;++i) {
            s->polygons.push_back(polygonDraw(std::to_string(i),rectangle(2+i*10,2,8+i*10,8,40),0xff0000));
            s->drawSequence.push_back({PrimitiveKind::Polygon,std::size_t(i),{},-1});
        }
        MapSceneNode node;MapGpuStats stats;MapFlatViewport flat;auto view=viewFor(40,40);
        node.sync(s,view,flat,stats,1);QVERIFY(stats.uploadsPending);QCOMPARE(node.childCount(),1);
        auto* warm=node.firstChild();auto* buffer=static_cast<QSGGeometryNode*>(warm)->geometry();
        for(int attempt=0;attempt<5&&stats.uploadsPending;++attempt) {
            view.translateX+=3;view.scale*=1.01;++view.revision;flat.originX+=3;flat.mapScale*=1.01f;
            node.sync(s,view,flat,stats,1);
            QCOMPARE(node.firstChild(),warm);QCOMPARE(static_cast<QSGGeometryNode*>(warm)->geometry(),buffer);
        }
        QVERIFY(!stats.uploadsPending);QCOMPARE(node.childCount(),3);QCOMPARE(stats.geometryUploadCount,std::uint64_t(3));
        QCOMPARE(stats.baseGeometryUploadCount,std::uint64_t(3));QCOMPARE(stats.interactionGeometryUploadCount,std::uint64_t(0));
        QCOMPARE(stats.resourceCreationCount,std::uint64_t(3));QCOMPARE(stats.resourceRetirementCount,std::uint64_t(0));
        for(auto* child=node.firstChild();child;child=child->nextSibling()) {
            const auto* material=static_cast<MapMaterial*>(static_cast<QSGGeometryNode*>(child)->material());
            QCOMPARE(material->globe1.x(),float(view.translateX));QCOMPARE(material->globe0.w(),float(view.scale));
        }
        const auto complete=stats;view.translateY+=7;++view.revision;
        node.sync(s,view,flat,stats,1);
        QCOMPARE(stats.geometryUploadCount,complete.geometryUploadCount);QCOMPARE(stats.treeRebuildCount,complete.treeRebuildCount);
    }
    void deletingSceneReleasesPacketStorageAndNodes() {
        auto s=scene();s->polygons={polygonDraw("a",rectangle(2,2,12,12,40),0xff0000)};
        s->drawSequence={{PrimitiveKind::Polygon,0,{},-1}};
        std::weak_ptr<const std::vector<float>> positions=s->polygons[0].geometryPacket.positions;
        std::weak_ptr<const RenderScene> snapshot=s;
        MapSceneNode node;MapGpuStats stats;MapFlatViewport flat;const auto view=viewFor(40,40);
        node.sync(s,view,flat,stats,1024*1024);QCOMPARE(node.childCount(),1);
        s.reset();QVERIFY(!snapshot.expired());QVERIFY(!positions.expired());
        const auto beforeDelete=stats;
        node.sync({},view,flat,stats,1024*1024);
        QCOMPARE(node.childCount(),0);QCOMPARE(stats.geometryBytes,std::size_t(0));
        QVERIFY(snapshot.expired());QVERIFY(positions.expired());QVERIFY(!stats.uploadsPending);
        QCOMPARE(stats.resourceCreationCount,std::uint64_t(1));QCOMPARE(stats.resourceRetirementCount,std::uint64_t(1));
        QCOMPARE(stats.liveResourceCount,std::size_t(0));QCOMPARE(stats.liveResourceBytes,std::size_t(0));
        logResourceCounters("delete-scene",beforeDelete,stats);
        qInfo().nospace()<<"M94_RESOURCE_COUNTERS weak-scene-released="<<snapshot.expired()
                         <<" weak-positions-released="<<positions.expired();
    }
    void recreatedSceneGraphUploadsRetainedCpuSceneAndRecoversUniforms() {
        auto s=scene();s->polygons={polygonDraw("a",rectangle(2,2,12,12,40),0xff0000)};
        s->drawSequence={{PrimitiveKind::Polygon,0,{},-1}};
        std::weak_ptr<const std::vector<float>> positions=s->polygons[0].geometryPacket.positions;
        MapFlatViewport flat;auto view=viewFor(40,40);MapGpuStats firstStats;
        {MapSceneNode first;first.sync(s,view,flat,firstStats,1024*1024);QCOMPARE(firstStats.geometryUploadCount,std::uint64_t(1));}
        QVERIFY(!positions.expired());view.translateX=19;view.viewportWidth=90;++view.revision;
        MapSceneNode recovered;MapGpuStats recoveredStats;recovered.sync(s,view,flat,recoveredStats,1024*1024);
        QCOMPARE(recovered.childCount(),1);QCOMPARE(recoveredStats.geometryUploadCount,std::uint64_t(1));
        const auto* material=static_cast<MapMaterial*>(static_cast<QSGGeometryNode*>(recovered.firstChild())->material());
        QCOMPARE(material->globe1.x(),19.f);QCOMPARE(material->globe1.z(),90.f);
        const auto before=recoveredStats;view.translateX=23;++view.revision;
        recovered.sync(s,view,flat,recoveredStats,1024*1024);
        QCOMPARE(recoveredStats.geometryUploadCount,before.geometryUploadCount);QCOMPARE(recoveredStats.treeRebuildCount,before.treeRebuildCount);
        QCOMPARE(recoveredStats.resourceCreationCount,std::uint64_t(1));QCOMPARE(recoveredStats.liveResourceCount,std::size_t(1));
    }
    void selectionOverlayUploadsAreSeparateFromStableBaseResources() {
        auto s=scene();s->preparationIdentity=std::make_shared<RenderScene::PreparationIdentity>();
        s->points={pointDraw("p",{5,5},0x123456)};s->drawSequence={{PrimitiveKind::Point,0,{},-1}};
        MapSceneNode node;MapGpuStats stats;MapFlatViewport flat;auto view=viewFor(40,40);
        node.sync(s,view,flat,stats,1024*1024);
        auto* base=node.firstChild();auto* baseBuffer=static_cast<QSGGeometryNode*>(base)->geometry();
        QCOMPARE(stats.baseGeometryUploadCount,std::uint64_t(1));QCOMPARE(stats.interactionGeometryUploadCount,std::uint64_t(0));
        const auto beforeSelection=stats;
        auto selected=std::make_shared<RenderScene>(*s);++selected->revision;++selected->revisions.selection;
        selected->interaction.selected={selected->points[0].object};selected->interaction.primary=selected->points[0].object;
        node.sync(selected,view,flat,stats,1024*1024);
        QCOMPARE(node.childCount(),2);QCOMPARE(node.firstChild(),base);
        QCOMPARE(static_cast<QSGGeometryNode*>(base)->geometry(),baseBuffer);
        QCOMPARE(stats.baseGeometryUploadCount,std::uint64_t(1));QCOMPARE(stats.interactionGeometryUploadCount,std::uint64_t(1));
        QCOMPARE(stats.resourceCreationCount,std::uint64_t(2));QCOMPARE(stats.liveResourceCount,std::size_t(2));
        logResourceCounters("select-overlay",beforeSelection,stats);
        for(int frame=0;frame<4;++frame) {
            const auto before=stats;
            auto styled=std::make_shared<RenderScene>(*selected);++styled->revision;++styled->revisions.presentation;
            styled->points[0].style.color=frame%2?0xff0000:0x00ff00;
            view.translateX+=4;++view.revision;
            node.sync(styled,view,flat,stats,1024*1024);
            QCOMPARE(stats.baseGeometryUploadCount,before.baseGeometryUploadCount);
            QCOMPARE(stats.interactionGeometryUploadCount,before.interactionGeometryUploadCount);
            QCOMPARE(stats.resourceCreationCount,before.resourceCreationCount);QCOMPARE(stats.resourceRetirementCount,before.resourceRetirementCount);
            QCOMPARE(node.firstChild(),base);QCOMPARE(static_cast<QSGGeometryNode*>(base)->geometry(),baseBuffer);
            const auto* material=static_cast<MapMaterial*>(static_cast<QSGGeometryNode*>(base)->material());
            QCOMPARE(material->color,frame%2?QVector4D(1,0,0,1):QVector4D(0,1,0,1));
            selected=styled;
        }
        auto cleared=std::make_shared<RenderScene>(*selected);++cleared->revision;++cleared->revisions.selection;cleared->interaction={};
        node.sync(cleared,view,flat,stats,1024*1024);
        QCOMPARE(node.childCount(),1);QCOMPARE(node.firstChild(),base);QCOMPARE(stats.liveResourceCount,std::size_t(1));
        QCOMPARE(stats.resourceRetirementCount,std::uint64_t(1));
        QCOMPARE(stats.baseGeometryUploadCount,std::uint64_t(1));QCOMPARE(stats.interactionGeometryUploadCount,std::uint64_t(1));
    }
    void typedPreparationMetadataInvalidatesOnlyItsResource_data() {
        QTest::addColumn<int>("changedField");
        QTest::newRow("geometry-version")<<0;QTest::newRow("lod")<<1;QTest::newRow("projection-policy")<<2;
    }
    void typedPreparationMetadataInvalidatesOnlyItsResource() {
        QFETCH(int,changedField);
        auto original=scene();original->polygons={polygonDraw("fill",rectangle(2,2,12,12,40),0xff0000)};
        original->points={pointDraw("marker",{5,5},0x0000ff)};
        original->drawSequence={{PrimitiveKind::Polygon,0,{},-1},{PrimitiveKind::Point,0,{},-1}};
        MapSceneNode node;MapGpuStats stats;MapFlatViewport flat;const auto view=viewFor(40,40);
        node.sync(original,view,flat,stats,1024*1024);
        auto* marker=node.firstChild()->nextSibling();auto* markerBuffer=static_cast<QSGGeometryNode*>(marker)->geometry();
        auto modified=std::make_shared<RenderScene>(*original);++modified->revision;++modified->revisions.geometry;
        if(changedField==0)++modified->polygons[0].geometry.version;
        if(changedField==1)--modified->polygons[0].lod;
        if(changedField==2)modified->polygons[0].preparationPolicy=ProjectionPreparationPolicy::GlobeReady;
        QCOMPARE(modified->polygons[0].geometryPacket.positions,original->polygons[0].geometryPacket.positions);
        node.sync(modified,view,flat,stats,1024*1024);
        QCOMPARE(stats.baseGeometryUploadCount,std::uint64_t(3));QCOMPARE(stats.interactionGeometryUploadCount,std::uint64_t(0));
        QCOMPARE(stats.resourceCreationCount,std::uint64_t(3));QCOMPARE(stats.resourceRetirementCount,std::uint64_t(1));
        QCOMPARE(stats.liveResourceCount,std::size_t(2));
        QCOMPARE(node.firstChild()->nextSibling(),marker);QCOMPARE(static_cast<QSGGeometryNode*>(marker)->geometry(),markerBuffer);
    }
    void equalProjectRevisionsCannotAliasReplacementBuffersAndOldStorageRetires() {
        auto original=scene();original->preparationIdentity=std::make_shared<RenderScene::PreparationIdentity>();
        original->polygons={polygonDraw("same-id",rectangle(2,2,12,12,40),0xff0000)};
        original->drawSequence={{PrimitiveKind::Polygon,0,{},-1}};
        std::weak_ptr<const std::vector<float>> obsolete=original->polygons[0].geometryPacket.positions;
        MapSceneNode node;MapGpuStats stats;MapFlatViewport flat;const auto view=viewFor(40,40);
        node.sync(original,view,flat,stats,1024*1024);
        auto replacement=scene();replacement->preparationIdentity=std::make_shared<RenderScene::PreparationIdentity>();
        replacement->polygons={polygonDraw("same-id",rectangle(20,20,30,30,40),0x0000ff)};
        replacement->drawSequence=original->drawSequence;
        QCOMPARE(replacement->revision,original->revision);QCOMPARE(replacement->polygons[0].geometry,original->polygons[0].geometry);
        QCOMPARE(replacement->polygons[0].geometryRevision,original->polygons[0].geometryRevision);
        node.sync(replacement,view,flat,stats,1024*1024);original.reset();
        QVERIFY(obsolete.expired());QCOMPARE(node.childCount(),1);
        const auto* geometry=static_cast<QSGGeometryNode*>(node.firstChild())->geometry();
        QCOMPARE(static_cast<const float*>(geometry->vertexData())[0],20.f);
        QCOMPARE(stats.baseGeometryUploadCount,std::uint64_t(2));QCOMPARE(stats.resourceCreationCount,std::uint64_t(2));
        QCOMPARE(stats.resourceRetirementCount,std::uint64_t(1));QCOMPARE(stats.liveResourceCount,std::size_t(1));
    }
    void preparedBuilderCameraFramesUseUniformsOnly_data() {
        QTest::addColumn<bool>("globe");
        QTest::newRow("flat")<<false;QTest::newRow("globe")<<true;
    }
    void preparedBuilderCameraFramesUseUniformsOnly() {
        QFETCH(bool,globe);
        pandoeditor::ProjectDocument document;document.documentId="m92-camera-frame-fixture";
        const auto add=[&](const std::string& id,const pandoeditor::Geometry& geometry) {
            document.geometries.insert({id,1},geometry);
            pandoeditor::GenericFeature feature;feature.id=id;feature.name=id;
            feature.geometry={id,1};
            document.genericFeatures.push_back(feature);
        };
        add("fill",rectangle(2,2,12,12,40));
        pandoeditor::Geometry line;line.type="LineString";line.lines={{{0,0},{10,10}}};add("line",line);
        pandoeditor::Geometry point;point.type="Point";point.points={{5,5}};add("point",point);
        pandoeditor::Project project;project.replace(document);
        GeometryPacketCache cache;MapSceneBuilder builder(cache);
        auto view=viewFor(40,40);if(globe)view.mode=ProjectionMode::Globe;
        const auto prepared=builder.build(project.snapshot(),view,{},{});
        auto frame=FramePipeline::compose(prepared,view);
        MapSceneNode node;MapGpuStats stats;MapFlatViewport flat;
        node.sync(frame->scene,frame->view,flat,stats,1024*1024,frame->worldPlan.get());
        QVERIFY(node.childCount()>0);QVERIFY(!stats.uploadsPending);
        const auto baseline=stats;
        const auto packetStats=cache.stats();
        const auto preparations=builder.preparationCount(),transients=builder.transientUpdateCount();
        std::vector<QSGGeometryNode*> nodes;std::vector<QSGGeometry*> buffers;
        for(auto* child=node.firstChild();child;child=child->nextSibling()) {
            auto* entry=static_cast<QSGGeometryNode*>(child);nodes.push_back(entry);buffers.push_back(entry->geometry());
        }
        for(int step=0;step<4;++step) {
            if(step==0){view.translateX+=12;view.translateY+=8;flat.originX+=12;flat.originY+=8;}
            if(step==1){view.scale*=1.25;flat.mapScale*=1.25f;}
            if(step==2){view.rotationLongitude+=15;view.rotationLatitude+=5;view.rotationRoll+=3;}
            if(step==3){view.viewportWidth+=80;view.viewportHeight+=60;}
            ++view.revision;
            const auto next=builder.build(project.snapshot(),view,{},prepared);
            QCOMPARE(next.get(),prepared.get());
            QCOMPARE(builder.preparationCount(),preparations);QCOMPARE(builder.transientUpdateCount(),transients);
            QCOMPARE(cache.stats().builds,packetStats.builds);QCOMPARE(cache.stats().hits,packetStats.hits);
            frame=FramePipeline::compose(next,view,frame);
            QCOMPARE(frame->scene.get(),prepared.get());QCOMPARE(frame->path,FrameUpdatePath::ViewOnly);
            const auto uniforms=stats.viewUniformUpdateCount;
            node.sync(frame->scene,frame->view,flat,stats,1024*1024,frame->worldPlan.get());
            QCOMPARE(stats.geometryUploadCount,baseline.geometryUploadCount);QCOMPARE(stats.uploadedBytes,baseline.uploadedBytes);
            QCOMPARE(stats.treeRebuildCount,baseline.treeRebuildCount);QCOMPARE(stats.nodeAttachmentCount,baseline.nodeAttachmentCount);
            QCOMPARE(stats.materialUpdateCount,baseline.materialUpdateCount);QCOMPARE(stats.uploadBytesThisFrame,std::size_t(0));
            QCOMPARE(stats.viewUniformUpdateCount,uniforms+std::uint64_t(nodes.size()));QVERIFY(!stats.uploadsPending);
            auto* child=node.firstChild();
            for(std::size_t i=0;i<nodes.size();++i) {
                QCOMPARE(child,static_cast<QSGNode*>(nodes[i]));QCOMPARE(nodes[i]->geometry(),buffers[i]);
                child=child->nextSibling();
            }
            QVERIFY(!child);
        }
        qInfo()<<"M92_FRAME_COUNTERS"<<(globe?"globe":"flat")
               <<"preparation delta"<<(builder.preparationCount()-preparations)
               <<"scene-copy delta"<<(builder.transientUpdateCount()-transients)
               <<"upload delta"<<(stats.geometryUploadCount-baseline.geometryUploadCount)
               <<"uploaded-byte delta"<<(stats.uploadedBytes-baseline.uploadedBytes)
               <<"tree delta"<<(stats.treeRebuildCount-baseline.treeRebuildCount)
               <<"attachment delta"<<(stats.nodeAttachmentCount-baseline.nodeAttachmentCount)
               <<"uniform delta"<<(stats.viewUniformUpdateCount-baseline.viewUniformUpdateCount);
    }
    void currentFramePlanControlsBoundedMeshCullingAndWrap() {
        auto mesh=std::make_shared<CountryBaseMesh>();
        mesh->positionsMicrodegrees={0,0,1000000,0,0,1000000,
                                     120000000,0,121000000,0,120000000,1000000};
        mesh->countryIndices={0,0,0,1,1,1};mesh->triangleIndices={0,1,2,3,4,5};
        mesh->countryTriangleRanges={0,3,3,3};mesh->countryBoundaryRanges={0,0,0,0};
        mesh->countryBounds={0,0,1000000,1000000,120000000,0,121000000,1000000};
        mesh->countryBoundsFlags={0,0};
        auto base=std::make_shared<WorldBaseFrame>();base->mesh=mesh;
        auto prepared=scene();prepared->worldBase=base;
        prepared->preparationIdentity=std::make_shared<RenderScene::PreparationIdentity>();
        prepared->worldCountries.resize(2);prepared->worldCountries[0].id="west";
        prepared->worldCountries[1].id="east";
        prepared->drawSequence={{PrimitiveKind::WorldFill,0,{},-1},{PrimitiveKind::WorldFill,1,{},-1}};
        auto view=viewFor(40,40);view.translateX=20;view.translateY=20;
        auto frame=FramePipeline::compose(prepared,view);
        QCOMPARE(frame->worldPlan->fills.visible,std::vector<bool>({true,false}));
        prepared->worldPlan=*frame->worldPlan; // deliberately retain the initial plan in preparation
        MapSceneNode node;MapGpuStats stats;MapFlatViewport flat;
        node.sync(frame->scene,frame->view,flat,stats,1024*1024,frame->worldPlan.get());
        QCOMPARE(node.childCount(),1);QCOMPARE(stats.visibleCountryCount,std::size_t(1));
        QCOMPARE(stats.drawIndexCount,std::size_t(3));
        const auto warm=stats;
        view.centerLongitude=10;++view.revision;frame=FramePipeline::compose(prepared,view,frame);
        node.sync(frame->scene,frame->view,flat,stats,1024*1024,frame->worldPlan.get());
        QCOMPARE(stats.geometryUploadCount,warm.geometryUploadCount);
        QCOMPARE(stats.treeRebuildCount,warm.treeRebuildCount);QCOMPARE(stats.nodeAttachmentCount,warm.nodeAttachmentCount);
        QVERIFY(stats.viewUniformUpdateCount>warm.viewUniformUpdateCount);
        view.centerLongitude=120;++view.revision;frame=FramePipeline::compose(prepared,view,frame);
        QCOMPARE(frame->scene.get(),prepared.get());QCOMPARE(frame->path,FrameUpdatePath::ViewOnly);
        QCOMPARE(frame->worldPlan->fills.visible,std::vector<bool>({false,true}));
        QCOMPARE(prepared->worldPlan.fills.visible,std::vector<bool>({true,false}));
        node.sync(frame->scene,frame->view,flat,stats,1024*1024,frame->worldPlan.get());
        QCOMPARE(node.childCount(),1);QCOMPARE(stats.visibleCountryCount,std::size_t(1));
        QCOMPARE(stats.drawIndexCount,std::size_t(3));
        auto* data=static_cast<QSGGeometryNode*>(node.firstChild())->geometry();
        QCOMPARE(static_cast<const float*>(data->vertexData())[0],120.f);
        const auto beforeWrap=stats.treeRebuildCount;
        view.centerLongitude=180;++view.revision;frame=FramePipeline::compose(prepared,view,frame);
        QCOMPARE(frame->worldPlan->worldOffsets,std::vector<double>({0,360}));
        QCOMPARE(prepared->worldPlan.worldOffsets,std::vector<double>({0}));
        node.sync(frame->scene,frame->view,flat,stats,1024*1024,frame->worldPlan.get());
        // A new visible world copy is a topology change, so reconciliation is expected.
        QVERIFY(stats.treeRebuildCount>beforeWrap);QCOMPARE(node.childCount(),2);
        QCOMPARE(stats.drawIndexCount,std::size_t(6));QVERIFY(!stats.uploadsPending);
    }
    void viewOnlyFramesRetainBuffersAndOnlyUpdateUniforms_data() {
        QTest::addColumn<bool>("globe");
        QTest::newRow("flat-pan-zoom-resize")<<false;
        QTest::newRow("globe-pan-zoom-rotation-resize")<<true;
    }
    void viewOnlyFramesRetainBuffersAndOnlyUpdateUniforms() {
        QFETCH(bool,globe);
        auto s=scene();s->preparationIdentity=std::make_shared<RenderScene::PreparationIdentity>();
        s->polygons={polygonDraw("fill",rectangle(2,2,12,12,40),0xff0000)};
        s->strokes={strokeDraw("line",{{0,0},{10,10}},0x00ff00,2)};
        s->points={pointDraw("point",{5,5},0x0000ff)};
        s->drawSequence={{PrimitiveKind::Polygon,0,{},-1},
                         {PrimitiveKind::Stroke,0,{},-1},{PrimitiveKind::Point,0,{},-1}};
        MapSceneNode node;MapGpuStats stats;MapFlatViewport flat;
        auto view=viewFor(40,40);if(globe)view.mode=ProjectionMode::Globe;
        node.sync(s,view,flat,stats,1024*1024);
        QCOMPARE(node.childCount(),3);QVERIFY(!stats.uploadsPending);
        const auto baseline=stats;
        std::vector<QSGGeometryNode*> nodes;
        std::vector<QSGGeometry*> buffers;
        for(auto* child=node.firstChild();child;child=child->nextSibling()) {
            auto* geometryNode=static_cast<QSGGeometryNode*>(child);
            nodes.push_back(geometryNode);buffers.push_back(geometryNode->geometry());
        }
        for(int frame=0;frame<4;++frame) {
            if(frame==0){view.translateX+=12;view.translateY+=8;flat.originX+=12;flat.originY+=8;}
            if(frame==1){view.scale*=1.25;flat.mapScale*=1.25f;}
            if(frame==2){view.rotationLongitude+=15;view.rotationLatitude+=5;view.rotationRoll+=3;}
            if(frame==3){view.viewportWidth+=80;view.viewportHeight+=60;}
            ++view.revision;
            // Publication may create a new wrapper while retaining the preparation.
            auto published=std::make_shared<RenderScene>(*s);
            published->revision+=frame+1;published->revisions.view=view.revision;
            const auto uniforms=stats.viewUniformUpdateCount;
            node.sync(published,view,flat,stats,1024*1024);
            QCOMPARE(stats.geometryUploadCount,baseline.geometryUploadCount);
            QCOMPARE(stats.uploadedBytes,baseline.uploadedBytes);
            QCOMPARE(stats.uploadBytesThisFrame,std::size_t(0));
            QCOMPARE(stats.treeRebuildCount,baseline.treeRebuildCount);
            QCOMPARE(stats.nodeAttachmentCount,baseline.nodeAttachmentCount);
            QCOMPARE(stats.materialUpdateCount,baseline.materialUpdateCount);
            QCOMPARE(stats.viewUniformUpdateCount,uniforms+std::uint64_t(3));
            QCOMPARE(stats.sceneRevision,published->revision);QVERIFY(!stats.uploadsPending);
            auto* child=node.firstChild();
            for(std::size_t i=0;i<nodes.size();++i) {
                QCOMPARE(child,static_cast<QSGNode*>(nodes[i]));
                QCOMPARE(nodes[i]->geometry(),buffers[i]);
                const auto* material=static_cast<MapMaterial*>(nodes[i]->material());
                QCOMPARE(material->globe1,QVector4D(float(view.translateX),float(view.translateY),
                                                   float(view.viewportWidth),float(view.viewportHeight)));
                child=child->nextSibling();
            }
            QVERIFY(!child);
        }
    }
    void preparationIdentityDoesNotHideStyleOrProjectReplacement() {
        auto s=scene();s->preparationIdentity=std::make_shared<RenderScene::PreparationIdentity>();
        s->polygons={polygonDraw("fill",rectangle(2,2,12,12,40),0xff0000)};
        s->drawSequence={{PrimitiveKind::Polygon,0,{},-1}};
        MapSceneNode node;MapGpuStats stats;MapFlatViewport flat;const auto view=viewFor(40,40);
        node.sync(s,view,flat,stats,1024*1024);
        const auto uploads=stats.geometryUploadCount,rebuilds=stats.treeRebuildCount;
        auto styled=std::make_shared<RenderScene>(*s);
        ++styled->revision;++styled->revisions.presentation;styled->polygons[0].style.color=0x00ff00;
        node.sync(styled,view,flat,stats,1024*1024);
        QCOMPARE(stats.treeRebuildCount,rebuilds+1);QCOMPARE(stats.geometryUploadCount,uploads);
        auto* material=static_cast<MapMaterial*>(static_cast<QSGGeometryNode*>(node.firstChild())->material());
        QCOMPARE(material->color,QVector4D(0,1,0,1));
        // A different project can reuse every numeric revision and packet key.
        auto replacement=std::make_shared<RenderScene>(*styled);
        replacement->preparationIdentity=std::make_shared<RenderScene::PreparationIdentity>();
        replacement->polygons[0]=polygonDraw("fill",rectangle(20,20,30,30,40),0x0000ff);
        node.sync(replacement,view,flat,stats,1024*1024);
        QCOMPARE(stats.treeRebuildCount,rebuilds+2);QCOMPARE(stats.geometryUploadCount,uploads+1);
        material=static_cast<MapMaterial*>(static_cast<QSGGeometryNode*>(node.firstChild())->material());
        QCOMPARE(material->color,QVector4D(0,0,1,1));
    }
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

        auto base=std::make_shared<RenderScene>(*s);base->interaction={};
        item.setSceneSnapshot(base,viewFor(40,30));
        QCOMPARE(paint(item,40,30).pixelColor(16,10),QColor(Qt::black));
        item.setSceneSnapshot(s,viewFor(40,30));
        const auto image=paint(item,40,30);
        const auto overlap=image.pixelColor(16,10);
        // Fixed-Web primary selection is #cda95d at .30*.35 alpha over
        // the black multiply result. QPainter quantizes opacity to 8 bits.
        // Independently evaluated production style: stroke-web-primary-33.json.
        QVERIFY(std::abs(overlap.red()-22)<=1&&std::abs(overlap.green()-18)<=1&&std::abs(overlap.blue()-10)<=1);
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

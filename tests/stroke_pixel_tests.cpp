#include "../renderer/gpumapitem.h"
#include "../renderer/maprenderitem.h"
#include "../app/mapscenebridge.h"
#include <QGuiApplication>
#include <QQuickWindow>
#include <QTest>
#include <QDir>
#include <QSGRendererInterface>
#include <cmath>

class StrokePixelTests final:public QObject {
    Q_OBJECT
private slots:
    void horizonPixels_data(){QTest::addColumn<bool>("painted");QTest::newRow("GPU")<<false;QTest::newRow("painted")<<true;}
    void horizonPixels() {
        QFETCH(bool,painted);QQuickWindow window;window.resize(192,160);window.setColor(Qt::white);
        MapSceneBridge bridge;MapViewState view;view.viewportWidth=192;view.viewportHeight=160;
        view.mode=ProjectionMode::Globe;view.scale=60;view.translateX=96;view.translateY=80;bridge.publishView(view);
        auto scene=std::make_shared<RenderScene>();scene->revision=1;scene->worldPlan.worldOffsets={0};
        StrokeDrawPacket draw;draw.key="horizon";draw.object={"generic","horizon"};draw.geometry={"horizon",1};draw.geometryRevision=1;
        draw.style.color=0;draw.style.width=12;
        draw.geometryPacket=makeStrokeGeometryPacket(pandoeditor::Geometry{"LineString",{},{{{-20,0},{120,0}}},{}});
        scene->strokes.push_back(draw);scene->drawSequence.push_back({PrimitiveKind::Stroke,0,{},-1});bridge.publishScene(scene);
        QQuickItem* item;QVariantList displayedStrokes;int receipts=0;
        if(painted){auto* renderer=new MapRenderItem(window.contentItem());renderer->setSceneBridge(&bridge);item=renderer;
            connect(renderer,&MapRenderItem::framePresented,this,[&](std::shared_ptr<const MapFrame> frame,QVariantList inventory){if(frame==bridge.frameSnapshot()){displayedStrokes=inventory;++receipts;}});}
        else {auto* renderer=new GpuMapItem(window.contentItem());renderer->setSceneBridge(&bridge);item=renderer;}
        item->setWidth(192);item->setHeight(160);window.show();window.hide();window.show();QVERIFY(QTest::qWaitForWindowExposed(&window,10000));
        auto image=window.grabWindow();QVERIFY(!image.isNull());
        const auto pixel=[&](int x,int y){return image.pixelColor(int((x+.5)*image.width()/192),int((y+.5)*image.height()/160));};
        QVERIFY2(pixel(150,80).red()<20,"visible portion must reach the geographic horizon");
        QVERIFY2(pixel(159,80).red()>240,"clipping must not create a round endpoint beyond the horizon");
        QVERIFY2(pixel(73,80).red()<20,"the original visible round start cap must remain");
        if(painted){QTRY_VERIFY_WITH_TIMEOUT(receipts>0,3000);QVERIFY2(!displayedStrokes.isEmpty(),"an actually displayed clipped stroke must appear in the presentation receipt");}
        qInfo()<<"STROKE_HORIZON backend="<<(painted?"painted":"GPU")<<"processed=3 mismatch=0 skip=0 dpr="<<window.devicePixelRatio();
    }
    void chainPixels_data(){QTest::addColumn<bool>("painted");QTest::newRow("GPU")<<false;QTest::newRow("painted")<<true;}
    void chainPixels() {
        QFETCH(bool,painted);
        QQuickWindow window;window.resize(192,160);window.setColor(Qt::white);
        MapSceneBridge bridge;MapViewState view;view.viewportWidth=192;view.viewportHeight=160;
        view.scale=180/3.14159265358979323846;view.translateX=50;view.translateY=100;
        bridge.publishView(view);
        auto scene=std::make_shared<RenderScene>();scene->revision=1;scene->worldPlan.worldOffsets={0};
        StrokeDrawPacket draw;draw.key="chain";draw.object={"generic","line"};draw.geometry={"line",1};draw.geometryRevision=1;
        draw.style.color=0;draw.style.width=12;
        draw.geometryPacket=makeStrokeGeometryPacket(pandoeditor::Geometry{"LineString",{},{{{10,0},{50,0},{50,40}}},{}});
        scene->strokes.push_back(draw);scene->drawSequence.push_back({PrimitiveKind::Stroke,0,{},-1});bridge.publishScene(scene);
        GpuMapItem* gpu=nullptr;QQuickItem* item=nullptr;
        if(painted){auto* p=new MapRenderItem(window.contentItem());p->setSceneBridge(&bridge);item=p;}
        else {gpu=new GpuMapItem(window.contentItem());gpu->setSceneBridge(&bridge);gpu->setOriginX(50);gpu->setOriginY(100);gpu->setMapScale(1);gpu->setMapMinX(0);gpu->setMapMaxLatitude(0);item=gpu;}
        item->setWidth(192);item->setHeight(160);
        window.show();window.hide();window.show();QVERIFY(QTest::qWaitForWindowExposed(&window,10000));
        QCOMPARE(window.rendererInterface()->graphicsApi(),QSGRendererInterface::Direct3D11);
        if(gpu)QTRY_VERIFY_WITH_TIMEOUT(gpu->rendererReady()&&!gpu->uploadsPending(),10000);
        auto image=window.grabWindow();QVERIFY(!image.isNull());
        const auto pixel=[&](int x,int y){return image.pixelColor(int((x+.5)*image.width()/192),int((y+.5)*image.height()/160));};
        // Fixed Web RoundCap/round join: points lie outside the butt quads.
        QVERIFY2(pixel(57,100).red()<20,"round start cap must cover the actual displayed endpoint");
        QVERIFY2(pixel(103,103).red()<20,"connected round join must cover the outside corner");
        int processed=2;
        if(gpu) {
            const int py=int(105.5*image.height()/160);
            const double sy=(py+.5)*160/image.height(),edge=6-std::abs(sy-100);
            const double t=std::clamp((edge+1)/2.,0.,1.),coverage=t*t*(3-2*t);
            const int expected=int(std::lround(255*(1-coverage)));
            QVERIFY2(std::abs(pixel(80,105).red()-expected)<=1,"fixed Web analytic AA uses 1 CSS px radius, applied at DPR exactly once");++processed;
        }
        const auto initialUploads=gpu?gpu->geometryUploadCount():0;
        auto dashed=std::make_shared<RenderScene>(*scene);dashed->revision=2;dashed->strokes.front().style.dashOn=6;dashed->strokes.front().style.dashOff=3;
        bridge.publishScene(dashed);image=window.grabWindow();
        QVERIFY2(pixel(100,84).red()<20,"dash phase must continue from the preceding 40-degree segment");++processed;
        QVERIFY2(pixel(100,86).red()>240,"fixed Web dashed chains omit solid round patches and retain their gaps");++processed;
        if(gpu)QCOMPARE(gpu->geometryUploadCount(),initialUploads);
        const auto path=qEnvironmentVariable("PANDOEDITOR_UI_CAPTURE_DIR");QVERIFY(!path.isEmpty());QVERIFY(QDir().mkpath(path));
        QVERIFY(image.save(path+(painted?"/painted-dash.png":"/GPU-dash.png")));
        const auto publishStyle=[&](const RenderStyle& style) {
            auto next=std::make_shared<RenderScene>(*scene);next->revision=++dashed->revision;
            next->strokes.front().style=style;bridge.publishScene(next);image=window.grabWindow();
        };
        auto style=draw.style;style.cap=mapstyle::Cap::Butt;publishStyle(style);
        QVERIFY2(pixel(57,100).red()>240,"ButtCap must omit the displayed endpoint disc");++processed;
        style=draw.style;style.join=mapstyle::Join::Miter;publishStyle(style);
        QVERIFY2(pixel(104,104).red()<20,"MiterJoin must extend the connected outside corner");++processed;
        style=draw.style;style.alpha=.5;publishStyle(style);
        QVERIFY2(std::abs(pixel(57,100).red()-128)<=1,"alpha must be applied once at an outside cap");++processed;
        style.color=0x808080;style.blendMode=BlendMode::Multiply;publishStyle(style);
        QVERIFY2(std::abs(pixel(57,100).red()-192)<=1,"Multiply must preserve alpha weighting");++processed;
        style=draw.style;style.antiAlias=false;publishStyle(style);
        QVERIFY2(pixel(80,105).red()<20&&pixel(80,106).red()>240,"AA-disabled stroke has a hard half-width boundary");processed+=2;
        if(gpu)QCOMPARE(gpu->geometryUploadCount(),initialUploads);
        auto variable=std::make_shared<RenderScene>(*scene);variable->revision=++dashed->revision;
        variable->strokes.front().geometryPacket.endpointWidths=std::make_shared<const std::vector<float>>(std::vector<float>{0,12,12,0});
        variable->strokes.front().geometryRevision=2;
        bridge.publishScene(variable);image=window.grabWindow();
        QVERIFY2(pixel(80,107).red()<20,"endpoint widths must interpolate along the actual displayed body");++processed;
        QVERIFY(image.save(path+(painted?"/painted-variable.png":"/GPU-variable.png")));
        variable=std::make_shared<RenderScene>(*variable);++variable->revision;
        variable->strokes.front().style.join=mapstyle::Join::Miter;
        bridge.publishScene(variable);image=window.grabWindow();
        QVERIFY2(pixel(104,104).red()<20,"variable endpoint widths must retain the connected miter corner");++processed;
        qInfo()<<"STROKE_PIXEL backend="<<(painted?"painted":"GPU")<<"processed="<<processed<<"mismatch=0 skip=0 dpr="<<window.devicePixelRatio();
    }
};
int main(int argc,char** argv){QGuiApplication app(argc,argv);StrokePixelTests tests;return QTest::qExec(&tests,argc,argv);}
#include "stroke_pixel_tests.moc"

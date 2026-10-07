#include "territorial_fixture.h"
#include "../app/mapscenebridge.h"
#include "../app/terrainimageprovider.h"
#include "../renderer/geographicimageitem.h"
#include "../renderer/terrainlandmaskitem.h"
#include <pandoeditor/map/mapscenebuilder.h>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <QQuickItem>
#include <QFile>
#include <QTest>
#include <QSGRendererInterface>
#include <QDir>
#include <cmath>
#include <algorithm>
#include <array>

namespace {
constexpr int width=384,height=192;
const QColor background(17,23,31);
// Independent LINEAR sampler and arithmetic from the pinned Web shader.
// Input images are decoded directly from original files, not native display helpers.
std::array<double,4> linear(const QImage& image,double x,double y) {
    const int ix=int(std::floor(x)),iy=int(std::floor(y));
    const double fx=x-ix,fy=y-iy;
    std::array<double,4> out{};
    for(int j=0;j<2;++j)for(int i=0;i<2;++i) {
        const auto c=image.pixelColor(std::clamp(ix+i,0,image.width()-1),std::clamp(iy+j,0,image.height()-1));
        const double w=(i?fx:1-fx)*(j?fy:1-fy);
        const int channels[]{c.red(),c.green(),c.blue(),c.alpha()};
        for(int k=0;k<4;++k)out[k]+=channels[k]*w;
    }
    return out;
}
pandoeditor::Geometry land(bool moved) {
    const double west=moved?20:-100,east=moved?100:-20;
    return {"Polygon",{},{},{{{{west,-40},{east,-40},{east,20},{west,20},{west,-40}}}}};
}
}

class TerrainOfficialPixelTests final:public QObject {
    Q_OBJECT
private slots:
    void originalData_data() {
        QTest::addColumn<bool>("dem");QTest::addColumn<int>("phase");
        const char* names[]{"color","gray","dark-color","dark-gray","pan","replace-mask","missing-mask","none"};
        for(bool dem:{true,false})for(int phase=0;phase<8;++phase)
            QTest::newRow(qPrintable(QString(dem?"DEM-":"raster-")+names[phase]))<<dem<<phase;
    }
    void originalData() {
        QFETCH(bool,dem);QFETCH(int,phase);
        const auto data=qEnvironmentVariable(dem?"PANDOEDITOR_OFFICIAL_DEM_ROOT":"PANDOEDITOR_OFFICIAL_RASTER_ROOT");
        const auto manifest=qEnvironmentVariable(dem?"PANDOEDITOR_OFFICIAL_DEM_MANIFEST":"PANDOEDITOR_OFFICIAL_RASTER_MANIFEST");
        QVERIFY2(!data.isEmpty()&&!manifest.isEmpty(),"Original data roots and immutable manifests are required; never synthetic fallback");
        QFile mf(manifest);QVERIFY(mf.open(QIODevice::ReadOnly));
        auto source=std::make_shared<TerrainTileProvider>(mf.readAll(),data);
        QVERIFY2(source->available(),qPrintable(source->error()));QCOMPARE(source->isDem(),dem);
        const auto version=dem?"v0.13.0":"v0.12.6";
        QImage raw[2]{QImage(data+"/terrain/"+version+"/0/0-0.webp"),QImage(data+"/terrain/"+version+"/0/1-0.webp")};
        QVERIFY(!raw[0].isNull()&&!raw[1].isNull());
        QCOMPARE(raw[0].size(),QSize(1026,677));QCOMPARE(raw[1].size(),QSize(328,677));
        QImage tint;if(dem){tint.load(data+"/terrain/v0.13.3/tint.webp");QVERIFY(!tint.isNull());}
        const bool gray=phase==1||phase==3,dark=phase==2||phase==3,moved=phase==5;
        const double pan=phase==4?64:0;
        pandoeditor::ProjectDocument doc;
        appendTerritory(doc,{"LAND","Hidden land",{},pandoeditor::UnitKind::General,false},{"land",1});
        doc.geometries.insert({"land",1},land(false));
        doc.presentation.webPresentation.visibility["countries"]=false;
        GeometryPacketCache cache;MapSceneBuilder builder(cache);MapSceneBridge bridge;TerrainImageBridge images;
        images.setSource(source);images.setRenderStyle(dark,0);
        MapViewState state;state.viewportWidth=width;state.viewportHeight=height;
        state.scale=width/(2*3.14159265358979323846);state.translateX=width/2.;state.translateY=height/2.;
        bridge.publishView(state);bridge.publishScene(builder.buildDocument(doc,1,bridge.viewState(),{},{}));
        QVariantList tiles;for(const auto& t:source->tilesForView(bridge.viewState()))tiles.append(QVariantMap{
            {"level",t.level},{"column",t.column},{"row",t.row},{"west",t.west},{"east",t.east},{"south",t.south},{"north",t.north}});
        QQuickView window;window.setResizeMode(QQuickView::SizeRootObjectToView);window.resize(width,height);
        window.rootContext()->setContextProperty("fixtureSceneBridge",&bridge);
        window.rootContext()->setContextProperty("fixtureTerrainBridge",&images);
        window.rootContext()->setContextProperty("fixtureTiles",tiles);
        window.setSource(QUrl("qrc:/tests/terrain_gpu_display_fixture.qml"));QCOMPARE(window.status(),QQuickView::Ready);
        auto* root=window.rootObject();QVERIFY(root);
        root->setProperty("terrainColorMode",gray?"gray":"color");
        auto* mask=root->findChild<TerrainLandMaskItem*>("physicalLandMask");QVERIFY(mask);
        int swaps=0;connect(&window,&QQuickWindow::frameSwapped,this,[&]{++swaps;});
        window.show();window.hide();window.show();QVERIFY(QTest::qWaitForWindowExposed(&window,10000));
        QCOMPARE(window.rendererInterface()->graphicsApi(),QSGRendererInterface::Direct3D11);
        // Physical raster uses its original RGB without a land-mask request.
        if(dem||gray)QTRY_VERIFY_WITH_TIMEOUT(mask->maskReady(),10000);
        QTRY_VERIFY_WITH_TIMEOUT(swaps>0,10000);
        // Transition through actual current production display before changing owners.
        QVERIFY(!window.grabWindow().isNull());
        if(phase==4) {
            root->setProperty("panPixels",pan);state.translateX+=pan;bridge.publishView(state);
            if(dem||gray)QTRY_VERIFY_WITH_TIMEOUT(mask->maskViewRevision()==bridge.viewState().revision,10000);
        }
        if(moved) {
            doc.geometries.insert({"land",2},land(true));pandoeditor::staticGeometryBinding(doc,"LAND").geometryRef.version=2;
            bridge.publishScene(builder.buildDocument(doc,2,bridge.viewState(),{},bridge.sceneSnapshot()));
            if(dem||gray)QTRY_VERIFY_WITH_TIMEOUT(mask->maskSceneRevision()==bridge.sceneSnapshot()->revision,10000);
        }
        if(phase==6)root->setProperty("maskEnabled",false);
        if(phase==7)root->setProperty("terrainEnabled",false);
        const auto captured=window.grabWindow();QVERIFY(!captured.isNull());
        int compared=0;
        // Away from polygon/tile boundaries; subpixel coordinates use the actual DPR.
        for(const QPoint point:{QPoint(130,96),QPoint(230,96),QPoint(130,125),QPoint(320,96)}) {
            const int px=int((point.x()+.5)*captured.width()/width),py=int((point.y()+.5)*captured.height()/height);
            const double sx=(px+.5)*width/captured.width(),sy=(py+.5)*height/captured.height();
            double lon=(sx-pan)/width*360-180,lat=90-sy/height*180;
            const bool onLand=lon>(moved?20:-100)&&lon<(moved?100:-20)&&lat>-40&&lat<20;
            QColor expected=background;
            if(phase!=7&&(!dem||phase!=6)&&(!gray||onLand)&&(phase!=6||!gray)) {
                const double worldX=(lon+180)/360*1350,worldY=(90-lat)/180*675;
                const int col=worldX>=1024?1:0;
                const auto encoded=linear(raw[col],1+worldX-col*1024-.5,1+worldY-.5);
                std::array<double,3> rgb{};
                if(!dem)for(int k=0;k<3;++k)rgb[k]=gray?encoded[3]:encoded[k];
                else {
                    double shade=encoded[2]/255;
                    if(gray)rgb={shade*255,shade*255,shade*255};
                    else {
                        const auto tc=linear(tint,(lon+180)/360*tint.width()-.5,(90-lat)/180*tint.height()-.5);
                        const double h=std::floor(encoded[0]+.5)*256+std::floor(encoded[1]+.5)-12000;
                        const double depth=std::pow(std::clamp(-h/8000.,0.,1.),.6);
                        const double shallow[]{.42,.66,.82},deep[]{.10,.24,.39};
                        const double factor=std::clamp(shade/(.42+.58*.70710678),.5,1.2);
                        for(int k=0;k<3;++k)rgb[k]=(onLand?tc[k]:255*(shallow[k]+depth*(deep[k]-shallow[k])))*factor;
                    }
                }
                const double darkFactors[]{.60,.68,.76};
                for(int k=0;k<3;++k)rgb[k]*=dark?1+.48*(darkFactors[k]-1):1;
                expected=QColor(std::clamp(int(std::lround(rgb[0])),0,255),std::clamp(int(std::lround(rgb[1])),0,255),std::clamp(int(std::lround(rgb[2])),0,255),255);
            }
            const auto actual=captured.pixelColor(px,py);
            const auto message=QString("original %1 phase=%2 at %3,%4 actual=%5 expected=%6 tolerance=1 RGB exact alpha").arg(dem?"DEM":"raster").arg(phase).arg(px).arg(py).arg(actual.name()).arg(expected.name());
            QVERIFY2(std::abs(actual.red()-expected.red())<=1&&std::abs(actual.green()-expected.green())<=1&&std::abs(actual.blue()-expected.blue())<=1&&actual.alpha()==expected.alpha(),qPrintable(message));
            ++compared;
        }
        QCOMPARE(compared,4);
        const auto dir=qEnvironmentVariable("PANDOEDITOR_UI_CAPTURE_DIR");QVERIFY(!dir.isEmpty());QVERIFY(QDir().mkpath(dir));
        QVERIFY(captured.save(dir+QString("/%1-%2.png").arg(dem?"DEM":"raster").arg(phase)));
        qInfo()<<"OFFICIAL_PIXEL case="<<QTest::currentDataTag()<<"processed=1 compared="<<compared<<"mismatch=0 skip=0 swaps="<<swaps;
    }
};
int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);
    qmlRegisterType<TerrainLandMaskItem>("Pandoeditor.TerrainTest",1,0,"TerrainLandMaskItem");
    qmlRegisterType<GeographicImageItem>("Pandoeditor.TerrainTest",1,0,"GeographicImageItem");
    TerrainOfficialPixelTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "terrain_official_pixel_tests.moc"

#include "territorial_fixture.h"
#include "../app/mapscenebridge.h"
#include "../app/terrainimageprovider.h"
#include "../renderer/geographicimageitem.h"
#include "../renderer/terrainlandmaskitem.h"
#include <pandoeditor/map/mapscenebuilder.h>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageWriter>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QRegularExpression>
#include <QSGRendererInterface>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantList>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>

namespace {
constexpr int width=384,height=192;
const QColor background(17,23,31);
// Independent literals from fixed Web shader, not TerrainRenderContract helpers:
// https://github.com/kimjeon-il/world-map/blob/5649c307da24d0965d63bc8c00206aed9b9d3438/assets/js/modules/terrain-dem-shaders.js
// Synthetic constant DEM: (50 * 256 + 200) - 12000 = +1000 metres.
// These files prove rendering arithmetic, not provenance of official DEM pixels.
QColor physicalColor(bool land,bool dark=false) {
    const double normalization=(212.0/255.0)/(0.42+0.58*0.70710678);
    const double shade=std::clamp(normalization,0.5,1.2);
    const double rgb[3]={land?140.0/255.0:0.42,land?170.0/255.0:0.66,land?120.0/255.0:0.82};
    const double darkFactor[3]={0.60,0.68,0.76};
    int channel[3];
    for(int i=0;i<3;++i)channel[i]=int(std::lround(255*rgb[i]*shade*(dark?1+0.48*(darkFactor[i]-1):1)));
    return {channel[0],channel[1],channel[2],255};
}
pandoeditor::Geometry rectangle(double west,double east) {
    // Unequal north/south bounds expose a mirrored offscreen mask.
    return {"Polygon",{},{},{{{{west,-40},{east,-40},{east,20},{west,20},{west,-40}}}}};
}
QVariantList tileModel(const std::vector<TerrainTileSpec>& tiles) {
    QVariantList rows;
    for(const auto& tile:tiles)rows.append(QVariantMap{{"level",tile.level},{"column",tile.column},{"row",tile.row},
        {"west",tile.west},{"east",tile.east},{"south",tile.south},{"north",tile.north}});
    return rows;
}
bool writeLossless(const QString& path,QSize size,QColor color) {
    if(!QDir().mkpath(QFileInfo(path).absolutePath()))return false;
    QImage image(size,QImage::Format_RGBA8888);image.fill(color);
    QImageWriter writer(path,"webp");writer.setQuality(100);
    if(!writer.write(image))return false;
    const QImage decoded(path);
    return decoded.size()==size&&decoded.pixelColor(size.width()/2,size.height()/2)==color&&
        decoded.pixelColor(0,0)==color&&decoded.pixelColor(size.width()-1,size.height()-1)==color;
}
QString apiName(QSGRendererInterface::GraphicsApi api) {
    switch(api) {
    case QSGRendererInterface::OpenGL:return "OpenGL";
    case QSGRendererInterface::Direct3D11:return "Direct3D11";
    case QSGRendererInterface::Vulkan:return "Vulkan";
    case QSGRendererInterface::Metal:return "Metal";
    case QSGRendererInterface::Software:return "Software";
    default:return QString::number(int(api));
    }
}
}

class TerrainGpuDisplayTests final : public QObject {
    Q_OBJECT
public:
    explicit TerrainGpuDisplayTests(QString manifest):manifestPath_(std::move(manifest)){}
private slots:
    void currentMaskControlsActualDemPixels() {
        QTest::failOnWarning(QRegularExpression(".*"));
        QFile manifestFile(manifestPath_);
        QVERIFY2(manifestFile.open(QIODevice::ReadOnly),qPrintable(manifestFile.errorString()));
        const auto manifest=manifestFile.readAll();
        QTemporaryDir data;
        QVERIFY(data.isValid());
        QVERIFY(writeLossless(data.path()+"/terrain/v0.13.0/0/0-0.webp",{1026,677},{50,200,212,255}));
        QVERIFY(writeLossless(data.path()+"/terrain/v0.13.0/0/1-0.webp",{328,677},{50,200,212,255}));
        QVERIFY(writeLossless(data.path()+"/terrain/v0.13.3/tint.webp",{4096,2048},{140,170,120,255}));
        auto source=std::make_shared<TerrainTileProvider>(manifest,data.path());
        QVERIFY2(source->available(),qPrintable(source->error()));
        QVERIFY(source->isDem());
        QCOMPARE(source->levelSize(0),QSize(1350,675));
        QCOMPARE(source->loadTile(0,0,0).pixelColor(400,250),QColor(50,200,212,255));
        QCOMPARE(source->loadTile(0,1,0).pixelColor(100,250),QColor(50,200,212,255));
        QCOMPARE(source->loadTint().pixelColor(1000,1000),QColor(140,170,120,255));

        pandoeditor::ProjectDocument document;
        appendTerritory(document,{"LAND","Hidden land",{},pandoeditor::UnitKind::General,false},{"land",1});
        appendTerritory(document,{"REGION","Independent region",{},pandoeditor::UnitKind::Regional,false},{"region",1});
        document.geometries.insert({"land",1},rectangle(-100,-20));
        document.geometries.insert({"region",1},rectangle(20,100));
        document.presentation.webPresentation.visibility["countries"]=false;
        document.presentation.webPresentation.visibility["subunits"]=false;
        document.presentation.webPresentation.visibility["regions"]=false;
        document.presentation.objectStyles[pandoeditor::territorialRef("LAND")].opacity=0;
        document.presentation.webPresentation.styles["countries"].boundaryVisible=false;
        GeometryPacketCache cache;
        MapSceneBuilder builder(cache);
        MapSceneBridge sceneBridge;
        TerrainImageBridge terrainBridge;
        terrainBridge.setSource(source);
        terrainBridge.setRenderStyle(false,0);
        const auto sourceEpoch=terrainBridge.sourceEpoch();
        MapViewState viewState;
        viewState.viewportWidth=width;viewState.viewportHeight=height;
        viewState.scale=width/(2*3.14159265358979323846);
        viewState.translateX=width/2.0;viewState.translateY=height/2.0;
        sceneBridge.publishView(viewState);
        auto scene=builder.buildDocument(document,1,sceneBridge.viewState(),{},{});
        QVERIFY(scene->physicalLandMask&&scene->physicalLandMask->polygons.size()==1);
        QVERIFY(scene->polygons.empty());
        sceneBridge.publishScene(scene);

        std::atomic<unsigned> frameEnds{0},frameSwaps{0};
        QQuickView view;
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.resize(width,height);
        view.rootContext()->setContextProperty("fixtureSceneBridge",&sceneBridge);
        view.rootContext()->setContextProperty("fixtureTerrainBridge",&terrainBridge);
        view.rootContext()->setContextProperty("fixtureTiles",tileModel(source->tilesForView(sceneBridge.viewState())));
        QObject::connect(&view,&QQuickWindow::afterFrameEnd,&view,[&]{++frameEnds;},Qt::DirectConnection);
        QObject::connect(&view,&QQuickWindow::frameSwapped,&view,[&]{++frameSwaps;},Qt::DirectConnection);
        view.setSource(QUrl("qrc:/tests/terrain_gpu_display_fixture.qml"));
        QCOMPARE(view.status(),QQuickView::Ready);
        auto* root=view.rootObject();
        QVERIFY(root);
        auto* mask=root->findChild<TerrainLandMaskItem*>("physicalLandMask");
        QVERIFY(mask);
        view.show();
        // Windows STARTUPINFO can hide the first ShowWindow in a hidden
        // console launch. Re-show the actual Qt window, as the UI harness does.
        view.hide();view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view,10000));
        view.update();
        const auto api=view.rendererInterface()->graphicsApi();
        std::cout<<"terrain GPU display: hardware_gate=RUN api="<<apiName(api).toStdString()
            <<" samples="<<view.format().samples()<<" syntheticPixels=true\n";
        QVERIFY2(api!=QSGRendererInterface::Software&&api!=QSGRendererInterface::Unknown,
                 "Hardware gate requires a real RHI renderer; software/unknown is a failure, never a skip");
        QVERIFY2(view.format().samples()<=1,"Binary mask requires a non-MSAA window");
        QTRY_VERIFY_WITH_TIMEOUT(mask->maskReady(),10000);
        const auto current=[&]{return mask->maskReady()&&mask->maskViewRevision()==sceneBridge.viewState().revision&&
            mask->maskSceneRevision()==sceneBridge.sceneSnapshot()->revision;};
        const auto rendered=[&] {
            view.update();
            return view.grabWindow();
        };
        const auto checkPixel=[&](const QImage& image,int x,int y,QColor expected) {
            if(image.isNull())return QString("grabWindow returned no actual pixels");
            const int px=std::min(image.width()-1,int((x+0.5)*image.width()/width));
            const int py=std::min(image.height()-1,int((y+0.5)*image.height()/height));
            const auto actual=image.pixelColor(px,py);
            if(std::abs(actual.red()-expected.red())<=1&&std::abs(actual.green()-expected.green())<=1&&
               std::abs(actual.blue()-expected.blue())<=1&&actual.alpha()==expected.alpha())return QString{};
            return QString("pixel %1,%2 actual RGBA(%3,%4,%5,%6), expected RGBA(%7,%8,%9,%10), tolerance=1 RGB")
                .arg(x).arg(y).arg(actual.red()).arg(actual.green()).arg(actual.blue()).arg(actual.alpha())
                .arg(expected.red()).arg(expected.green()).arg(expected.blue()).arg(expected.alpha());
        };
        unsigned processed=0;
        QTRY_VERIFY_WITH_TIMEOUT(current(),10000);
        auto image=rendered();
        auto error=checkPixel(image,130,96,physicalColor(true));QVERIFY2(error.isEmpty(),qPrintable(error));
        error=checkPixel(image,230,96,physicalColor(false));QVERIFY2(error.isEmpty(),qPrintable(error));
        error=checkPixel(image,130,70,physicalColor(false));QVERIFY2(error.isEmpty(),qPrintable(error));
        error=checkPixel(image,130,125,physicalColor(true));QVERIFY2(error.isEmpty(),qPrintable(error));
        // Same positive-height bytes on both sides: independent Regional geometry cannot paint land.
        QVERIFY(mask->maskPixelSize().width()>0&&mask->maskPixelSize().height()>0);
        QVERIFY(mask->geometryBytes()>0);
        const auto uploads=mask->geometryUploadCount();QVERIFY(uploads>0);++processed;

        for(int repeat=0;repeat<3;++repeat) {
            image=rendered();error=checkPixel(image,130,96,physicalColor(true));QVERIFY2(error.isEmpty(),qPrintable(error));
        }
        QCOMPARE(mask->geometryUploadCount(),uploads);++processed;

        const auto pan=[&](double pixels) {
            root->setProperty("panPixels",pixels);
            auto moved=sceneBridge.viewState();moved.translateX=width/2.0+pixels;
            sceneBridge.publishView(moved);
            view.rootContext()->setContextProperty("fixtureTiles",tileModel(source->tilesForView(sceneBridge.viewState())));
            view.update();
        };
        pan(64);QTRY_VERIFY_WITH_TIMEOUT(current(),10000);
        image=rendered();error=checkPixel(image,194,96,physicalColor(true));QVERIFY2(error.isEmpty(),qPrintable(error));
        error=checkPixel(image,130,96,physicalColor(false));QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(mask->geometryUploadCount(),uploads);
        QCOMPARE(terrainBridge.sourceEpoch(),sourceEpoch);++processed;

        root->setProperty("terrainColorMode","gray");
        image=rendered();error=checkPixel(image,194,96,{212,212,212,255});QVERIFY2(error.isEmpty(),qPrintable(error));
        error=checkPixel(image,270,96,background);QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(mask->geometryUploadCount(),uploads);++processed;

        root->setProperty("terrainColorMode","color");terrainBridge.setRenderStyle(true,0);
        image=rendered();error=checkPixel(image,194,96,physicalColor(true,true));QVERIFY2(error.isEmpty(),qPrintable(error));
        error=checkPixel(image,270,96,physicalColor(false,true));QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(terrainBridge.sourceEpoch(),sourceEpoch);++processed;

        terrainBridge.setRenderStyle(false,0);
        document.geometries.insert({"land",2},rectangle(20,100));
        pandoeditor::staticGeometryBinding(document,"LAND").geometryRef.version=2;
        const auto replaced=builder.buildDocument(document,2,sceneBridge.viewState(),{},scene);
        QVERIFY(replaced->physicalLandMask!=scene->physicalLandMask);
        QVERIFY(replaced->polygons.empty());
        sceneBridge.publishScene(replaced);QTRY_VERIFY_WITH_TIMEOUT(current(),10000);
        image=rendered();error=checkPixel(image,320,96,physicalColor(true));QVERIFY2(error.isEmpty(),qPrintable(error));
        error=checkPixel(image,194,96,physicalColor(false));QVERIFY2(error.isEmpty(),qPrintable(error));
        QVERIFY(mask->geometryUploadCount()>uploads);++processed;

        root->setProperty("maskLive",false);
        const auto beforeFreeze=frameEnds.load();pan(32);
        QTRY_VERIFY_WITH_TIMEOUT(frameEnds.load()>beforeFreeze,10000);
        QTRY_VERIFY_WITH_TIMEOUT(!mask->maskReady(),10000);
        image=rendered();error=checkPixel(image,300,96,background);QVERIFY2(error.isEmpty(),qPrintable(error));
        error=checkPixel(image,194,96,background);QVERIFY2(error.isEmpty(),qPrintable(error));
        root->setProperty("maskLive",true);view.update();QTRY_VERIFY_WITH_TIMEOUT(current(),10000);
        image=rendered();error=checkPixel(image,300,96,physicalColor(true));QVERIFY2(error.isEmpty(),qPrintable(error));++processed;

        root->setProperty("maskEnabled",false);
        image=rendered();error=checkPixel(image,300,96,background);QVERIFY2(error.isEmpty(),qPrintable(error));
        error=checkPixel(image,194,96,background);QVERIFY2(error.isEmpty(),qPrintable(error));
        root->setProperty("maskEnabled",true);view.update();QTRY_VERIFY_WITH_TIMEOUT(current(),10000);
        image=rendered();error=checkPixel(image,300,96,physicalColor(true));QVERIFY2(error.isEmpty(),qPrintable(error));++processed;

        QTRY_VERIFY_WITH_TIMEOUT(frameEnds.load()>0&&frameSwaps.load()>0,10000);
        QCOMPARE(processed,8u);
        std::cout<<"terrain GPU display: hardware_gate=RUN api="<<apiName(api).toStdString()
            <<" processed="<<processed<<" passed="<<processed<<" failed=0 skip=0"
            <<" afterFrameEnd="<<frameEnds.load()<<" frameSwapped="<<frameSwaps.load()
            <<" maskUploads="<<mask->geometryUploadCount()<<" maskBytes="<<mask->geometryBytes()
            <<" viewRevision="<<mask->maskViewRevision()<<" sceneRevision="<<mask->maskSceneRevision()
            <<" manifestSha256="<<QCryptographicHash::hash(manifest,QCryptographicHash::Sha256).toHex().constData()
            <<" syntheticPixels=true receipts=submission-and-window-swap-not-GPU-fence\n";
    }
    void rasterOriginalChannelsReachActualPixels() {
        QTest::failOnWarning(QRegularExpression(".*"));
        // This is the exact fixed Web raster manifest, not a DEM-manifest rewrite.
        const auto rasterPath=QDir::cleanPath(QFileInfo(manifestPath_).absolutePath()+"/../v0.12.6/manifest.json");
        QFile manifestFile(rasterPath);
        QVERIFY2(manifestFile.open(QIODevice::ReadOnly),qPrintable(manifestFile.errorString()));
        const auto manifest=manifestFile.readAll();
        QCOMPARE(manifest.size(),qsizetype(2024));
        QCOMPARE(QCryptographicHash::hash(manifest,QCryptographicHash::Sha256).toHex(),
                 QByteArray("093ae0f088622e2867cad5f318749c47e28776a870e6467d00c31ef810d8690f"));
        QTemporaryDir data;QVERIFY(data.isValid());
        const auto raw=QColor(255,0,0,64); // RGB and data-A are intentionally unrelated.
        QVERIFY(writeLossless(data.path()+"/terrain/v0.12.6/0/0-0.webp",{1026,677},raw));
        QVERIFY(writeLossless(data.path()+"/terrain/v0.12.6/0/1-0.webp",{328,677},raw));
        auto source=std::make_shared<TerrainTileProvider>(manifest,data.path());
        QVERIFY2(source->available(),qPrintable(source->error()));QVERIFY(!source->isDem());
        QCOMPARE(source->levelSize(0),QSize(1350,675));
        QCOMPARE(source->loadTile(0,0,0,false).pixelColor(400,250),raw);
        QCOMPARE(source->loadTile(0,1,0,false).pixelColor(100,250),raw);

        pandoeditor::ProjectDocument document;
        appendTerritory(document,{"LAND","Hidden land",{},pandoeditor::UnitKind::General,false},{"land",1});
        document.geometries.insert({"land",1},rectangle(-100,-20));
        document.presentation.webPresentation.visibility["countries"]=false;
        document.presentation.objectStyles[pandoeditor::territorialRef("LAND")].opacity=0;
        GeometryPacketCache cache;MapSceneBuilder builder(cache);
        MapSceneBridge sceneBridge;TerrainImageBridge terrainBridge;
        terrainBridge.setSource(source);terrainBridge.setRenderStyle(false,0);
        const auto sourceEpoch=terrainBridge.sourceEpoch();
        MapViewState state;state.viewportWidth=width;state.viewportHeight=height;
        state.scale=width/(2*3.14159265358979323846);state.translateX=width/2.0;state.translateY=height/2.0;
        sceneBridge.publishView(state);
        const auto scene=builder.buildDocument(document,1,sceneBridge.viewState(),{},{});
        QVERIFY(scene->physicalLandMask&&scene->physicalLandMask->polygons.size()==1);
        QVERIFY(scene->polygons.empty());sceneBridge.publishScene(scene);

        std::atomic<unsigned> frameEnds{0},frameSwaps{0};
        QQuickView view;view.setResizeMode(QQuickView::SizeRootObjectToView);view.resize(width,height);
        view.rootContext()->setContextProperty("fixtureSceneBridge",&sceneBridge);
        view.rootContext()->setContextProperty("fixtureTerrainBridge",&terrainBridge);
        view.rootContext()->setContextProperty("fixtureTiles",tileModel(source->tilesForView(sceneBridge.viewState())));
        QObject::connect(&view,&QQuickWindow::afterFrameEnd,&view,[&]{++frameEnds;},Qt::DirectConnection);
        QObject::connect(&view,&QQuickWindow::frameSwapped,&view,[&]{++frameSwaps;},Qt::DirectConnection);
        view.setSource(QUrl("qrc:/tests/terrain_gpu_display_fixture.qml"));
        QCOMPARE(view.status(),QQuickView::Ready);auto* root=view.rootObject();QVERIFY(root);
        auto* mask=root->findChild<TerrainLandMaskItem*>("physicalLandMask");QVERIFY(mask);
        root->setProperty("maskEnabled",false);
        view.show();QVERIFY(QTest::qWaitForWindowExposed(&view,10000));view.update();
        const auto api=view.rendererInterface()->graphicsApi();
        std::cout<<"raster GPU display: hardware_gate=RUN api="<<apiName(api).toStdString()
            <<" samples="<<view.format().samples()<<" syntheticPixels=true\n";
        QVERIFY2(api!=QSGRendererInterface::Software&&api!=QSGRendererInterface::Unknown,
                 "Actual RHI raster pixels are required; software/unknown is a failure, never a skip");
        QVERIFY2(view.format().samples()<=1,"Binary mask requires a non-MSAA window");
        const auto current=[&]{return mask->maskReady()&&mask->maskViewRevision()==sceneBridge.viewState().revision&&
            mask->maskSceneRevision()==sceneBridge.sceneSnapshot()->revision;};
        const auto rendered=[&]{view.update();return view.grabWindow();};
        const auto checkPixel=[&](const QImage& image,int x,int y,QColor expected) {
            if(image.isNull())return QString("grabWindow returned no actual pixels");
            const int px=std::min(image.width()-1,int((x+0.5)*image.width()/width));
            const int py=std::min(image.height()-1,int((y+0.5)*image.height()/height));
            const auto actual=image.pixelColor(px,py);
            if(std::abs(actual.red()-expected.red())<=1&&std::abs(actual.green()-expected.green())<=1&&
               std::abs(actual.blue()-expected.blue())<=1&&actual.alpha()==expected.alpha())return QString{};
            return QString("pixel %1,%2 actual RGBA(%3,%4,%5,%6), expected RGBA(%7,%8,%9,%10), tolerance=1 RGB")
                .arg(x).arg(y).arg(actual.red()).arg(actual.green()).arg(actual.blue()).arg(actual.alpha())
                .arg(expected.red()).arg(expected.green()).arg(expected.blue()).arg(expected.alpha());
        };
        // Independent fixed Web literals: raw RGB is opaque red; raw A gives64.
        // Dark factors are 1+.48*(.60/.68/.76-1), giving red206 and gray52/54/57.
        constexpr quint64 additionalBytes=quint64(1026+328)*677*4;
        QTRY_VERIFY_WITH_TIMEOUT(terrainBridge.displayBackingBytes()>=additionalBytes,10000);
        QTRY_VERIFY_WITH_TIMEOUT(terrainBridge.displayBackingCount()>=2,10000);
        const auto peakBacking=terrainBridge.displayBackingBytes();
        unsigned processed=0;
        auto image=rendered();auto error=checkPixel(image,130,96,QColor(255,0,0,255));
        QVERIFY2(error.isEmpty(),qPrintable(error));
        error=checkPixel(image,230,96,QColor(255,0,0,255));QVERIFY2(error.isEmpty(),qPrintable(error));
        QVERIFY(!mask->maskReady());++processed; // Physical raster never requests a mask texture.

        root->setProperty("terrainColorMode","gray");
        image=rendered();error=checkPixel(image,130,96,background);QVERIFY2(error.isEmpty(),qPrintable(error));
        error=checkPixel(image,230,96,background);QVERIFY2(error.isEmpty(),qPrintable(error));
        QVERIFY(!mask->maskReady());++processed;

        root->setProperty("maskEnabled",true);view.update();QTRY_VERIFY_WITH_TIMEOUT(current(),10000);
        image=rendered();error=checkPixel(image,130,96,QColor(64,64,64,255));QVERIFY2(error.isEmpty(),qPrintable(error));
        error=checkPixel(image,230,96,background);QVERIFY2(error.isEmpty(),qPrintable(error));
        error=checkPixel(image,130,70,background);QVERIFY2(error.isEmpty(),qPrintable(error));
        error=checkPixel(image,130,125,QColor(64,64,64,255));QVERIFY2(error.isEmpty(),qPrintable(error));
        QVERIFY(mask->geometryUploadCount()>0);++processed;

        terrainBridge.setRenderStyle(true,0);
        image=rendered();error=checkPixel(image,130,96,QColor(52,54,57,255));QVERIFY2(error.isEmpty(),qPrintable(error));
        error=checkPixel(image,230,96,background);QVERIFY2(error.isEmpty(),qPrintable(error));++processed;

        root->setProperty("terrainColorMode","color");root->setProperty("maskEnabled",false);
        image=rendered();error=checkPixel(image,130,96,QColor(206,0,0,255));QVERIFY2(error.isEmpty(),qPrintable(error));
        error=checkPixel(image,230,96,QColor(206,0,0,255));QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(terrainBridge.sourceEpoch(),sourceEpoch);
        QVERIFY(terrainBridge.displayBackingBytes()>=additionalBytes);++processed;

        root->setProperty("terrainTileValid",false);
        QTRY_COMPARE_WITH_TIMEOUT(terrainBridge.displayBackingBytes(),quint64(0),10000);
        QCOMPARE(terrainBridge.displayBackingCount(),quint64(0));
        image=rendered();error=checkPixel(image,130,96,background);QVERIFY2(error.isEmpty(),qPrintable(error));
        QVERIFY(source->cachedBytes()>=additionalBytes);++processed; // Raw decode cache is separate.

        root->setProperty("terrainTileValid",true);
        QTRY_VERIFY_WITH_TIMEOUT(terrainBridge.displayBackingBytes()>=additionalBytes,10000);
        root->setProperty("terrainEnabled",false);
        QTRY_COMPARE_WITH_TIMEOUT(terrainBridge.displayBackingBytes(),quint64(0),10000);
        QCOMPARE(terrainBridge.displayBackingCount(),quint64(0));
        image=rendered();error=checkPixel(image,230,96,background);QVERIFY2(error.isEmpty(),qPrintable(error));++processed;

        root->setProperty("terrainEnabled",true);
        QTRY_VERIFY_WITH_TIMEOUT(terrainBridge.displayBackingBytes()>=additionalBytes,10000);
        terrainBridge.setSource({});
        QTRY_COMPARE_WITH_TIMEOUT(terrainBridge.displayBackingBytes(),quint64(0),10000);
        QCOMPARE(terrainBridge.displayBackingCount(),quint64(0));
        image=rendered();error=checkPixel(image,130,96,background);QVERIFY2(error.isEmpty(),qPrintable(error));
        error=checkPixel(image,230,96,background);QVERIFY2(error.isEmpty(),qPrintable(error));++processed;

        QTRY_VERIFY_WITH_TIMEOUT(frameEnds.load()>0&&frameSwaps.load()>0,10000);
        QCOMPARE(processed,8u);
        std::cout<<"raster GPU display: hardware_gate=RUN api="<<apiName(api).toStdString()
            <<" processed="<<processed<<" passed="<<processed<<" failed=0 skip=0"
            <<" afterFrameEnd="<<frameEnds.load()<<" frameSwapped="<<frameSwaps.load()
            <<" maskUploads="<<mask->geometryUploadCount()<<" maskBytes="<<mask->geometryBytes()
            <<" peakDisplayBackingBytes="<<peakBacking<<" minimumAdditionalBytes="<<additionalBytes
            <<" finalDisplayBackingBytes="<<terrainBridge.displayBackingBytes()
            <<" manifestSha256="<<QCryptographicHash::hash(manifest,QCryptographicHash::Sha256).toHex().constData()
            <<" syntheticPixels=true receipts=submission-and-window-swap-not-GPU-fence\n";
    }
private:
    QString manifestPath_;
};

int main(int argc,char** argv) {
    if(qEnvironmentVariable("PANDOEDITOR_TERRAIN_GPU_DISPLAY")!="1") {
        std::cerr<<"terrain GPU display: hardware_gate=NOT_RUN; set PANDOEDITOR_TERRAIN_GPU_DISPLAY=1 for actual RHI execution\n";
        return 2;
    }
    if(argc<2) {std::cerr<<"terrain GPU display: fixed DEM manifest argument required\n";return 1;}
    const QString manifest=QString::fromLocal8Bit(argv[1]);
    // Keep the production manifest separate from QTest's own command-line parser.
    for(int i=1;i<argc-1;++i)argv[i]=argv[i+1];--argc;argv[argc]=nullptr;
    QSurfaceFormat format=QSurfaceFormat::defaultFormat();format.setSamples(0);QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication application(argc,argv);
    qmlRegisterType<TerrainLandMaskItem>("Pandoeditor.TerrainTest",1,0,"TerrainLandMaskItem");
    qmlRegisterType<GeographicImageItem>("Pandoeditor.TerrainTest",1,0,"GeographicImageItem");
    TerrainGpuDisplayTests tests(manifest);
    return QTest::qExec(&tests,argc,argv);
}
#include "terrain_gpu_display_tests.moc"

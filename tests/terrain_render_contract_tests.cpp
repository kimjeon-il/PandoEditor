#include "../renderer/terrainrendercontract.h"
#include <QtTest>
#include <cmath>

class TerrainRenderContractTests : public QObject {
    Q_OBJECT
private slots:
    void packedChannels() {
        QCOMPARE(TerrainRenderContract::elevation(46,224),0.0);
        QCOMPARE(TerrainRenderContract::elevation(0,0),-12000.0);
        QCOMPARE(TerrainRenderContract::elevation(255,255),53535.0);
    }
    void gutterCoordinates() {
        const auto uv=TerrainRenderContract::uvBounds(QSize(514,258),1);
        QVERIFY(std::abs(uv.x()-1.0/514)<1e-8);
        QVERIFY(std::abs(uv.y()-1.0/258)<1e-8);
        QVERIFY(std::abs(uv.z()-513.0/514)<1e-7);
        QVERIFY(std::abs(uv.w()-257.0/258)<1e-7);
    }
    void styleUsesMaskAndBlueChannel() {
        using namespace TerrainRenderContract;
        const Color tint{0.3,0.5,0.2};
        const double flat=0.42+0.58*0.70710678;
        const auto gray=color(1000,0.25,tint,false,false,false);
        QCOMPARE(gray.r,0.25); QCOMPARE(gray.g,0.25); QCOMPARE(gray.b,0.25);
        // Positive elevation in an authoritative ocean mask stays ocean.
        const auto ocean=color(1000,flat,tint,true,false,false);
        QVERIFY(std::abs(ocean.r-0.42)<1e-12);
        const auto land=color(-1000,flat,tint,true,true,false);
        QVERIFY(std::abs(land.r-tint.r)<1e-12);
        const auto deep=color(-8000,flat,tint,true,false,false);
        QVERIFY(std::abs(deep.b-0.39)<1e-12);
        const auto dark=color(-1000,flat,tint,true,true,true);
        QVERIFY(std::abs(dark.g-tint.g*(1-0.48+0.48*0.68))<1e-12);
    }
    void derivedShadeHalfTexelAndPole() {
        using namespace TerrainRenderContract;
        const auto plane=[](int x,int y){return 200.0*x-100.0*y;};
        const auto a=derivedShade(0.2,0.7,QSize(16,16),QSize(1350,675),0,plane);
        const auto b=derivedShade(0.7,0.2,QSize(16,16),QSize(1350,675),0,plane);
        QVERIFY(std::abs(a-b)<1e-12);
        QVERIFY(a>=0.42&&a<=1.0);
        QCOMPARE(blendedShade(0.2,a,1,89.5),0.2);
        QCOMPARE(blendedShade(0.2,a,1,-90),0.2);
        QVERIFY(std::abs(blendedShade(0.2,a,0.5,0)-(0.2+a)*0.5)<1e-12);
    }
    void eightSampleFootprint() {
        QVector<QPoint> calls;
        const auto sample=[&](int x,int y){calls.push_back({x,y});return 0.0;};
        TerrainRenderContract::derivedShade(4.75/16,6.75/16,QSize(16,16),QSize(1350,675),0,sample);
        const QVector<QPoint> left{{4,6},{5,6},{4,7},{5,7},{3,6},{3,7},{4,5},{5,5}};
        QCOMPARE(calls,left);
        calls.clear();
        TerrainRenderContract::derivedShade(5.25/16,7.25/16,QSize(16,16),QSize(1350,675),0,sample);
        const QVector<QPoint> right{{4,6},{5,6},{4,7},{5,7},{6,6},{6,7},{4,8},{5,8}};
        QCOMPARE(calls,right);
    }
    void maskReadinessRequiresExactCurrentFrame() {
        using namespace TerrainRenderContract;
        const MaskStamp request{7,11,QSizeF(800,600),QSize(1600,1200)};
        QVERIFY(maskCurrent(true,true,request,request));
        QVERIFY(!maskCurrent(false,true,request,request)); // Submitted geometry only.
        QVERIFY(!maskCurrent(true,false,request,request)); // Missing provider/texture.
        auto old=request;old.viewRevision=6;QVERIFY(!maskCurrent(true,true,old,request));
        old=request;old.sceneRevision=10;QVERIFY(!maskCurrent(true,true,old,request));
        old=request;old.logicalSize=QSizeF(801,600);QVERIFY(!maskCurrent(true,true,old,request));
        old=request;old.textureSize=QSize(800,600);QVERIFY(!maskCurrent(true,true,old,request));
        old=request;old.textureSize={};QVERIFY(!maskCurrent(true,true,old,old));
        old=request;old.logicalSize={};QVERIFY(!maskCurrent(true,true,old,old));
    }
    void rasterUsesOpaqueOriginalChannels() {
        QImage raw(2,1,QImage::Format_RGBA8888);
        raw.setPixelColor(0,0,QColor(255,0,0,64));
        raw.setPixelColor(1,0,QColor(10,90,240,128));
        const auto original=raw.copy();
        const auto color=TerrainRenderContract::rasterDisplayImage(raw,false);
        const auto gray=TerrainRenderContract::rasterDisplayImage(raw,true);
        QCOMPARE(color.format(),QImage::Format_RGBX8888);
        QCOMPARE(gray.format(),QImage::Format_RGBX8888);
        QCOMPARE(color.pixelColor(0,0),QColor(255,0,0,255));
        QCOMPARE(gray.pixelColor(0,0),QColor(64,64,64,255));
        QCOMPARE(color.pixelColor(1,0),QColor(10,90,240,255));
        QCOMPARE(gray.pixelColor(1,0),QColor(128,128,128,255));
        QCOMPARE(raw,original); // Source alpha remains data and is never overwritten.
        QVERIFY(color.constBits()!=raw.constBits());
        QCOMPARE(color.sizeInBytes(),qsizetype(8));
        QVERIFY(TerrainRenderContract::rasterDisplayImage({},false).isNull());
    }
};
QTEST_GUILESS_MAIN(TerrainRenderContractTests)
#include "terrain_render_contract_tests.moc"

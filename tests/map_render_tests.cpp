#include "maprenderitem.h"
#include <QtTest>
#include <QImage>
#include <QPainter>
#include <hydroloadscheduler.h>

class MapRenderTests:public QObject {
    Q_OBJECT
private slots:
    void builtinHydroPaintsWidthAndLakeHoleWithoutDocumentGeometry(){
        MapRenderItem item;item.setWidth(64);item.setHeight(64);item.setMapScale(10);
        item.setHydroProjection({{"cosLatitude",1.},{"minX",-1.},{"maxLatitude",4.}});
        item.setHydroStyle({{"riversVisible",true},{"lakesVisible",true},
            {"riverColor","#ff0000"},{"lakeColor","#0000ff"},{"riverOpacity",1.},{"lakeOpacity",1.}});
        auto frame=std::make_shared<HydroRuntimeFrame>();
        pandoeditor::HydroLakeShape lake;lake.fid=3;lake.logicalFid=3;
        lake.polygons={{{{0,0},{3000000,0},{3000000,3000000},{0,3000000},{0,0}},
                        {{1000000,1000000},{1000000,2000000},{2000000,2000000},{2000000,1000000},{1000000,1000000}}}};
        frame->packet.lakes.push_back(lake);
        frame->packet.rivers.push_back({1,1,{0,-1000000},{3000000,-1000000},2,10,false});
        item.setHydroFrame(frame);
        QImage image(64,64,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);
        QPainter painter(&image);item.paint(&painter);painter.end();
        QCOMPARE(image.pixelColor(15,15),QColor("#0000ff"));
        QCOMPARE(image.pixelColor(25,25),QColor(Qt::white));
        QVERIFY(image.pixelColor(38,46).red()>200);
        QCOMPARE(image.pixelColor(12,46),QColor(Qt::white));
        item.setHydroStyle({{"riversVisible",false},{"lakesVisible",false}});
        image.fill(Qt::white);QPainter hiddenPainter(&image);item.paint(&hiddenPainter);hiddenPainter.end();
        QCOMPARE(image.pixelColor(15,15),QColor(Qt::white));
        QCOMPARE(image.pixelColor(25,50),QColor(Qt::white));
    }
    void pointAndOpenLineDoNotBecomePolygonFills(){
        MapRenderItem item;item.setWidth(40);item.setHeight(40);item.setMapScale(1);
        item.setPaths({QVariantMap{{"countryId","river"},{"geometryType","LineString"},{"path","M5 5 L30 5 L30 30"}},
            QVariantMap{{"countryId","place"},{"geometryType","Point"},{"points",QVariantList{QVariantMap{{"x",10.},{"y",30.}}}}}});
        item.setVisuals({{"river",QVariantMap{{"visible",true},{"color","#0000ff"},{"boundary",false}}},
            {"place",QVariantMap{{"visible",true},{"color","#ff0000"},{"boundary",false}}}});
        QImage image(40,40,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);QPainter painter(&image);item.paint(&painter);painter.end();
        QCOMPARE(image.pixelColor(20,12),QColor(Qt::white));QVERIFY(image.pixelColor(20,5).blue()>200);QVERIFY(image.pixelColor(20,5).red()<100);
        QVERIFY(image.pixelColor(10,30).red()>200);QVERIFY(image.pixelColor(10,30).blue()<100);
    }
    void multiplyAndSelectionBoundaryAreActuallyPainted(){
        MapRenderItem item;item.setWidth(40);item.setHeight(30);item.setOriginX(0);item.setOriginY(0);item.setMapScale(1);
        item.setPaths({QVariantMap{{"countryId","red"},{"path","M2 2 L22 2 L22 22 L2 22 L2 2 Z"}},QVariantMap{{"countryId","blue"},{"path","M12 2 L32 2 L32 22 L12 22 L12 2 Z"}}});
        item.setVisuals({{"red",QVariantMap{{"visible",true},{"color","#ff0000"},{"opacity",1.},{"rank",0.},{"blendMode","normal"},{"boundary",true},{"kind","country"}}},{"blue",QVariantMap{{"visible",true},{"color","#0000ff"},{"opacity",1.},{"rank",1.},{"blendMode","multiply"},{"boundary",true},{"kind","subunit"}}}});
        item.setSelectedPaths({QVariantMap{{"countryId","red"},{"path","M2 2 L22 2 L22 22 L2 22 L2 2 Z"}}});item.setPrimaryId("red");
        QImage image(40,30,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);QPainter painter(&image);item.paint(&painter);painter.end();
        const auto overlap=image.pixelColor(16,10);QVERIFY(overlap.red()<20&&overlap.green()<20&&overlap.blue()<20);
        QVERIFY(image.pixelColor(2,10).blue()>40); // selected outline is dark blue, not a fill-only edge
    }
};
QTEST_MAIN(MapRenderTests)
#include "map_render_tests.moc"

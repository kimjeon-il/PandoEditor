#include "maprenderitem.h"
#include <QtTest>
#include <QImage>
#include <QPainter>
#include <QQuickWindow>
#include <hydroloadscheduler.h>
#include <pandoeditor/maprenderorder.h>
#include <tuple>
#include <vector>

class MapRenderTests:public QObject {
    Q_OBJECT
private slots:
    void viewportChangesRepaintCachedMapTexture() {
        QQuickWindow window;window.resize(48,48);window.setColor(Qt::white);
        MapRenderItem item(window.contentItem());item.setWidth(48);item.setHeight(48);
        item.setPaths({QVariantMap{{"countryId","generic"},{"path","M5 5 L15 5 L15 15 L5 15 Z"}}});
        item.setVisuals({{"generic",QVariantMap{{"visible",true},{"color","#ff0000"},{"boundary",false}}}});
        window.show();
        auto colorAt=[&](int x,int y){return window.grabWindow().pixelColor(x,y);};
        QTRY_COMPARE_WITH_TIMEOUT(colorAt(10,10),QColor("#ff0000"),2000);
        item.setOriginX(20);
        QTRY_COMPARE_WITH_TIMEOUT(colorAt(30,10),QColor("#ff0000"),2000);
        QCOMPARE(colorAt(10,10),QColor(Qt::white));
        item.setOriginY(20);
        QTRY_COMPARE_WITH_TIMEOUT(colorAt(30,30),QColor("#ff0000"),2000);
        QCOMPARE(colorAt(30,10),QColor(Qt::white));
        item.setOriginX(0);item.setOriginY(0);item.setMapScale(2);
        QTRY_COMPARE_WITH_TIMEOUT(colorAt(25,25),QColor("#ff0000"),2000);
        QCOMPARE(colorAt(10,10),QColor("#ff0000"));
        window.close();
    }
    void territorialChildrenOwnTheirPixelsAboveCountry(){
        using namespace pandoeditor;
        ProjectDocument document;
        for(const auto& [id,kind]:std::vector<std::pair<std::string,UnitKind>>{
            {"country",UnitKind::Country},{"subunit",UnitKind::Subunit},{"region",UnitKind::Region}}){
            TerritorialUnit unit;unit.id=id;unit.kind=kind;document.units.push_back(unit);
        }
        MapRenderItem item;item.setWidth(40);item.setHeight(40);
        QVariantList paths;QVariantMap visuals;
        for(const auto& [id,color,shape]:std::vector<std::tuple<QString,QString,QString>>{
            {"region","#0000ff","M15 15 L25 15 L25 25 L15 25 Z"},
            {"country","#ff0000","M5 5 L35 5 L35 35 L5 35 Z"},
            {"subunit","#00ff00","M10 10 L30 10 L30 30 L10 30 Z"}}){
            const auto order=mapRenderOrder(document,territorialRef(id.toStdString()),RenderPrimitiveRole::Fill);
            paths.append(QVariantMap{{"countryId",id},{"path",shape}});
            visuals[id]=QVariantMap{{"visible",true},{"color",color},{"boundary",false},
                {"drawFillPass",order.pass},{"drawGroup",order.group},{"drawObject",order.object}};
        }
        item.setPaths(paths);item.setVisuals(visuals);
        QImage image(40,40,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);
        QPainter painter(&image);item.paint(&painter);painter.end();
        QCOMPARE(image.pixelColor(7,7),QColor("#ff0000"));
        QCOMPARE(image.pixelColor(12,12),QColor("#00ff00"));
        QCOMPARE(image.pixelColor(20,20),QColor("#0000ff"));
    }
    void overlayPairsFollowWebDrawGroups_data(){
        QTest::addColumn<QString>("left");QTest::addColumn<QString>("right");
        QTest::addColumn<QString>("top");
        QTest::newRow("religion-ethnicity")<<"religion"<<"ethnicity"<<"ethnicity";
        QTest::newRow("ethnicity-language")<<"ethnicity"<<"language"<<"language";
        QTest::newRow("language-subunit")<<"language"<<"subunit"<<"language";
        QTest::newRow("subunit-region")<<"subunit"<<"region"<<"region";
        QTest::newRow("region-generic")<<"region"<<"generic"<<"generic";
    }
    void overlayPairsFollowWebDrawGroups(){
        QFETCH(QString,left);QFETCH(QString,right);QFETCH(QString,top);
        using namespace pandoeditor;
        ProjectDocument document;
        for(const auto& [id,kind]:std::vector<std::pair<std::string,UnitKind>>{
            {"subunit",UnitKind::Subunit},{"region",UnitKind::Region}}){
            TerritorialUnit unit;unit.id=id;unit.kind=kind;document.units.push_back(unit);
        }
        for(const auto& type:{"religion","ethnicity","language"}){
            DistributionLayer layer;layer.id=type;layer.type=type;document.distributionLayers.push_back(layer);
            DistributionEntry entry;entry.id=type;entry.layerId=type;document.distributionEntries.push_back(entry);
        }
        auto ref=[](const QString& name)->ObjectRef{
            const auto id=name.toStdString();
            return {name=="subunit"||name=="region"?"territorial":
                name=="generic"?"generic":"distributionEntry",id};
        };
        auto visual=[&](const QString& name,const QString& color){
            const auto order=mapRenderOrder(document,ref(name),RenderPrimitiveRole::Fill);
            return QVariantMap{{"visible",true},{"color",color},{"boundary",false},
                {"drawFillPass",order.pass},{"drawGroup",order.group},{"drawObject",order.object}};
        };
        MapRenderItem item;item.setWidth(40);item.setHeight(40);
        const auto shape=QStringLiteral("M5 5 L35 5 L35 35 L5 35 Z");
        item.setPaths({QVariantMap{{"countryId",left},{"path",shape}},
            QVariantMap{{"countryId",right},{"path",shape}}});
        item.setVisuals({{left,visual(left,"#ff0000")},{right,visual(right,"#0000ff")}});
        QImage image(40,40,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);
        QPainter painter(&image);item.paint(&painter);painter.end();
        QCOMPARE(image.pixelColor(20,20),QColor(top==left?"#ff0000":"#0000ff"));
    }
    void webPassesPlaceLakeAboveCountryAndStrokeAboveLake(){
        MapRenderItem item;item.setWidth(40);item.setHeight(40);item.setMapScale(1);
        item.setHydroProjection({{"cosLatitude",1.},{"minX",0.},{"maxLatitude",40.}});
        item.setPaths({QVariantMap{{"countryId","country"},{"path","M5 5 L35 5 L35 35 L5 35 Z"}},
            QVariantMap{{"countryId","overlay"},{"geometryType","LineString"},{"path","M10 20 L30 20"}}});
        item.setVisuals({{"country",QVariantMap{{"visible",true},{"color","#ff0000"},{"boundary",false},{"drawFillPass",10}}},
            {"overlay",QVariantMap{{"visible",true},{"color","#00ff00"},{"boundary",false},{"drawLinePass",60}}}});
        auto frame=std::make_shared<HydroRuntimeFrame>();
        frame->packet.lakes.push_back({1,1,{{{{10000000,10000000},{30000000,10000000},
            {30000000,30000000},{10000000,30000000},{10000000,10000000}}}}});
        frame->packet.rivers.push_back({2,2,{10000000,15000000},{30000000,15000000},3,3,false});
        item.setHydroFrame(frame);item.setHydroStyle({{"lakesVisible",true},{"riversVisible",true},
            {"lakeColor","#0000ff"},{"riverColor","#ffff00"}});
        QImage image(40,40,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);
        QPainter painter(&image);item.paint(&painter);painter.end();
        QCOMPARE(image.pixelColor(15,15),QColor("#0000ff"));
        QCOMPARE(image.pixelColor(15,20),QColor("#00ff00"));
        QCOMPARE(image.pixelColor(20,25),QColor("#ffff00"));
        QCOMPARE(image.pixelColor(7,7),QColor("#ff0000"));
    }
    void hydroDrawsAboveDistributionFillAndPointsAboveGenericFill(){
        MapRenderItem item;item.setWidth(40);item.setHeight(40);item.setMapScale(1);
        item.setHydroProjection({{"cosLatitude",1.},{"minX",0.},{"maxLatitude",40.}});
        item.setPaths({QVariantMap{{"countryId","religion"},{"path","M0 0 L39 0 L39 39 L0 39 Z"}},
            QVariantMap{{"countryId","generic"},{"path","M0 0 L39 0 L39 39 L0 39 Z"}},
            QVariantMap{{"countryId","place"},{"geometryType","Point"},
                {"points",QVariantList{QVariantMap{{"x",20.},{"y",20.}}}}}});
        item.setVisuals({{"religion",QVariantMap{{"visible",true},{"color","#00ff00"},
                {"drawFillPass",20},{"drawGroup",0},{"boundary",false}}},
            {"generic",QVariantMap{{"visible",true},{"color","#ff0000"},
                {"drawFillPass",20},{"drawGroup",5},{"boundary",false}}},
            {"place",QVariantMap{{"visible",true},{"color","#ffff00"},
                {"drawLinePass",70},{"boundary",false}}}});
        auto frame=std::make_shared<HydroRuntimeFrame>();
        frame->packet.lakes.push_back({4,4,{{{{10000000,10000000},{30000000,10000000},
            {30000000,30000000},{10000000,30000000},{10000000,10000000}}}}});
        item.setHydroFrame(frame);item.setHydroStyle({{"lakesVisible",true},{"riversVisible",false},
            {"lakeBoundaryVisible",false},{"lakeColor","#0000ff"}});
        QImage image(40,40,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);
        QPainter painter(&image);item.paint(&painter);painter.end();
        QCOMPARE(image.pixelColor(15,15),QColor("#0000ff"));
        QCOMPARE(image.pixelColor(20,20),QColor("#ffff00"));
        QCOMPARE(image.pixelColor(5,5),QColor("#ff0000"));
    }
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
        item.setHydroStyle({{"riversVisible",true},{"lakesVisible",true},
            {"riverColor","#ff0000"},{"lakeColor","#0000ff"},{"riverOpacity",.5},{"lakeOpacity",.5},
            {"lakeBoundaryVisible",false}});
        image.fill(Qt::white);QPainter translucent(&image);item.paint(&translucent);translucent.end();
        QVERIFY(image.pixelColor(15,15).red()>120&&image.pixelColor(15,15).red()<140);
        const auto translucentRiver=image.pixelColor(30,50);
        QVERIFY2(translucentRiver.green()>120&&translucentRiver.green()<140,
                 qPrintable(translucentRiver.name()));
        item.setHiddenHydroIds({QStringLiteral("3")});
        image.fill(Qt::white);QPainter hiddenLake(&image);item.paint(&hiddenLake);hiddenLake.end();
        QCOMPARE(image.pixelColor(15,15),QColor(Qt::white));
        item.setHiddenHydroIds({});
        item.setHydroStyle({{"riversVisible",false},{"lakesVisible",false}});
        image.fill(Qt::white);QPainter hiddenPainter(&image);item.paint(&hiddenPainter);hiddenPainter.end();
        QCOMPARE(image.pixelColor(15,15),QColor(Qt::white));
        QCOMPARE(image.pixelColor(25,50),QColor(Qt::white));
    }
    void overlappingMultiPolygonPartsRemainFilled(){
        MapRenderItem item;item.setWidth(40);item.setHeight(40);item.setMapScale(10);
        item.setHydroProjection({{"cosLatitude",1.},{"minX",0.},{"maxLatitude",4.}});
        item.setHydroStyle({{"lakesVisible",true},{"riversVisible",false},
            {"lakeColor","#0000ff"},{"lakeBoundaryVisible",false}});
        auto frame=std::make_shared<HydroRuntimeFrame>();
        pandoeditor::HydroLakeShape lake;lake.fid=9;lake.logicalFid=9;
        lake.polygons={{{{0,0},{2000000,0},{2000000,2000000},{0,2000000},{0,0}}},
                       {{{1000000,1000000},{3000000,1000000},{3000000,3000000},
                         {1000000,3000000},{1000000,1000000}}}};
        frame->packet.lakes.push_back(lake);item.setHydroFrame(frame);
        QImage image(40,40,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);
        QPainter painter(&image);item.paint(&painter);painter.end();
        QCOMPARE(image.pixelColor(15,25),QColor("#0000ff"));
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

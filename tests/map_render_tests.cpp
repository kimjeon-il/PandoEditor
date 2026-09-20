#include "maprenderitem.h"
#include <QtTest>
#include <QImage>
#include <QPainter>

class MapRenderTests:public QObject {
    Q_OBJECT
private slots:
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

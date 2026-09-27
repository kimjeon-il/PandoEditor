#include "maprenderitem.h"
#include "renderpacket.h"
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <stdexcept>

int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);
    MapRenderItem item;item.setWidth(100);item.setHeight(100);
    pandoeditor::Geometry geometry{"Polygon",{},{},{{
        {{-5,-5},{5,-5},{5,5},{-5,5},{-5,-5}}
    }}};
    PolygonDrawPacket polygon;polygon.object={"territorial","TEST"};
    polygon.style.color=0xff0000;polygon.geometryPacket=makePolygonGeometryPacket(geometry);
    auto scene=std::make_shared<RenderScene>();scene->revision=1;
    scene->polygons.push_back(polygon);
    scene->drawSequence.push_back({PrimitiveKind::Polygon,0,{0,0,0}});
    MapViewState view;view.viewportWidth=100;view.viewportHeight=100;
    view.scale=100;view.translateX=50;view.translateY=50;
    item.setSceneSnapshot(scene,view);
    QImage image(100,100,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
    QPainter painter(&image);item.paint(&painter);painter.end();
    if(image.pixelColor(50,50).red()<200||image.pixelColor(0,0).alpha()!=0)
        throw std::runtime_error("CPU typed scene adapter paint");
}

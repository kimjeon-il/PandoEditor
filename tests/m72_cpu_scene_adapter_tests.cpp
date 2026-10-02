#include "maprenderitem.h"
#include <pandoeditor/map/renderpacket.h>
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

    // Preview world meshes are submitted outside drawSequence by both backends.
    auto mesh=std::make_shared<CountryBaseMesh>();
    mesh->preview=true;
    mesh->positionsMicrodegrees={
        -5000000,-5000000,
         5000000,-5000000,
         5000000, 5000000,
        -5000000, 5000000
    };
    mesh->countryIndices={0,0,0,0};
    mesh->triangleIndices={0,1,2,0,2,3};
    mesh->lineIndices={0,1,1,2,2,3,3,0};
    mesh->countryTriangleRanges={0,6};
    mesh->countryBoundaryRanges={0,8};
    auto base=std::make_shared<WorldBaseFrame>();
    base->mesh=mesh;base->ranges.push_back({"TEST","TEST","world-country-TEST"});
    auto preview=std::make_shared<RenderScene>();
    preview->revision=2;preview->worldBase=base;
    WorldCountryDraw country;country.id="TEST";country.fill.color=0x00ff00;
    country.boundary.color=0x006600;country.boundary.width=1;
    preview->worldCountries.push_back(country);
    preview->worldPlan.worldOffsets={0};
    preview->worldPlan.fills.visible={true};
    preview->worldPlan.strokes.visible={true};
    item.setSceneSnapshot(preview,view);
    image.fill(Qt::transparent);
    QPainter previewPainter(&image);item.paint(&previewPainter);previewPainter.end();
    if(image.pixelColor(50,50).green()<200)
        throw std::runtime_error("CPU preview world base path");
}

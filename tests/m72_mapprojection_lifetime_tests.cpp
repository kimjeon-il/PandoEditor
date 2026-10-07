#include "territorial_fixture.h"
#include "mapprojection.h"
#include <pandoeditor/map/editcoordinates.h>
#include <pandoeditor/map/projectionengine.h>
#include <QCoreApplication>
#include <cmath>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    pandoeditor::ProjectDocument document;document.documentId="lifetime-test";
    document.geometries.insert({"country",1},{"Polygon",{},{},{{
        {{0,0},{2,0},{2,2},{0,2},{0,0}}
    }}});
    appendTerritory(document,{"C","Country",{},pandoeditor::UnitKind::General,false},{"country",1});
    document.presentation.userLayers={{"custom","Custom"}};
    document.presentation.membership[pandoeditor::territorialRef("C")]="custom";
    document.presentation.objectStyles[pandoeditor::territorialRef("C")]={0x556677,1};
    document.geometries.insert({"point",1},{"Point",{{10,10}},{},{}});
    document.genericFeatures.push_back({"P","Point",{}, {"point",1}});
    pandoeditor::validateDocument(document);
    MapProjection projection;projection.rebuild(document);
    if(projection.paths.size()!=2||projection.width<=0||projection.height<=0)
        throw std::runtime_error("bounds-only view path or geometry lifetime");

    pandoeditor::ProjectDocument regional;regional.documentId="regional-projection";
    regional.geometries.insert({"regional",1},{"Polygon",{},{},{{{{12,50},{16,50},{16,54},{12,54},{12,50}}}}});
    appendTerritory(regional,{"R","Regional",{},pandoeditor::UnitKind::General,false},{"regional",1});
    regional.presentation.objectStyles[pandoeditor::territorialRef("R")]={0x556677,1};
    projection.rebuild(regional);
    MapCamera camera;camera.setMetrics({projection.width,projection.height,projection.cosLatitudeValue(),projection.minXValue(),projection.maxLatitudeValue()});
    MapViewState view;view.mode=ProjectionMode::Flat;view.viewportWidth=1920;view.viewportHeight=929;
    view.scale=5000;view.translateX=960;view.translateY=464.5;view.centerLongitude=14;view.centerLatitude=52;
    if(!camera.adoptView(view))throw std::runtime_error("regional camera rejected");
    // Fixed web uses d3.geo.equirectangular for both geographic rendering and editing.
    // Independent formula checks the actual local-edit mapping against that contract.
    for(const auto geographic:{pandoeditor::Point{14,52},pandoeditor::Point{12,50},pandoeditor::Point{16,54}}) {
        const auto actual=pandoeditor::map::editMapToScreen(projection.project(geographic),camera.display());
        const double expectedX=960+5000*(geographic.x-14)*3.14159265358979323846/180;
        const double expectedY=464.5-5000*(geographic.y-52)*3.14159265358979323846/180;
        if(std::abs(actual.x-expectedX)>1e-8||std::abs(actual.y-expectedY)>1e-8)
            throw std::runtime_error("regional edit pixels differ from fixed-web equirectangular rendering");
    }
    std::cout<<"4 projection checks processed; 0 failures\n";
}

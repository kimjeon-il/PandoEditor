#include <pandoeditor/map/mapcamera.h>
#include <pandoeditor/map/projectionengine.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool near(double a,double b,double eps=1e-8){return std::abs(a-b)<=eps;}

MapCamera makeCamera() {
    MapCamera camera;
    require(camera.setMetrics({360,180,1,-180,90}),"install world metrics");
    require(camera.resize(800,600,1),"initial resize");
    return camera;
}

void firstRunAndIndependentProjectionState() {
    auto camera=makeCamera();
    require(camera.mode()==ProjectionMode::Globe,"first run is globe");
    require(near(camera.view().scale,264),"globe fits viewport");
    auto globe=camera.view();globe.centerLongitude=35;globe.centerLatitude=12;globe.scale=240;
    require(camera.adoptView(globe),"adopt globe");
    require(camera.setProjectionMode(ProjectionMode::Flat),"switch flat");
    auto flat=camera.view();flat.viewportWidth=900;flat.viewportHeight=500;flat.scale=180;
    flat.translateX=450;flat.translateY=250;flat.centerLongitude=-20;flat.centerLatitude=4;
    require(camera.adoptView(flat),"adopt flat");
    require(camera.setProjectionMode(ProjectionMode::Globe),"switch back globe");
    require(near(camera.view().centerLongitude,35)&&near(camera.view().scale,240),
            "globe camera retained");
    require(camera.setProjectionMode(ProjectionMode::Flat),"switch back flat");
    require(near(camera.view().centerLongitude,-20)&&near(camera.view().viewportWidth,900),
            "flat camera retained");
}

void flatZoomKeepsCursorAnchor() {
    auto camera=makeCamera();
    camera.setProjectionMode(ProjectionMode::Flat);
    const auto before=camera.display();
    const double x=213,y=177;
    const double mapX=(x-before.originX)/before.mapScale;
    const double mapY=(y-before.originY)/before.mapScale;
    require(camera.zoomAt(2,x,y),"zoom");
    const auto after=camera.display();
    require(near((x-after.originX)/after.mapScale,mapX,1e-7),"x anchor retained");
    require(near((y-after.originY)/after.mapScale,mapY,1e-7),"y anchor retained");
    require(near(after.flatZoom,before.flatZoom*2),"zoom doubled");
}

void restoredFlatCenterKeepsGeographicZoomAnchor() {
    for(const auto center:{pandoeditor::Point{15,50.5},pandoeditor::Point{-40,-25},pandoeditor::Point{179,20}}) {
        auto camera=makeCamera();camera.resize(1920,929,1);camera.setProjectionMode(ProjectionMode::Flat);
        auto restored=camera.view();restored.centerLongitude=center.x;restored.centerLatitude=center.y;
        restored.scale=5000;restored.translateX=960;restored.translateY=464.5;
        require(camera.adoptView(restored),"restore nonzero geographic center");
        const auto before=unprojectFlat(960,464.5,camera.view());
        require(camera.zoomAt(1.1,960,464.5),"zoom restored geographic center");
        const auto after=unprojectFlat(960,464.5,camera.view());
        require(near(before.x,after.x)&&near(before.y,after.y),"restored geographic zoom anchor retained");
    }
}

void panFitAndFocusAreEngineOwned() {
    auto camera=makeCamera();
    camera.setProjectionMode(ProjectionMode::Flat);
    camera.beginPan();
    require(camera.panFromGesture(30,-20),"pan");
    camera.endPan();
    auto moved=camera.display();
    require(near(moved.panX,30)&&near(moved.panY,-20),"pan state");
    require(camera.fit(),"fit");
    auto fitted=camera.display();
    require(near(fitted.flatZoom,1)&&near(fitted.panX,0)&&near(fitted.panY,0),"flat fit reset");
    require(camera.focusRect(100,50,20,10,10),"focus rect");
    auto focused=camera.display();
    require(focused.flatZoom>=1.25&&focused.flatZoom<=10,"focus zoom bounds");
    require(near(focused.originX+(100+10)*focused.mapScale,400,1e-6),"focus x centered");
    require(near(focused.originY+(50+5)*focused.mapScale,300,1e-6),"focus y centered");
}

void resizePreservesCameraIntent() {
    auto camera=makeCamera();
    const auto globeZoom=camera.display().globeZoom;
    camera.setProjectionMode(ProjectionMode::Flat);
    camera.zoomAt(1.5,400,300);
    camera.beginPan();camera.panFromGesture(25,10);camera.endPan();
    const auto before=camera.display();
    require(camera.resize(1000,700,2),"resize");
    const auto after=camera.display();
    require(near(after.flatZoom,before.flatZoom),"flat relative zoom retained");
    require(near(after.panX,before.panX)&&near(after.panY,before.panY),"flat pan retained");
    camera.setProjectionMode(ProjectionMode::Globe);
    require(near(camera.display().globeZoom,globeZoom),"globe relative zoom retained");
    require(near(camera.view().translateX,500)&&near(camera.view().translateY,350),"globe recentered");
    require(near(camera.view().devicePixelRatio,2),"dpr retained");
}
}

int main(){
    firstRunAndIndependentProjectionState();
    flatZoomKeepsCursorAnchor();
    restoredFlatCenterKeepsGeographicZoomAnchor();
    panFitAndFocusAreEngineOwned();
    resizePreservesCameraIntent();
    std::cout<<"5 camera groups processed; 3 restored-center cases; 0 failures\n";
}

#include <pandoeditor/map/viewportresourcescheduler.h>
#include <cmath>
#include <stdexcept>

namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool near(double a,double b,double eps=1e-8){return std::abs(a-b)<=eps;}

MapCamera makeCamera() {
    MapCamera camera;
    require(camera.setMetrics({360,180,1,-180,90}),"metrics");
    require(camera.resize(800,600,1),"resize");
    return camera;
}

void coalescesIdleViewportChanges() {
    auto camera=makeCamera();
    camera.setProjectionMode(ProjectionMode::Flat);
    ViewportResourceScheduler scheduler;
    require(scheduler.noteViewport(camera.display(),camera.metrics()),"arm first settle");
    require(camera.zoomAt(1.5,400,300),"zoom");
    require(scheduler.noteViewport(camera.display(),camera.metrics()),"restart settle");
    auto request=scheduler.takeReady();
    require(request&&request->generation==1,"one coalesced request");
    require(request->resources==ViewportResourceKind::All,"both resources");
    require(near(request->view.scale,camera.view().scale),"latest view wins");
    require(!scheduler.takeReady(),"nothing duplicated");
}

void defersEveryViewportQueryDuringInteraction() {
    auto camera=makeCamera();
    ViewportResourceScheduler scheduler;
    scheduler.noteViewport(camera.display(),camera.metrics());
    require(scheduler.takeReady().has_value(),"initial settled request");
    require(scheduler.beginInteraction(),"begin outer interaction");
    camera.zoomAt(1.2,400,300);
    require(!scheduler.noteViewport(camera.display(),camera.metrics()),"interaction does not arm timer");
    camera.zoomAt(1.2,400,300);
    require(!scheduler.noteViewport(camera.display(),camera.metrics()),"second interaction update deferred");
    require(!scheduler.takeReady(),"no request while interacting");
    require(scheduler.endInteraction(),"settle becomes immediately ready");
    auto request=scheduler.takeReady();
    require(request&&request->generation==2,"one request after interaction");
    require(scheduler.stats().deferredUpdates>=2,"deferred updates counted");
}

void nestedInteractionsWaitForLastEnd() {
    auto camera=makeCamera();
    ViewportResourceScheduler scheduler;
    scheduler.noteViewport(camera.display(),camera.metrics());
    scheduler.takeReady();
    scheduler.beginInteraction();
    scheduler.beginInteraction();
    camera.zoomAt(1.1,400,300);
    scheduler.noteViewport(camera.display(),camera.metrics());
    require(!scheduler.endInteraction(),"inner end does not flush");
    require(!scheduler.takeReady(),"still blocked");
    require(scheduler.endInteraction(),"outer end flushes");
    require(scheduler.takeReady().has_value(),"request after outer end");
}

void labelsShareTheViewportSettleContract() {
    auto camera=makeCamera();
    ViewportResourceScheduler scheduler;
    require(scheduler.noteViewport(camera.display(),camera.metrics(),
                                   ViewportResourceKind::Labels),"label viewport arms settle");
    auto request=scheduler.takeReady();
    require(request&&request->resources==ViewportResourceKind::Labels,"label-only request");

    scheduler.beginInteraction();
    camera.zoomAt(1.2,400,300);
    require(!scheduler.noteViewport(camera.display(),camera.metrics(),
                                    ViewportResourceKind::Labels),
            "label layout deferred during interaction");
    require(!scheduler.takeReady(),"label request blocked during interaction");
    require(scheduler.endInteraction(),"label request ready at settle");
    request=scheduler.takeReady();
    require(request&&request->resources==ViewportResourceKind::Labels,
            "deferred label request emitted once");
}

void invalidationsMergeByResourceKind() {
    auto camera=makeCamera();
    ViewportResourceScheduler scheduler;
    scheduler.noteViewport(camera.display(),camera.metrics(),ViewportResourceKind::Terrain);
    auto terrain=scheduler.takeReady();
    require(terrain&&terrain->resources==ViewportResourceKind::Terrain,"terrain-only request");
    require(scheduler.invalidate(ViewportResourceKind::Hydro),"hydro invalidation arms settle");
    auto hydro=scheduler.takeReady();
    require(hydro&&hydro->resources==ViewportResourceKind::Hydro,"hydro-only request");
}

void flatHydroWindowMatchesLegacyViewportMath() {
    auto camera=makeCamera();
    camera.setProjectionMode(ProjectionMode::Flat);
    camera.zoomAt(2,400,300);
    camera.beginPan();camera.panFromGesture(30,-20);camera.endPan();
    const auto display=camera.display();
    const auto request=buildViewportResourceRequest(
        display,camera.metrics(),ViewportResourceKind::All,1);
    const double mapX=(display.view.viewportWidth/2-display.originX)/display.mapScale;
    const double mapY=(display.view.viewportHeight/2-display.originY)/display.mapScale;
    require(near(request.hydroWindow.longitude,mapX-180),"flat longitude");
    require(near(request.hydroWindow.latitude,90-mapY),"flat latitude");
    require(near(request.hydroWindow.scale,
        display.mapScale*180/3.14159265358979323846),"legacy flat hydro scale");
}

void globeHydroWindowUsesPublishedCamera() {
    auto camera=makeCamera();
    auto globe=camera.view();
    globe.centerLongitude=30;globe.centerLatitude=20;
    globe.rotationLongitude=5;globe.rotationLatitude=-2;
    globe.scale=250;
    camera.adoptView(globe);
    const auto request=buildViewportResourceRequest(
        camera.display(),camera.metrics(),ViewportResourceKind::Hydro,1);
    require(near(request.hydroWindow.longitude,35),"globe longitude");
    require(near(request.hydroWindow.latitude,18),"globe latitude");
    require(near(request.hydroWindow.scale,250),"globe scale");
}
}

int main() {
    coalescesIdleViewportChanges();
    defersEveryViewportQueryDuringInteraction();
    nestedInteractionsWaitForLastEnd();
    labelsShareTheViewportSettleContract();
    invalidationsMergeByResourceKind();
    flatHydroWindowMatchesLegacyViewportMath();
    globeHydroWindowUsesPublishedCamera();
}

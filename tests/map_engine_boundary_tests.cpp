#include <pandoeditor/map/countryculling.h>
#include <pandoeditor/map/builtinhydrochannel.h>
#include <pandoeditor/map/geometrypacketcache.h>
#include <pandoeditor/map/geometrysnap.h>
#include <pandoeditor/map/labelengine.h>
#include <pandoeditor/map/mapscenebuilder.h>
#include <pandoeditor/map/mapcamera.h>
#include <pandoeditor/map/mappicker.h>
#include <pandoeditor/map/mapviewstate.h>
#include <pandoeditor/map/projectionengine.h>
#include <pandoeditor/map/renderlod.h>
#include <pandoeditor/map/renderpacket.h>
#include <pandoeditor/map/renderquality.h>
#include <pandoeditor/map/renderscene.h>
#include <pandoeditor/map/scenepatch.h>
#include <pandoeditor/map/viewportresourcescheduler.h>

#include <cassert>

int main() {
    MapViewState view;
    view.viewportWidth=1280;
    view.viewportHeight=720;
    view.scale=1;
    view.devicePixelRatio=1;
    assert(validMapViewState(view));

    RenderScene scene;
    scene.revisions.view=view.revision;
    scene.worldPlan.worldOffsets={0};
    assert(scene.worldPlan.worldOffsets.size()==1);

    RenderQualityProfile quality;
    assert(quality.backgroundLod==RenderLod::High);

    pandoeditor::Project project;
    geometrysnap::Index snapIndex;
    geometrysnap::Request snapRequest;
    assert(snapIndex.prepareAndCollect(project.snapshot(),snapRequest).candidates.empty());
    return 0;
}

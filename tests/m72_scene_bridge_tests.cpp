#include "mapscenebridge.h"
#include <QCoreApplication>
#include <atomic>
#include <stdexcept>
#include <thread>

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    MapSceneBridge bridge;
    auto first=std::make_shared<RenderScene>();first->revision=1;
    bridge.publishScene(first);
    auto snapshot=bridge.sceneSnapshot();
    if(!snapshot||snapshot->revision!=1)throw std::runtime_error("scene publish");
    std::atomic<bool> consistent{true};
    std::thread reader([&] {
        for(int i=0;i<1000;++i) {
            auto scene=bridge.sceneSnapshot();
            auto view=bridge.viewState();
            if(!scene||(scene->revision!=1&&scene->revision!=2)||view.revision>1)
                consistent=false;
        }
    });
    auto next=std::make_shared<RenderScene>();next->revision=2;
    bridge.publishScene(next);
    auto view=bridge.viewState();view.centerLongitude=5;
    bridge.publishView(view);
    reader.join();
    if(!consistent||bridge.sceneSnapshot()->revision!=2||bridge.viewState().revision!=1)
        throw std::runtime_error("immutable snapshots across threads");
    const auto prepared=bridge.sceneSnapshot();
    const auto publications=bridge.scenePublicationCount();
    for(int i=0;i<12;++i) {
        auto camera=bridge.viewState();camera.centerLongitude+=1;camera.scale*=1.01;
        bridge.publishView(camera);
        const auto frame=bridge.frameSnapshot();
        if(frame->scene!=prepared||frame->path!=FrameUpdatePath::ViewOnly||
           frame->view.revision!=bridge.viewState().revision||
           bridge.scenePublicationCount()!=publications)
            throw std::runtime_error("camera frames retain immutable scene and coherent view");
    }
    auto duplicate=bridge.frameSnapshot();bridge.publishView(duplicate->view);
    if(bridge.frameSnapshot()!=duplicate)throw std::runtime_error("identical view publishes no frame");
    auto mesh=std::make_shared<CountryBaseMesh>();
    mesh->triangleIndices={0,1,2,3,4,5};mesh->countryTriangleRanges={0,3,3,3};
    mesh->countryBounds={-1000000,-1000000,1000000,1000000,119000000,-1000000,121000000,1000000};
    mesh->countryBoundsFlags={0,0};
    auto base=std::make_shared<WorldBaseFrame>();base->mesh=mesh;
    auto world=std::make_shared<RenderScene>();world->worldBase=base;world->revision=20;
    MapViewState camera;camera.viewportWidth=40;camera.viewportHeight=40;
    camera.scale=180./3.141592653589793;camera.translateX=20;camera.translateY=20;
    const auto culled=FramePipeline::compose(world,camera);
    const auto noCull=FramePipeline::compose(world,camera,culled,false);
    if(culled->worldPlan->fills.visible!=std::vector<bool>({true,false})||
       noCull->worldPlan->fills.visible!=std::vector<bool>({true,true}))
        throw std::runtime_error("no-cull diagnostic policy is frame-local");
}

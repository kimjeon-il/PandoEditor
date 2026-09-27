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
}

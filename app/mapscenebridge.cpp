#include "mapscenebridge.h"
#include <atomic>
#include <stdexcept>
#include <limits>

std::shared_ptr<const RenderScene> MapSceneBridge::sceneSnapshot() const {
    return std::atomic_load_explicit(&scene_,std::memory_order_acquire);
}
MapViewState MapSceneBridge::viewState() const {
    std::lock_guard<std::mutex> lock(viewMutex_);return view_;
}
void MapSceneBridge::publishScene(std::shared_ptr<const RenderScene> scene) {
    if(!scene)throw std::invalid_argument("cannot publish an empty scene");
    std::uint64_t revision;
    {
        std::lock_guard<std::mutex> lock(sceneMutex_);
        const auto previous=sceneSnapshot();
        if(previous&&scene->revision<=previous->revision) {
            if(previous->revision==std::numeric_limits<std::uint64_t>::max())
                throw std::overflow_error("scene publication revision overflow");
            auto replacement=std::make_shared<RenderScene>(*scene);
            replacement->revision=previous->revision+1;
            scene=std::move(replacement);
        }
        revision=scene->revision;
        std::atomic_store_explicit(&scene_,std::move(scene),std::memory_order_release);
    }
    emit sceneChanged(revision);
}
void MapSceneBridge::publishView(const MapViewState& view) {
    std::uint64_t revision;
    bool changed;
    {
        std::lock_guard<std::mutex> lock(viewMutex_);
        const auto before=view_.revision;
        view_=advanceViewRevision(view_,view);
        revision=view_.revision;
        changed=revision!=before;
    }
    if(changed)emit viewChanged(revision);
}

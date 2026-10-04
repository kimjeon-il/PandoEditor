#include "mapscenebridge.h"
#include <atomic>
#include <stdexcept>
#include <limits>

std::shared_ptr<const RenderScene> MapSceneBridge::sceneSnapshot() const {
    return frameSnapshot()->scene;
}
MapViewState MapSceneBridge::viewState() const {
    return frameSnapshot()->view;
}
std::shared_ptr<const MapFrame> MapSceneBridge::frameSnapshot() const {
    return std::atomic_load_explicit(&frame_,std::memory_order_acquire);
}
void MapSceneBridge::publishScene(std::shared_ptr<const RenderScene> scene) {
    if(!scene)throw std::invalid_argument("cannot publish an empty scene");
    std::uint64_t revision;
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        const auto oldFrame=frameSnapshot();
        const auto previous=oldFrame->scene;
        if(previous==scene)return;
        if(previous&&scene->revision<=previous->revision) {
            if(previous->revision==std::numeric_limits<std::uint64_t>::max())
                throw std::overflow_error("scene publication revision overflow");
            auto replacement=std::make_shared<RenderScene>(*scene);
            replacement->revision=previous->revision+1;
            scene=std::move(replacement);
        }
        revision=scene->revision;
        const auto frame=FramePipeline::compose(std::move(scene),oldFrame->view,oldFrame,cullingEnabled_);
        if(frame->path==FrameUpdatePath::InteractionOnly)++interactionFrames_;
        ++scenePublications_;
        std::atomic_store_explicit(&frame_,frame,std::memory_order_release);
    }
    emit sceneChanged(revision);
}
void MapSceneBridge::publishView(const MapViewState& view) {
    std::uint64_t revision;
    bool changed;
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        const auto previous=frameSnapshot();
        const auto next=advanceViewRevision(previous->view,view);
        revision=next.revision;
        changed=revision!=previous->view.revision;
        if(changed) {
            ++viewFrames_;
            std::atomic_store_explicit(&frame_,FramePipeline::compose(previous->scene,next,previous,cullingEnabled_),std::memory_order_release);
        }
    }
    if(changed)emit viewChanged(revision);
}

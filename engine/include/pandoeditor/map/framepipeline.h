#pragma once
#include <pandoeditor/map/countryculling.h>
#include <pandoeditor/map/renderscene.h>
#include <pandoeditor/map/projectionengine.h>
#include <algorithm>

enum class FrameUpdatePath { FullPreparation, InteractionOnly, ViewOnly, Unchanged };

// View culling belongs to a frame, not to immutable geometry/presentation preparation.
struct MapFrame {
    std::shared_ptr<const RenderScene> scene;
    MapViewState view;
    std::shared_ptr<const WorldRenderPlan> worldPlan;
    FrameUpdatePath path=FrameUpdatePath::Unchanged;
    bool cullingEnabled=true;
};

class FramePipeline {
public:
    static std::shared_ptr<const MapFrame> compose(
        std::shared_ptr<const RenderScene> scene,const MapViewState& view,
        const std::shared_ptr<const MapFrame>& previous={},bool cullingEnabled=true) {
        const bool sameView=previous&&
            advanceViewRevision(previous->view,view).revision==previous->view.revision;
        const bool samePolicy=previous&&previous->cullingEnabled==cullingEnabled;
        if(previous&&scene==previous->scene&&sameView&&samePolicy)return previous;
        auto frame=std::make_shared<MapFrame>();frame->scene=std::move(scene);frame->view=view;
        frame->cullingEnabled=cullingEnabled;
        const auto before=previous?previous->scene:nullptr;
        if(frame->scene==before)frame->path=FrameUpdatePath::ViewOnly;
        else if(frame->scene&&before&&frame->scene->preparationIdentity&&
            frame->scene->preparationIdentity==before->preparationIdentity&&
            frame->scene->worldBase==before->worldBase&&
            frame->scene->revisions.geometry==before->revisions.geometry&&
            frame->scene->revisions.presentation==before->revisions.presentation&&
            frame->scene->revisions.dataset==before->revisions.dataset)
            frame->path=FrameUpdatePath::InteractionOnly;
        else frame->path=FrameUpdatePath::FullPreparation;
        const auto base=frame->scene?frame->scene->worldBase:nullptr;
        const auto oldBase=before?before->worldBase:nullptr;
        if(previous&&previous->worldPlan&&base==oldBase&&sameView&&samePolicy)
            frame->worldPlan=previous->worldPlan;
        else {
            auto plan=std::make_shared<WorldRenderPlan>();
            if(base&&base->mesh)*plan=worldRenderPlanForView(*base->mesh,view);
            else plan->worldOffsets=visibleFlatWorldOffsets(view);
            if(!cullingEnabled) {
                std::fill(plan->fills.visible.begin(),plan->fills.visible.end(),true);
                std::fill(plan->strokes.visible.begin(),plan->strokes.visible.end(),true);
            }
            frame->worldPlan=std::move(plan);
        }
        return frame;
    }
};

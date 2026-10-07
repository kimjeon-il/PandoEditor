#pragma once
#include <pandoeditor/map/interactionstylepolicy.h>
#include <pandoeditor/map/mapviewstate.h>
#include <pandoeditor/map/renderpacket.h>

inline RenderStyle fixedInteractionStyle(mapstyle::InteractionRole role,
    const mapstyle::Options& options,const MapViewState& view) {
    const auto policy=mapstyle::interactionStroke(role,options,
        {view.viewportWidth,view.viewportHeight,view.scale,view.mode==ProjectionMode::Flat});
    RenderStyle result;result.color=policy.color;result.width=float(policy.width);
    result.alpha=float(policy.alpha);result.fillAlpha=float(policy.fillAlpha);
    result.dashOn=float(policy.dashOn);result.dashOff=float(policy.dashOff);return result;
}

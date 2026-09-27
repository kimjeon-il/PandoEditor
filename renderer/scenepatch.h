#pragma once
#include "renderscene.h"
#include <set>

// Keep immutable geometry packet ownership for unaffected objects while a
// replacement scene prepares only the changed object(s).
struct ScenePatchStats {
    std::size_t retainedPolygons=0,retainedStrokes=0,retainedPoints=0;
    std::size_t removedPackets=0;
};
ScenePatchStats appendUnchangedScenePackets(RenderScene& target,const RenderScene& previous,
                                             const std::set<pandoeditor::ObjectRef>& changed);

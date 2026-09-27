#include "renderscene.h"
#include <limits>
#include <stdexcept>

bool operator==(const SceneRevisions& a,const SceneRevisions& b) noexcept {
    return a.document==b.document&&a.geometry==b.geometry&&
        a.presentation==b.presentation&&a.selection==b.selection&&
        a.view==b.view&&a.dataset==b.dataset;
}

std::uint64_t nextSceneRevision(const std::shared_ptr<const RenderScene>& previous) {
    if(!previous)return 1;
    if(previous->revision==std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("render scene revision overflow");
    return previous->revision+1;
}

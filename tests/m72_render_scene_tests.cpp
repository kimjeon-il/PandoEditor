#include "renderscene.h"
#include <stdexcept>
#include <type_traits>
#include <limits>

namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
void sceneSnapshotIsImmutable() {
    static_assert(std::is_const_v<std::remove_reference_t<decltype(*std::declval<std::shared_ptr<const RenderScene>>())>>);
    RenderScene editable;
    editable.revision=1;editable.revisions.document=4;
    editable.polygons.push_back(PolygonDrawPacket{});
    std::shared_ptr<const RenderScene> snapshot=std::make_shared<RenderScene>(std::move(editable));
    require(snapshot->revision==1&&snapshot->polygons.size()==1,"immutable typed scene snapshot");
    require(nextSceneRevision(snapshot)==2&&nextSceneRevision({})==1,"scene revision advances");
    auto maxed=std::make_shared<RenderScene>();maxed->revision=std::numeric_limits<std::uint64_t>::max();
    bool overflow=false;try{nextSceneRevision(maxed);}catch(const std::overflow_error&){overflow=true;}
    require(overflow,"scene revision never wraps");
}
void allRevisionDomainsRemainSeparate() {
    SceneRevisions revisions;
    revisions.document=1;revisions.geometry=2;revisions.presentation=3;
    revisions.selection=4;revisions.view=5;revisions.dataset=6;
    require(revisions.document==1&&revisions.geometry==2&&revisions.presentation==3&&
        revisions.selection==4&&revisions.view==5&&revisions.dataset==6,"independent revision vector");
    RenderScene scene;scene.revisions=revisions;scene.revision=12;
    auto same=revisions;same.view++;
    require(!(same==revisions),"view revision is independent");
    scene.interaction.selected.push_back({"territorial","DEU"});
    require(scene.interaction.selected.front().id=="DEU","interaction uses typed refs");
}
}
int main(){sceneSnapshotIsImmutable();allRevisionDomainsRemainSeparate();}

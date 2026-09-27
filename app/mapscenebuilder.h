#pragma once
#include "geometrypacketcache.h"
#include "mapviewstate.h"
#include "renderscene.h"
#include "renderquality.h"
#include <pandoeditor/project.h>
#include <set>

class MapSceneBuilder {
public:
    explicit MapSceneBuilder(GeometryPacketCache& cache):cache_(cache){}
    void setWorldBase(std::shared_ptr<const WorldBaseFrame> base) {worldBase_=std::move(base);}
    void setQuality(RenderQualityProfile quality) {quality_=quality;}
    std::shared_ptr<const RenderScene> build(
        const pandoeditor::ProjectSnapshot& snapshot,const MapViewState& view,
        const InteractionRenderPacket& interaction,
        const std::shared_ptr<const RenderScene>& previous);
    std::shared_ptr<const RenderScene> buildPatch(
        const pandoeditor::ProjectSnapshot&,const MapViewState&,
        const InteractionRenderPacket&,const std::shared_ptr<const RenderScene>&,
        const std::set<pandoeditor::ObjectRef>& changed);
    // Pure document seam for structural tests and the M7.1 in-memory fixture loader.
    std::shared_ptr<const RenderScene> buildDocument(
        const pandoeditor::ProjectDocument& document,std::uint64_t documentRevision,
        const MapViewState& view,const InteractionRenderPacket& interaction,
        const std::shared_ptr<const RenderScene>& previous);
private:
    std::shared_ptr<const RenderScene> buildDocumentImpl(
        const pandoeditor::ProjectDocument&,std::uint64_t,const MapViewState&,
        const InteractionRenderPacket&,const std::shared_ptr<const RenderScene>&,
        const std::set<pandoeditor::ObjectRef>* changed);
    GeometryPacketCache& cache_;
    std::shared_ptr<const WorldBaseFrame> worldBase_;
    RenderQualityProfile quality_;
};

#pragma once
#include <pandoeditor/map/geometrypacketcache.h>
#include <pandoeditor/map/builtinhydrochannel.h>
#include <pandoeditor/map/mapviewstate.h>
#include <pandoeditor/map/renderscene.h>
#include <pandoeditor/map/renderquality.h>
#include <pandoeditor/project.h>
#include <set>

class MapSceneBuilder {
public:
    explicit MapSceneBuilder(GeometryPacketCache& cache):cache_(cache){}
    void setWorldBase(std::shared_ptr<const WorldBaseFrame> base) {worldBase_=std::move(base);}
    void setBuiltinHydro(std::shared_ptr<const BuiltinHydroRenderFrame> frame) {builtinHydro_=std::move(frame);}
    void setQuality(RenderQualityProfile quality) {quality_=quality;}
    std::uint64_t preparationCount() const {return preparations_;}
    std::uint64_t transientUpdateCount() const {return transientUpdates_;}
    std::uint64_t unchangedCount() const {return unchanged_;}
    std::uint64_t deltaUpdateCount() const {return deltaUpdates_;}
    std::uint64_t presentationUpdateCount() const {return presentationUpdates_;}
    std::shared_ptr<const RenderScene> refresh(
        const pandoeditor::ProjectSnapshot&,const MapViewState&,
        const InteractionRenderPacket&,const std::shared_ptr<const RenderScene>&,
        const pandoeditor::SceneDirtySet* dirty=nullptr);
    std::shared_ptr<const RenderScene> buildDelta(
        const pandoeditor::ProjectSnapshot&,const MapViewState&,
        const InteractionRenderPacket&,const std::shared_ptr<const RenderScene>&,
        const pandoeditor::SceneDirtySet&);
    bool canReusePreparation(const pandoeditor::ProjectSnapshot&,const MapViewState&,
                             const std::shared_ptr<const RenderScene>&) const;
    bool preparationMatchesView(const MapViewState&,const std::shared_ptr<const RenderScene>&) const;
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
    void remember(const pandoeditor::ProjectSnapshot&,const MapViewState&,
                  const std::shared_ptr<const RenderScene>&);
    std::shared_ptr<const RenderScene> buildDocumentImpl(
        const pandoeditor::ProjectDocument&,std::uint64_t,const MapViewState&,
        const InteractionRenderPacket&,const std::shared_ptr<const RenderScene>&,
        const std::set<pandoeditor::ObjectRef>* changed,
        const std::set<pandoeditor::ObjectRef>* geometryChanged=nullptr);
    GeometryPacketCache& cache_;
    std::shared_ptr<const WorldBaseFrame> worldBase_;
    std::shared_ptr<const BuiltinHydroRenderFrame> builtinHydro_;
    RenderQualityProfile quality_;
    // Retain the immutable snapshot: raw document-address reuse alone is not
    // a safe identity check after a project replacement/undo.
    std::optional<pandoeditor::ProjectSnapshot> preparedSnapshot_;
    std::weak_ptr<const RenderScene> preparedScene_;
    std::shared_ptr<const BuiltinHydroRenderFrame> preparedHydro_;
    ProjectionMode preparedMode_=ProjectionMode::Flat;
    RenderLod preparedLod_=RenderLod::High;
    std::uint64_t preparations_=0,transientUpdates_=0,unchanged_=0;
    std::uint64_t deltaUpdates_=0,presentationUpdates_=0;
};

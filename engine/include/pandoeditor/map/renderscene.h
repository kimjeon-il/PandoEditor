#pragma once
#include <pandoeditor/map/renderpacket.h>
#include <pandoeditor/map/countrymesh.h>
#include <pandoeditor/map/drawplan.h>
#include <pandoeditor/map/interactionstylepolicy.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

struct SceneRevisions {
    std::uint64_t document=0,geometry=0,presentation=0;
    std::uint64_t selection=0,view=0,dataset=0;
};

struct InteractionRenderPacket {
    std::vector<pandoeditor::ObjectRef> candidates;
    std::vector<pandoeditor::ObjectRef> selected;
    std::optional<pandoeditor::ObjectRef> primary,hover,editTarget;
    mapstyle::Options styleOptions;
};

struct SceneDrawRef {
    PrimitiveKind primitive=PrimitiveKind::Polygon;
    std::size_t index=0;
    pandoeditor::MapRenderOrder order;
    int layerOrder=-1;
    float layerOpacity=1;
};

struct WorldBaseRange {
    std::string sourceId,ownerId,geometryId;
    bool operator==(const WorldBaseRange& other) const noexcept {
        return sourceId==other.sourceId&&ownerId==other.ownerId&&geometryId==other.geometryId;
    }
};

struct WorldBaseFrame {
    std::shared_ptr<const CountryBaseMesh> mesh;
    // Precision and document readiness are independent: a render-only preview
    // can be bound to a fully materialized canonical document.
    bool documentReady=true;
    bool startupPreview() const noexcept {return mesh&&mesh->preview&&!documentReady;}
    // One entry per immutable GPU mesh slot. Several slots may belong to the
    // same logical document object after built-in territory classification.
    std::vector<WorldBaseRange> ranges;
};

struct WorldCountryDraw {
    std::string id;
    bool visible=true;
    RenderStyle fill,boundary;
};

// Physical land ownership is independent of paint visibility, opacity and
// interaction overlays. Buffers remain geographic and shared with fill packets.
struct PhysicalLandMaskPacket {
    std::shared_ptr<const WorldBaseFrame> worldBase;
    std::vector<std::size_t> baseSlots;
    std::vector<PolygonDrawPacket> polygons;
};

struct RenderScene {
    // Shared by interaction copies of one preparation. Current view/culling
    // live in MapFrame, which retains this exact snapshot on camera changes.
    // Revisions alone can collide when another project replaces the scene.
    struct PreparationIdentity {};
    std::shared_ptr<const PreparationIdentity> preparationIdentity;
    std::uint64_t revision=0;
    SceneRevisions revisions;
    // Preparation signatures permit comparison without retaining source document state.
    std::uint64_t geometrySignature=0,presentationSignature=0;
    std::uint64_t interactionSignature=0,datasetSignature=0;
    std::vector<PolygonDrawPacket> polygons;
    std::vector<StrokeDrawPacket> strokes;
    std::vector<PointDrawPacket> points;
    std::vector<SceneDrawRef> drawSequence;
    std::shared_ptr<const WorldBaseFrame> worldBase;
    std::shared_ptr<const PhysicalLandMaskPacket> physicalLandMask;
    std::vector<WorldCountryDraw> worldCountries;
    WorldRenderPlan worldPlan;
    InteractionRenderPacket interaction;
};

bool operator==(const SceneRevisions& left,const SceneRevisions& right) noexcept;
std::uint64_t nextSceneRevision(const std::shared_ptr<const RenderScene>& previous);
std::vector<std::size_t> worldRangeIndicesForOwner(const WorldBaseFrame& frame,
                                                    const std::string& ownerId);

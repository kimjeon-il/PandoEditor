#pragma once
#include <pandoeditor/map/renderpacket.h>
#include <pandoeditor/map/countrymesh.h>
#include <pandoeditor/map/drawplan.h>
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
    // Render precision is independent of canonical document readiness.
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

struct RenderScene {
    // Shared only by transient view/interaction copies of one preparation.
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
    std::vector<WorldCountryDraw> worldCountries;
    WorldRenderPlan worldPlan;
    InteractionRenderPacket interaction;
};

bool operator==(const SceneRevisions& left,const SceneRevisions& right) noexcept;
std::uint64_t nextSceneRevision(const std::shared_ptr<const RenderScene>& previous);
std::vector<std::size_t> worldRangeIndicesForOwner(const WorldBaseFrame& frame,
                                                    const std::string& ownerId);

#pragma once
#include "renderpacket.h"
#include "countrymesh.h"
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
};

struct WorldBaseFrame {
    std::shared_ptr<const CountryBaseMesh> mesh;
    std::vector<std::string> countryIds;
};

struct WorldCountryDraw {
    std::string id;
    bool visible=true;
    RenderStyle fill,boundary;
};

struct RenderScene {
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
    InteractionRenderPacket interaction;
};

bool operator==(const SceneRevisions& left,const SceneRevisions& right) noexcept;
std::uint64_t nextSceneRevision(const std::shared_ptr<const RenderScene>& previous);

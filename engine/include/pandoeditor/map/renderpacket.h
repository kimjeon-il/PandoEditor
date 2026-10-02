#pragma once
#include <pandoeditor/document.h>
#include <pandoeditor/maprenderorder.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

enum class BlendMode { Normal, Multiply, Screen };
enum class PrimitiveKind { Polygon, Stroke, Point, WorldFill, WorldStroke };

struct RenderStyle {
    std::uint32_t color=0;
    float alpha=1, width=1, fillAlpha=1;
    BlendMode blendMode=BlendMode::Normal;
    float dashOn=0,dashOff=0;
};

// Positions are derived unwrapped longitude/latitude pairs, never source geometry.
struct PolygonGeometryPacket {
    std::shared_ptr<const std::vector<float>> positions;
    // View-independent unit sphere vertices (x east, y north, z toward lon=0).
    std::shared_ptr<const std::vector<float>> unitSpherePositions;
    std::shared_ptr<const std::vector<std::uint32_t>> indices;
    std::shared_ptr<const std::vector<std::uint32_t>> globeIndices;
    // Offsets into derived vertices/rings retain polygon and hole ancestry.
    std::shared_ptr<const std::vector<std::uint32_t>> ringOffsets;
    std::shared_ptr<const std::vector<std::uint32_t>> polygonOffsets;
    std::size_t vertexCount=0,triangleCount=0,globeTriangleCount=0;
};

struct StrokeGeometryPacket {
    std::shared_ptr<const std::vector<float>> startsEnds;
    std::shared_ptr<const std::vector<float>> unitSphereStartsEnds;
    std::size_t segmentCount=0;
};

struct PointGeometryPacket {
    std::shared_ptr<const std::vector<float>> positions;
    std::shared_ptr<const std::vector<float>> unitSpherePositions;
    std::size_t pointCount=0;
};

struct PolygonDrawPacket {
    std::string key;
    pandoeditor::ObjectRef object;
    pandoeditor::GeometryRef geometry;
    std::uint64_t geometryRevision=0;
    double order=0;
    pandoeditor::MapRenderOrder drawOrder;
    RenderStyle style;
    PolygonGeometryPacket geometryPacket;
};

struct StrokeDrawPacket {
    std::string key;
    pandoeditor::ObjectRef object;
    pandoeditor::GeometryRef geometry;
    std::uint64_t geometryRevision=0;
    double order=0;
    pandoeditor::MapRenderOrder drawOrder;
    RenderStyle style;
    StrokeGeometryPacket geometryPacket;
};

struct PointDrawPacket {
    std::string key;
    pandoeditor::ObjectRef object;
    pandoeditor::GeometryRef geometry;
    std::uint64_t geometryRevision=0;
    double order=0;
    pandoeditor::MapRenderOrder drawOrder;
    RenderStyle style;
    PointGeometryPacket geometryPacket;
    std::string labelText;
    bool pinned=false;
    std::optional<pandoeditor::Point> manualPosition;
};

PolygonGeometryPacket makePolygonGeometryPacket(const pandoeditor::Geometry& geometry);
StrokeGeometryPacket makeStrokeGeometryPacket(const pandoeditor::Geometry& geometry);
PointGeometryPacket makePointGeometryPacket(const pandoeditor::Geometry& geometry);

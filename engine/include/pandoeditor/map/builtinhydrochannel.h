#pragma once

#include <pandoeditor/hydroformat.h>
#include <pandoeditor/map/renderpacket.h>
#include <pandoeditor/presentation.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct BuiltinHydroFeaturePacket {
    pandoeditor::ObjectRef object;
    std::string category;
    std::uint32_t fid=0,logicalFid=0;
    bool borderAligned=false;
    PolygonGeometryPacket polygon;
    StrokeGeometryPacket stroke;
};

struct BuiltinHydroRenderFrame {
    std::uint64_t revision=0;
    std::vector<BuiltinHydroFeaturePacket> features;
};

BuiltinHydroFeaturePacket prepareBuiltinHydroFeature(
    const pandoeditor::ObjectRef& object,const std::string& category,
    const pandoeditor::HydroPhysicalFeature& feature);

std::shared_ptr<const BuiltinHydroRenderFrame> makeBuiltinHydroRenderFrame(
    std::uint64_t revision,
    const std::vector<BuiltinHydroFeaturePacket>& features);

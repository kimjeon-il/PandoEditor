#include <pandoeditor/map/builtinhydrochannel.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
pandoeditor::Geometry hydroGeometry(const pandoeditor::HydroPhysicalFeature& feature) {
    pandoeditor::Geometry result;
    const auto point=[](pandoeditor::HydroPoint value) {
        return pandoeditor::Point{value.longitude*1e-6,value.latitude*1e-6};
    };
    if(feature.kind==1) {
        result.type=feature.geometry.lines.size()==1?"LineString":"MultiLineString";
        for(const auto& source:feature.geometry.lines) {
            pandoeditor::Ring line;line.reserve(source.size());
            for(const auto value:source)line.push_back(point(value));
            result.lines.push_back(std::move(line));
        }
    } else if(feature.kind==2) {
        result.type=feature.geometry.polygons.size()==1?"Polygon":"MultiPolygon";
        for(const auto& sourcePolygon:feature.geometry.polygons) {
            pandoeditor::Polygon polygon;polygon.reserve(sourcePolygon.size());
            for(const auto& sourceRing:sourcePolygon) {
                pandoeditor::Ring ring;ring.reserve(sourceRing.size());
                for(const auto value:sourceRing)ring.push_back(point(value));
                polygon.push_back(std::move(ring));
            }
            result.polygons.push_back(std::move(polygon));
        }
    } else throw std::invalid_argument("unsupported built-in hydro feature kind");
    return result;
}

std::shared_ptr<const std::vector<float>> riverEndpointWidths(
    const pandoeditor::HydroPhysicalFeature& feature,
    std::size_t expectedSegments) {
    auto result=std::make_shared<std::vector<float>>();
    result->reserve(expectedSegments*2);
    for(std::size_t part=0;part<feature.geometry.lines.size();++part) {
        const auto& line=feature.geometry.lines[part];
        const auto* widths=part<feature.widths.size()?&feature.widths[part]:nullptr;
        for(std::size_t i=1;i<line.size();++i) {
            if(line[i-1].longitude==line[i].longitude&&line[i-1].latitude==line[i].latitude)
                continue;
            const double start=widths&&i-1<widths->size()?(*widths)[i-1]:feature.strokeWidth;
            const double end=widths&&i<widths->size()?(*widths)[i]:start;
            if(!std::isfinite(start)||!std::isfinite(end)||start<0||end<0||
               start>std::numeric_limits<float>::max()||end>std::numeric_limits<float>::max())
                throw std::invalid_argument("invalid built-in hydro width");
            result->push_back(static_cast<float>(start));
            result->push_back(static_cast<float>(end));
        }
    }
    if(result->size()!=expectedSegments*2)
        throw std::logic_error("built-in hydro width/segment mismatch");
    return result;
}
}

BuiltinHydroFeaturePacket prepareBuiltinHydroFeature(
    const pandoeditor::ObjectRef& object,const std::string& category,
    const pandoeditor::HydroPhysicalFeature& feature) {
    if(object.domain!="hydroBuiltin"||object.id.empty())
        throw std::invalid_argument("built-in hydro object ref required");
    if(category!="river"&&category!="lake")
        throw std::invalid_argument("invalid built-in hydro category");
    if((category=="river")!=(feature.kind==1))
        throw std::invalid_argument("built-in hydro category/kind mismatch");

    BuiltinHydroFeaturePacket result;
    result.object=object;
    result.category=category;
    result.fid=feature.fid;
    result.logicalFid=feature.logicalFid;
    result.borderAligned=bool(feature.flags&1);
    const auto geometry=hydroGeometry(feature);
    if(feature.kind==1) {
        result.stroke=makeStrokeGeometryPacket(geometry);
        result.stroke.endpointWidths=riverEndpointWidths(feature,result.stroke.segmentCount);
    } else {
        result.polygon=makePolygonGeometryPacket(geometry);
        result.stroke=makeStrokeGeometryPacket(geometry);
    }
    return result;
}

std::shared_ptr<const BuiltinHydroRenderFrame> makeBuiltinHydroRenderFrame(
    std::uint64_t revision,const std::vector<BuiltinHydroFeaturePacket>& features) {
    if(!revision)return {};
    auto result=std::make_shared<BuiltinHydroRenderFrame>();
    result->revision=revision;
    result->features=features;
    return result;
}

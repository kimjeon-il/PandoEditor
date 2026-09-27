#include "renderpacket.h"
#include <earcut.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
using Position=std::array<double,2>;
constexpr double radians=3.141592653589793238462643383279502884/180;
std::shared_ptr<const std::vector<float>> sphereBuffer(const std::vector<float>& geographic) {
    auto result=std::make_shared<std::vector<float>>();
    result->reserve(geographic.size()/2*3);
    for(std::size_t i=0;i+1<geographic.size();i+=2) {
        const double lon=geographic[i]*radians,lat=geographic[i+1]*radians;
        const double horizontal=std::cos(lat);
        result->push_back(static_cast<float>(horizontal*std::sin(lon)));
        result->push_back(static_cast<float>(std::sin(lat)));
        result->push_back(static_cast<float>(horizontal*std::cos(lon)));
    }
    return result;
}
void requireFinite(pandoeditor::Point point) {
    if(!std::isfinite(point.x)||!std::isfinite(point.y)||point.y< -90||point.y>90)
        throw std::invalid_argument("non-finite or invalid geographic coordinate");
}
std::vector<Position> unwrap(const pandoeditor::Ring& ring,double anchor,bool closed) {
    const auto size=ring.size()-(closed&&ring.size()>1&&
        ring.front().x==ring.back().x&&ring.front().y==ring.back().y?1:0);
    std::vector<Position> result;
    result.reserve(size);
    for(std::size_t i=0;i<size;++i) {
        const auto source=ring[i];requireFinite(source);
        const double previous=result.empty()?anchor:result.back()[0];
        const double longitude=source.x+360*std::round((previous-source.x)/360);
        if(!std::isfinite(longitude)||std::abs(longitude)>std::numeric_limits<float>::max())
            throw std::invalid_argument("unwrapped longitude out of range");
        result.push_back({longitude,source.y});
    }
    return result;
}
void appendPositions(std::vector<float>& buffer,const std::vector<Position>& points) {
    for(const auto& point:points) {
        buffer.push_back(static_cast<float>(point[0]));
        buffer.push_back(static_cast<float>(point[1]));
    }
}
}

PolygonGeometryPacket makePolygonGeometryPacket(const pandoeditor::Geometry& geometry) {
    if(geometry.type!="Polygon"&&geometry.type!="MultiPolygon")
        throw std::invalid_argument("polygon packet requires polygon geometry");
    auto positions=std::make_shared<std::vector<float>>();
    auto indices=std::make_shared<std::vector<std::uint32_t>>();
    auto globeIndices=std::make_shared<std::vector<std::uint32_t>>();
    auto ringOffsets=std::make_shared<std::vector<std::uint32_t>>(1,0);
    auto polygonOffsets=std::make_shared<std::vector<std::uint32_t>>(1,0);
    for(const auto& polygon:geometry.polygons) {
        if(polygon.empty()||polygon.front().empty())continue;
        std::vector<std::vector<Position>> rings;
        rings.reserve(polygon.size());
        const double anchor=polygon.front().front().x;
        for(const auto& ring:polygon) {
            auto derived=unwrap(ring,anchor,true);
            if(derived.size()<3)throw std::invalid_argument("polygon ring has fewer than three vertices");
            rings.push_back(std::move(derived));
        }
        const auto base=positions->size()/2;
        std::size_t count=0;
        for(const auto& ring:rings)count+=ring.size();
        if(base+count>std::numeric_limits<std::uint32_t>::max())
            throw std::overflow_error("polygon packet vertex index overflow");
        const auto local=mapbox::earcut<std::uint32_t>(rings);
        if(local.empty())throw std::invalid_argument("polygon triangulation failed");
        const bool north=std::any_of(rings.front().begin(),rings.front().end(),
            [](const Position& p){return p[1]>=89.8;});
        const bool south=std::any_of(rings.front().begin(),rings.front().end(),
            [](const Position& p){return p[1]<=-89.8;});
        std::vector<std::uint32_t> localGlobe=local;
        if(north!=south) {
            auto polar=rings;
            for(auto& ring:polar)for(auto& point:ring) {
                const double latitude=point[1],longitude=point[0]*radians;
                const double radius=north?90-latitude:90+latitude;
                point={radius*std::sin(longitude),
                    radius*std::cos(longitude)*(north?-1:1)};
            }
            localGlobe=mapbox::earcut<std::uint32_t>(polar);
            if(localGlobe.empty())throw std::invalid_argument("polar triangulation failed");
        }
        for(const auto& ring:rings) {
            appendPositions(*positions,ring);
            ringOffsets->push_back(static_cast<std::uint32_t>(positions->size()/2));
        }
        polygonOffsets->push_back(static_cast<std::uint32_t>(ringOffsets->size()-1));
        for(auto index:local) {
            if(index>=count)throw std::logic_error("invalid polygon triangulation index");
            indices->push_back(static_cast<std::uint32_t>(base)+index);
        }
        for(auto index:localGlobe) {
            if(index>=count)throw std::logic_error("invalid polar triangulation index");
            globeIndices->push_back(static_cast<std::uint32_t>(base)+index);
        }
    }
    return {positions,sphereBuffer(*positions),indices,globeIndices,ringOffsets,polygonOffsets,
        positions->size()/2,indices->size()/3,globeIndices->size()/3};
}

StrokeGeometryPacket makeStrokeGeometryPacket(const pandoeditor::Geometry& geometry) {
    auto buffer=std::make_shared<std::vector<float>>();
    const auto addRing=[&](const pandoeditor::Ring& ring,bool closed) {
        if(ring.size()<2)return;
        auto points=unwrap(ring,ring.front().x,false);
        for(std::size_t i=1;i<points.size();++i) {
            const auto& a=points[i-1];const auto& b=points[i];
            if(a==b)continue;
            appendPositions(*buffer,{a,b});
        }
        if(closed&&points.front()!=points.back())appendPositions(*buffer,{points.back(),points.front()});
    };
    if(geometry.type=="LineString"||geometry.type=="MultiLineString") {
        for(const auto& line:geometry.lines)addRing(line,false);
    } else if(geometry.type=="Polygon"||geometry.type=="MultiPolygon") {
        for(const auto& polygon:geometry.polygons)
            for(const auto& ring:polygon)addRing(ring,true);
    } else throw std::invalid_argument("stroke packet requires line or polygon geometry");
    return {buffer,sphereBuffer(*buffer),buffer->size()/4};
}

PointGeometryPacket makePointGeometryPacket(const pandoeditor::Geometry& geometry) {
    if(geometry.type!="Point"&&geometry.type!="MultiPoint")
        throw std::invalid_argument("point packet requires point geometry");
    auto positions=std::make_shared<std::vector<float>>();
    for(const auto& point:geometry.points) {
        requireFinite(point);
        if(std::abs(point.x)>std::numeric_limits<float>::max())
            throw std::invalid_argument("point longitude out of range");
        positions->push_back(static_cast<float>(point.x));
        positions->push_back(static_cast<float>(point.y));
    }
    return {positions,sphereBuffer(*positions),positions->size()/2};
}

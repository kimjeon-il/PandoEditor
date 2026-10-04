#pragma once
#include <pandoeditor/presentation.h>
#include <pandoeditor/temporal.h>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace pandoeditor {
using Ring = std::vector<Point>;
using Polygon = std::vector<Ring>;
using MultiPolygon = std::vector<Polygon>;
struct GeometryRef {
    std::string id;
    std::uint32_t version=1;
    bool operator<(const GeometryRef& b) const { return std::tie(id,version)<std::tie(b.id,b.version); }
    bool operator==(const GeometryRef& b) const { return id==b.id && version==b.version; }
};
struct Geometry {
    std::string type="MultiPolygon";
    std::vector<Point> points;
    std::vector<Ring> lines;
    MultiPolygon polygons;
};
class GeometryStore {
public:
    void insert(GeometryRef ref, Geometry geometry);
    std::shared_ptr<const Geometry> get(const GeometryRef& ref) const;
    const auto& versions() const { return versions_; }
private:
    std::map<GeometryRef,std::shared_ptr<const Geometry>> versions_;
};
struct Validity { std::optional<std::string> from, to; };
std::pair<std::int64_t,std::int64_t> temporalBounds(const Validity& validity);
}

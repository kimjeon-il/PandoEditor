#pragma once
#include <pandoeditor/document.h>
#include <vector>

namespace pandoeditor {
struct GeoBounds {
    double west=0,south=0,east=0,north=0;
    bool wrapsDateline=false;
};

GeoBounds geometryBounds(const Geometry& geometry);
std::vector<GeoBounds> geometryPartBounds(const Geometry& geometry);
std::vector<GeoBounds> splitWrappedBounds(const GeoBounds& bounds);
bool intersects(const GeoBounds& left,const GeoBounds& right);
bool contains(const GeoBounds& bounds,Point point);
}

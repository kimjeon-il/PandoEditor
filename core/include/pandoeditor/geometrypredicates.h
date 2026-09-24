#pragma once
#include <pandoeditor/document.h>

namespace pandoeditor {
double planarArea(const Geometry& geometry) noexcept;
bool significantArea(double area, double referenceArea) noexcept;
bool geometryContains(const Geometry& container, const Geometry& subject);
bool geometrySignificantOverlap(const Geometry& left, const Geometry& right);
}

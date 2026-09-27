#pragma once
#include "mapviewstate.h"
#include <pandoeditor/document.h>
#include <pandoeditor/geobounds.h>
#include <optional>
#include <vector>

struct ProjectedPoint {
    double x=0,y=0,depth=0,frontness=0;
    bool visibleHemisphere=false,finite=false;
};

// scale is pixels per radian for Flat, and the projected sphere radius in pixels for Globe.
ProjectedPoint projectPoint(pandoeditor::Point geographic,const MapViewState& view,
                            double worldOffsetDegrees=0);
pandoeditor::Point unprojectFlat(double screenX,double screenY,const MapViewState& view);
std::optional<pandoeditor::Point> unprojectGlobe(double screenX,double screenY,const MapViewState& view);
std::optional<pandoeditor::Point> unprojectView(double screenX,double screenY,const MapViewState& view);
std::vector<pandoeditor::GeoBounds> geographicPickWindows(
    double screenX,double screenY,double radiusPixels,const MapViewState& view);
// Returns degrees (-360, 0, +360), matching the pinned web oracle's radian offsets.
std::vector<double> visibleFlatWorldOffsets(const MapViewState& view);

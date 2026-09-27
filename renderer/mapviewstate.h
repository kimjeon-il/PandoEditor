#pragma once
#include <cstdint>

enum class ProjectionMode { Flat, Globe };

struct MapViewState {
    ProjectionMode mode=ProjectionMode::Flat;
    double viewportWidth=1,viewportHeight=1;
    double centerLongitude=0,centerLatitude=0;
    double rotationLongitude=0,rotationLatitude=0,rotationRoll=0;
    double scale=1,translateX=0,translateY=0,devicePixelRatio=1;
    std::uint64_t revision=0;
};

bool validMapViewState(const MapViewState& view) noexcept;
MapViewState advanceViewRevision(const MapViewState& previous,const MapViewState& proposed);

#pragma once
#include "countrymesh.h"
#include "mapviewstate.h"
#include <cstddef>
#include <vector>

struct CountryDrawRange {std::size_t first=0,count=0;};
struct CountryDrawPlan {
    std::vector<CountryDrawRange> ranges;
    std::vector<bool> visible;
    std::size_t visibleCountryCount=0,indexCount=0,fullIndexCount=0;
    bool culled=false,fallback=false;
};
enum class CountryRangeKind {Triangle,Boundary};
CountryDrawPlan countryDrawRangesForView(const CountryBaseMesh&,const MapViewState&,
    CountryRangeKind kind=CountryRangeKind::Triangle,double paddingPixels=64,
    double fullRangeThreshold=.7,std::size_t maxRanges=96);

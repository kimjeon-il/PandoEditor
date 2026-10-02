#pragma once
#include <cstddef>
#include <vector>

struct CountryDrawRange {
    std::size_t first=0,count=0;
};

struct CountryDrawPlan {
    std::vector<CountryDrawRange> ranges;
    std::vector<bool> visible;
    std::size_t visibleCountryCount=0,indexCount=0,fullIndexCount=0;
    bool culled=false,fallback=false;
};

struct WorldRenderPlan {
    std::vector<double> worldOffsets;
    CountryDrawPlan fills;
    CountryDrawPlan strokes;
};

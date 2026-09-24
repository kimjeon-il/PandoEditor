#pragma once
#include <pandoeditor/hydroformat.h>
#include <vector>

namespace pandoeditor {
struct HydroStageGrid {std::uint8_t id=0;double minZoom=0;std::uint16_t columns=0,rows=0;};
struct HydroFlatWindow {
    double threshold=0,width=1,height=1,scale=1,longitude=0,latitude=0;
};
double webHydroThreshold(double zoom);
std::vector<HydroTileKey> hydroViewportTiles(const std::vector<HydroStageGrid>& stages,
                                            const HydroFlatWindow& window);
}

#include <pandoeditor/hydroviewport.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pandoeditor {
namespace {
double normalize(double value) {
    auto result=std::fmod(std::fmod(value+180.,360.)+360.,360.)-180.;
    return result==0?0:result;
}
}
double webHydroThreshold(double zoom) {
    if(!std::isfinite(zoom)||zoom<=0)throw std::invalid_argument("invalid hydro zoom");
    return 2.4+std::log2(std::max(1.,zoom))*2.05;
}
std::vector<HydroTileKey> hydroViewportTiles(const std::vector<HydroStageGrid>& stages,
                                            const HydroFlatWindow& view) {
    if(!std::isfinite(view.threshold)||!std::isfinite(view.width)||!std::isfinite(view.height)||
       !std::isfinite(view.scale)||!std::isfinite(view.longitude)||!std::isfinite(view.latitude)||
       view.width<=0||view.height<=0||view.scale<=0)
        throw std::invalid_argument("invalid hydro viewport");
    const double pi=3.14159265358979323846;
    const double halfLongitude=view.width/std::max(1.,view.scale)*90./pi+2.;
    const double halfLatitude=view.height/std::max(1.,view.scale)*90./pi+2.;
    const double centerLon=normalize(view.longitude);
    const double centerLat=std::clamp(view.latitude,-90.,90.);
    std::vector<HydroTileKey> tiles;
    for(const auto& stage:stages){
        if(stage.minZoom>view.threshold+1e-9)continue;
        if(!stage.columns||!stage.rows)throw std::invalid_argument("invalid hydro stage grid");
        const double tileLon=360./stage.columns,tileLat=180./stage.rows;
        std::vector<std::uint16_t> xs;
        for(std::uint16_t x=0;x<stage.columns;x++){
            const auto center=-180.+(x+.5)*tileLon;
            const auto delta=std::abs(normalize(center-centerLon));
            if(halfLongitude>=180.-1e-9||delta<=halfLongitude+tileLon/2+1e-9)xs.push_back(x);
        }
        for(std::uint16_t y=0;y<stage.rows;y++){
            const auto center=90.-(y+.5)*tileLat;
            if(std::abs(center-centerLat)>halfLatitude+tileLat/2+1e-9)continue;
            for(const auto x:xs)tiles.push_back({stage.id,x,y});
        }
    }
    return tiles;
}
}

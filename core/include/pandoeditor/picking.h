#pragma once
#include <pandoeditor/document.h>
#include <algorithm>
#include <cmath>
namespace pandoeditor {
// Same boundary/holes policy as the legacy Project picker, reusable for all units.
inline bool pointInCountry(Point point,const MultiPolygon& polygons) {
    auto inRing=[&](const Ring& ring){
        if(ring.empty())return 0;
        bool inside=false;
        for(std::size_t i=0,j=ring.size()-1;i<ring.size();j=i++){
            const auto a=ring[j],b=ring[i];
            const double cross=(point.x-a.x)*(b.y-a.y)-(point.y-a.y)*(b.x-a.x);
            if(std::abs(cross)<=1e-10&&point.x>=std::min(a.x,b.x)-1e-10&&point.x<=std::max(a.x,b.x)+1e-10&&
               point.y>=std::min(a.y,b.y)-1e-10&&point.y<=std::max(a.y,b.y)+1e-10)return 2;
            if((a.y>point.y)!=(b.y>point.y)&&point.x<(b.x-a.x)*(point.y-a.y)/(b.y-a.y)+a.x)inside=!inside;
        }
        return inside?1:0;
    };
    for(const auto& polygon:polygons){
        if(polygon.empty())continue;
        const int outer=inRing(polygon.front());if(outer==2)return true;if(!outer)continue;
        bool hole=false;
        for(std::size_t i=1;i<polygon.size();++i){const int value=inRing(polygon[i]);if(value==2)return true;if(value==1)hole=true;}
        if(!hole)return true;
    }
    return false;
}
} // namespace pandoeditor

#include "geographicimagemesh.h"
#include <pandoeditor/map/projectionengine.h>
#include <algorithm>
#include <stdexcept>

GeographicImageMesh buildGeographicImageMesh(const GeographicImageBounds& bounds,
                                               const MapViewState& view,int columns,int rows)
{
    if(!validMapViewState(view)||columns<1||rows<1||columns>128||rows>128||
       !(bounds.east>bounds.west)||!(bounds.north>bounds.south)||
       bounds.south< -90||bounds.north>90)
        throw std::invalid_argument("invalid geographic image mesh");
    GeographicImageMesh result;
    result.vertices.reserve(std::size_t(columns+1)*std::size_t(rows+1)*4);
    std::vector<bool> visible(std::size_t(columns+1)*std::size_t(rows+1));
    for(int row=0;row<=rows;++row)for(int column=0;column<=columns;++column) {
        const double u=double(column)/columns,v=double(row)/rows;
        const double longitude=bounds.west+(bounds.east-bounds.west)*u;
        const double latitude=bounds.north-(bounds.north-bounds.south)*v;
        const auto projected=projectPoint({longitude,latitude},view);
        const auto index=std::size_t(row)*std::size_t(columns+1)+std::size_t(column);
        visible[index]=projected.finite&&projected.visibleHemisphere;
        result.vertices.insert(result.vertices.end(),{float(projected.x),float(projected.y),float(u),float(v)});
    }
    const auto append=[&](std::uint16_t a,std::uint16_t b,std::uint16_t c) {
        if(view.mode==ProjectionMode::Flat||(visible[a]&&visible[b]&&visible[c]))
            result.indices.insert(result.indices.end(),{a,b,c});
    };
    for(int row=0;row<rows;++row)for(int column=0;column<columns;++column) {
        const auto a=std::uint16_t(row*(columns+1)+column);
        const auto b=std::uint16_t(a+1),c=std::uint16_t(a+columns+1),d=std::uint16_t(c+1);
        append(a,c,b);append(b,c,d);
    }
    return result;
}

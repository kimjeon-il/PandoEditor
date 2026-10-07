#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

struct TerrainGridMesh {
    int columns=1,rows=1;
    // Longitude, latitude, u, v. Terrain indices follow the fixed Web Uint32 grid.
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
};
struct TerrainGridShape {int columns=1,rows=1;};
inline TerrainGridShape terrainGridShape(double longitudeSpan,double latitudeSpan,bool globe,double physicalScale) {
    constexpr double pi=3.14159265358979323846;
    const double angularStep=globe?std::max(.75,std::min(8.,std::sqrt(4./std::max(1.,physicalScale))*180./pi)):360.;
    return {std::max(1,int(std::ceil(std::abs(longitudeSpan)/angularStep))),
            std::max(1,int(std::ceil(std::abs(latitudeSpan)/angularStep)))};
}
inline TerrainGridMesh buildTerrainGridMesh(double west,double north,double east,double south,
                                           bool globe,double physicalScale) {
    TerrainGridMesh mesh;
    const auto shape=terrainGridShape(east-west,north-south,globe,physicalScale);
    mesh.columns=shape.columns;mesh.rows=shape.rows;
    for(int y=0;y<=mesh.rows;++y)for(int x=0;x<=mesh.columns;++x) {
        const auto u=float(x)/mesh.columns,v=float(y)/mesh.rows;
        mesh.vertices.insert(mesh.vertices.end(),{float(west+(east-west)*u),float(north+(south-north)*v),u,v});
    }
    for(int y=0;y<mesh.rows;++y)for(int x=0;x<mesh.columns;++x) {
        const std::uint32_t a=y*(mesh.columns+1)+x,b=a+1,c=a+mesh.columns+1,d=c+1;
        mesh.indices.insert(mesh.indices.end(),{a,c,b,b,c,d});
    }
    return mesh;
}

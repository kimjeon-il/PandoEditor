#include <pandoeditor/map/renderpacket.h>
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
double triangleArea(const std::vector<float>& positions,const std::vector<std::uint32_t>& indices) {
    double area=0;
    for(std::size_t i=0;i+2<indices.size();i+=3) {
        const auto a=indices[i]*2,b=indices[i+1]*2,c=indices[i+2]*2;
        area+=std::abs((positions[b]-positions[a])*(positions[c+1]-positions[a+1])-
            (positions[c]-positions[a])*(positions[b+1]-positions[a+1]))/2;
    }
    return area;
}
bool pointInTriangle(double x,double y,const std::vector<float>& p,
                     std::uint32_t ai,std::uint32_t bi,std::uint32_t ci) {
    const auto cross=[&](std::uint32_t a,std::uint32_t b) {
        return (p[2*b]-p[2*a])*(y-p[2*a+1])-(p[2*b+1]-p[2*a+1])*(x-p[2*a]);
    };
    const double a=cross(ai,bi),b=cross(bi,ci),c=cross(ci,ai);
    return (a>=0&&b>=0&&c>=0)||(a<=0&&b<=0&&c<=0);
}
void holeDoesNotFill() {
    pandoeditor::Geometry geometry{"Polygon",{},{},{{
        {{0,0},{10,0},{10,10},{0,10},{0,0}},
        {{3,3},{3,7},{7,7},{7,3},{3,3}}
    }}};
    auto packet=makePolygonGeometryPacket(geometry);
    require(packet.vertexCount==8,"closed endpoint removed from packet only");
    require(packet.triangleCount>0,"hole tessellation exists");
    require(std::abs(triangleArea(*packet.positions,*packet.indices)-84)<1e-4,"hole area is excluded");
    for(std::size_t i=0;i<packet.indices->size();i+=3)
        require(!pointInTriangle(5,5,*packet.positions,(*packet.indices)[i],
                (*packet.indices)[i+1],(*packet.indices)[i+2]),"hole center remains empty");
    require(geometry.polygons[0][0].size()==5,"source remains closed");
}
void multiPolygonPacket() {
    pandoeditor::Geometry geometry{"MultiPolygon",{},{},
        {{{{0,0},{2,0},{2,2},{0,2},{0,0}}},
         {{{20,0},{22,0},{22,2},{20,2},{20,0}}}}};
    auto packet=makePolygonGeometryPacket(geometry);
    require(packet.vertexCount==8&&packet.triangleCount==4,"both components triangulated");
    require(std::abs(triangleArea(*packet.positions,*packet.indices)-8)<1e-4,"component areas");
}
void datelinePolygonFiniteAndLocal() {
    pandoeditor::Geometry geometry{"Polygon",{},{},{{
        {{179,70},{-179,70},{-179,80},{179,80},{179,70}},
        {{179.5,72},{-179.5,72},{-179.5,74},{179.5,74},{179.5,72}}
    }}};
    auto packet=makePolygonGeometryPacket(geometry);
    require(packet.triangleCount>0,"dateline hole triangulated");
    for(auto coordinate:*packet.positions)require(std::isfinite(coordinate),"packet finite");
    require(std::abs(triangleArea(*packet.positions,*packet.indices)-18)<1e-4,"wrapped hole area");
    for(std::size_t i=0;i<packet.indices->size();i+=3)
        require(!pointInTriangle(180,73,*packet.positions,(*packet.indices)[i],
                (*packet.indices)[i+1],(*packet.indices)[i+2]),"dateline hole remains empty");
    require(geometry.polygons[0][0][1].x==-179,"source longitude unchanged");
}
void strokeDropsOnlyZeroLength() {
    pandoeditor::Geometry geometry{"MultiLineString",{},
        {{{0,0},{0,0},{1,0},{1,0},{1,1}},{{4,4},{4,4},{5,5}}},{}};
    auto packet=makeStrokeGeometryPacket(geometry);
    require(packet.segmentCount==3,"only zero-length segments dropped");
    require(packet.startsEnds->size()==12,"four geographic floats per segment");
}
void typedDrawPacketOwnsOnlyDerivedBuffers() {
    static_assert(std::is_same_v<decltype(PolygonDrawPacket::geometryPacket),PolygonGeometryPacket>);
    static_assert(std::is_same_v<decltype(PolygonGeometryPacket::positions),
        std::shared_ptr<const std::vector<float>>>);
    PolygonDrawPacket draw;
    draw.object={"territorial","DEU"};draw.geometry={"deu",1};
    draw.style.color=0x112233;
    require(draw.object.id=="DEU"&&draw.geometry.version==1,"typed identity only");
    {
        pandoeditor::Geometry source{"Polygon",{},{},{{
            {{0,0},{1,0},{1,1},{0,1},{0,0}}
        }}};
        draw.geometryPacket=makePolygonGeometryPacket(source);
    }
    require(draw.geometryPacket.triangleCount==2,"packet survives source lifetime");
    require(draw.geometryPacket.positions->size()==8,"packet stores only numeric positions");
}
}
int main() {
    holeDoesNotFill();multiPolygonPacket();datelinePolygonFiniteAndLocal();
    strokeDropsOnlyZeroLength();typedDrawPacketOwnsOnlyDerivedBuffers();
}

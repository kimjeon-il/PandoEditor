#include "renderpacket.h"
#include <iostream>
#include <stdexcept>

int main() {
    std::size_t total=0;if(!(std::cin>>total))return 2;
    for(std::size_t row=0;row<total;++row) {
        std::string id,type;std::size_t parts;
        if(!(std::cin>>id>>type>>parts))return 2;
        pandoeditor::Geometry geometry;geometry.type=type;
        if(type=="Polygon"||type=="MultiPolygon") {
            for(std::size_t i=0;i<parts;++i) {
                std::size_t rings;std::cin>>rings;
                pandoeditor::Polygon polygon;
                for(std::size_t j=0;j<rings;++j) {
                    std::size_t n;std::cin>>n;pandoeditor::Ring ring;
                    for(std::size_t k=0;k<n;++k){pandoeditor::Point point;std::cin>>point.x>>point.y;ring.push_back(point);}
                    polygon.push_back(std::move(ring));
                }
                geometry.polygons.push_back(std::move(polygon));
            }
        } else {
            for(std::size_t i=0;i<parts;++i) {
                std::size_t n;std::cin>>n;pandoeditor::Ring line;
                for(std::size_t j=0;j<n;++j){pandoeditor::Point point;std::cin>>point.x>>point.y;line.push_back(point);}
                geometry.lines.push_back(std::move(line));
            }
        }
        if(!std::cin)throw std::runtime_error("truncated corpus probe input");
        try {
            if(type=="Polygon"||type=="MultiPolygon") {
                const auto packet=makePolygonGeometryPacket(geometry);
                if(!packet.triangleCount)throw std::runtime_error("no triangles");
                std::cout<<id<<' '<<packet.vertexCount<<' '<<packet.triangleCount<<'\n';
            } else {
                const auto packet=makeStrokeGeometryPacket(geometry);
                if(!packet.segmentCount)throw std::runtime_error("no segments");
                std::cout<<id<<' '<<packet.segmentCount<<'\n';
            }
        }catch(const std::exception& error){std::cerr<<id<<": "<<error.what()<<'\n';return 1;}
    }
}

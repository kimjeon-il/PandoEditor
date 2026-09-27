#include <pandoeditor/geobounds.h>
#include <iomanip>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        std::size_t parts=0;
        if(!(std::cin>>parts)||parts==0)throw std::runtime_error("missing polygon parts");
        pandoeditor::Geometry shape;shape.type="MultiPolygon";
        for(std::size_t part=0;part<parts;++part) {
            std::size_t count=0;
            if(!(std::cin>>count)||count<3)throw std::runtime_error("invalid outer ring");
            pandoeditor::Ring ring;ring.reserve(count);
            for(std::size_t i=0;i<count;++i) {
                pandoeditor::Point point;
                if(!(std::cin>>point.x>>point.y))throw std::runtime_error("invalid position");
                ring.push_back(point);
            }
            shape.polygons.push_back({std::move(ring)});
        }
        const auto b=pandoeditor::geometryBounds(shape);
        std::cout<<std::setprecision(17)<<b.west<<' '<<b.south<<' '<<b.east<<' '
            <<b.north<<' '<<int(b.wrapsDateline)<<' '<<shape.polygons.size()<<'\n';
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}

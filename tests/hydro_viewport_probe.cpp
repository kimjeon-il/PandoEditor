#include <pandoeditor/hydroviewport.h>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <vector>

int main(int argc,char** argv) {
    if(argc==3&&std::string(argv[1])=="--threshold") {
        try{std::cout<<std::setprecision(17)<<pandoeditor::webHydroThreshold(std::stod(argv[2]))<<'\n';return 0;}
        catch(...){return 3;}
    }
    if(argc<11||(argc-7)%4!=0)return 2;
    try {
        pandoeditor::HydroFlatWindow view{
            std::stod(argv[1]),std::stod(argv[2]),std::stod(argv[3]),
            std::stod(argv[4]),std::stod(argv[5]),std::stod(argv[6])};
        std::vector<pandoeditor::HydroStageGrid> stages;
        for(int i=7;i<argc;i+=4)stages.push_back({
            static_cast<std::uint8_t>(std::stoul(argv[i])),std::stod(argv[i+1]),
            static_cast<std::uint16_t>(std::stoul(argv[i+2])),
            static_cast<std::uint16_t>(std::stoul(argv[i+3]))});
        const auto tiles=pandoeditor::hydroViewportTiles(stages,view);
        std::cout<<'[';
        for(std::size_t i=0;i<tiles.size();i++){
            if(i)std::cout<<',';
            std::cout<<"{\"stage\":"<<int(tiles[i].stage)<<",\"x\":"<<tiles[i].x<<",\"y\":"<<tiles[i].y<<'}';
        }
        std::cout<<"]\n";
        return 0;
    }catch(...){return 3;}
}

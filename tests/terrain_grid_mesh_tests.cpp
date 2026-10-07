#include "../renderer/terraingridmesh.h"
#include <iostream>
#include <stdexcept>
#include <functional>
int main() {
    const std::vector<std::pair<const char*,std::function<void()>>> cases={
        {"flat affine four vertices",[]{const auto m=buildTerrainGridMesh(-180,90,0,-90,false,50000);if(m.vertices.size()!=16||m.indices.size()!=6)throw std::runtime_error("flat mesh must be 1x1");}},
        {"globe physical scale 1 eight degree grid",[]{const auto m=buildTerrainGridMesh(-180,90,0,-90,true,1);if(m.columns!=23||m.rows!=23)throw std::runtime_error("low globe grid");}},
        {"globe physical scale 1000 exact independent counts",[]{const auto m=buildTerrainGridMesh(-180,90,0,-90,true,1000);if(m.columns!=50||m.rows!=50)throw std::runtime_error("physical scale grid");}},
        {"globe floor uses uint32 beyond 65535",[]{const auto m=buildTerrainGridMesh(-180,90,180,-90,true,50000);if(m.columns!=480||m.rows!=240||m.vertices.size()/4!=115921||m.indices.size()!=691200||m.indices.back()!=115920)throw std::runtime_error("high globe uint32 grid");}},
        {"non square clipped tile grid",[]{const auto m=buildTerrainGridMesh(171,90,180,80,true,1);if(m.columns!=2||m.rows!=2)throw std::runtime_error("clipped angular spans");}},
        {"UV bounds and triangle winding",[]{const auto m=buildTerrainGridMesh(170,10,190,-10,false,1);if(m.vertices.front()!=170||m.vertices[1]!=10||m.vertices[12]!=190||m.vertices[13]!=-10||m.vertices[14]!=1||m.vertices[15]!=1||m.indices!=std::vector<std::uint32_t>{0,2,1,1,2,3})throw std::runtime_error("geographic and UV grid");}}
    };
    int passed=0,failed=0;for(const auto& item:cases)try{item.second();++passed;std::cout<<"PASS "<<item.first<<'\n';}catch(const std::exception& e){++failed;std::cout<<"FAIL "<<item.first<<": "<<e.what()<<'\n';}
    std::cout<<"processed="<<cases.size()<<" passed="<<passed<<" failed="<<failed<<" skip=0\n";return failed?1:0;
}

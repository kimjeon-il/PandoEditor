#include "terrainprovider.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <set>

namespace {
void require(bool condition,const std::string& message) {
    if(!condition)throw std::runtime_error(message);
}
MapViewState viewFor(ProjectionMode mode,double scale,double dpr=1) {
    MapViewState view;view.mode=mode;view.scale=scale;view.devicePixelRatio=dpr;
    view.viewportWidth=390;view.viewportHeight=200;
    view.translateX=195;view.translateY=100;return view;
}
void requireLevel(const TerrainTileProvider& provider,const MapViewState& view,int expected) {
    const auto tiles=provider.tilesForView(view);
    require(!tiles.empty(),"demand must select visible target tiles");
    for(const auto& tile:tiles)
        require(tile.level==expected,"expected literal L"+std::to_string(expected)+
            "; observed L"+std::to_string(tile.level));
}
std::string key(const TerrainTileSpec& spec) {
    return std::to_string(spec.level)+"/"+std::to_string(spec.column)+"-"+std::to_string(spec.row);
}
std::set<std::string> keys(const std::vector<TerrainTileSpec>& specs) {
    std::set<std::string> result;for(const auto& spec:specs)result.insert(key(spec));return result;
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    int passed=0,failed=0;
    const auto test=[&](const std::string& name,const std::function<void()>& body) {
        try {body();++passed;std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& error) {++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
    };
    try {
        require(argc==3,"fixed DEM and raster manifest arguments required");
        QFile demFile(QString::fromLocal8Bit(argv[1])),rasterFile(QString::fromLocal8Bit(argv[2]));
        require(demFile.open(QIODevice::ReadOnly)&&rasterFile.open(QIODevice::ReadOnly),"fixed manifests readable");
        QTemporaryDir root;require(root.isValid(),"isolated metadata-only fixture root");
        TerrainTileProvider dem(demFile.readAll(),root.path()),raster(rasterFile.readAll(),root.path());
        require(dem.available()&&raster.available(),"fixed DEM and raster metadata supported");
        // Literal fixed-Web real-grid cases, authored before native implementation.
        // Wrong 1.6 threshold, DPR omission, globe-L0 lock or intermediate demand
        // breaks these observations of the production provider's selected tiles.
        for(const auto mode:{ProjectionMode::Flat,ProjectionMode::Globe}) {
            const std::string projection=mode==ProjectionMode::Flat?"flat":"globe";
            for(const auto row:{std::pair<double,int>{100,0},{200,1},{400,2},{800,3},
                                {1600,4},{3200,5},{1000000,5},{191.8385,0},{191.8386,1}})
                test(projection+" CSS scale "+std::to_string(row.first)+" sourceDPR1",[&] {
                    requireLevel(dem,viewFor(mode,row.first),row.second);
                });
            for(const auto row:{std::pair<double,int>{0.5,0},{1,0},{1.5,1},{2,1},{3,2},{4,2}})
                test(projection+" desktop scale177.45 DPR"+std::to_string(row.first),[&] {
                    requireLevel(dem,viewFor(mode,177.45,row.first),row.second);
                });
            test(projection+" raster saturates at actual L4",[&] {
                requireLevel(raster,viewFor(mode,1000000),4);
            });
            test(projection+" window-layout sourceDPR cap changes L2 to L1",[&] {
                const auto view=viewFor(mode,177.45,3);
                require(dem.targetLevelForView(view,false)==2,"desktop window800: literal L2");
                require(dem.targetLevelForView(view,true)==1,"mobile window799: literal L1");
                const auto mobile=dem.tilesForView(view,true);
                require(!mobile.empty(),"mobile target visible");
                for(const auto& tile:mobile)require(tile.level==1,"draw API receives authoritative layout");
            });
            test(projection+" viewport dimensions do not choose source LOD",[&] {
                for(double width:{390.,799.,800.,1600.}) {
                    auto view=viewFor(mode,177.45,3);view.viewportWidth=width;view.translateX=width/2;
                    require(dem.targetLevelForView(view,false)==2,"panel width is not window layout");
                    require(dem.targetLevelForView(view,true)==1,"layout remains explicit under panel resize");
                }
            });
            test(projection+" target L5 is direct with complete L0 reserve and bounded neighbors",[&] {
                const auto plan=dem.planForView(viewFor(mode,1000000));
                require(plan.targetLevel==5,"direct target L5");
                require(keys(plan.baseTiles)==std::set<std::string>{"0/0-0","0/1-0"},"both complete-world base keys");
                require(keys(plan.targetTiles)==std::set<std::string>{"5/20-10","5/21-10"},
                        "literal real-grid tight view includes padded neighbor, not entire globe");
                require(keys(plan.prefetchTiles)==std::set<std::string>{"5/19-9","5/20-9","5/21-9","5/22-9",
                        "5/19-10","5/22-10","5/19-11","5/20-11","5/21-11","5/22-11"},"bounded eight-neighbors");
                require(plan.requests.size()==14,"deduplicated base, target and neighbors only");
                std::set<std::string> requested;
                for(const auto& request:plan.requests) {
                    require(request.spec.level==0||request.spec.level==5,"no intermediate L1-L4 requests");
                    require(request.spec.worldOffsetDegrees==0,"request specs are canonical");
                    require(requested.insert(key(request.spec)).second,"no duplicate asset demand");
                    require(request.spec.path.endsWith("/terrain/v0.13.0/"+QString::fromStdString(key(request.spec))+".webp"),
                            "real DEM tile-version path remains v0.13.0");
                    if(request.spec.level==0)require(request.priority==50000,"base priority50000");
                }
                require(plan.requests[0].priority==50000&&plan.requests[1].priority==50000,"base admitted first");
                require(key(plan.requests[2].spec)=="5/21-10"&&key(plan.requests[3].spec)=="5/20-10",
                        "closest target precedes farther padded target");
                require(plan.requests[2].priority<30000&&plan.requests[3].priority>1000,"distance-based target priority");
                for(std::size_t index=4;index<plan.requests.size();++index)
                    require(plan.requests[index].priority==1000,"prefetch low priority");
                require(dem.resourceCacheSnapshot().residentCount==0&&dem.resourceCacheSnapshot().pendingCount==0,
                        "pure plan does not request/decode/protect before consumer admission");
            });
        }
        test("flat dateline replicas share canonical paths and draw on both sides",[&] {
            auto view=viewFor(ProjectionMode::Flat,1000000);view.centerLongitude=180;
            const auto plan=dem.planForView(view);
            // Independently applying pinned Web's flat predicate: L5 column41
            // center174.133333 has wrapped gap5.866667 < halfLon4.266667 +
            // padding2 + viewport half0.011173. It belongs to padded demand.
            require(keys(plan.targetTiles)==std::set<std::string>{"5/0-10","5/41-10","5/42-10"},"wrapped real-grid dateline demand");
            const auto draws=dem.tilesForView(view);
            require(draws.size()==3,"bounded padded dateline copies");
            for(const auto& draw:draws) {
                require(draw.worldOffsetDegrees==(draw.column==0?360:0),"correct draw world offset");
                const auto canonical=std::find_if(plan.targetTiles.begin(),plan.targetTiles.end(),[&](const auto& spec) {
                    return key(spec)==key(draw);
                });
                require(canonical!=plan.targetTiles.end()&&draw.path==canonical->path,"replica asset identity unchanged");
            }
        });
        test("flat translation pan updates effective demand center",[&] {
            auto canonical=viewFor(ProjectionMode::Flat,1000000);canonical.centerLongitude=180;
            auto translated=viewFor(ProjectionMode::Flat,1000000);
            translated.translateX-=1000000*3.14159265358979323846;
            require(keys(dem.planForView(translated).targetTiles)==keys(dem.planForView(canonical).targetTiles),
                    "same projected center under native translation must have same demand");
        });
        test("globe combined center and rotation use native projection signs",[&] {
            auto view=viewFor(ProjectionMode::Globe,1000000);view.centerLongitude=30;view.rotationLongitude=150;
            require(keys(dem.planForView(view).targetTiles)==std::set<std::string>{"5/0-10","5/41-10","5/42-10"},
                    "literal great-circle dateline cap follows native center+rotation");
        });
        test("globe polar cap includes wrapped longitude neighbors without entire L5 world",[&] {
            auto view=viewFor(ProjectionMode::Globe,1000000);view.centerLatitude=88;
            const auto plan=dem.planForView(view);
            require(plan.targetTiles.size()==43,"all43 top-row longitude tiles at north-pole cap");
            for(const auto& tile:plan.targetTiles)require(tile.row==0&&tile.worldOffsetDegrees==0,"canonical pole row only");
        });
        test("L0 demand deduplicates target against mandatory reserve",[&] {
            const auto plan=dem.planForView(viewFor(ProjectionMode::Flat,100));
            require(plan.requests.size()==2,"world-base demand keys deduplicate L0 target and neighbors");
            for(const auto& request:plan.requests)require(request.priority==50000,"mandatory reserve priority preserved");
            require(plan.baseTiles[0].west==-180&&plan.baseTiles[0].north==90&&plan.baseTiles[0].south==-90,
                    "base uses geographic interior without gutter expansion");
            require(plan.baseTiles[1].east==180&&plan.baseTiles[0].east==plan.baseTiles[1].west,"exact full-world base coverage");
        });
        test("invalid view rejects demand without state mutation",[&] {
            auto view=viewFor(ProjectionMode::Flat,100);view.scale=0;
            require(dem.targetLevelForView(view)==-1,"invalid LOD sentinel");
            require(dem.planForView(view).requests.empty()&&dem.tilesForView(view).empty(),"invalid view has no demand");
        });
        std::cout<<passed<<" passed; "<<failed<<" failed; skip=0\n";
        return failed?1:0;
    } catch(const std::exception& error) {std::cerr<<"FAIL setup: "<<error.what()<<'\n';return 1;}
}

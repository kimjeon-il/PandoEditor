#include <pandoeditor/map/labelengine.h>
#include <pandoeditor/map/projectionengine.h>
#include <iomanip>
#include <iostream>
#include <set>

// All names are forced, isolating flag decoration from name-placement policy.
int main() {
    MapViewState view;view.mode=ProjectionMode::Flat;
    view.viewportWidth=800;view.viewportHeight=600;
    view.scale=8*180/3.14159265358979323846;view.translateX=400;view.translateY=300;
    std::cout<<std::setprecision(17)<<"[";bool first=true;
    for(double zoom:{1.79,1.8,4.})for(int seed=0;seed<16;++seed) {
        std::vector<MapLabelSource> sources;std::set<pandoeditor::ObjectRef> selected;
        for(int i=0;i<12;++i) {
            MapLabelSource s;s.ref={i%4==3?"label":"territorial",std::to_string(i)};
            s.text=s.ref.id;s.priority=100-i;s.geographic={(i%6)*((seed%4)*3.5+4)-25.,(i/6)*((seed/4)*2.+1.)};
            s.nameVisible=(i+seed)%5!=0;s.flagVisible=i%4!=3&&(i+seed)%7!=0;
            s.width=s.nameVisible?80:18;s.height=s.nameVisible?20:12;
            s.collisionGroup=i%2?"a":"b";selected.insert(s.ref);sources.push_back(s);
        }
        MapLabelEngine engine;engine.setSources(sources,1);
        MapLabelLayoutOptions options;options.zoom=zoom;options.viewportWidth=800;options.viewportHeight=600;
        const auto placed=engine.layout(view,options,selected);
        if(!first)std::cout<<",";first=false;
        std::cout<<"{\"zoom\":"<<zoom<<",\"input\":[";
        for(std::size_t i=0;i<sources.size();++i) {
            const auto& s=sources[i];if(i)std::cout<<",";
            const auto point=projectPoint(s.geographic,view);const double x=point.x,y=point.y;
            std::cout<<"{\"sourceType\":\""<<(s.ref.domain=="territorial"?"country":"label")
                <<"\",\"source\":{\"id\":\""<<s.ref.id<<"\",\"available\":"<<(s.flagVisible?"true":"false")
                <<"},\"nameVisible\":"<<(s.nameVisible?"true":"false")<<",\"box\":{\"left\":"<<x-s.width/2
                <<",\"right\":"<<x+s.width/2<<",\"top\":"<<y-s.height/2<<",\"bottom\":"<<y+s.height/2<<"}}";
        }
        std::cout<<"],\"actual\":[";
        for(std::size_t i=0;i<placed.size();++i) {
            if(i)std::cout<<",";const auto& p=placed[i];
            std::cout<<"{\"id\":\""<<p.ref.id<<"\",\"name\":"<<(p.nameVisible?"true":"false")
                <<",\"flag\":"<<(p.flagVisible?"true":"false")<<"}";
        }
        std::cout<<"]}";
    }
    std::cout<<"]\n";
}

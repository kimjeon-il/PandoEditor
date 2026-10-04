#include <pandoeditor/map/mapscenebuilder.h>
#include "world_fixture_loader.h"
#include <pandoeditor/spatialindex.h>
#include <algorithm>
#include <stdexcept>
#include <string>

namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool has(const RenderScene& scene,const std::string& key) {
    for(const auto& p:scene.polygons)if(p.key==key)return true;
    for(const auto& p:scene.strokes)if(p.key==key)return true;
    for(const auto& p:scene.points)if(p.key==key)return true;
    return false;
}
}
int main() {
    const auto document=m71fixture::loadWorldCorpusProject(QStringLiteral(M71_WORLD_FIXTURE));
    GeometryPacketCache cache;MapSceneBuilder builder(cache);MapViewState view;
    auto scene=builder.buildDocument(document,1,view,{},{});
    require(has(*scene,"territorial:DEU")&&has(*scene,"territorial:RUS"),"real country packets");
    require(has(*scene,"generic:DATELINE")&&has(*scene,"generic:POLAR"),"synthetic packets separate");
    for(const auto& unit:document.units)
        if(unit.kind!=pandoeditor::UnitKind::Country)
            require(has(*scene,"territorial:"+unit.id),"nested territorial packet");
    for(const auto& feature:document.hydro)require(has(*scene,"hydro:"+feature.id),"real hydro packet");
    for(const auto& feature:document.genericFeatures)require(has(*scene,"generic:"+feature.id),"generic packet");
    for(const auto& entry:document.distributionEntries)
        require(has(*scene,"distributionEntry:"+entry.id),"overlapping distribution packet");
    for(const auto& label:document.labels)require(has(*scene,"label:"+label.id),"label packet");
    require(scene->revisions.geometry>0&&scene->polygons.size()>=13,"typed corpus scene");
    pandoeditor::Project project;project.replace(document);
    for(const auto mode:{ProjectionMode::Flat,ProjectionMode::Globe}) {
        MapViewState camera;camera.mode=mode;
        auto prepared=builder.build(project.snapshot(),camera,{},{});
        const auto preparations=builder.preparationCount();
        const auto transients=builder.transientUpdateCount();
        const auto packets=cache.stats();
        for(int frame=0;frame<4;++frame) {
            if(frame==0){camera.translateX+=30;camera.translateY+=20;}
            if(frame==1)camera.scale*=1.25;
            if(frame==2){camera.rotationLongitude+=10;camera.rotationLatitude+=5;camera.rotationRoll+=3;}
            if(frame==3){camera.viewportWidth+=100;camera.viewportHeight+=50;}
            ++camera.revision;
            auto updated=builder.build(project.snapshot(),camera,{},prepared);
            require(updated==prepared,"camera publication retains exact prepared corpus scene");
            require(builder.preparationCount()==preparations,"camera performs no corpus preparation");
            require(builder.transientUpdateCount()==transients,"camera performs no corpus scene copy");
            require(cache.stats().builds==packets.builds&&cache.stats().hits==packets.hits,
                    "camera never visits corpus geometry cache");
        }
    }
    pandoeditor::GeoSpatialIndex index;index.rebuild(document,pandoeditor::validateDocument(document));
    const auto wrapped=index.query({{178,69,-178,81,true}});
    require(std::find(wrapped.begin(),wrapped.end(),pandoeditor::ObjectRef{"generic","DATELINE"})!=wrapped.end(),
        "M7.1 dateline spatial candidate");
    const auto pole=index.query({{90,89.7,150,90}});
    require(std::find(pole.begin(),pole.end(),pandoeditor::ObjectRef{"generic","POLAR"})!=pole.end(),
        "M7.1 polar spatial candidate");
}

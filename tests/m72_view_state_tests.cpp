#include <pandoeditor/map/mapviewstate.h>
#include <pandoeditor/document.h>
#include <stdexcept>

namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
void defaultsAndValidation() {
    const MapViewState view;
    require(view.mode==ProjectionMode::Flat&&view.viewportWidth==1&&view.viewportHeight==1&&
        view.scale==1&&view.devicePixelRatio==1&&view.revision==0,"valid flat defaults");
    require(validMapViewState(view),"default view validates");
    auto invalid=view;invalid.scale=0;
    require(!validMapViewState(invalid),"zero scale rejected");
    invalid=view;invalid.viewportHeight=0;
    require(!validMapViewState(invalid),"zero viewport rejected");
}
void revisionOnlyChangesWhenViewChanges() {
    MapViewState before;before.revision=7;
    pandoeditor::Geometry geometry{"Polygon",{},{},{{{{179,70},{-179,70},{-179,80},{179,70}}}}};
    const auto saved=geometry.polygons.front().front();
    const auto unchanged=advanceViewRevision(before,before);
    require(unchanged.revision==7,"unchanged view does not advance");
    auto next=before;next.mode=ProjectionMode::Globe;next.centerLongitude=179;
    const auto advanced=advanceViewRevision(before,next);
    require(advanced.revision==8&&advanced.mode==ProjectionMode::Globe,"view advances once");
    const auto& retained=geometry.polygons.front().front();
    require(retained.size()==saved.size(),"view cannot change ring size");
    for(std::size_t i=0;i<saved.size();++i)
        require(retained[i].x==saved[i].x&&retained[i].y==saved[i].y,"view cannot mutate canonical rings");
}
}
int main(){defaultsAndValidation();revisionOnlyChangesWhenViewChanges();}

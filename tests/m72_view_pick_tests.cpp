#include "territorial_fixture.h"
#include <pandoeditor/map/projectionengine.h>
#include <pandoeditor/spatialindex.h>
#include <algorithm>
#include <stdexcept>

namespace {
using namespace pandoeditor;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool has(const std::vector<ObjectRef>& rows,const std::string& id) {
    return std::find(rows.begin(),rows.end(),territorialRef(id))!=rows.end();
}
void flatAndGlobeCandidates() {
    ProjectDocument doc;DocumentIndex objects;
    doc.geometries.insert({"DEU",1},{"Polygon",{},{},{{
        {{10,50},{12,50},{12,52},{10,52},{10,50}}
    }}});
    appendTerritory(doc,{"DEU","Germany",{},UnitKind::General,false},{"DEU",1});
    objects.objects[territorialRef("DEU")]=0;
    GeoSpatialIndex index;index.rebuild(doc,objects);
    MapViewState view;view.viewportWidth=800;view.viewportHeight=600;
    view.translateX=400;view.translateY=300;view.scale=200;
    view.centerLongitude=11;view.centerLatitude=51;
    require(has(index.query(geographicPickWindows(400,300,8,view)),"DEU"),"flat screen to geographic candidate");
    view.mode=ProjectionMode::Globe;view.revision=1;
    require(has(index.query(geographicPickWindows(400,300,8,view)),"DEU"),"globe screen to geographic candidate");
    require(geographicPickWindows(800,300,8,view).empty(),"back/off-globe screen misses");
    require(index.geometryRevision()==1,"view change does not rebuild index");
}
}
int main(){flatAndGlobeCandidates();}

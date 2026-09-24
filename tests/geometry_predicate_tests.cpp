#include <pandoeditor/geometrypredicates.h>
#include <cassert>
using namespace pandoeditor;
static Geometry square(double x,double y,double size){Geometry g;g.type="Polygon";g.polygons.push_back(Polygon{Ring{{x,y},{x+size,y},{x+size,y+size},{x,y+size},{x,y}}});return g;}
int main(){auto parent=square(0,0,10),child=square(1,1,2),outside=square(9,1,2);assert(geometryContains(parent,child));assert(!geometryContains(parent,outside));assert(!geometrySignificantOverlap(parent,square(10,0,1)));assert(geometrySignificantOverlap(parent,child));}

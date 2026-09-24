#pragma once
#include <pandoeditor/document.h>
#include <string>

namespace pandoeditor {
enum class RenderPrimitiveRole { Fill, Line, Boundary, Point, Label };
struct MapRenderOrder {
    int pass=0,group=0;
    double object=0;
    bool operator<(const MapRenderOrder& rhs) const {
        if(pass!=rhs.pass)return pass<rhs.pass;
        if(group!=rhs.group)return group<rhs.group;
        return object<rhs.object;
    }
};
MapRenderOrder mapRenderOrder(const ProjectDocument& document,const ObjectRef& ref,
                             RenderPrimitiveRole role);
MapRenderOrder mapBuiltinHydroRenderOrder(const std::string& kind,
                                         RenderPrimitiveRole role,bool borderAligned=false);
int mapPickOrder(const ProjectDocument& document,const ObjectRef& ref);
int mapBuiltinHydroPickOrder();
}

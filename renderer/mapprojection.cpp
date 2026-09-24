#include "mapprojection.h"
#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <limits>

void MapProjection::rebuild(const pandoeditor::ProjectDocument& document)
{
    // Read-only render DTOs share geometry; they are not a second editable model.
    std::vector<pandoeditor::CountryView> objects;
    objects.reserve(document.units.size());
    for(const auto& unit:document.units) {
        const auto ref=pandoeditor::territorialRef(unit.id);
        const auto& style=document.presentation.objectStyles.at(ref);
        const auto& layer=pandoeditor::nativeLayerId(document,ref);
        const auto geometry=document.geometries.get(unit.geometry);
        objects.push_back({unit.id,unit.name,geometry->polygons,style.color,unit.notes,style.opacity,layer,unit.locked});
    }
    const auto index=pandoeditor::validateDocument(document);
    pandoeditor::Ring contentBounds;
    for(const auto& [ref,unused]:index.objects)if(ref.domain!="territorial") {
        const auto gr=pandoeditor::objectGeometry(document,index,ref);if(!gr)continue;
        const auto g=document.geometries.get(*gr);if(!g)continue;
        contentBounds.insert(contentBounds.end(),g->points.begin(),g->points.end());
        for(const auto& line:g->lines)contentBounds.insert(contentBounds.end(),line.begin(),line.end());
        for(const auto& polygon:g->polygons)for(const auto& ring:polygon)contentBounds.insert(contentBounds.end(),ring.begin(),ring.end());
    }
    const pandoeditor::MultiPolygon boundsGeometry{{contentBounds}};
    if(!contentBounds.empty())objects.push_back({"","",boundsGeometry,0,"",1,"",false});
    rebuild(objects);
    if(!contentBounds.empty())paths.removeLast(); // bounds-only DTO is never rendered/picked
    for(const auto& [ref,unused]:index.objects) {
        if(ref.domain=="territorial") continue;
        const auto geometryRef=pandoeditor::objectGeometry(document,index,ref); if(!geometryRef) continue;
        const auto geometry=document.geometries.get(*geometryRef); if(!geometry) continue;
        QString path; QVariantList points;
        double left=std::numeric_limits<double>::infinity(),top=left,right=-left,bottom=-left;
        auto append=[&](const pandoeditor::Ring& ring,bool closed) {
            for(std::size_t i=0;i<ring.size();++i) {
                const auto p=project(ring[i]); left=std::min(left,p.x); right=std::max(right,p.x); top=std::min(top,p.y); bottom=std::max(bottom,p.y);
                path+=QString("%1%2 %3 ").arg(i?"L":"M").arg(p.x,0,'g',17).arg(p.y,0,'g',17);
            }
            if(closed) path+="Z ";
        };
        for(const auto& polygon:geometry->polygons) for(const auto& ring:polygon) append(ring,true);
        for(const auto& line:geometry->lines) append(line,false);
        for(const auto& point:geometry->points) {
            append({point},false); const auto p=project(point); points.append(QVariantMap{{"x",p.x},{"y",p.y}});
        }
        paths.append(QVariantMap{{"countryId",QStringLiteral("content/")+QString::fromStdString(ref.domain)+"/"+QString::fromStdString(ref.id)},
            {"domain",QString::fromStdString(ref.domain)},{"objectId",QString::fromStdString(ref.id)},
            {"geometryType",QString::fromStdString(geometry->type)},{"points",points},{"path",path},
            {"left",left},{"top",top},{"width",std::max(.001,right-left)},{"height",std::max(.001,bottom-top)}});
    }
}
void MapProjection::rebuild(const std::vector<pandoeditor::CountryView>& countries)
{
    if(countries.empty()){paths.clear();width=height=cosLatitude=1;minX=maxLatitude=0;return;}
    double minLon=180,maxLon=-180,minLat=90,maxLat=-90;
    for(const auto& c:countries)for(const auto& p:c.polygons)for(const auto& r:p)for(auto v:r){
        minLon=std::min(minLon,v.x);maxLon=std::max(maxLon,v.x);
        minLat=std::min(minLat,v.y);maxLat=std::max(maxLat,v.y);
    }
    cosLatitude=std::max(0.01,std::cos((minLat+maxLat)*0.5*3.141592653589793/180));
    minX=minLon*cosLatitude;maxLatitude=maxLat;
    width=std::max(0.001,(maxLon-minLon)*cosLatitude);height=std::max(0.001,maxLat-minLat);
    paths.clear();
    for(const auto& c:countries){
        QString path;
        double left=std::numeric_limits<double>::infinity(),top=left,right=-left,bottom=-left;
        for(const auto& p:c.polygons)for(const auto& r:p){
            bool first=true;
            for(auto v:r){
                const double x=v.x*cosLatitude-minX,y=maxLat-v.y;
                left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);
                path+=QString("%1%2 %3 ").arg(first?"M":"L").arg(x,0,'g',17).arg(y,0,'g',17);first=false;
            }
            path+="Z ";
        }
        paths.append(QVariantMap{{"countryId",QString::fromStdString(c.id)},{"path",path},
            {"left",left},{"top",top},{"width",right-left},{"height",bottom-top}});
    }
}
pandoeditor::Point MapProjection::project(pandoeditor::Point point) const{return {point.x*cosLatitude-minX,maxLatitude-point.y};}
pandoeditor::Point MapProjection::unproject(double x,double y) const{return {(x+minX)/cosLatitude,maxLatitude-y};}

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
    rebuild(objects);
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

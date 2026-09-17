#include "mapprojection.h"
#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <limits>

void MapProjection::rebuild(const std::vector<pandoeditor::Country>& countries)
{
    double minLon=180,maxLon=-180,minLat=90,maxLat=-90;
    for (const auto& c:countries) for (const auto& p:c.polygons) for (const auto& r:p) for (auto v:r) {
        minLon=std::min(minLon,v.x); maxLon=std::max(maxLon,v.x);
        minLat=std::min(minLat,v.y); maxLat=std::max(maxLat,v.y);
    }
    cosLatitude=std::max(0.01,std::cos((minLat+maxLat)*0.5*3.141592653589793/180));
    minX=minLon*cosLatitude; maxLatitude=maxLat;
    width=std::max(0.001,(maxLon-minLon)*cosLatitude); height=std::max(0.001,maxLat-minLat);
    paths.clear();
    for (const auto& c:countries) {
        QString path;
        for (const auto& p:c.polygons) for (const auto& r:p) {
            bool first=true;
            for (auto v:r) {
                path += QString("%1%2 %3 ").arg(first ? "M" : "L").arg(v.x*cosLatitude-minX,0,'g',17).arg(maxLat-v.y,0,'g',17);
                first=false;
            }
            path += "Z ";
        }
        paths.append(QVariantMap{{"countryId",QString::fromStdString(c.id)},{"path",path}});
    }
}
pandoeditor::Point MapProjection::unproject(double x,double y) const
{
    return {(x+minX)/cosLatitude,maxLatitude-y};
}

#include <pandoeditor/geometrypredicates.h>
#include <cmath>
#include <algorithm>

namespace pandoeditor {
namespace {
constexpr double epsilon=1e-10;
double cross(Point a,Point b,Point c) { return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x); }
bool onSegment(Point a,Point b,Point p) { return std::abs(cross(a,b,p))<=epsilon && p.x>=std::min(a.x,b.x)-epsilon && p.x<=std::max(a.x,b.x)+epsilon && p.y>=std::min(a.y,b.y)-epsilon && p.y<=std::max(a.y,b.y)+epsilon; }
int location(const Ring& ring,Point p) {
    if(ring.size()<3)return 0; bool inside=false;
    for(std::size_t i=0,j=ring.size()-1;i<ring.size();j=i++) { auto a=ring[j],b=ring[i]; if(onSegment(a,b,p))return 2; if((a.y>p.y)!=(b.y>p.y) && p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x)inside=!inside; }
    return inside?1:0;
}
int location(const Geometry& g,Point p) {
    for(const auto& poly:g.polygons) { if(poly.empty())continue; const auto outer=location(poly.front(),p); if(outer==2)return 2; if(outer!=1)continue; bool hole=false; for(std::size_t i=1;i<poly.size();++i){auto h=location(poly[i],p);if(h==2)return 2;if(h==1)hole=true;} if(!hole)return 1; } return 0;
}
double ringArea(const Ring& ring) { double sum=0;for(std::size_t i=1;i<ring.size();++i)sum+=ring[i-1].x*ring[i].y-ring[i].x*ring[i-1].y;return std::abs(sum)*.5; }
int sign(double x) { return x>epsilon?1:x<-epsilon?-1:0; }
bool properCross(Point a,Point b,Point c,Point d) { auto a1=sign(cross(a,b,c)),a2=sign(cross(a,b,d)),b1=sign(cross(c,d,a)),b2=sign(cross(c,d,b));return a1*a2<0&&b1*b2<0; }
template<class F> void edges(const Geometry& g,F f){for(const auto& p:g.polygons)for(const auto& r:p)for(std::size_t i=1;i<r.size();++i)f(r[i-1],r[i]);}
bool crosses(const Geometry&a,const Geometry&b){bool hit=false;edges(a,[&](Point x,Point y){edges(b,[&](Point u,Point v){hit=hit||properCross(x,y,u,v);});});return hit;}
Point interior(const Ring& r) { if(r.empty())return {}; Point p{};for(std::size_t i=0;i+1<r.size();++i){p.x+=r[i].x;p.y+=r[i].y;}const double n=std::max<std::size_t>(1,r.size()-1);p.x/=n;p.y/=n;return p; }
}
double planarArea(const Geometry& g) noexcept { double sum=0;for(const auto& p:g.polygons)for(std::size_t i=0;i<p.size();++i)sum+=(i?-1:1)*ringArea(p[i]);return std::abs(sum); }
bool significantArea(double value,double reference) noexcept { return value>std::max(1e-10,std::abs(reference)*1e-9); }
bool geometryContains(const Geometry& container,const Geometry& subject) {
    if(container.polygons.empty()||subject.polygons.empty()||crosses(container,subject))return false;
    for(const auto& poly:subject.polygons) { if(poly.empty())return false; for(const auto& p:poly.front())if(location(container,p)==0)return false; }
    // A container hole may not be covered by the subject's filled area.
    for(const auto& poly:container.polygons)for(std::size_t i=1;i<poly.size();++i)if(location(subject,interior(poly[i]))==1)return false;
    return true;
}
bool geometrySignificantOverlap(const Geometry& a,const Geometry& b) {
    if(a.polygons.empty()||b.polygons.empty())return false;
    if(crosses(a,b)) return true;
    for(const auto& p:a.polygons)if(!p.empty()&&location(b,interior(p.front()))==1&&significantArea(planarArea(a),planarArea(b)))return true;
    for(const auto& p:b.polygons)if(!p.empty()&&location(a,interior(p.front()))==1&&significantArea(planarArea(b),planarArea(a)))return true;
    return false;
}
}

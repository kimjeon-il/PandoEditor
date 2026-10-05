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
// Only seam-crossing inputs use this read-only longitude view. Boolean
// geometry remains owned by the app's pinned calculator; predicates never
// rewrite rings, geometry bindings or serialized project data.
bool needsGeographicView(const Geometry& geometry) {
    bool westSeam=false,eastSeam=false;
    for(const auto& polygon:geometry.polygons)for(const auto& ring:polygon)for(std::size_t index=0;index<ring.size();++index) {
        const auto x=ring[index].x;if(x< -180||x>180)return true;westSeam|=x== -180;eastSeam|=x==180;
        if(index){const auto jump=std::abs(x-ring[index-1].x);if(jump>180&&jump<360)return true;}
    }
    // Already clipped seam components can contain opposite halves of the same
    // hole/notch even though no individual edge crosses a longitude strip.
    return westSeam&&eastSeam;
}
Geometry geographicView(const Geometry& source) {
    Geometry result=source;
    for(std::size_t p=0;p<source.polygons.size();++p) {
        const auto& polygon=source.polygons[p];if(polygon.empty()||polygon.front().empty())continue;
        const auto reference=polygon.front().front().x;
        for(std::size_t r=0;r<polygon.size();++r) {
            auto previous=reference;
            for(std::size_t index=0;index<polygon[r].size();++index) {
                auto x=polygon[r][index].x;if(!std::isfinite(x)||!std::isfinite(polygon[r][index].y))return {};
                const bool fullWorld=index&&std::abs(x-polygon[r][index-1].x)==360;
                if(!fullWorld){while(x-previous>180)x-=360;while(x-previous< -180)x+=360;}
                result.polygons[p][r][index].x=x;previous=x;
            }
        }
    }
    return result;
}
std::pair<double,double> longitudeRange(const Ring& ring) {
    double west=ring.front().x,east=west;for(const auto point:ring){west=std::min(west,point.x);east=std::max(east,point.x);}return {west,east};
}
int periodicRingLocation(const Ring& ring,Point point) {
    if(ring.size()<3)return 0;const auto [west,east]=longitudeRange(ring);bool boundary=false;
    const auto first=std::ceil((west-point.x-epsilon)/360),last=std::floor((east-point.x+epsilon)/360);
    for(auto copy=first;copy<=last;++copy) {const auto found=location(ring,{point.x+copy*360,point.y});if(found==1)return 1;boundary|=found==2;}
    return boundary?2:0;
}
int periodicLocation(const Geometry& geometry,Point point) {
    bool boundary=false;
    for(const auto& polygon:geometry.polygons) {
        if(polygon.empty())continue;const auto outer=periodicRingLocation(polygon.front(),point);if(!outer)continue;
        bool hole=false,holeBoundary=false;
        for(std::size_t index=1;index<polygon.size();++index) {const auto found=periodicRingLocation(polygon[index],point);hole|=found==1;holeBoundary|=found==2;}
        // A full-world shell's artificial seam may run through the excluded
        // periodic half of a hole; outer-boundary membership is not land there.
        if(hole)continue;
        if(outer==1&&!holeBoundary)return 1;boundary|=outer==2||holeBoundary;
    }
    return boundary?2:0;
}
std::vector<double> periodicCuts(const Geometry& geometry,Point a,Point b) {
    std::vector<double> cuts{0,1};const auto dx=b.x-a.x,dy=b.y-a.y,length=dx*dx+dy*dy;
    if(length<=epsilon*epsilon)return cuts;
    edges(geometry,[&](Point c,Point d){
        if(std::max(c.y,d.y)<std::min(a.y,b.y)-epsilon||std::min(c.y,d.y)>std::max(a.y,b.y)+epsilon)return;
        const auto first=std::ceil((std::min(a.x,b.x)-std::max(c.x,d.x)-epsilon)/360);
        const auto last=std::floor((std::max(a.x,b.x)-std::min(c.x,d.x)+epsilon)/360);
        for(auto copy=first;copy<=last;++copy) {
            const Point start{c.x+copy*360,c.y},finish{d.x+copy*360,d.y};
            const auto sx=finish.x-start.x,sy=finish.y-start.y,denominator=dx*sy-dy*sx;
            if(std::abs(denominator)>epsilon) {
                const auto t=((start.x-a.x)*sy-(start.y-a.y)*sx)/denominator;
                const auto u=((start.x-a.x)*dy-(start.y-a.y)*dx)/denominator;
                if(t>=-epsilon&&t<=1+epsilon&&u>=-epsilon&&u<=1+epsilon)cuts.push_back(std::clamp(t,0.,1.));
            } else if(std::abs(cross(a,b,start))<=epsilon) {
                for(const auto point:{start,finish}){const auto t=((point.x-a.x)*dx+(point.y-a.y)*dy)/length;if(t>=-epsilon&&t<=1+epsilon)cuts.push_back(std::clamp(t,0.,1.));}
            }
        }
    });
    std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end(),[](double a,double b){return std::abs(a-b)<=1e-12;}),cuts.end());return cuts;
}
bool geographicContains(const Geometry& container,const Geometry& subject) {
    if(container.polygons.empty()||subject.polygons.empty())return false;
    bool contained=true;
    edges(subject,[&](Point a,Point b){
        if(!contained)return;auto cuts=periodicCuts(container,a,b);const auto own=periodicCuts(subject,a,b);cuts.insert(cuts.end(),own.begin(),own.end());std::sort(cuts.begin(),cuts.end());
        const auto outside=[&](double t){const Point point{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};return periodicLocation(subject,point)!=0&&periodicLocation(container,point)==0;};
        for(const auto t:cuts)if(outside(t)){contained=false;return;}
        for(std::size_t index=1;index<cuts.size();++index)if(outside((cuts[index-1]+cuts[index])/2)){contained=false;return;}
    });
    if(!contained)return false;
    // Explicit holes can become notches in adjacent longitude strips. Inspect
    // both sides of every container boundary interval, so neither those holes
    // nor artificial seam edges are mistaken for filled/excluded territory.
    edges(container,[&](Point a,Point b){
        if(!contained)return;auto cuts=periodicCuts(subject,a,b);const auto other=periodicCuts(container,a,b);cuts.insert(cuts.end(),other.begin(),other.end());std::sort(cuts.begin(),cuts.end());
        const auto dx=b.x-a.x,dy=b.y-a.y,length=std::hypot(dx,dy);if(length<=epsilon)return;
        for(std::size_t index=1;index<cuts.size()&&contained;++index) {
            if(cuts[index]-cuts[index-1]<=1e-12)continue;const auto t=(cuts[index-1]+cuts[index])/2;const Point middle{a.x+dx*t,a.y+dy*t};
            auto distance=length*(cuts[index]-cuts[index-1])*1e-5;
            for(int attempt=0;attempt<10&&contained;++attempt,distance*=.1)for(const auto side:{-1.,1.}) {
                const Point sample{middle.x-side*dy/length*distance,middle.y+side*dx/length*distance};
                if(periodicLocation(container,sample)==0&&periodicLocation(subject,sample)==1){contained=false;break;}
            }
        }
    });
    return contained;
}
bool geographicOverlap(const Geometry& left,const Geometry& right) {
    const auto enters=[](const Geometry& boundary,const Geometry& other) {
        bool inside=false;edges(boundary,[&](Point a,Point b){if(inside)return;auto cuts=periodicCuts(other,a,b);const auto own=periodicCuts(boundary,a,b);cuts.insert(cuts.end(),own.begin(),own.end());std::sort(cuts.begin(),cuts.end());
            const auto enters=[&](double t){const Point point{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};return periodicLocation(boundary,point)!=0&&periodicLocation(other,point)==1;};
            for(const auto t:cuts)if(enters(t)){inside=true;return;}
            for(std::size_t index=1;index<cuts.size();++index)if(enters((cuts[index-1]+cuts[index])/2)){inside=true;return;}
        });return inside;
    };
    if(enters(left,right)||enters(right,left))return true;
    // Coincident components can overlap while unrelated islands mean neither
    // whole geometry contains the other. Find a witness in both filled sides,
    // rather than mistaking shared boundary-only samples for zero overlap.
    bool overlap=false;edges(left,[&](Point a,Point b){
        if(overlap)return;auto cuts=periodicCuts(right,a,b);const auto own=periodicCuts(left,a,b);cuts.insert(cuts.end(),own.begin(),own.end());std::sort(cuts.begin(),cuts.end());
        const auto dx=b.x-a.x,dy=b.y-a.y,length=std::hypot(dx,dy);if(length<=epsilon)return;
        for(std::size_t index=1;index<cuts.size()&&!overlap;++index) {
            if(cuts[index]-cuts[index-1]<=1e-12)continue;const auto t=(cuts[index-1]+cuts[index])/2;const Point middle{a.x+dx*t,a.y+dy*t};auto distance=length*(cuts[index]-cuts[index-1])*1e-5;
            for(int attempt=0;attempt<10&&!overlap;++attempt,distance*=.1)for(const auto side:{-1.,1.}) {
                const Point sample{middle.x-side*dy/length*distance,middle.y+side*dx/length*distance};
                if(periodicLocation(left,sample)==1&&periodicLocation(right,sample)==1){overlap=true;break;}
            }
        }
    });
    return overlap;
}

}
double planarArea(const Geometry& g) noexcept { double sum=0;for(const auto& p:g.polygons)for(std::size_t i=0;i<p.size();++i)sum+=(i?-1:1)*ringArea(p[i]);return std::abs(sum); }
bool significantArea(double value,double reference) noexcept { return value>std::max(1e-10,std::abs(reference)*1e-9); }
bool geometryContains(const Geometry& container,const Geometry& subject) {
    if(container.polygons.empty()||subject.polygons.empty())return false;
    if(needsGeographicView(container)||needsGeographicView(subject))return geographicContains(geographicView(container),geographicView(subject));
    if(crosses(container,subject))return false;
    for(const auto& poly:subject.polygons) { if(poly.empty())return false; for(const auto& p:poly.front())if(location(container,p)==0)return false; }
    // A container hole may not be covered by the subject's filled area.
    for(const auto& poly:container.polygons)for(std::size_t i=1;i<poly.size();++i)if(location(subject,interior(poly[i]))==1)return false;
    return true;
}
bool geometrySignificantOverlap(const Geometry& a,const Geometry& b) {
    if(a.polygons.empty()||b.polygons.empty())return false;
    if(needsGeographicView(a)||needsGeographicView(b))return geographicOverlap(geographicView(a),geographicView(b));
    if(crosses(a,b)) return true;
    // Aligned edges may overlap without any proper crossing (and a polygon's
    // vertex average can lie in a hole). Check boundary samples in both ways.
    bool interiorHit=false;
    edges(a,[&](Point p,Point q){interiorHit=interiorHit||location(b,p)==1||location(b,{(p.x+q.x)/2,(p.y+q.y)/2})==1;});
    edges(b,[&](Point p,Point q){interiorHit=interiorHit||location(a,p)==1||location(a,{(p.x+q.x)/2,(p.y+q.y)/2})==1;});
    if(interiorHit||geometryContains(a,b)||geometryContains(b,a))return true;
    for(const auto& p:a.polygons)if(!p.empty()&&location(b,interior(p.front()))==1&&significantArea(planarArea(a),planarArea(b)))return true;
    for(const auto& p:b.polygons)if(!p.empty()&&location(a,interior(p.front()))==1&&significantArea(planarArea(b),planarArea(a)))return true;
    return false;
}
}

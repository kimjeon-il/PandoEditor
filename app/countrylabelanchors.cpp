#include "countrylabelanchors.h"
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>

namespace {
using Ring=std::vector<pandoeditor::Point>;
using Polygon=std::vector<Ring>;
double normalizeLongitude(double value) {
    while(value>180)value-=360;while(value<-180)value+=360;return value;
}
Ring unwrap(const Ring& raw,std::optional<double> reference={}) {
    if(raw.empty())return {};
    Ring result;result.reserve(raw.size());
    const auto logical=raw.size()>1&&raw.front().x==raw.back().x&&raw.front().y==raw.back().y?
        raw.size()-1:raw.size();
    if(!logical)return {};
    double longitude=normalizeLongitude(raw.front().x);
    if(reference) {while(longitude-*reference>180)longitude-=360;while(longitude-*reference<-180)longitude+=360;}
    result.push_back({longitude,raw.front().y});
    for(std::size_t index=1;index<logical;++index) {
        longitude=normalizeLongitude(raw[index].x);
        while(longitude-result.back().x>180)longitude-=360;
        while(longitude-result.back().x<-180)longitude+=360;
        result.push_back({longitude,raw[index].y});
    }
    return result;
}
double ringArea(const Ring& ring) {
    double area=0;if(ring.size()<3)return 0;
    for(std::size_t i=0,j=ring.size()-1;i<ring.size();j=i++)area+=ring[j].x*ring[i].y-ring[i].x*ring[j].y;
    return area/2;
}
double segmentDistanceSquared(double x,double y,const pandoeditor::Point& a,const pandoeditor::Point& b) {
    double px=a.x,py=a.y,dx=b.x-px,dy=b.y-py;
    if(dx!=0||dy!=0) {const auto t=((x-px)*dx+(y-py)*dy)/(dx*dx+dy*dy);if(t>1){px=b.x;py=b.y;}else if(t>0){px+=dx*t;py+=dy*t;}}
    dx=x-px;dy=y-py;return dx*dx+dy*dy;
}
double polygonDistance(double x,double y,const Polygon& polygon) {
    bool inside=false;double minimum=std::numeric_limits<double>::infinity();
    for(const auto& ring:polygon)for(std::size_t i=0,j=ring.size()-1;i<ring.size();j=i++) {
        const auto& a=ring[i];const auto& b=ring[j];
        if(((a.y>y)!=(b.y>y))&&x<(b.x-a.x)*(y-a.y)/((b.y-a.y)==0?1e-30:(b.y-a.y))+a.x)inside=!inside;
        minimum=std::min(minimum,segmentDistanceSquared(x,y,a,b));
    }
    const auto distance=std::sqrt(minimum);return inside?distance:-distance;
}
struct Cell {
    double x=0,y=0,h=0,d=0,max=0;
    Cell(double px,double py,double ph,const Polygon& polygon):x(px),y(py),h(ph),d(polygonDistance(px,py,polygon)),max(d+h*std::sqrt(2.0)){}
};
struct CellLess {bool operator()(const Cell& a,const Cell& b) const{return a.max<b.max;}};
Cell centroidCell(const Polygon& polygon) {
    const auto& ring=polygon.front();double area=0,x=0,y=0;
    for(std::size_t i=0,j=ring.size()-1;i<ring.size();j=i++) {
        const auto cross=ring[i].x*ring[j].y-ring[j].x*ring[i].y;
        x+=(ring[i].x+ring[j].x)*cross;y+=(ring[i].y+ring[j].y)*cross;area+=cross*3;
    }
    if(std::abs(area)<1e-18)return Cell(ring.front().x,ring.front().y,0,polygon);
    return Cell(x/area,y/area,0,polygon);
}
pandoeditor::Point polylabel(const Polygon& polygon) {
    const auto& outer=polygon.front();double minX=outer.front().x,maxX=minX,minY=outer.front().y,maxY=minY;
    for(const auto& point:outer){minX=std::min(minX,point.x);maxX=std::max(maxX,point.x);minY=std::min(minY,point.y);maxY=std::max(maxY,point.y);}
    const auto width=maxX-minX,height=maxY-minY,cellSize=std::min(width,height);
    if(!(cellSize>0))return outer.front();
    std::priority_queue<Cell,std::vector<Cell>,CellLess> cells;const auto h=cellSize/2;
    for(double x=minX;x<maxX;x+=cellSize)for(double y=minY;y<maxY;y+=cellSize)cells.emplace(x+h,y+h,h,polygon);
    auto best=centroidCell(polygon);Cell box(minX+width/2,minY+height/2,0,polygon);if(box.d>best.d)best=box;
    const auto precision=std::max(0.0015,cellSize/512);
    while(!cells.empty()) {
        const auto cell=cells.top();cells.pop();if(cell.d>best.d)best=cell;if(cell.max-best.d<=precision)continue;
        const auto next=cell.h/2;cells.emplace(cell.x-next,cell.y-next,next,polygon);cells.emplace(cell.x+next,cell.y-next,next,polygon);
        cells.emplace(cell.x-next,cell.y+next,next,polygon);cells.emplace(cell.x+next,cell.y+next,next,polygon);
    }
    return {best.x,best.y};
}
}

CountryLabelAnchors::CountryLabelAnchors(QByteArray pinnedJson,QObject* parent):QObject(parent) {
    if(pinnedJson.isEmpty())return;
    const auto document=QJsonDocument::fromJson(pinnedJson);if(!document.isObject())return;
    const auto root=document.object();version_=root.value("version").toString();method_=root.value("method").toString();
    const auto anchors=root.value("anchors").toObject();
    for(auto it=anchors.begin();it!=anchors.end();++it) {
        const auto point=it.value().toArray();if(point.size()!=2||!point[0].isDouble()||!point[1].isDouble())continue;
        fixed_.insert(it.key(),{point[0].toDouble(),point[1].toDouble()});
        fixedBytes_+=sizeof(pandoeditor::Point)+sizeof(QString)+std::size_t(it.key().capacity())*sizeof(QChar);
    }
}
std::optional<pandoeditor::Point> CountryLabelAnchors::fixed(const QString& sourceId) const {
    const auto found=fixed_.constFind(sourceId);if(found==fixed_.cend())return std::nullopt;return found.value();
}
std::optional<pandoeditor::Point> CountryLabelAnchors::anchor(const QString& ownerId,const QString& sourceId) const {
    const auto derived=derived_.constFind(ownerId);if(derived!=derived_.cend())return derived->point;
    return invalidated_.contains(ownerId)?std::nullopt:fixed(sourceId);
}
std::optional<pandoeditor::Point> CountryLabelAnchors::derive(const pandoeditor::Geometry& geometry) {
    if(geometry.type!="Polygon"&&geometry.type!="MultiPolygon")return std::nullopt;
    const pandoeditor::Polygon* best=nullptr;double bestArea=-1;
    for(const auto& polygon:geometry.polygons)if(!polygon.empty()) {
        const auto outer=unwrap(polygon.front());const auto area=std::abs(ringArea(outer));if(area>bestArea){best=&polygon;bestArea=area;}
    }
    if(!best||best->empty())return std::nullopt;
    auto outer=unwrap(best->front());if(outer.size()<3)return std::nullopt;
    double reference=0;for(const auto& point:outer)reference+=point.x;reference/=outer.size();
    Polygon polygon{outer};for(std::size_t i=1;i<best->size();++i){auto ring=unwrap((*best)[i],reference);if(ring.size()>=3)polygon.push_back(std::move(ring));}
    double latitude=0;for(const auto& point:outer)latitude+=point.y;latitude/=outer.size();
    const auto xScale=std::max(0.08,std::cos(latitude*3.14159265358979323846/180));
    for(auto& ring:polygon)for(auto& point:ring)point.x=(point.x-reference)*xScale;
    auto result=polylabel(polygon);result.x=normalizeLongitude(result.x/xScale+reference);result.y=std::max(-90.,std::min(90.,result.y));return result;
}
void CountryLabelAnchors::setProjectScope(QString projectInstance) {
    if(projectInstance_==projectInstance)return;
    if(scopeEpoch_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("anchor scope exhausted");
    ++scopeEpoch_;projectInstance_=std::move(projectInstance);
    requests_.clear();derived_.clear();invalidated_.clear();invalidations_=failures_=stale_=0;
}
void CountryLabelAnchors::invalidateOwner(const QString& ownerId) {
    const bool hadRequest=requests_.remove(ownerId)>0;const bool hadDerived=derived_.remove(ownerId)>0;
    invalidated_.remove(ownerId);
    if((hadRequest||hadDerived)&&invalidations_!=std::numeric_limits<std::uint64_t>::max())++invalidations_;
}
quint64 CountryLabelAnchors::recompute(QString ownerId,pandoeditor::Geometry geometry,std::uint32_t version) {
    pandoeditor::GeometryRef ref{ownerId.toStdString(),version};
    return recompute(std::move(ownerId),std::move(geometry),std::move(ref));
}
quint64 CountryLabelAnchors::recompute(QString ownerId,pandoeditor::Geometry geometry,pandoeditor::GeometryRef ref) {
    if(generation_==std::numeric_limits<quint64>::max())throw std::overflow_error("label anchor token exhausted");
    invalidateOwner(ownerId);invalidated_.insert(ownerId);
    const auto token=++generation_;requests_[ownerId]={token,ref};
    auto* watcher=new QFutureWatcher<std::optional<pandoeditor::Point>>(this);
    connect(watcher,&QFutureWatcher<std::optional<pandoeditor::Point>>::finished,this,[this,watcher,token,ownerId,ref] {
        watcher->deleteLater();
        try{commitDerived(token,ownerId,ref,watcher->result());}
        catch(...){commitDerived(token,ownerId,ref,std::nullopt);}
    });
    watcher->setFuture(QtConcurrent::run([geometry=std::move(geometry)]{return derive(geometry);}));return token;
}
bool CountryLabelAnchors::commitDerived(quint64 token,const QString& ownerId,std::uint32_t version,
                                        std::optional<pandoeditor::Point> value) {
    const auto request=requests_.constFind(ownerId);
    if(request==requests_.cend()||request->ref.version!=version)return false;
    const auto ref=request->ref;return commitDerived(token,ownerId,ref,value);
}
bool CountryLabelAnchors::commitDerived(quint64 token,const QString& ownerId,const pandoeditor::GeometryRef& ref,
                                        std::optional<pandoeditor::Point> value) {
    const auto request=requests_.constFind(ownerId);
    if(request==requests_.cend()||request->token!=token||!(request->ref==ref)) {
        if(stale_!=std::numeric_limits<std::uint64_t>::max())++stale_;return false;
    }
    requests_.erase(request);
    if(!value){if(failures_!=std::numeric_limits<std::uint64_t>::max())++failures_;emit recomputeFailed(ownerId);return false;}
    derived_[ownerId]={ref,*value};emit changed();return true;
}
std::size_t CountryLabelAnchors::derivedStorageBytes() const {
    std::size_t bytes=0;
    for(auto it=derived_.cbegin();it!=derived_.cend();++it)
        bytes+=sizeof(Derived)+sizeof(QString)+std::size_t(it.key().capacity())*sizeof(QChar)+it->ref.id.capacity();
    return bytes;
}
pandoeditor::ResourceCacheSnapshot CountryLabelAnchors::resourceCacheSnapshot() const {
    pandoeditor::ResourceCacheSnapshot result;result.scopeEpoch=scopeEpoch_;
    result.residentCount=std::size_t(fixed_.size()+derived_.size());
    result.residentBytes=fixedBytes_+derivedStorageBytes();result.budgetBytes=result.residentBytes;
    result.protectedBytes=result.residentBytes;result.activeBytes=derivedStorageBytes();
    result.pendingCount=std::size_t(requests_.size());result.pendingUnknownCount=result.pendingCount;
    result.invalidationCount=invalidations_;result.failureCount=failures_;result.staleCompletionCount=stale_;
    result.protectionCounts[static_cast<std::size_t>(pandoeditor::ResourceProtection::Visible)]=result.residentCount;
    return result;
}

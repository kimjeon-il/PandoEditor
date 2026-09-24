#include "referencetracing.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace {
double luminance(QRgb pixel){return .2126*qRed(pixel)+.7152*qGreen(pixel)+.0722*qBlue(pixel);}
double gradient(const QImage& image,int x,int y){x=std::clamp(x,1,image.width()-2);y=std::clamp(y,1,image.height()-2);const double gx=luminance(image.pixel(x+1,y))-luminance(image.pixel(x-1,y));const double gy=luminance(image.pixel(x,y+1))-luminance(image.pixel(x,y-1));return std::hypot(gx,gy);}
}
QVector<QPoint> referenceLiveWire(const QImage& source,QPoint start,QPoint end,int maximumVisited)
{
    const QImage image=source.convertToFormat(QImage::Format_RGB32);if(image.width()<3||image.height()<3||!image.rect().contains(start)||!image.rect().contains(end))return {};
    const int count=image.width()*image.height(),startIndex=start.y()*image.width()+start.x(),endIndex=end.y()*image.width()+end.x();QVector<double> distances(count,std::numeric_limits<double>::infinity());QVector<int> previous(count,-1);using Entry=std::pair<double,int>;std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> queue;distances[startIndex]=0;queue.push({0,startIndex});int visited=0;
    constexpr int dx[8]={-1,0,1,-1,1,-1,0,1},dy[8]={-1,-1,-1,0,0,1,1,1};
    while(!queue.empty()&&visited++<maximumVisited){const auto [distance,index]=queue.top();queue.pop();if(distance!=distances[index])continue;if(index==endIndex)break;const int x=index%image.width(),y=index/image.width();for(int direction=0;direction<8;++direction){const int nx=x+dx[direction],ny=y+dy[direction];if(nx<1||ny<1||nx>=image.width()-1||ny>=image.height()-1)continue;const int next=ny*image.width()+nx;const double edge=(direction%2?1.:1.41421356237)*(1.+255.-std::min(255.,gradient(image,nx,ny)));if(distance+edge<distances[next]){distances[next]=distance+edge;previous[next]=index;queue.push({distances[next],next});}}}
    if(startIndex!=endIndex&&previous[endIndex]<0)return {};QVector<QPoint> path;for(int index=endIndex;index>=0;index=previous[index]){path.prepend({index%image.width(),index/image.width()});if(index==startIndex)break;}return path;
}
QVector<QPointF> refineReferenceLine(const QImage& source,const QVector<QPointF>& line,int radius)
{
    const QImage image=source.convertToFormat(QImage::Format_RGB32);if(image.width()<3||image.height()<3)return line;QVector<QPointF> result;result.reserve(line.size());for(int index=0;index<line.size();++index){const auto point=line[index];QPointF tangent=index+1<line.size()?line[index+1]-point:index?point-line[index-1]:QPointF(1,0);const double length=std::hypot(tangent.x(),tangent.y());QPointF normal=length?QPointF(-tangent.y()/length,tangent.x()/length):QPointF(0,1),best=point;double score=-1;for(int offset=-radius;offset<=radius;++offset){const QPoint candidate=(point+normal*offset).toPoint();if(!image.rect().adjusted(1,1,-1,-1).contains(candidate))continue;const double value=gradient(image,candidate.x(),candidate.y());if(value>score){score=value;best=candidate;}}result.push_back(best);}return result;
}

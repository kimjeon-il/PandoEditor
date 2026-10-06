#include "terrainrendercontract.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace TerrainRenderContract {
namespace {double mix(double a,double b,double t){return a+(b-a)*t;}}
bool maskCurrent(bool prepared,bool hasTexture,const MaskStamp& frame,const MaskStamp& request) {
    return prepared&&hasTexture&&frame.viewRevision==request.viewRevision&&
        frame.sceneRevision==request.sceneRevision&&frame.logicalSize==request.logicalSize&&
        frame.textureSize==request.textureSize&&!frame.textureSize.isEmpty()&&
        frame.logicalSize.width()>0&&frame.logicalSize.height()>0&&
        std::isfinite(frame.logicalSize.width())&&std::isfinite(frame.logicalSize.height());
}
QImage rasterDisplayImage(const QImage& raw,bool gray) {
    if(raw.isNull())return {};
    const auto straight=raw.convertToFormat(QImage::Format_RGBA8888);
    QImage display(raw.size(),QImage::Format_RGBX8888);
    if(display.isNull())return {};
    for(int y=0;y<raw.height();++y) {
        const auto* source=straight.constScanLine(y);auto* target=display.scanLine(y);
        for(int x=0;x<raw.width();++x) {
            const auto neutral=source[x*4+3];
            for(int channel=0;channel<3;++channel)
                target[x*4+channel]=gray?neutral:source[x*4+channel];
            target[x*4+3]=255;
        }
    }
    return display;
}
double elevation(unsigned red,unsigned green){return red*256.0+green-12000.0;}
QVector4D uvBounds(QSize size,int gutter) {
    if(gutter<0||size.width()<=2*gutter||size.height()<=2*gutter)
        throw std::invalid_argument("DEM texture must include a nonempty interior");
    return {float(gutter)/size.width(),float(gutter)/size.height(),
            float(size.width()-gutter)/size.width(),float(size.height()-gutter)/size.height()};
}
double derivedShade(double u,double v,QSize texture,QSize level,double latitude,
                    const std::function<double(int,int)>& sample) {
    if(texture.isEmpty()||level.isEmpty())throw std::invalid_argument("DEM dimensions must be positive");
    const double px=u*texture.width()-0.5,py=v*texture.height()-0.5;
    const int x=int(std::floor(px)),y=int(std::floor(py));
    const double fx=px-x,fy=py-y;const bool sx=fx>=0.5,sy=fy>=0.5;
    const int ox=sx?2:-1,oy=sy?2:-1;
    const auto h00=sample(x,y),h10=sample(x+1,y),h01=sample(x,y+1),h11=sample(x+1,y+1);
    const auto hx0=sample(x+ox,y),hx1=sample(x+ox,y+1),hy0=sample(x,y+oy),hy1=sample(x+1,y+oy);
    const double cx=std::fmod(fx+0.5,1.0),cy=std::fmod(fy+0.5,1.0);
    const auto east=[&](double left,double right,double outer){
        const auto prev=sx?left:outer,mid=sx?right:left,next=sx?outer:right;
        return mix(mid-prev,next-mid,cx);
    };
    const auto south=[&](double top,double bottom,double outer){
        const auto prev=sy?top:outer,mid=sy?bottom:top,next=sy?outer:bottom;
        return mix(mid-prev,next-mid,cy);
    };
    const auto riseEast=mix(east(h00,h10,hx0),east(h01,h11,hx1),fy)/
        (40030228.884*std::max(0.0001,std::cos(latitude*0.01745329251994329577))/level.width());
    const auto riseNorth=-mix(south(h00,h01,hy0),south(h10,h11,hy1),fx)/(20015114.442/level.height());
    const auto length=std::sqrt(riseEast*riseEast+riseNorth*riseNorth+1);
    const auto dot=(riseEast*0.5-riseNorth*0.5+0.70710678)/length;
    return 0.42+0.58*std::max(0.0,dot);
}
double blendedShade(double encoded,double derived,double blend,double latitude) {
    return blend>0.001&&std::abs(latitude)<89.5?mix(encoded,derived,blend):encoded;
}
Color color(double height,double shade,Color tint,bool physical,bool landPass,bool dark) {
    Color result{shade,shade,shade};
    if(physical) {
        const double depth=std::pow(std::clamp(-height/8000.0,0.0,1.0),0.6);
        const Color base=landPass?tint:Color{mix(0.42,0.10,depth),mix(0.66,0.24,depth),mix(0.82,0.39,depth)};
        const auto ratio=std::clamp(shade/(0.42+0.58*0.70710678),0.5,1.2);
        result={base.r*ratio,base.g*ratio,base.b*ratio};
    }
    if(dark)result={result.r*mix(1,0.60,0.48),result.g*mix(1,0.68,0.48),result.b*mix(1,0.76,0.48)};
    return result;
}
}

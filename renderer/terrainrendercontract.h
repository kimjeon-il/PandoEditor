#pragma once
#include <QSize>
#include <QImage>
#include <QVector4D>
#include <functional>

// CPU reference for the fixed Web DEM shader; it never processes display tiles.
namespace TerrainRenderContract {
struct Color {double r,g,b;};
struct MaskStamp {
    quint64 viewRevision=0,sceneRevision=0;
    QSizeF logicalSize;
    QSize textureSize;
};
bool maskCurrent(bool prepared,bool hasTexture,const MaskStamp& frame,const MaskStamp& request);
// Raster A stores Gray Earth data, not transparency. Only this derived display
// backing is made opaque; the verified original decode remains unchanged.
QImage rasterDisplayImage(const QImage& raw,bool gray);
double elevation(unsigned red,unsigned green);
QVector4D uvBounds(QSize textureSize,int gutter);
// sample supplies decoded texel heights with the texture's ClampToEdge rule.
double derivedShade(double u,double v,QSize textureSize,QSize levelSize,
                    double latitude,const std::function<double(int,int)>& sample);
double blendedShade(double encoded,double derived,double blend,double latitude);
Color color(double height,double shade,Color tint,bool physical,bool landPass,bool dark);
}

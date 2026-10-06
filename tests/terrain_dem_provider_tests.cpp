#include "terrainprovider.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImageWriter>
#include <QImageReader>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
bool writeLossless(const QImage& image,const QString& path) {
    QImageWriter writer(path,"webp");writer.setQuality(100);return writer.write(image);
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        require(argc==2,"fixed DEM manifest argument required");
        QFile file(QString::fromLocal8Bit(argv[1]));require(file.open(QIODevice::ReadOnly),"manifest readable");
        const auto manifest=file.readAll();QTemporaryDir root;require(root.isValid(),"isolated fixture root");
        TerrainTileProvider provider(manifest,root.path());
        require(provider.available(),"current fixed DEM representation must be accepted");
        std::cout<<"PASS fixed DEM manifest\n";
        const auto first=root.path()+"/terrain/v0.13.0/0/0-0.webp";
        require(QDir().mkpath(QFileInfo(first).absolutePath()),"fixture directory");
        QImage raw(1026,677,QImage::Format_RGBA8888);raw.fill(QColor(50,33,172,255));
        require(writeLossless(raw,first),"lossless WebP writer required");
        const auto color=provider.loadTile(0,0,0,false),gray=provider.loadTile(0,0,0,true);
        require(!color.isNull()&&color.pixelColor(400,250)==QColor(50,33,172,255),"raw elevation and shade bytes preserved");
        require(color.constBits()==gray.constBits(),"DEM gray shares raw bytes instead of qGray elevation");
        require(provider.resourceCacheSnapshot().residentCount==1,"DEM style changes must not duplicate cache");
        std::cout<<"PASS raw channels and style cache identity\n";
        QImage broken(4,4,QImage::Format_RGBA8888);broken.fill(Qt::white);
        require(writeLossless(broken,root.path()+"/terrain/v0.13.0/0/1-0.webp"),"bad-size fixture write");
        require(provider.loadTile(0,1,0).isNull(),"DEM dimensions include exact real gutter and edge size");
        require(provider.decodeError().contains("1-0.webp"),"failed decoding exposes the exact source path");
        std::cout<<"PASS truncated dimensions reject\n";
        const auto tintPath=root.path()+"/terrain/v0.13.3/tint.webp";
        require(QDir().mkpath(QFileInfo(tintPath).absolutePath()),"tint fixture directory");
        QImage tint(4096,2048,QImage::Format_RGB32);tint.fill(QColor(140,170,120));
        require(writeLossless(tint,tintPath),"tint fixture write");
        const auto loaded=provider.loadTint();require(loaded.size()==QSize(4096,2048),"tint dimensions");
        require(loaded.format()==QImage::Format_RGBA8888,"accounted RGBA8 tint format");
        require(provider.cachedBytes()==std::size_t(color.sizeInBytes()+loaded.sizeInBytes()),"all decoded tile and tint bytes counted");
        std::cout<<"PASS tint decoded byte accounting\n";
        require(provider.decodeError().contains("1-0.webp"),"unrelated successful tint cannot erase tile decode failure");
        QImage repaired(328,677,QImage::Format_RGBA8888);repaired.fill(QColor(50,33,172,255));
        require(writeLossless(repaired,root.path()+"/terrain/v0.13.0/0/1-0.webp"),"repair fixture only the failed path");
        require(!provider.loadTile(0,1,0).isNull()&&provider.decodeError().isEmpty(),"successful retry clears its own decode failure");
        std::cout<<"PASS persistent decode failure and same-path recovery\n";
        TerrainTileProvider refused(manifest,root.path(),[](const QString&){return QString();});
        require(refused.available(),"pinned metadata remains usable while optional data is absent");
        require(refused.loadTile(0,0,0).isNull()&&refused.loadTint().isNull(),
                "verifier refusal cannot fall through to unverified bytes under the cache root");
        std::cout<<"PASS verifier refusal preserves missing/corrupt state\n";
        auto bad=QJsonDocument::fromJson(manifest).object();bad["version"]="0.13.99";
        require(!TerrainTileProvider(QJsonDocument(bad).toJson(),root.path()).available(),"unknown data version rejects");
        auto encoding=QJsonDocument::fromJson(manifest).object();auto elevation=encoding["elevation"].toObject();
        elevation["biasMeters"]=0;encoding["elevation"]=elevation;
        require(!TerrainTileProvider(QJsonDocument(encoding).toJson(),root.path()).available(),"different encoding rejects");
        std::cout<<"PASS version and encoding identity\n";
        provider.protectVisible({});provider.setCacheBudget(0);
        require(provider.cachedBytes()==0,"released DEM and tint become evictable");
        std::cout<<"PASS released resource reclamation\n";
        auto rasterPath=QString::fromLocal8Bit(argv[1]);
        rasterPath.replace("v0.13.3/manifest.json","v0.12.6/manifest.json");
        rasterPath.replace("v0.13.3\\manifest.json","v0.12.6\\manifest.json");
        QFile rasterManifest(rasterPath);require(rasterManifest.open(QIODevice::ReadOnly),"fixed raster fallback manifest");
        TerrainTileProvider raster(rasterManifest.readAll(),root.path());require(raster.available(),"raster fallback remains supported");
        const auto rasterTile=root.path()+"/terrain/v0.12.6/0/0-0.webp";
        require(QDir().mkpath(QFileInfo(rasterTile).absolutePath()),"raster fixture directory");
        QImage rawRaster(16,16,QImage::Format_RGBA8888);rawRaster.fill(QColor(255,0,0,64));
        // Qt 6.8.3 scans a WebPBitstreamFeatures-sized header; a uniform tiny
        // lossless WebP can be shorter. Distinct data pixels keep this decoder
        // fixture large enough without changing the independently tested pixel.
        rawRaster.setPixelColor(15,15,QColor(17,99,33,64));
        rawRaster.setPixelColor(14,15,QColor(150,50,240,64));
        require(writeLossless(rawRaster,rasterTile),"raster RGBA data fixture");
        QImageReader rasterReader(rasterTile,"webp");const auto directRaster=rasterReader.read();
        std::cout<<"raster bytes="<<QFileInfo(rasterTile).size()<<" direct format="<<int(directRaster.format())<<" errorCode="<<int(rasterReader.error())
                 <<" path="<<raster.relativeTilePath(TerrainTileSpec{}).toStdString()<<'\n';
        const auto rasterRaw=raster.loadTile(0,0,0);
        const auto observed=rasterRaw.pixelColor(0,0);
        std::cout<<"raster decoded RGBA="<<observed.red()<<','<<observed.green()<<','<<observed.blue()<<','<<observed.alpha()
                 <<" format="<<int(rasterRaw.format())<<" expected=255,0,0,64\n";
        require(observed==QColor(255,0,0,64),"original raster RGBA remains decoded data");
        require(raster.loadTile(0,0,0,true).pixelColor(0,0)==QColor(64,64,64,255),
                "fixed Web gray uses encoded A and opaque output, not RGB luminance");
        std::cout<<"PASS fixed raster alpha-data gray interpretation\n9/9 cases passed; fail=0 skip=0\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}

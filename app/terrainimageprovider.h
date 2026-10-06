#pragma once
#include "../renderer/terrainprovider.h"
#include <QQuickImageProvider>
#include <QObject>
#include <memory>
#include <mutex>
#include <map>

struct TerrainImageFrame {
    QImage image,tint;
    QSize levelSize;
    int gutter=0;
    bool dem=false;
    quint64 sourceEpoch=0;
};

// Shared typed entry point for geographic terrain items and the image provider.
class TerrainImageBridge final : public QObject {
    Q_OBJECT
public:
    explicit TerrainImageBridge(QObject* parent=nullptr):QObject(parent){}
    void setSource(std::shared_ptr<TerrainTileProvider> source);
    QImage acquire(int level,int column,int row,bool gray=false) const;
    TerrainImageFrame acquireFrame(int level,int column,int row) const;
    void setRenderStyle(bool darkTheme,float shadeBlend);
    bool darkTheme() const {return darkTheme_;}
    float shadeBlend() const {return shadeBlend_;}
    quint64 sourceEpoch() const {return sourceEpoch_;}
    void setDisplayBacking(const QObject* owner,const QImage& backing);
    void releaseDisplayBacking(const QObject* owner);
    quint64 displayBackingBytes() const;
    quint64 displayBackingCount() const;
signals:
    void sourceChanged();
    void renderStyleChanged();
    void displayBackingChanged();
private:
    std::shared_ptr<TerrainTileProvider> source_;
    quint64 sourceEpoch_=0;
    bool darkTheme_=false;
    float shadeBlend_=0;
    // GUI-thread ledger of the extra opaque raster display images held by items.
    // Excludes decoder temporaries, upload staging, GPU textures and raw cache.
    std::map<const QObject*,std::pair<qint64,quint64>> displayBackings_;
};

// Qt Quick requests only visible tile images; the provider keeps one pinned,
// byte-bounded decode cache shared with the viewport's terrain dataset.
class TerrainImageProvider final : public QQuickImageProvider {
public:
    TerrainImageProvider():QQuickImageProvider(QQuickImageProvider::Image){}
    void setSource(std::shared_ptr<TerrainTileProvider> source);
    QImage requestImage(const QString& id,QSize* size,const QSize& requestedSize) override;
private:
    std::mutex mutex_;
    std::shared_ptr<TerrainTileProvider> source_;
};

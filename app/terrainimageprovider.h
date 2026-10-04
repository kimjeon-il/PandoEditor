#pragma once
#include "../renderer/terrainprovider.h"
#include <QQuickImageProvider>
#include <QObject>
#include <memory>
#include <mutex>

// Shared typed entry point for geographic terrain items and the image provider.
class TerrainImageBridge final : public QObject {
    Q_OBJECT
public:
    explicit TerrainImageBridge(QObject* parent=nullptr):QObject(parent){}
    void setSource(std::shared_ptr<TerrainTileProvider> source);
    QImage acquire(int level,int column,int row,bool gray=false) const;
signals:
    void sourceChanged();
private:
    std::shared_ptr<TerrainTileProvider> source_;
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

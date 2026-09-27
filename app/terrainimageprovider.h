#pragma once
#include "../renderer/terrainprovider.h"
#include <QQuickImageProvider>
#include <memory>
#include <mutex>

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

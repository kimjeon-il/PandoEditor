#include "terrainimageprovider.h"
#include <QStringList>
#include <utility>

void TerrainImageProvider::setSource(std::shared_ptr<TerrainTileProvider> source) {
    std::lock_guard lock(mutex_);source_=std::move(source);
}
QImage TerrainImageProvider::requestImage(const QString& id,QSize* size,const QSize&) {
    std::shared_ptr<TerrainTileProvider> source;
    {std::lock_guard lock(mutex_);source=source_;}
    if(!source||!source->available())return {};
    const auto parts=id.split('/');
    if(parts.size()!=3)return {};
    bool a=false,b=false,c=false;
    const auto level=parts[0].toInt(&a),column=parts[1].toInt(&b),row=parts[2].toInt(&c);
    if(!a||!b||!c)return {};
    const auto image=source->loadTile(level,column,row);
    if(size)*size=image.size();
    return image;
}

void TerrainImageBridge::setSource(std::shared_ptr<TerrainTileProvider> source) {
    source_=std::move(source);emit sourceChanged();
}
QImage TerrainImageBridge::acquire(int level,int column,int row,bool gray) const {
    const auto source=source_;
    return source?source->loadTile(level,column,row,gray):QImage{};
}

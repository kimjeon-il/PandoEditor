#include "terrainimageprovider.h"
#include <QStringList>
#include <utility>
#include <algorithm>
#include <set>

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
    if(source_==source)return;
    // Source replacement retires this source's viewport ownership. Retained
    // providers may remain cached, but cannot keep visible/fallback/pending pins.
    // A same-source LOD transition keeps its separate display handoff policy.
    if(source_)source_->protectVisible({});
    source_=std::move(source);++sourceEpoch_;emit sourceChanged();
}

TerrainImageFrame TerrainImageBridge::acquireFrame(int level,int column,int row) const {
    TerrainImageFrame frame;frame.sourceEpoch=sourceEpoch_;
    const auto source=source_;if(!source||!source->available())return frame;
    frame.dem=source->isDem();frame.gutter=source->gutter();
    frame.levelSize=source->levelSize(level);
    // DEM RG are packed height bytes. Gray is a shader mode, never qGray(RG).
    frame.image=source->loadTile(level,column,row,false);
    if(frame.dem)frame.tint=source->loadTint();
    return frame;
}
void TerrainImageBridge::setRenderStyle(bool darkTheme,float shadeBlend) {
    shadeBlend=std::clamp(shadeBlend,0.f,1.f);
    if(darkTheme_==darkTheme&&shadeBlend_==shadeBlend)return;
    darkTheme_=darkTheme;shadeBlend_=shadeBlend;emit renderStyleChanged();
}
void TerrainImageBridge::setDisplayBacking(const QObject* owner,const QImage& backing) {
    if(!owner)return;
    if(backing.isNull()){releaseDisplayBacking(owner);return;}
    const std::pair<qint64,quint64> value{backing.cacheKey(),quint64(backing.sizeInBytes())};
    const auto found=displayBackings_.find(owner);
    if(found!=displayBackings_.end()&&found->second==value)return;
    displayBackings_[owner]=value;emit displayBackingChanged();
}
void TerrainImageBridge::releaseDisplayBacking(const QObject* owner) {
    if(displayBackings_.erase(owner))emit displayBackingChanged();
}
quint64 TerrainImageBridge::displayBackingBytes() const {
    std::set<qint64> seen;quint64 bytes=0;
    for(const auto& entry:displayBackings_)
        if(seen.insert(entry.second.first).second)bytes+=entry.second.second;
    return bytes;
}
quint64 TerrainImageBridge::displayBackingCount() const {
    std::set<qint64> seen;for(const auto& entry:displayBackings_)seen.insert(entry.second.first);
    return quint64(seen.size());
}
QImage TerrainImageBridge::acquire(int level,int column,int row,bool gray) const {
    const auto source=source_;
    return source?source->loadTile(level,column,row,gray):QImage{};
}

#include "terrainimageprovider.h"
#include <QStringList>
#include <utility>
#include <algorithm>
#include <set>
#include <QCryptographicHash>
#include <QMetaObject>
#include <QPointer>
#include "../renderer/terrainrendercontract.h"

TerrainImageBridge::TerrainImageBridge(QObject* parent):QObject(parent) {
    qRegisterMetaType<TerrainRenderObservation>();qRegisterMetaType<TerrainRenderOwnerSnapshot>();
}
QString TerrainImageBridge::imageContentKey(const QImage& image,quint64 sourceEpoch) {
    if(image.isNull())return {};
    const auto rgba=image.convertToFormat(QImage::Format_RGBA8888);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const auto dimensions=QByteArray::number(sourceEpoch)+":"+QByteArray::number(rgba.width())+":"+
        QByteArray::number(rgba.height())+":";
    hash.addData(dimensions);
    for(int y=0;y<rgba.height();++y)
        hash.addData(QByteArrayView(reinterpret_cast<const char*>(rgba.constScanLine(y)),qsizetype(rgba.width())*4));
    return QStringLiteral("rgba-sha256:")+QString::fromLatin1(hash.result().toHex());
}
TerrainRenderResource TerrainImageBridge::prepareRenderResource(const TerrainTileSpec& spec,bool gray) const {
    QString knownTint;
    const auto source=source_;const auto epoch=sourceEpoch_;
    const auto tint=source&&source->isDem()?source->loadTint():QImage{};
    if(!tint.isNull()) {
        // QImage allocation identity is only a memoization key. The published
        // identity is the SHA of immutable pixels and source, never cacheKey.
        std::lock_guard lock(tintIdentityMutex_);
        if(tintIdentityEpoch_!=epoch||tintIdentityImageKey_!=tint.cacheKey()) {
            tintIdentity_=imageContentKey(tint,epoch);
            tintIdentityEpoch_=epoch;tintIdentityImageKey_=tint.cacheKey();
        }
        knownTint=tintIdentity_;
    }
    return prepareRenderResource(source,epoch,spec,gray,knownTint,tint.cacheKey());
}
TerrainRenderResource TerrainImageBridge::prepareRenderResource(std::shared_ptr<TerrainTileProvider> source,
    quint64 sourceEpoch,const TerrainTileSpec& spec,bool gray,const QString& knownTintContentKey,qint64 knownTintCacheKey) {
    TerrainRenderResource result;result.gray=gray;result.frame.sourceEpoch=sourceEpoch;
    if(!source||!source->available())return result;
    result.frame.dem=source->isDem();result.frame.gutter=source->gutter();result.frame.levelSize=source->levelSize(spec.level);
    result.frame.image=source->loadTile(spec.level,spec.column,spec.row,false);
    if(result.frame.dem)result.frame.tint=source->loadTint();
    result.tintContentKey=result.frame.tint.isNull()?QString{}:
        knownTintContentKey.isEmpty()||result.frame.tint.cacheKey()!=knownTintCacheKey?
            imageContentKey(result.frame.tint,sourceEpoch):knownTintContentKey;
    if(!result.frame.dem)result.frame.image=TerrainRenderContract::rasterDisplayImage(result.frame.image,gray);
    result.resource={QStringLiteral("%1/%2/%3").arg(spec.level).arg(spec.column).arg(spec.row)+
        (result.frame.dem?QStringLiteral("/raw"):gray?QStringLiteral("/raster-gray"):QStringLiteral("/raster-color")),
        imageContentKey(result.frame.image,result.frame.sourceEpoch),spec.level,
        {spec.west,spec.north,spec.east,spec.south}};
    return result;
}
void TerrainImageBridge::setRenderInput(TerrainRenderInput input) {
    {std::lock_guard lock(renderMutex_);renderInput_=std::make_shared<const TerrainRenderInput>(std::move(input));}
    emit renderInputChanged();
}
std::shared_ptr<const TerrainRenderInput> TerrainImageBridge::renderInput() const {
    std::lock_guard lock(renderMutex_);return renderInput_;
}
TerrainRenderOwnerSnapshot TerrainImageBridge::ownerSnapshot() const {
    std::lock_guard lock(renderMutex_);return renderOwner_;
}
TerrainRenderStats TerrainImageBridge::renderStats() const {
    std::lock_guard lock(renderMutex_);return renderStats_;
}
std::optional<TerrainRenderAdoption> TerrainImageBridge::renderAdoption() const {
    std::lock_guard lock(renderMutex_);return renderAdoption_;
}
void TerrainImageBridge::acknowledgeDisplay(const TerrainDisplayReceipt& receipt,std::vector<TerrainDisplayResourceId> protectedIds) {
    {std::lock_guard lock(renderMutex_);
        if(!renderInput_||renderInput_->scope!=receipt.scope||!lastRenderReceipt_||
           lastRenderReceipt_->scope!=receipt.scope||lastRenderReceipt_->candidateId!=receipt.candidateId||
           lastRenderReceipt_->frameSequence!=receipt.frameSequence||lastRenderReceipt_->draws!=receipt.draws)return;
        renderAdoption_=TerrainRenderAdoption{receipt,std::move(protectedIds)};
    }
    emit renderInputChanged();
}
void TerrainImageBridge::observeRender(TerrainRenderObservation observation) {
    QMetaObject::invokeMethod(this,[this,observation=std::move(observation)]() mutable {
        {std::lock_guard lock(renderMutex_);if(!renderInput_)return;
            const auto& a=renderInput_->scope;const auto& b=observation.scope;
            const bool sameOwner=a.sourceEpoch==b.sourceEpoch&&a.windowEpoch==b.windowEpoch&&
                a.contextEpoch==b.contextEpoch&&a.projectGeneration==b.projectGeneration;
            if(observation.kind==TerrainRenderObservation::ResourceRetired?!sameOwner:a!=b)return;
            if(observation.kind!=TerrainRenderObservation::ResourceRetired&&!renderInput_->enabled)return;
            renderStats_=observation.stats;
            if(observation.kind==TerrainRenderObservation::DisplayReceipt&&observation.receipt)
                lastRenderReceipt_=observation.receipt;
        }
        emit renderObserved(std::move(observation));
    },Qt::QueuedConnection);
}
void TerrainImageBridge::observeOwner(TerrainRenderOwnerSnapshot owner) {
    QMetaObject::invokeMethod(this,[this,owner] {
        {std::lock_guard lock(renderMutex_);
            if(owner.windowEpoch<renderOwner_.windowEpoch||
              (owner.windowEpoch==renderOwner_.windowEpoch&&owner.contextEpoch<renderOwner_.contextEpoch))return;
            renderOwner_=owner;lastRenderReceipt_.reset();renderAdoption_.reset();
        }
        emit renderOwnerChanged(owner);
    },Qt::QueuedConnection);
}

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

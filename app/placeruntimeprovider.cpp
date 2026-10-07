#include "placeruntimeprovider.h"
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace {
quint64 next(quint64 value) {if(value==std::numeric_limits<quint64>::max())throw std::overflow_error("PL-PLACE-LIFECYCLE: revision exhausted");return value+1;}
bool sameViewport(const PlaceViewport& a,const PlaceViewport& b) {
    const auto fields=[](const PlaceViewport& v){const auto& m=v.view;return std::tie(m.mode,m.viewportWidth,m.viewportHeight,m.centerLongitude,m.centerLatitude,m.rotationLongitude,m.rotationLatitude,m.rotationRoll,m.scale,m.translateX,m.translateY,m.devicePixelRatio,v.zoom,v.safeLeft,v.safeTop,v.safeRight,v.safeBottom);};
    return fields(a)==fields(b);
}
struct Result {PlaceQueryResult value;QString error;bool cancelled=false;};
Result run(const std::function<PlaceQueryResult()>& task) {
    try{return {task(),{},false};}
    catch(const PlaceRuntimeCancelled&){return {{},{},true};}
    catch(const std::exception& e){return {{},QString::fromUtf8(e.what()),false};}
}
std::size_t recordsBytes(const std::vector<PlaceRecord>& records) {std::size_t bytes=sizeof(records)+records.capacity()*sizeof(PlaceRecord);for(const auto& r:records)bytes+=PlaceRuntimeStore::recordBytes(r)-sizeof(PlaceRecord);return bytes;}
}
PlaceRuntimeProvider::PlaceRuntimeProvider(QObject* parent):QObject(parent){}
PlaceRuntimeProvider::~PlaceRuntimeProvider() {cancel(viewportCancel_);cancel(searchCancel_);}
void PlaceRuntimeProvider::cancel(CancelFlag& flag) {if(flag)flag->store(true,std::memory_order_relaxed);flag.reset();}
bool PlaceRuntimeProvider::open(const QString& path,const QString& project,QString& error,std::size_t budget) {
    // Validate before mutating identity, active jobs, retained records or snapshot.
    auto candidate=PlaceRuntimeStore::open(path,error,budget);if(!candidate)return false;
    const auto generation=next(generation_);cancel(viewportCancel_);cancel(searchCancel_);
    store_=std::move(candidate);generation_=generation;projectInstance_=project;
    viewportRevision_=searchRevision_=0;desired_.reset();requested_.reset();queuedViewport_.reset();queuedSearch_.reset();moving_=false;
    snapshot_.reset();retained_.clear();retentionOrder_.clear();protectedIds_.clear();searchResults_.clear();searchTruncated_=false;
    ++stats_.sourceOpens;emit sourceChanged();return true;
}
void PlaceRuntimeProvider::close(const QString& project) {
    const auto generation=next(generation_);cancel(viewportCancel_);cancel(searchCancel_);generation_=generation;projectInstance_=project;
    store_.reset();snapshot_.reset();desired_.reset();requested_.reset();queuedViewport_.reset();queuedSearch_.reset();retained_.clear();retentionOrder_.clear();protectedIds_.clear();searchResults_.clear();searchTruncated_=false;moving_=false;
    emit sourceChanged();
}
std::optional<PlaceSourceIdentity> PlaceRuntimeProvider::sourceIdentity() const {if(!store_)return {};return PlaceSourceIdentity{store_->revision(),store_->manifestSha256(),generation_};}
void PlaceRuntimeProvider::cancelViewport() {cancel(viewportCancel_);viewportRevision_=next(viewportRevision_);requested_.reset();queuedViewport_.reset();}
void PlaceRuntimeProvider::cancelSearch() {cancel(searchCancel_);searchRevision_=next(searchRevision_);queuedSearch_.reset();}
void PlaceRuntimeProvider::beginInteraction() {moving_=true;cancelViewport();cancelSearch();}
bool PlaceRuntimeProvider::settle() {moving_=false;return desired_?requestViewport(*desired_):false;}
bool PlaceRuntimeProvider::settle(const PlaceViewport& view) {moving_=false;return requestViewport(view);}
bool PlaceRuntimeProvider::requestViewport(const PlaceViewport& view) {
    desired_=view;if(!store_||moving_)return false;
    if(requested_&&sameViewport(*requested_,view))return false;
    cancel(viewportCancel_);viewportRevision_=next(viewportRevision_);requested_=view;
    if(viewportRunning_){queuedViewport_=view;++stats_.coalescedViewportRequests;return true;}
    startViewport(view);return true;
}
void PlaceRuntimeProvider::startViewport(const PlaceViewport& view) {
    const auto revision=viewportRevision_,generation=generation_;const auto project=projectInstance_;
    const auto identity=*sourceIdentity();auto flag=std::make_shared<std::atomic_bool>(false);viewportCancel_=flag;
    const auto source=store_;requested_=view;viewportRunning_=true;++stats_.viewportRequests;++stats_.pendingCount;
    auto* watcher=new QFutureWatcher<Result>(this);
    connect(watcher,&QFutureWatcher<Result>::finished,this,[this,watcher,flag,revision,generation,project,identity] {
        const auto result=watcher->result();watcher->deleteLater();--stats_.pendingCount;viewportRunning_=false;
        if(generation!=generation_||revision!=viewportRevision_||project!=projectInstance_||flag->load(std::memory_order_relaxed)||moving_){++stats_.staleCompletionCount;if(flag->load(std::memory_order_relaxed))++stats_.cancelledCount;drainViewport();return;}
        viewportCancel_.reset();
        if(result.cancelled){requested_.reset();++stats_.cancelledCount;drainViewport();return;}
        if(!result.error.isEmpty()){requested_.reset();++stats_.failureCount;emit loadFailed(result.error);drainViewport();return;}
        auto snapshot=std::make_shared<PlaceRuntimeSnapshot>();snapshot->identity=identity;snapshot->requestRevision=revision;snapshot->signature=result.value.signature;
        snapshot->records=result.value.records;snapshot->tileCount=result.value.tileCount;snapshot->candidatesExamined=result.value.candidatesExamined;
        snapshot->residentBytes=sizeof(PlaceRuntimeSnapshot)+recordsBytes(snapshot->records)+std::size_t(snapshot->signature.capacity())*sizeof(QChar);
        snapshot_=std::move(snapshot);++stats_.acceptedSnapshots;emit snapshotChanged();drainViewport();
    });
    watcher->setFuture(QtConcurrent::run([source,view,flag]{return run([&]{return source->queryViewport(view,[&]{return flag->load(std::memory_order_relaxed);});});}));
}
void PlaceRuntimeProvider::drainViewport() {
    if(!viewportRunning_&&queuedViewport_&&store_&&!moving_){const auto view=*queuedViewport_;queuedViewport_.reset();startViewport(view);}
}
bool PlaceRuntimeProvider::search(const QString& query) {
    if(!store_||moving_)return false;cancel(searchCancel_);searchRevision_=next(searchRevision_);
    if(searchRunning_){queuedSearch_=query;++stats_.coalescedSearchRequests;return true;}
    startSearch(query);return true;
}
void PlaceRuntimeProvider::startSearch(const QString& query) {
    const auto revision=searchRevision_,generation=generation_;const auto project=projectInstance_;const auto source=store_;
    auto flag=std::make_shared<std::atomic_bool>(false);searchCancel_=flag;searchRunning_=true;++stats_.searchRequests;++stats_.pendingCount;
    auto* watcher=new QFutureWatcher<Result>(this);
    connect(watcher,&QFutureWatcher<Result>::finished,this,[this,watcher,flag,revision,generation,project] {
        const auto result=watcher->result();watcher->deleteLater();--stats_.pendingCount;searchRunning_=false;
        if(generation!=generation_||revision!=searchRevision_||project!=projectInstance_||flag->load(std::memory_order_relaxed)||moving_){++stats_.staleCompletionCount;if(flag->load(std::memory_order_relaxed))++stats_.cancelledCount;drainSearch();return;}
        searchCancel_.reset();if(result.cancelled){++stats_.cancelledCount;drainSearch();return;}if(!result.error.isEmpty()){++stats_.failureCount;emit loadFailed(result.error);drainSearch();return;}
        searchResults_=result.value.records;searchTruncated_=result.value.truncated;for(const auto& record:searchResults_)retain(record);emit searchCompleted();drainSearch();
    });
    watcher->setFuture(QtConcurrent::run([source,query,flag]{return run([&]{return source->search(query,[&]{return flag->load(std::memory_order_relaxed);});});}));
}
void PlaceRuntimeProvider::drainSearch() {
    if(!searchRunning_&&queuedSearch_&&store_&&!moving_){const auto query=*queuedSearch_;queuedSearch_.reset();startSearch(query);}
}
std::optional<PlaceRecord> PlaceRuntimeProvider::recordById(const QString& id) const {
    const auto found=retained_.constFind(id);if(found!=retained_.cend())return found.value();
    if(snapshot_)for(const auto& record:snapshot_->records)if(record.id==id)return record;return {};
}
void PlaceRuntimeProvider::retain(const PlaceRecord& record) {
    if(!store_||!PlaceRuntimeStore::isBuiltinId(record.id)||record.id!="builtin:place:"+record.source+":"+record.sourceId)return;
    if(record.name.isEmpty()||record.name.toUcs4().size()>256||record.sourceId.toUcs4().size()>128||record.source.toUcs4().size()>32||record.countryCode.toUcs4().size()>8||record.featureCode.toUcs4().size()>32||
       !std::isfinite(record.coordinates.x)||std::abs(record.coordinates.x)>180||!std::isfinite(record.coordinates.y)||std::abs(record.coordinates.y)>90||
       !std::isfinite(record.population)||record.population<0||!std::isfinite(record.priority)||!std::isfinite(record.minZoom)||record.minZoom<0)return;
    retentionOrder_.removeAll(record.id);retentionOrder_.append(record.id);retained_.insert(record.id,record);trimRetention();
}
void PlaceRuntimeProvider::setProtectedIds(const QStringList& ids) {
    protectedIds_=ids;
    // Capture selected records from the previous snapshot before the next
    // viewport replaces it. Offscreen selection keeps its stable source value.
    for(const auto& id:ids)if(const auto record=recordById(id))retain(*record);
    trimRetention();
}
void PlaceRuntimeProvider::trimRetention() {
    while(std::size_t(retained_.size())>PlaceRuntimeLimits::RetainedRecords){auto victim=std::find_if(retentionOrder_.begin(),retentionOrder_.end(),[&](const auto& id){return !protectedIds_.contains(id);});
        if(victim==retentionOrder_.end())victim=std::find_if(retentionOrder_.begin(),retentionOrder_.end(),[&](const auto& id){return protectedIds_.isEmpty()||id!=protectedIds_.front();});
        if(victim==retentionOrder_.end())break;retained_.remove(*victim);retentionOrder_.erase(victim);
    }
}
PlaceStoreStats PlaceRuntimeProvider::storeStats() const {return store_?store_->stats():PlaceStoreStats{};}
PlaceProviderStats PlaceRuntimeProvider::stats() const {
    auto result=stats_;result.retainedRecords=std::size_t(retained_.size());result.snapshotRecords=snapshot_?snapshot_->records.size():0;result.snapshotBytes=snapshot_?snapshot_->residentBytes:0;
    for(auto i=retained_.begin();i!=retained_.end();++i)result.retainedBytes+=PlaceRuntimeStore::recordBytes(i.value());
    result.retainedBytes+=recordsBytes(searchResults_);for(const auto& id:retentionOrder_)result.retainedBytes+=std::size_t(id.capacity())*sizeof(QChar);for(const auto& id:protectedIds_)result.retainedBytes+=std::size_t(id.capacity())*sizeof(QChar);
    result.moving=moving_;result.queuedCount=std::size_t(bool(queuedViewport_))+std::size_t(bool(queuedSearch_));return result;
}
pandoeditor::ResourceCacheSnapshot PlaceRuntimeProvider::resourceCacheSnapshot() const {
    const auto store=storeStats();const auto runtime=stats();pandoeditor::ResourceCacheSnapshot result;
    result.scopeEpoch=generation_;result.budgetBytes=store.cacheBudget;result.residentBytes=store.cacheBytes;
    result.residentCount=store.cachedEntries;
    result.pendingCount=runtime.pendingCount;result.pendingUnknownCount=runtime.pendingCount;result.hitCount=store.cacheHits;result.missCount=store.fileReadCount;result.evictionCount=store.evictions;
    result.staleCompletionCount=runtime.staleCompletionCount;result.failureCount=runtime.failureCount;
    // Store budget constrains the combined encoded+decoded cache. Source manifest,
    // immutable snapshot and bounded retained records are separate reported owners.
    result.overBudgetBytes=store.cacheBytes>store.cacheBudget?store.cacheBytes-store.cacheBudget:0;
    return result;
}

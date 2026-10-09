#include "editorcontroller.h"
#include <pandoeditor/presentationcommands.h>
#include "losslessjson.h"
#include <QtConcurrent>
#include "hydrodataprovider.h"
#include <pandoeditor/map/projectionengine.h>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QCryptographicHash>
#include <QDir>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QStringList>
#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>
using namespace pandoeditor;
namespace { QString displayText(const std::string& value){return QString::fromStdString(value);} }
QVariantList EditorController::presentationGroups() const {
    QVariantList rows;const auto& p=project_.document().presentation.webPresentation;
    for(const auto& group:std::vector<std::string>{"countries","subunits","regions"}) {
        const auto name=group=="countries"?"basemapLabels":group=="subunits"?"subunitLabels":"regionLabels";
        const auto flag=group=="countries"?"countryFlags":group=="subunits"?"subunitFlags":"regionFlags";
        const auto found=p.styles.find(group);const auto style=found==p.styles.end()?PresentationStyle{}:found->second;
        rows.append(QVariantMap{{"key",QString::fromStdString(group)},{"title",group=="countries"?QStringLiteral("최상위 일반객체"):group=="subunits"?QStringLiteral("하위 일반객체"):QStringLiteral("독립 권역")},
            {"visible",groupVisible(p,group)},{"nameKey",name},{"flagKey",flag},{"names",groupVisible(p,name)},{"flags",groupVisible(p,flag)},{"opacity",style.opacity.value_or(1)},{"boundary",style.boundaryVisible.value_or(true)},{"colorVisible",style.colorVisible.value_or(true)}});
    }
    for(const auto& entry:std::vector<std::pair<std::string,QString>>{{"labels",QStringLiteral("지명")},{"rivers",QStringLiteral("강")},{"lakes",QStringLiteral("호수")},{"distributions",QStringLiteral("분포")},{"genericFeatures",QStringLiteral("기타 객체")}}) {
        const auto found=p.styles.find(entry.first);const auto style=found==p.styles.end()?PresentationStyle{}:found->second;
        rows.append(QVariantMap{{"key",QString::fromStdString(entry.first)},{"title",entry.second},{"visible",groupVisible(p,entry.first)},{"opacity",style.opacity.value_or(1)},{"content",true}});
    }
    return rows;
}
QVariantMap EditorController::distributionDisplay() const {
    const auto& settings=project_.document().presentation.webPresentation.distributionSettings;
    QVariantList layers;
    for(const auto& layer:project_.document().distributionLayers)if(effectiveMapVisibility(project_.document(),{"distributionLayer",layer.id}))layers.append(QVariantMap{{"id",displayText(layer.id)},{"name",displayText(layer.name.empty()?layer.id:layer.name)}});
    const auto selected=std::find_if(layers.begin(),layers.end(),[&](const auto& v){return v.toMap()["id"].toString()==displayText(settings.activeLayerId);});
    const auto active=selected!=layers.end()?selected->toMap()["id"].toString():layers.empty()?QString{}:layers.front().toMap()["id"].toString();
    return {{"mode",settings.renderMode==DistributionRenderMode::Single?"single":"overlap"},{"boundaryVisible",settings.boundaryVisible},{"activeLayerId",active},{"layers",layers}};
}
QVariantMap EditorController::hydroDataStatus() const {
    const auto& settings=project_.document().physicalData;if(settings.source.empty()&&!hydroRuntime_.isOpen())return QVariantMap{{"ready",false},{"version",displayText(settings.version)},{"dataset",displayText(settings.dataset)},{"error",physicalActive_+physicalQueued_>0?QStringLiteral("필요한 수계 자료를 받는 중입니다."):(physicalError_.isEmpty()?(worldHydroNotice_.isEmpty()?QStringLiteral("수계 자료가 아직 준비되지 않았습니다."):worldHydroNotice_):physicalError_)}};
    if(hydroRuntime_.isOpen())return QVariantMap{{"ready",true},{"root",displayText(settings.source)},
        {"version",displayText(settings.version)},{"dataset",displayText(settings.dataset)},
        {"viewportLoaded",hydroViewportLoaded()},{"error",QString()}};
    const auto inspected=inspectHydroData(displayText(settings.source));return QVariantMap{{"ready",inspected.ready},{"root",inspected.root},{"version",inspected.version},{"dataset",inspected.dataset},{"error",inspected.error}};
}
QVariantMap EditorController::terrainDataStatus() const {
    const auto state=terrainDisplay_.snapshot();
    std::set<qint64> preparedImageIds;quint64 preparedBytes=0,preparedCpuOnlyBytes=0;
    for(const auto& item:terrainPrepared_) {
        const auto& image=item.second.frame.image;
        if(preparedImageIds.insert(image.cacheKey()).second)preparedBytes+=image.sizeInBytes();
        if(std::find(state.uploadSubmittedResources.begin(),state.uploadSubmittedResources.end(),item.first)==state.uploadSubmittedResources.end())
            preparedCpuOnlyBytes+=image.sizeInBytes();
    }
    const auto& terrainView=sceneBridge_.viewState();
    const bool mobileLayout=terrainLayoutWidth_<=799;
    const double sourceDpr=std::min(mobileLayout?2.:3.,std::max(1.,terrainView.devicePixelRatio));
    const auto demMetadataError=terrainDemProvider_?terrainDemProvider_->error():QString{};
    const auto demDecodeError=terrainDemProvider_?terrainDemProvider_->decodeError():QString{};
    const bool fallback=terrainProvider_&&terrainProvider_!=terrainDemProvider_;
    const auto fallbackReason=!fallback?QString{}:!demMetadataError.isEmpty()?demMetadataError:
        !demDecodeError.isEmpty()?demDecodeError:
        terrainDemProvider_&&terrainDemProvider_->available()?QStringLiteral("DEM base data is not ready"):
        QStringLiteral("DEM metadata is unavailable");
    const auto activeError=terrainProvider_?(!terrainProvider_->decodeError().isEmpty()?
        terrainProvider_->decodeError():terrainProvider_->error()):QStringLiteral("Terrain package not installed");
    const auto demError=!demMetadataError.isEmpty()?demMetadataError:demDecodeError;
    return {{"available",terrainProvider_&&terrainProvider_->available()},
            {"targetLevel",terrainProvider_?terrainProvider_->targetLevelForView(terrainView,mobileLayout):-1},
            {"sourceDevicePixelRatio",sourceDpr},{"layoutWidth",terrainLayoutWidth_},
            {"mobileLayout",mobileLayout},
            {"version",terrainProvider_?terrainProvider_->manifestVersion():QString{}},
            {"representation",terrainProvider_&&terrainProvider_->isDem()?"dem-relief-v1":"raster"},
            {"demMetadataReady",terrainDemProvider_&&terrainDemProvider_->available()},
            {"demMetadataError",demMetadataError},{"demDecodeError",demDecodeError},
            {"fallbackReason",fallbackReason},
            {"demCpuTilesReady",terrainProvider_&&terrainProvider_->isDem()&&!state.cpuReadyResources.empty()},
            {"requestedCount",qulonglong(state.requestedKeys.size())},{"cpuReadyCount",qulonglong(state.cpuReadyResources.size())},
            {"uploadSubmittedCount",qulonglong(state.uploadSubmittedResources.size())},
            {"candidateId",qulonglong(state.candidate?state.candidate->id:0)},
            {"submittedFrame",qulonglong(state.submitted?state.submitted->frameSequence:0)},
            {"displayReceiptAccepted",state.receiptAccepted},{"displayedDraws",qulonglong(state.displayed.size())},
            {"rejectedEvents",qulonglong(state.rejectedEvents)},
            {"cpuDecodeReservedBytes",terrainDecodeReservations_},{"cpuDecodeDeferred",terrainPreparationDeferred_},
            {"pendingJobs",qulonglong(terrainDecodeWatcher_?1:0)},
            {"cpuPreparationPending",terrainPreparePending_},{"cpuPreparationScheduled",terrainPreparationTimer_.isActive()},
            {"uploadWorkPending",terrainRenderStats_.uploadWorkPending},
            {"pendingJobsScope",QStringLiteral("actual terrain CPU decode/hash worker; asset downloads reported separately")},
            {"preparedUniqueImageBytes",preparedBytes},{"preparedCpuOnlyImageBytes",preparedCpuOnlyBytes},
            {"cpuReservationScope",QStringLiteral("nominal raw, conversion and hash backing estimates; decoder scratch unobserved")},
            {"cpuRenderImageBytes",terrainRenderStats_.cpuImageBytes},{"qsgTextureNominalBytes",terrainRenderStats_.textureNominalBytes},
            {"stagingNominalBytes",terrainRenderStats_.stagingNominalBytes},{"meshBytes",terrainRenderStats_.meshBytes},
            {"uploadOperations",terrainRenderStats_.uploadOperations},{"uploadBytes",terrainRenderStats_.uploadBytes},
            {"pendingDisplayAdoptions",terrainRenderStats_.pendingSubmittedFrames},
            {"frameUploadMillis",terrainRenderStats_.frameUploadMillis},{"oversizedOperations",terrainRenderStats_.oversizedOperations},
            {"allocationFailures",terrainRenderStats_.allocationFailures},{"textureSizeChanges",terrainRenderStats_.textureSizeChanges},
            {"memoryPressure",terrainRenderStats_.memoryPressure},{"mandatoryOverflowBytes",terrainRenderStats_.mandatoryOverflowBytes},
            {"temporaryProtectedBytes",terrainRenderStats_.temporaryProtectedBytes},
            {"gpuResidentBytes",QVariant{}},{"presentationObservation",QStringLiteral("QSG frame submission and associated frameSwapped; no GPU fence or DWM observation")},
            {"missingTiles",terrainMissingTiles_},
            {"activeDownloads",physicalActive_},{"queuedDownloads",physicalQueued_},
            {"error",fallback&&!demError.isEmpty()?demError:!activeError.isEmpty()?activeError:physicalError_}};
}
void EditorController::executeTerrainResources(const ViewportResourceRequest& request) {
    // Hydro/labels keep their own settle policy. Terrain starts only after the
    // last navigation input has been quiet for the fixed Web's 500 ms.
    Q_UNUSED(request);
    updateTerrainDemand(false);
    if(terrainWindowVisible_&&activeMapInteractions_==0)terrainPreparationTimer_.start();
}
void EditorController::setTerrainWindowVisible(bool visible) {
    if(terrainWindowVisible_==visible)return;
    terrainWindowVisible_=visible;
    if(!visible) {
        terrainPreparationTimer_.stop();terrainPreparePending_=false;
        if(!terrainDemand_.base.empty())terrainDisplay_.cancel(terrainDemand_.scope);
        publishTerrainRenderInput();
        const auto snap=terrainDisplay_.snapshot();std::vector<TerrainTileSpec> retained;
        for(const auto& id:snap.protectedResources)if(const auto found=terrainPreparedSpecs_.find(id);found!=terrainPreparedSpecs_.end())retained.push_back(found->second);
        if(terrainProvider_)terrainProvider_->protectRenderResources({},retained);
    } else {
        updateTerrainDemand(false);
        if(activeMapInteractions_==0)terrainPreparationTimer_.start();
    }
}
void EditorController::resetTerrainRender() {
    terrainPreparationTimer_.stop();terrainPreparePending_=false;
    terrainDisplay_.reset();terrainDisplaySource_.reset();terrainDemand_={};
    terrainPrepared_.clear();terrainPreparedSpecs_.clear();terrainDemandSpecs_.clear();terrainFailedPreparation_.clear();
    terrainUploadedTint_.clear();terrainTint_={};terrainTintContentKey_.clear();
    terrainFallbackDraws_.clear();terrainAssetPending_=terrainMissingTiles_=0;terrainTiles_.clear();
    for(const auto& source:distinctTerrainProviders())source->protectRenderResources({},{});
    TerrainRenderInput disabled;disabled.enabled=false;disabled.releaseResources=true;disabled.view=sceneBridge_.viewState();
    if(terrainResourceBridge_)terrainResourceBridge_->setRenderInput(std::move(disabled));
}
void EditorController::updateTerrainDemand(bool prepareCpu) {
    const auto view=sceneBridge_.viewState();
    if(terrainMode_=="none") {resetTerrainRender();return;}
    if(!terrainProvider_||!terrainProvider_->available()||!validMapViewState(view))return;
    // Asset readiness chooses the CPU source. The render owner separately
    // retains the last displayed raster until the new DEM base+tint is drawn.
    if(terrainDemProvider_&&terrainDemProvider_->available()) {
        bool baseReady=true;
        for(const QString& path:{QStringLiteral("terrain/v0.13.0/0/0-0.webp"),
            QStringLiteral("terrain/v0.13.0/0/1-0.webp"),QStringLiteral("terrain/v0.13.3/tint.webp")}) {
            if(!physicalAssetReady(path)){baseReady=false;requestPhysicalAsset(path);}
        }
        const auto next=baseReady&&terrainDemProvider_->decodeError().isEmpty()?terrainDemProvider_:terrainRasterProvider_;
        if(next&&next!=terrainProvider_) {
            terrainProvider_=next;emit terrainChanged();
        }
    }
    terrainResourceBridge_->setSource(terrainProvider_);
    const auto owner=terrainResourceBridge_->ownerSnapshot();
    const auto scene=sceneBridge_.sceneSnapshot();
    if(terrainProjectInstance_!=project_.instanceId()) {
        terrainProjectInstance_=project_.instanceId();++terrainProjectGeneration_;
    }
    const auto previous=terrainDisplay_.snapshot();
    TerrainDisplayScope scope{terrainResourceBridge_->sourceEpoch(),owner.windowEpoch,owner.contextEpoch,
        terrainProjectGeneration_,view.revision,scene?scene->revision:0,
        previous.scope.candidateSequence,previous.scope.requestSequence};
    const bool gray=terrainMode_=="gray";
    const auto plan=terrainProvider_->planForView(view,terrainLayoutWidth_<=799);
    const auto resource=[&](const TerrainTileSpec& tile) {
        return TerrainDisplayResource{QStringLiteral("%1/%2/%3").arg(tile.level).arg(tile.column).arg(tile.row)+
            (terrainProvider_->isDem()?QStringLiteral("/raw"):gray?QStringLiteral("/raster-gray"):QStringLiteral("/raster-color")),
            {},tile.level,{tile.west,tile.north,tile.east,tile.south}};
    };
    TerrainDisplayDemand demand;demand.scope=scope;
    for(const auto& tile:plan.baseTiles)demand.base.push_back(resource(tile));
    for(const auto& tile:plan.targetTiles)demand.target.push_back(resource(tile));
    for(const auto& tile:plan.prefetchTiles)demand.prefetch.push_back(resource(tile));
    std::set<double> offsets;
    for(const auto& tile:terrainProvider_->tilesForView(view,terrainLayoutWidth_<=799)) {
        offsets.insert(tile.worldOffsetDegrees);
        demand.domains.push_back({tile.west+tile.worldOffsetDegrees,tile.north,
                                  tile.east+tile.worldOffsetDegrees,tile.south});
    }
    if(demand.domains.empty())demand.domains.push_back({-180,90,180,-90});
    if(offsets.empty())offsets.insert(0);
    demand.worldOffsets.assign(offsets.begin(),offsets.end());
    const bool variantChanged=terrainDemand_.target!=demand.target||terrainDemand_.base!=demand.base||
        terrainDemand_.prefetch!=demand.prefetch||terrainDemand_.domains!=demand.domains||
        terrainDemand_.worldOffsets!=demand.worldOffsets;
    if(scope!=previous.scope||variantChanged||previous.cancelled||terrainDemand_.base.empty()) {
        const bool sameOwner=scope.sourceEpoch==previous.scope.sourceEpoch&&scope.windowEpoch==previous.scope.windowEpoch&&
            scope.contextEpoch==previous.scope.contextEpoch&&scope.projectGeneration==previous.scope.projectGeneration;
        if(!sameOwner) {
            terrainPrepared_.clear();terrainPreparedSpecs_.clear();terrainUploadedTint_.clear();
            terrainTint_={};terrainTintContentKey_.clear();
        }
        demand.scope.requestSequence=++terrainRequestSequence_;
        demand.scope.candidateSequence=terrainRequestSequence_;
        terrainDemand_=std::move(demand);terrainDisplay_.beginDemand(terrainDemand_);
        terrainFailedPreparation_.clear();
        terrainDemandSpecs_.clear();
        for(const auto& request:plan.requests)terrainDemandSpecs_[resource(request.spec).key]=request.spec;
        const auto held=terrainDisplay_.snapshot().protectedResources;
        for(auto it=terrainPrepared_.begin();it!=terrainPrepared_.end();) {
            if(!terrainDemandSpecs_.count(it->first.key)&&std::find(held.begin(),held.end(),it->first)==held.end()) {
                terrainPreparedSpecs_.erase(it->first);it=terrainPrepared_.erase(it);
            } else ++it;
        }
        for(const auto& item:terrainPrepared_)if(terrainDemandSpecs_.count(item.first.key))
            terrainDisplay_.cpuReady(terrainDemand_.scope,item.second.resource);
        if(!terrainProvider_->isDem()||(!terrainTintContentKey_.isEmpty()&&terrainUploadedTint_==terrainTintContentKey_))
            terrainDisplay_.buildCandidate();
    }
    terrainMissingTiles_=0;
    const auto snap=terrainDisplay_.snapshot();
    for(const auto& item:terrainDemand_.target)
        if(std::find(snap.uploadSubmittedKeys.begin(),snap.uploadSubmittedKeys.end(),item.key)==snap.uploadSubmittedKeys.end())++terrainMissingTiles_;
    publishTerrainRenderInput();
    if(!prepareCpu||!terrainWindowVisible_||activeMapInteractions_>0||!owner.alive)return;
    terrainPreparePending_=true;
    if(terrainDecodeWatcher_)return; // At most one decode/hash job; latest demand wins.
    const auto budget=quality_.profile().terrainCacheBudgetBytes;
    const bool pressure=terrainRenderStats_.memoryPressure||terrainProvider_->cachedBytes()>budget;
    std::set<QString> prepared;
    for(const auto& item:terrainPrepared_)if(item.second.frame.sourceEpoch==scope.sourceEpoch)prepared.insert(item.first.key);
    for(const auto& request:plan.requests) {
        const auto spec=resource(request.spec);
        if(prepared.count(spec.key)||terrainFailedPreparation_.count(spec.key))continue;
        const bool base=std::any_of(terrainDemand_.base.begin(),terrainDemand_.base.end(),[&](const auto& b){return b.key==spec.key;});
        const bool prefetch=std::any_of(terrainDemand_.prefetch.begin(),terrainDemand_.prefetch.end(),[&](const auto& b){return b.key==spec.key;});
        const auto bytes=terrainProvider_->expectedDecodedTileBytes(request.spec);
        if(!base&&(pressure||bytes>budget||terrainProvider_->cachedBytes()>budget-bytes)) {
            ++terrainPreparationDeferred_;continue;
        }
        const auto relative=terrainProvider_->relativeTilePath(request.spec);
        if(!physicalAssetReady(relative)) {++terrainAssetPending_;requestPhysicalAsset(relative);continue;}
        std::vector<TerrainTileSpec> retained;
        for(const auto& id:snap.protectedResources)if(const auto found=terrainPreparedSpecs_.find(id);found!=terrainPreparedSpecs_.end())retained.push_back(found->second);
        terrainProvider_->protectRenderResources({request.spec},retained);
        // The pinned DEM decoder requires a 4096x2048 RGBA tint. Reserve its
        // first decode/hash backing as well as the admitted tile, before work.
        terrainDecodeReservations_=bytes*(terrainProvider_->isDem()?2:3)+
            (terrainProvider_->isDem()&&terrainTintContentKey_.isEmpty()?2*4096ull*2048*4:0);
        const auto capturedScope=terrainDemand_.scope;const auto source=terrainProvider_;
        const auto knownTint=terrainTintContentKey_;const auto knownTintCacheKey=terrainTint_.cacheKey();const auto tile=request.spec;
        auto* watcher=new QFutureWatcher<TerrainRenderResource>(this);terrainDecodeWatcher_=watcher;
        connect(watcher,&QFutureWatcher<TerrainRenderResource>::finished,this,[this,watcher,capturedScope,source,tile,prefetch,key=spec.key] {
            auto result=watcher->result();watcher->deleteLater();terrainDecodeWatcher_=nullptr;terrainDecodeReservations_=0;
            const bool failed=result.frame.image.isNull()||(source->isDem()&&result.frame.tint.isNull());
            if(capturedScope==terrainDemand_.scope&&terrainWindowVisible_&&activeMapInteractions_==0&&terrainMode_!="none"&&!failed) {
                result.prefetch=prefetch;const TerrainDisplayResourceId id{result.resource.key,result.resource.contentKey};
                if(terrainDisplay_.cpuReady(capturedScope,result.resource)) {
                    terrainPrepared_[id]=result;terrainPreparedSpecs_[id]=tile;
                    if(!result.frame.tint.isNull()){terrainTint_=result.frame.tint;terrainTintContentKey_=result.tintContentKey;}
                    publishTerrainRenderInput();emit terrainChanged();
                }
            }
            if(capturedScope==terrainDemand_.scope&&failed) {
                terrainFailedPreparation_.insert(key);
                if(result.frame.image.isNull())requestPhysicalAsset(source->relativeTilePath(tile));
                if(source->isDem()&&result.frame.tint.isNull())requestPhysicalAsset("terrain/v0.13.3/tint.webp");
                emit terrainChanged();
            }
            if(terrainPreparePending_&&terrainWindowVisible_&&activeMapInteractions_==0)
                QTimer::singleShot(0,this,[this]{updateTerrainDemand(true);});
        });
        watcher->setFuture(QtConcurrent::run([source,capturedScope,tile,gray,knownTint,knownTintCacheKey] {
            return TerrainImageBridge::prepareRenderResource(source,capturedScope.sourceEpoch,tile,gray,knownTint,knownTintCacheKey);
        }));
        return;
    }
    terrainPreparePending_=false;
}
void EditorController::publishTerrainRenderInput() {
    if(terrainDemand_.base.empty())return;
    const auto snap=terrainDisplay_.snapshot();TerrainRenderInput input;
    input.scope=terrainDemand_.scope;input.view=sceneBridge_.viewState();
    input.meshPhysicalScale=input.view.scale*std::min(terrainLayoutWidth_<=799?2.:3.,std::max(1.,input.view.devicePixelRatio));
    const auto scene=sceneBridge_.sceneSnapshot();input.sceneRevision=scene?scene->revision:0;
    input.enabled=terrainMode_!="none"&&terrainWindowVisible_;input.gray=terrainMode_=="gray";input.inputActive=activeMapInteractions_>0;
    input.requiresTint=terrainProvider_->isDem();
    input.budgetBytes=quality_.profile().terrainCacheBudgetBytes;
    input.tint=terrainTint_;input.tintContentKey=terrainTintContentKey_;
    input.protectedResources=snap.protectedResources;
    std::set<QString> requested(snap.requestedKeys.begin(),snap.requestedKeys.end());
    for(const auto& item:terrainPrepared_)if(requested.count(item.first.key)) {
        auto current=item.second;
        current.prefetch=std::any_of(terrainDemand_.prefetch.begin(),terrainDemand_.prefetch.end(),[&](const auto& r){return r.key==item.first.key;})&&
            !std::any_of(terrainDemand_.target.begin(),terrainDemand_.target.end(),[&](const auto& r){return r.key==item.first.key;});
        input.resources.push_back(std::move(current));
    }
    for(const auto& base:terrainDemand_.base) {
        const auto found=std::find_if(input.resources.begin(),input.resources.end(),[&](const auto& r){return r.resource.key==base.key;});
        input.baseResources.push_back({base.key,found==input.resources.end()?QString{}:found->resource.contentKey});
    }
    // DEM CPU tint is necessary but never substitutes for an actual commit.
    if(!terrainProvider_->isDem()||(!terrainTintContentKey_.isEmpty()&&terrainUploadedTint_==terrainTintContentKey_)) {
        input.candidate=snap.candidate;
    }
    terrainResourceBridge_->setRenderInput(std::move(input));
}
void EditorController::observeTerrainRender(const TerrainRenderObservation& event) {
    if(event.kind==TerrainRenderObservation::Pressure&&event.stats.cpuImageBytes==terrainRenderStats_.cpuImageBytes&&
       event.stats.textureNominalBytes==terrainRenderStats_.textureNominalBytes&&event.stats.meshBytes==terrainRenderStats_.meshBytes&&
       event.stats.stagingNominalBytes==terrainRenderStats_.stagingNominalBytes&&event.stats.uploadOperations==terrainRenderStats_.uploadOperations&&
       event.stats.memoryPressure==terrainRenderStats_.memoryPressure&&event.stats.temporaryProtectedBytes==terrainRenderStats_.temporaryProtectedBytes&&
       event.stats.pendingSubmittedFrames==terrainRenderStats_.pendingSubmittedFrames&&
       event.stats.uploadWorkPending==terrainRenderStats_.uploadWorkPending)
        return;
    terrainRenderStats_=event.stats;terrainFallbackDraws_=event.fallbackDraws;
    if(event.kind==TerrainRenderObservation::UploadSubmitted) {
        bool changed=false;const auto before=terrainDisplay_.snapshot();
        for(const auto& resource:event.resources) {
            const TerrainDisplayResourceId id{resource.key,resource.contentKey};
            if(std::find(before.uploadSubmittedResources.begin(),before.uploadSubmittedResources.end(),id)==before.uploadSubmittedResources.end())
                changed=terrainDisplay_.uploadSubmitted(event.scope,resource)||changed;
        }
        if(event.scope==terrainDemand_.scope&&!event.tintContentKey.isEmpty()&&event.tintContentKey==terrainTintContentKey_&&terrainUploadedTint_!=event.tintContentKey) {
            terrainUploadedTint_=event.tintContentKey;changed=true;
        }
        if(changed&&(!terrainProvider_->isDem()||terrainUploadedTint_==terrainTintContentKey_)) {
            terrainDisplay_.buildCandidate();publishTerrainRenderInput();
        }
    } else if(event.kind==TerrainRenderObservation::CandidateSubmitted&&event.receipt) {
        const auto candidate=terrainDisplay_.snapshot().candidate;
        if(candidate&&event.receipt->candidateId==candidate->id)
            terrainDisplay_.submitCandidate(*candidate,event.receipt->frameSequence,event.receipt->draws);
    } else if(event.kind==TerrainRenderObservation::DisplayReceipt&&event.receipt) {
        if(terrainDisplay_.acceptDisplayReceipt(*event.receipt)) {
            const auto snap=terrainDisplay_.snapshot();
            terrainResourceBridge_->acknowledgeDisplay(*event.receipt,snap.protectedResources);
            terrainTiles_.clear();
            for(const auto& draw:snap.displayed)terrainTiles_.push_back(QVariantMap{{"key",draw.resource.key},
                {"level",draw.resource.level},{"west",draw.bounds.west},{"east",draw.bounds.east},
                {"north",draw.bounds.north},{"south",draw.bounds.south}});
            publishTerrainRenderInput();
        }
    } else if(event.kind==TerrainRenderObservation::ResourceRetired) {
        bool changed=false,requiredMissing=false;
        for(const auto& id:event.retired) {
            if(id.key=="@terrain/tint") {if(id.contentKey==terrainUploadedTint_)terrainUploadedTint_.clear();continue;}
            if(terrainDisplay_.retireResource(event.scope,id.key,id.contentKey)) {
                terrainPrepared_.erase(id);terrainPreparedSpecs_.erase(id);
                changed=true;
                requiredMissing=requiredMissing||terrainDemandSpecs_.count(id.key)>0;
            }
        }
        if(changed){terrainDisplay_.buildCandidate();publishTerrainRenderInput();}
        if(terrainWindowVisible_&&activeMapInteractions_==0&&(terrainPreparationDeferred_||requiredMissing))
            QTimer::singleShot(0,this,[this]{updateTerrainDemand(true);});
    }
    const auto snap=terrainDisplay_.snapshot();std::vector<TerrainTileSpec> retained;
    for(const auto& id:snap.protectedResources)if(const auto found=terrainPreparedSpecs_.find(id);found!=terrainPreparedSpecs_.end())retained.push_back(found->second);
    if(terrainProvider_)terrainProvider_->protectRenderResources({},retained);
    emit terrainChanged();emit renderQualityChanged();
}
QVariantMap EditorController::hydroProjection() const{return projection_.hydroParameters();}
QVariantMap EditorController::hydroStyle() const {
    const auto& presentation=project_.document().presentation.webPresentation;
    const auto rivers=presentation.styles.find("rivers"),lakes=presentation.styles.find("lakes");
    return {{"riversVisible",groupVisible(presentation,"rivers")},
        {"lakesVisible",groupVisible(presentation,"lakes")},
        {"lakeBoundaryVisible",lakes==presentation.styles.end()?true:lakes->second.boundaryVisible.value_or(true)},
        {"riverOpacity",rivers==presentation.styles.end()?1.:rivers->second.opacity.value_or(1.)},
        {"lakeOpacity",lakes==presentation.styles.end()?1.:lakes->second.opacity.value_or(1.)}};
}
QVariantList EditorController::hiddenHydroIds() const {
    QVariantList result;for(const auto& id:project_.document().physicalData.hiddenHydroIds)
        result.append(QString::fromStdString(id));
    return result;
}
void EditorController::executeHydroResources(const ViewportResourceRequest& request) {
    if(!hydroRuntime_.isOpen()){ensureHydroBootstrap();return;}
    bool missing=false;
    for(const auto& path:hydroRuntime_.requiredAssetPaths(request.hydroWindow))
        if(!physicalAssetReady(path)){missing=true;requestPhysicalAsset(path);}
    if(missing)return;
    hydroRuntime_.requestViewport(request.hydroWindow);
}
bool EditorController::configureHydroData(const QUrl& value) {
    if(hasPendingEdits()||jobBusy()||hasWebImportPreview())return false;
    const auto path=value.isLocalFile()?value.toLocalFile():value.toString();const auto inspected=inspectHydroData(path);if(!inspected.ready){emit errorOccurred(inspected.error);return false;}
    HydroRuntimeProvider verified;QString hydroError;
    if(!verified.open(path,projectInstanceId(),mobileMode_,hydroError)){
        emit errorOccurred(hydroError);return false;
    }
    auto settings=project_.document().physicalData;settings.dataset=inspected.dataset.toStdString();settings.version=inspected.version.toStdString();settings.source=inspected.root.toStdString();
    CommandArguments args;args.action=SetPhysicalData{settings};auto request=CommandProcessor::makeRequest(project_,"physical-data.configure",args);auto prepared=CommandProcessor::prepare(project_,request);if(!prepared.ok()||!prepared.preview)return false;const auto result=CommandProcessor::confirm(project_,*prepared.preview);if(!result.ok())return false;
    noteAppliedImpact(result.impact);
    syncHydroData();
    publish(false);return true;
}
void EditorController::syncHydroData() {
    if(geometryEdit_&&geometryEdit_->riverPreparation&&!geometryEdit_->riverPreparation->identity) {
        // An initial river request owns its first source open. General asset
        // notifications merely wake that owner, never replace its provider.
        const auto generation=geometryEdit_->generation,epoch=geometryEdit_->riverPreparation->epoch;
        QTimer::singleShot(0,this,[this,generation,epoch]{continueRiverPreparation(generation,epoch);});
        return;
    }
    riverCache_.clear();riverCacheSource_.reset();
    hydroRuntime_.close(projectInstanceId());
    auto source=displayText(project_.document().physicalData.source);
    if(source.isEmpty()&&!physicalRoot_.isEmpty()) {
        ensureHydroBootstrap();
        const auto candidate=QDir(physicalRoot_).filePath("hydro/v0.13.2/manifest.json");
        if(inspectHydroData(candidate).ready)source=QFileInfo(candidate).absolutePath();
    }
    if(source.isEmpty())return;
    QString error;
    if(!hydroRuntime_.open(source,projectInstanceId(),mobileMode_,error))
        emit errorOccurred(error);
    else {
        hydroRuntime_.setCacheBudget(quality_.profile().hydroCacheBudgetBytes);
        std::set<quint32> logical;
        for(const auto& selected:selection_.items())if(selected.domain=="hydroBuiltin")
            if(const auto record=hydroRuntime_.recordById(displayText(selected.id)))logical.insert(record->logicalFid);
        hydroRuntime_.setSelectedLogicals(logical);
        invalidateViewportResources(ViewportResourceKind::Hydro);
    }
}
bool EditorController::setDistributionDisplay(const QString& mode,bool boundaryVisible,const QString& activeLayerId) {
    if(mode!="overlap"&&mode!="single")return false;
    auto settings=project_.document().presentation.webPresentation.distributionSettings;
    if(!activeLayerId.isNull()) {
        if(!activeLayerId.isEmpty()&&!project_.index().objects.count({"distributionLayer",activeLayerId.toStdString()}))return false;
        settings.activeLayerId=activeLayerId.toStdString();
    }
    settings.renderMode=mode=="single"?DistributionRenderMode::Single:DistributionRenderMode::Overlap;settings.boundaryVisible=boundaryVisible;
    const auto result=PresentationCommandProcessor::apply(project_,SetDistributionSettings{settings});if(result==PresentationResult::Applied)publishPresentation();return result==PresentationResult::Applied||result==PresentationResult::NoOp;
}
bool EditorController::setPresentationColorVisible(const QString& group,bool visible) {
    if(group!="countries"&&group!="subunits"&&group!="regions")return false;
    PresentationStyle patch;patch.colorVisible=visible;
    const auto result=PresentationCommandProcessor::apply(project_,PatchGroupPresentation{group.toStdString(),patch});
    if(result==PresentationResult::Applied)publishPresentation();
    return result==PresentationResult::Applied||result==PresentationResult::NoOp;
}
namespace {
pandoeditor::Point geometryCenter(const pandoeditor::Geometry& geometry) {
    double west=180,east=-180,south=90,north=-90;bool any=false;
    const auto add=[&](pandoeditor::Point point) {
        if(!std::isfinite(point.x)||!std::isfinite(point.y))return;
        west=std::min(west,point.x);east=std::max(east,point.x);
        south=std::min(south,point.y);north=std::max(north,point.y);any=true;
    };
    for(const auto point:geometry.points)add(point);
    for(const auto& line:geometry.lines)for(const auto point:line)add(point);
    for(const auto& polygon:geometry.polygons)for(const auto& ring:polygon)
        for(const auto point:ring)add(point);
    return any?pandoeditor::Point{(west+east)/2,(south+north)/2}:pandoeditor::Point{};
}
}

void EditorController::rebuildLabelSources() {
    if(labelSourceRevision_==std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("label source revision overflow");
    const auto* guiApp=qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    const std::optional<QFontMetricsF> metrics=guiApp?
        std::optional<QFontMetricsF>(QFontMetricsF(QGuiApplication::font())):std::nullopt;
    const auto textWidth=[&](const QString& value) {
        return metrics?metrics->horizontalAdvance(value):double(value.size())*7.0;
    };
    const auto textHeight=[&]() {return metrics?metrics->height():14.0;};
    std::vector<MapLabelSource> sources;
    sources.reserve(project_.document().units.size()+project_.document().labels.size());
    labelFlagSources_.clear();
    const auto& document=project_.viewDocument();
    for(const auto& unit:document.units) {
        const auto ref=territorialRef(unit.id);
        if(!effectiveMapVisibility(document,ref))continue;
        const auto properties=project_.propertyView(ref);if(!properties)continue;
        const auto resolved=resolvedTerritorialPresentation(document,ref);
        const auto flag=resolved.flagVisible?labelFlagSource(ref):QString();
        const bool nameVisible=resolved.nameVisible;
        const bool flagVisible=resolved.flagVisible&&!flag.isEmpty();
        if(!nameVisible&&!flagVisible)continue;
        const auto geometry=document.geometries.get(pandoeditor::staticGeometryBinding(document,unit.id).geometryRef);if(!geometry)continue;
        auto geographic=geometryCenter(*geometry);
        if(labelAnchors_)if(const auto anchor=labelAnchors_->anchor(
            displayText(ref.id),labelSourceId(ref.id)))geographic=*anchor;

        LabelSettings stored;
        if(const auto found=document.presentation.webPresentation.labelSettings.find(ref);
           found!=document.presentation.webPresentation.labelSettings.end())stored=found->second;
        const auto kind=isRootGeneral(document,unit)?std::string("country"):std::string("region");
        const auto settings=automaticLabelSettings(kind,stored);
        if(settings.pinned&&settings.manualPosition)geographic=*settings.manualPosition;
        const auto name=QString::fromStdString(properties->displayName);
        MapLabelSource source;
        source.ref=ref;source.text=properties->displayName;source.geographic=geographic;
        source.collisionGroup=settings.collisionGroup;
        source.width=nameVisible?std::max(22.,textWidth(name)+16):MapFlagWidth;
        source.height=nameVisible?std::max(19.,textHeight()):MapFlagHeight;
        source.priority=settings.priority.value_or(0);
        source.minZoom=settings.minZoom.value_or(0);
        source.maxZoom=settings.maxZoom.value_or(std::numeric_limits<double>::infinity());
        source.pinned=settings.pinned;source.nameVisible=nameVisible;source.flagVisible=flagVisible;
        sources.push_back(std::move(source));
        if(flagVisible)labelFlagSources_[ref]=flag;
    }

    for(const auto& label:document.labels) {
        const ObjectRef ref{"label",label.id};
        if(!effectiveMapVisibility(document,ref))continue;
        const auto geometry=document.geometries.get(label.geometry);
        if(!geometry||geometry->points.empty())continue;
        const auto properties=project_.propertyView(ref);if(!properties)continue;
        LabelSettings stored;
        if(const auto found=document.presentation.webPresentation.labelSettings.find(ref);
           found!=document.presentation.webPresentation.labelSettings.end())stored=found->second;
        const auto settings=automaticLabelSettings(label.kind,stored);
        auto geographic=geometry->points.front();
        if(settings.pinned&&settings.manualPosition)geographic=*settings.manualPosition;
        const auto name=QString::fromStdString(properties->displayName);
        MapLabelSource source;
        source.ref=ref;source.text=properties->displayName;source.geographic=geographic;
        source.collisionGroup=settings.collisionGroup;
        source.width=std::max(22.,textWidth(name)+16);
        source.height=std::max(19.,textHeight());
        source.priority=settings.priority.value_or(0);
        source.minZoom=settings.minZoom.value_or(0);
        source.maxZoom=settings.maxZoom.value_or(std::numeric_limits<double>::infinity());
        source.pinned=settings.pinned;source.nameVisible=true;source.flagVisible=false;
        sources.push_back(std::move(source));
    }

    ++labelSourceRevision_;
    labelEngine_.setSources(std::move(sources),labelSourceRevision_);
    labelSourcesDirty_=false;
}

void EditorController::refreshPlacedLabelRows() {
    QVariantList rows;rows.reserve(static_cast<qsizetype>(labelEngine_.placements().size()));
    for(const auto& placement:labelEngine_.placements()) {
        const auto flag=labelFlagSources_.find(placement.ref);
        QVariantList nameLines;nameLines.reserve(qsizetype(placement.lines.size()));
        for(const auto& line:placement.lines)
            nameLines.append(QVariantMap{{"language",QString::fromStdString(line.language)},
                {"text",QString::fromStdString(line.text)}});
        rows.append(QVariantMap{
            {"ref",objectRefValue(placement.ref)},
            {"x",placement.x},{"y",placement.y},
            {"name",QString::fromStdString(placement.text)},
            {"nameLines",nameLines},
            {"nameVisible",placement.nameVisible},{"pinned",placement.pinned},
            {"flagSource",flag==labelFlagSources_.end()?QString():flag->second},
            {"flagWidth",MapFlagWidth},{"flagHeight",MapFlagHeight},{"flagGap",MapFlagGap},
            {"flagVisible",placement.flagVisible&&flag!=labelFlagSources_.end()}
        });
    }
    if(rows==placedLabels_)return;
    placedLabels_=std::move(rows);
    placedLabelModel_.setRows(placedLabels_);
    emit labelLayoutChanged();
}

void EditorController::reprojectLabelPlacements() {
    try {
        labelEngine_.reproject(sceneBridge_.viewState(),camera_.display().zoom);
        refreshPlacedLabelRows();
    } catch(const std::exception& error) {
        emit errorOccurred(QStringLiteral("Label reprojection failed: ")+
                           QString::fromUtf8(error.what()));
    }
}

void EditorController::executeLabelResources(const ViewportResourceRequest& request) {
    if(labelSourcesDirty_)rebuildLabelSources();
    labelEngine_.setBuiltinSuppressedIds(copiedPlaceSourceIds(project_.document()));
    refreshBuiltinPlaceLabels();
    MapLabelLayoutOptions options;
    options.zoom=camera_.display().zoom;
    options.viewportWidth=request.view.viewportWidth;
    options.viewportHeight=request.view.viewportHeight;
    options.bottomInset=mobileMode_?96.:32.;
    options.collisionPadding=mobileMode_?5.:3.;
    options.maxCandidates=2048;
    options.maxPlaced=2048;
    options.labelDensity=quality_.profile().labelDensity;
    const auto selectedItems=selection_.items();
    std::set<ObjectRef> selected(selectedItems.begin(),selectedItems.end());
    if(geometryEdit_)selected.insert(geometryEdit_->target);
    if(contentSession_)selected.insert(contentSession_->edit.target);
    selected.insert(colorTargets_.begin(),colorTargets_.end());
    for(const auto& session:fieldSessions_)selected.insert(session.second.ref);
    labelEngine_.layout(request.view,options,selected);
    refreshPlacedLabelRows();
}

bool EditorController::setLabelPinned(const QVariantMap& value,bool pinned,double longitude,double latitude,bool hasPosition) {
    const auto ref=existingObjectRef(value);if(!ref||(ref->domain!="territorial"&&ref->domain!="label"))return false;
    LabelSettings settings;if(const auto found=project_.document().presentation.webPresentation.labelSettings.find(*ref);found!=project_.document().presentation.webPresentation.labelSettings.end())settings=found->second;
    settings.pinned=pinned;if(hasPosition){if(!std::isfinite(longitude)||!std::isfinite(latitude))return false;settings.manualPosition=Point{longitude,latitude};}else if(!pinned)settings.manualPosition.reset();
    const auto result=PresentationCommandProcessor::apply(project_,SetLabelSettings{*ref,settings});if(result==PresentationResult::Applied)publishPresentation();return result==PresentationResult::Applied||result==PresentationResult::NoOp;
}
bool EditorController::resetLabelPosition(const QVariantMap& value) {
    const auto ref=existingObjectRef(value);if(!ref)return false;LabelSettings settings;if(const auto found=project_.document().presentation.webPresentation.labelSettings.find(*ref);found!=project_.document().presentation.webPresentation.labelSettings.end())settings=found->second;settings.pinned=false;settings.manualPosition.reset();
    const auto result=PresentationCommandProcessor::apply(project_,SetLabelSettings{*ref,settings});if(result==PresentationResult::Applied)publishPresentation();return result==PresentationResult::Applied||result==PresentationResult::NoOp;
}
bool EditorController::setLabelMapPosition(const QVariantMap& value,double mapX,double mapY) {
    if(!std::isfinite(mapX)||!std::isfinite(mapY))return false;const auto position=projection_.unproject(mapX,mapY);return setLabelPinned(value,true,position.x,position.y,true);
}
void EditorController::publishPresentation() {
    ++presentationRecoveryGeneration_;
    closeObjectChooser();
    if(hover_&&!objectVisible(*hover_)) {
        hover_.reset();hoverSource_.clear();++hoverRevision_;emit hoverChanged();
    }
    emit presentationChanged();emit visualChanged();emit stateChanged();emit searchChanged();emit dirtyChanged();
    presentationSaveInstance_=project_.instanceId();presentationSaveTimer_.start();
}
QString EditorController::presentationRecoveryPath() const {
    const auto fingerprint=QCryptographicHash::hash(
        QByteArray::fromStdString(project_.document().documentId),QCryptographicHash::Sha256).toHex();
    return storage_.privateProjectPath()+".presentation-recovery."+QString::fromLatin1(fingerprint)+".json";
}
QString EditorController::availablePresentationRecoveryPath() const {
    const QFileInfo privateFile(storage_.privateProjectPath());
    QDir directory(privateFile.absolutePath());
    const auto files=directory.entryInfoList(
        QStringList{privateFile.fileName()+".presentation-recovery.*.json"},QDir::Files,QDir::Time);
    return files.empty()?QString():files.front().absoluteFilePath();
}
bool EditorController::presentationRecoveryAvailable() const { return !availablePresentationRecoveryPath().isEmpty(); }
namespace {
QByteArray presentationRecoveryBytes(const pandoeditor::ProjectSnapshot& snapshot,
                                    const QString& sourcePath,std::uint64_t presentationRevision) {
    using V=losslessjson::Value;V envelope=V::obj();
    envelope.object["format"]=V::str("pandoeditor-presentation-recovery");
    envelope.object["sourcePath"]=V::str(sourcePath.toStdString());
    envelope.object["documentId"]=V::str(snapshot.document().documentId);
    envelope.object["contentRevision"]=V::num(snapshot.revision());
    envelope.object["presentationRevision"]=V::num(presentationRevision);
    auto bytes=envelope.encode();bytes.chop(1);
    // The codec already produced valid JSON; do not parse a whole second DOM to wrap it.
    return bytes+",\"project\":"+projectcodec::encode(snapshot)+"}";
}
}
void EditorController::startPresentationRecovery() {
    if(startupBusy_||presentationSaveInstance_!=project_.instanceId())return;
    if(presentationRecoveryWrite_){presentationRecoveryPending_=true;return;}
    presentationRecoveryPending_=false;
    const auto snapshot=project_.snapshot();
    const auto revision=project_.presentationRevision(),generation=presentationRecoveryGeneration_;
    const auto path=presentationRecoveryPath(),sourcePath=filePath_;
    using Result=std::pair<QByteArray,QString>;
    auto* watcher=new QFutureWatcher<Result>(this);presentationRecoveryWrite_=watcher;
    connect(watcher,&QFutureWatcher<Result>::finished,this,[this,watcher,snapshot,revision,generation,path] {
        presentationRecoveryWrite_=nullptr;
        const auto result=watcher->result();watcher->deleteLater();
        if(generation==presentationRecoveryGeneration_&&snapshot.matches(project_)&&
           revision==project_.presentationRevision()&&presentationSaveInstance_==project_.instanceId()) {
            if(!result.second.isEmpty())emit errorOccurred(result.second);
            else try {
                storage_.write(QUrl::fromLocalFile(path),result.first);
                emit presentationRecoveryChanged();
            }catch(const std::exception& error) {emit errorOccurred(QString::fromUtf8(error.what()));}
        }
        if(presentationRecoveryPending_&&!presentationSaveTimer_.isActive())startPresentationRecovery();
    });
    watcher->setFuture(QtConcurrent::run([snapshot,sourcePath,revision]() -> Result {
        try {return {presentationRecoveryBytes(snapshot,sourcePath,revision),{}};}
        catch(const std::exception& error){return {{},QString::fromUtf8(error.what())};}
        catch(...){return {{},QStringLiteral("표시 설정 복구본을 준비하지 못했습니다.")};}
    }));
}
bool EditorController::flushPresentationRecovery() {
    presentationSaveTimer_.stop();++presentationRecoveryGeneration_;presentationRecoveryPending_=false;
    if(startupBusy_||presentationSaveInstance_!=project_.instanceId())return false;
    try {
        storage_.write(QUrl::fromLocalFile(presentationRecoveryPath()),
            presentationRecoveryBytes(project_.snapshot(),filePath_,project_.presentationRevision()));
        emit presentationRecoveryChanged();return true;
    }catch(const std::exception& e){emit errorOccurred(QStringLiteral("표시 설정 복구본을 저장하지 못했습니다: ")+QString::fromUtf8(e.what()));return false;}
}
bool EditorController::discardPresentationRecovery() {
    ++presentationRecoveryGeneration_;presentationRecoveryPending_=false;
    presentationSaveTimer_.stop();presentationSaveInstance_.clear();
    const auto current=presentationRecoveryPath();
    const auto target=QFileInfo::exists(current)?current:availablePresentationRecoveryPath();
    const bool ok=target.isEmpty()||QFile::remove(target);
    if(ok)emit presentationRecoveryChanged();else emit errorOccurred(QStringLiteral("복구본을 삭제하지 못했습니다."));return ok;
}
bool EditorController::discardOwnPresentationRecovery() {
    ++presentationRecoveryGeneration_;presentationRecoveryPending_=false;
    const auto path=presentationRecoveryPath();
    if(!QFileInfo::exists(path))return true;
    if(!QFile::remove(path)){emit errorOccurred(QStringLiteral("복구본을 삭제하지 못했습니다."));return false;}
    emit presentationRecoveryChanged();return true;
}
bool EditorController::restorePresentationRecovery() {
    if(startupBusy_)return false;
    try {
        const auto recovery=availablePresentationRecoveryPath();
        losslessjson::require(!recovery.isEmpty(),"RECOVERY_NOT_FOUND");
        auto root=losslessjson::parse(storage_.read(QUrl::fromLocalFile(recovery)));
        losslessjson::require(root.object.at("format").string=="pandoeditor-presentation-recovery","INVALID_RECOVERY");
        losslessjson::require(root.object.at("contentRevision").kind==losslessjson::Value::Number,"INVALID_RECOVERY");
        losslessjson::require(root.object.at("presentationRevision").kind==losslessjson::Value::Number,"INVALID_RECOVERY");
        auto bytes=root.object.at("project").encode();auto candidate=projectcodec::decode(bytes);
        losslessjson::require(candidate.documentId==root.object.at("documentId").string,"INVALID_RECOVERY_DOCUMENT");
        replaceFromBytes(bytes,true,QString::fromStdString(root.object.at("sourcePath").string));
        return true;
    }catch(const std::exception& e){emit errorOccurred(QStringLiteral("복구본을 열지 못했습니다: ")+QString::fromUtf8(e.what()));return false;}
}
bool EditorController::setPresentationVisibility(const QString& key,bool visible) {
    auto r=PresentationCommandProcessor::apply(project_,SetPresentationVisibility{key.toStdString(),visible});if(r==PresentationResult::Applied)publishPresentation();return r==PresentationResult::Applied||r==PresentationResult::NoOp;
}
bool EditorController::setPresentationOpacity(const QString& group,double opacity) {
    PresentationStyle s;s.opacity=opacity;auto r=PresentationCommandProcessor::apply(project_,PatchGroupPresentation{group.toStdString(),s});if(r==PresentationResult::Applied)publishPresentation();return r==PresentationResult::Applied||r==PresentationResult::NoOp;
}
bool EditorController::setPresentationBoundary(const QString& group,bool visible) {
    PresentationStyle s;s.boundaryVisible=visible;auto r=PresentationCommandProcessor::apply(project_,PatchGroupPresentation{group.toStdString(),s});if(r==PresentationResult::Applied)publishPresentation();return r==PresentationResult::Applied||r==PresentationResult::NoOp;
}
bool EditorController::toggleSelectionVisibility() {
    auto r=PresentationCommandProcessor::apply(project_,SetBatchVisibility{selection_.items(),{}});if(r==PresentationResult::Applied)publishPresentation();return r==PresentationResult::Applied||r==PresentationResult::NoOp;
}

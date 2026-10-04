#include "editorcontroller.h"
#include "terrainimageprovider.h"
#include "../renderer/gpumapitem.h"
#include "defaultflagresolver.h"
#include "worlddatasetloader.h"
#include <pandoeditor/maprenderorder.h>
#include <QFile>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDir>
#include <QUuid>
#include <QGuiApplication>
#include <QStyleHints>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <cmath>
#include <map>
#include <limits>
#include <stdexcept>

namespace {
QString text(const std::string& value) { return QString::fromStdString(value); }
QString rgb(std::uint32_t color) { return QString("#%1").arg(color,6,16,QChar('0')); }

bool validImageDataUrl(const QString& source)
{
    const auto separator=source.indexOf(QStringLiteral(";base64,"));
    if(!source.startsWith(QStringLiteral("data:image/")) || separator < 11) return false;
    const auto bytes=QByteArray::fromBase64(source.mid(separator+8).toLatin1());
    if(bytes.isEmpty()) return false;
    return bytes.startsWith("\x89PNG\r\n\x1a\n") || bytes.startsWith("\xff\xd8\xff") ||
           bytes.startsWith("GIF87a") || bytes.startsWith("GIF89a") ||
           (bytes.startsWith("RIFF") && bytes.mid(8,4)=="WEBP") ||
           bytes.trimmed().startsWith("<svg");
}

}
EditorController::EditorController(QObject* parent):EditorController(EditorControllerConfig{},parent) {}
EditorController::EditorController(EditorControllerConfig config,QObject* parent)
    :QObject(parent),quality_(config.mobileMode),
     appearancePath_(std::move(config.appearancePath)),storage_(std::move(config.privateProjectPath)),mobileMode_(config.mobileMode)
{
    qualityClock_.start();
    loadAppearancePreferences();
    connect(QGuiApplication::styleHints(),&QStyleHints::colorSchemeChanged,this,[this]{emit appearanceChanged();});
    packetCache_.setBudget(quality_.profile().renderPacketCacheBudgetBytes);
    worldDataRoot_=std::move(config.worldDataRoot);
    if(config.autosaveEnabled) {
        autosave_=std::make_unique<ProjectAutosave>(std::move(config.autosaveProjectPath),
                                                     std::move(config.autosaveViewPath),this);
        startupBusy_=QFile::exists(autosave_->projectPath());
        if(startupBusy_)worldStatus_=QStringLiteral("restoring");
    }
    if(config.projectPreviewEnabled) {
        projectPreview_=std::make_unique<ProjectPreviewService>(std::move(config.projectPreviewCachePath),this);
        projectPreviewSourceSha_=std::move(config.projectPreviewSourceSha);
        connect(projectPreview_.get(),&ProjectPreviewService::generationFailed,this,&EditorController::errorOccurred);
    }
    terrainResourceBridge_=std::make_unique<TerrainImageBridge>(this);
    connect(this,&EditorController::terrainChanged,this,[this]{terrainResourceBridge_->setSource(terrainProvider_);});
    QFile anchorFile(QStringLiteral(":/world/country-label-anchors-v0.10.1.json"));
    labelAnchors_=std::make_unique<CountryLabelAnchors>(
        anchorFile.open(QIODevice::ReadOnly)?anchorFile.readAll():QByteArray{},this);
    connect(labelAnchors_.get(),&CountryLabelAnchors::changed,this,[this] {
        emit presentationChanged();emit visualChanged();
    });
    bootstrapWorldEnabled_=config.bootstrapWorld;
    initializePhysicalData();
    if(startupBusy_||config.bootstrapWorld) {
        project_.replace(pandoeditor::ProjectDocument(
            std::vector<pandoeditor::Country>{},std::vector<pandoeditor::Layer>{{"countries","국가"}}));
        projection_.setWorldExtent();
    } else {
        QFile sample(":/assets/sample.pando.json");
        if(!sample.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot read bundled sample");
        project_.replace(projectcodec::decode(sample.readAll()));
        projection_.rebuild(project_.document());
    }
    camera_.setMetrics(mapCameraMetrics());
    sceneBridge_.publishView(camera_.view());
    camera_.acceptPublishedView(sceneBridge_.viewState());
    selectionInstance_=project_.instanceId();reloadDrafts();
    connect(&hydroRuntime_,&HydroRuntimeProvider::frameChanged,this,&EditorController::hydroFrameChanged);
    connect(&hydroRuntime_,&HydroRuntimeProvider::frameChanged,this,&EditorController::searchChanged);
    connect(&hydroRuntime_,&HydroRuntimeProvider::frameChanged,this,&EditorController::stateChanged);
    connect(&hydroRuntime_,&HydroRuntimeProvider::frameChanged,this,[this] {
        refreshBuiltinHydroScene();
        refreshTypedScene();
    });
    connect(&hydroRuntime_,&HydroRuntimeProvider::loadFailed,this,&EditorController::errorOccurred);
    connect(this,&EditorController::dirtyChanged,this,[this](){++importEditEpoch_;});
    // Connect before any QML observers: all readers of one notification share
    // one read model, including the panels that are currently hidden.
    connect(this,&EditorController::propertyChanged,this,[this]{objectPropertiesCache_.reset();});
    connect(this,&EditorController::selectionChanged,this,[this]{objectPropertiesCache_.reset();});
    connect(this,&EditorController::stateChanged,this,[this]{objectRowsCache_.reset();});
    connect(this,&EditorController::stateChanged,this,[this]{layersCache_.reset();});
    connect(this,&EditorController::geometryChanged,this,[this]{layersCache_.reset();});
    connect(this,&EditorController::stateChanged,this,&EditorController::propertyChanged);
    connect(this,&EditorController::draftsChanged,this,&EditorController::propertyChanged);
    connect(this,&EditorController::jobChanged,this,&EditorController::propertyChanged);
    connect(this,&EditorController::webImportChanged,this,&EditorController::propertyChanged);
    jobs_=std::make_unique<CommandJobRunner>([this]() ->const pandoeditor::Project& {return project_;});
    connect(jobs_.get(),&CommandJobRunner::changed,this,&EditorController::jobChanged,Qt::QueuedConnection);
    presentationSaveTimer_.setSingleShot(true);presentationSaveTimer_.setInterval(500);
    connect(&presentationSaveTimer_,&QTimer::timeout,this,&EditorController::startPresentationRecovery);
    viewportResourceTimer_.setSingleShot(true);
    viewportResourceTimer_.setInterval(ViewportResourceScheduler::SettleDelayMs);
    connect(&viewportResourceTimer_,&QTimer::timeout,this,&EditorController::flushViewportResources);
    const auto refresh=[this] {refreshTypedScene();};
    connect(this,&EditorController::selectionChanged,this,refresh);
    connect(this,&EditorController::selectionChanged,this,[this] {
        invalidateViewportResources(ViewportResourceKind::Labels);
    });
    connect(this,&EditorController::selectionChanged,this,[this] {
        std::set<quint32> logical;
        for(const auto& selected:selection_.items())if(selected.domain=="hydroBuiltin")
            if(const auto record=hydroRuntime_.recordById(text(selected.id)))logical.insert(record->logicalFid);
        hydroRuntime_.setSelectedLogicals(logical);
    });
    connect(this,&EditorController::hoverChanged,this,refresh);
    connect(this,&EditorController::objectChooserChanged,this,refresh);
    connect(this,&EditorController::geometryEditChanged,this,refresh);
    connect(this,&EditorController::contentEditChanged,this,refresh);
    connect(this,&EditorController::colorEditChanged,this,refresh);
    connect(this,&EditorController::structureChanged,this,refresh);
    connect(this,&EditorController::presentationChanged,this,refresh);
    connect(this,&EditorController::presentationChanged,this,[this] {
        labelSourcesDirty_=true;
        labelEngine_.clear();labelFlagSources_.clear();
        if(!placedLabels_.isEmpty()){placedLabels_.clear();placedLabelModel_.setRows({});emit labelLayoutChanged();}
        invalidateViewportResources(ViewportResourceKind::Labels);
    });
    connect(this,&EditorController::geometryChanged,this,refresh);
    connect(this,&EditorController::geometryChanged,this,[this] {
        labelSourcesDirty_=true;
        labelEngine_.clear();labelFlagSources_.clear();
        if(!placedLabels_.isEmpty()){placedLabels_.clear();placedLabelModel_.setRows({});emit labelLayoutChanged();}
        syncMapCameraMetrics();
        invalidateViewportResources(ViewportResourceKind::Labels);
    });
    connect(&sceneBridge_,&MapSceneBridge::viewChanged,this,[this] {
        updateWorldDetail();
        sceneBuilder_.setWorldBase(worldBase_);
        sceneBuilder_.setQuality(quality_.profile());
        if(!sceneBuilder_.canReusePreparation(project_.snapshot(),sceneBridge_.viewState(),
                                              sceneBridge_.sceneSnapshot()))refreshTypedScene();
    });
    connect(&sceneBridge_,&MapSceneBridge::viewChanged,this,[this] {
        camera_.acceptPublishedView(sceneBridge_.viewState());
        reprojectLabelPlacements();
        scheduleViewportResources();
        emit viewStateChanged();
    });
    if(autosave_) {
        connect(&sceneBridge_,&MapSceneBridge::viewChanged,this,[this] {
            if(!startupBusy_&&worldStatus_!="recovery-failed")
                autosave_->scheduleView(sceneBridge_.viewState());
        });
        connect(autosave_.get(),&ProjectAutosave::saveFailed,this,&EditorController::errorOccurred);
    }
    QFile historicalFile(QStringLiteral(":/historical/historical-library-pilot.json"));
    if(historicalFile.open(QIODevice::ReadOnly))
        installHistoricalSource(historicalFile.readAll(),QStringLiteral("내장 pilot"),true);
    refreshTypedScene();
    scheduleViewportResources(ViewportResourceKind::Labels);
    if(startupBusy_)QTimer::singleShot(0,this,[this,useWorldBase=config.bootstrapWorld] {
        startAutosaveRecovery(useWorldBase);
    });
    else if(config.bootstrapWorld)QTimer::singleShot(0,this,[this,initial=project_.snapshot()] {
        if(initial.matches(project_))startWorldBootstrap();
    });
}
void EditorController::refreshTypedScene() {
    if(startupBusy_)return;
    updateWorldDetail();
    try {
        std::shared_ptr<const RenderScene> previous=sceneBridge_.sceneSnapshot();
        if(sceneInstance_!=project_.instanceId()) {
            // Keep the previous publication solely for monotonic scene revisions.
            // Builder project identity invalidation still creates fresh preparation.
            packetCache_.clear();sceneInstance_=project_.instanceId();pendingSceneImpact_.reset();
        }
        sceneBuilder_.setWorldBase(worldBase_);
        sceneBuilder_.setBuiltinHydro(builtinHydroScene_);
        sceneBuilder_.setQuality(quality_.profile());
        InteractionRenderPacket interaction;
        if(objectChooserOpen())interaction.candidates=chooserRefs_;
        interaction.selected=selection_.items();
        interaction.primary=selection_.primary();interaction.hover=hover_;
        if(geometryEdit_) {
            interaction.editTarget=geometryEdit_->target;
            if(geometryEdit_->mergeIntent)interaction.selected=geometryEdit_->mergeIntent->donors;
            if(geometryEdit_->annexIntent)interaction.selected=geometryEdit_->annexIntent->donors;
            if(geometryEdit_->territorySelection){interaction.selected.clear();for(const auto& source:geometryEdit_->territorySelection->state().sources)interaction.selected.push_back(source.ref);}
        }
        const auto* dirty=pendingSceneImpact_&&pendingSceneImpactRevision_==project_.revision()?
            &pendingSceneImpact_->sceneDirty:nullptr;
        const auto preparationsBefore=sceneBuilder_.preparationCount();
        const auto deltasBefore=sceneBuilder_.deltaUpdateCount();
        auto scene=sceneBuilder_.refresh(project_.snapshot(),sceneBridge_.viewState(),interaction,previous,dirty);
        if(scene!=sceneBridge_.sceneSnapshot()) {
            sceneBridge_.publishScene(std::move(scene));
            if(sceneBuilder_.deltaUpdateCount()!=deltasBefore)++scenePatchCount_;
            else if(sceneBuilder_.preparationCount()!=preparationsBefore)++sceneFullBuildCount_;
            emit renderQualityChanged();
        }
        pendingSceneImpact_.reset();
    }catch(const std::exception& error) {
        emit errorOccurred(QStringLiteral("Typed map scene preparation failed: ")+
            QString::fromUtf8(error.what()));
    }
}
void EditorController::noteAppliedImpact(const pandoeditor::ChangeImpact& impact) {
    lastEditAffectedObjects_=impact.changedObjects.size();
    lastEditRetainedGeometries_=impact.retainedGeometryCount;
    lastEditNewGeometryBytes_=impact.estimatedNewGeometryBytes;
    if(impact.sceneDirty.datasetResource)mapPicker_.reset();
    else mapPicker_.applyImpact(project_.snapshot(),impact.sceneDirty.geometryObjects);
    try {
        pendingSceneImpact_=impact;
        pendingSceneImpactRevision_=project_.revision();
    }catch(...) {pendingSceneImpact_.reset();}
    for(const auto& owner:impact.sceneDirty.geometryObjects)if(owner.domain=="territorial")scheduleDerivedLabelAnchor(owner);
}

void EditorController::initializePhysicalData() {
    QFile inventoryFile(QStringLiteral(":/world/physical-inventory-c0bd31d1.json"));
    if(!inventoryFile.open(QIODevice::ReadOnly))return;
    const auto inventory=parsePhysicalInventory(inventoryFile.readAll());
    if(!inventory.valid()){physicalError_=inventory.error;return;}
    physicalStore_=std::make_unique<PhysicalDataStore>(QString(),3,2,this);
    physicalStore_->setExternalRoot(qEnvironmentVariable("PANDOEDITOR_WORLD_DATA_ROOT"));
    physicalStore_->cleanupVersions(inventory.dataset,inventory.version);
    physicalRoot_=QDir(physicalStore_->root()).filePath(inventory.dataset+'/'+inventory.version);
    for(const auto& asset:inventory.assets)physicalAssets_.insert(asset.path,asset);
    const auto seed=[this](const QString& relative,const QString& resource) {
        const auto found=physicalAssets_.constFind(relative);if(found==physicalAssets_.cend())return;
        QFile file(resource);if(file.open(QIODevice::ReadOnly))physicalStore_->installVerified(*found,file.readAll());
    };
    seed("terrain/v0.12.6/manifest.json",":/world/terrain/v0.12.6/manifest.json");
    seed("hydro/v0.13.1/manifest.json",":/world/hydro/v0.13.1/manifest.json");
    connect(physicalStore_.get(),&PhysicalDataStore::activityChanged,this,[this](int active,int queued) {
        physicalActive_=active;physicalQueued_=queued;emit terrainChanged();emit stateChanged();
    });
    connect(physicalStore_.get(),&PhysicalDataStore::assetFailed,this,[this](const QString&,const QString& message) {
        physicalError_=message;emit terrainChanged();emit stateChanged();
    });
    connect(physicalStore_.get(),&PhysicalDataStore::assetReady,this,
        [this](const QString& path,const QString&,bool) {
            physicalError_.clear();
            if(path.startsWith("terrain/"))
                invalidateViewportResources(ViewportResourceKind::Terrain);
            if(path.startsWith("hydro/")) {
                if(!hydroRuntime_.isOpen())syncHydroData();
                invalidateViewportResources(ViewportResourceKind::Hydro);
            }
            emit terrainChanged();emit stateChanged();
        });
}
QString EditorController::physicalAssetPath(const QString& relativePath) const {
    if(!physicalStore_)return {};
    const auto found=physicalAssets_.constFind(relativePath);if(found==physicalAssets_.cend())return {};
    const auto existing=physicalStore_->resolveExisting(*found);
    return existing.isEmpty()?physicalStore_->cachePath(*found):existing;
}
bool EditorController::physicalAssetReady(const QString& relativePath) const {
    if(!physicalStore_)return false;const auto found=physicalAssets_.constFind(relativePath);
    return found!=physicalAssets_.cend()&&!physicalStore_->resolveExisting(*found).isEmpty();
}
void EditorController::requestPhysicalAsset(const QString& relativePath) {
    if(!physicalStore_)return;
    const auto found=physicalAssets_.constFind(relativePath);if(found!=physicalAssets_.cend())physicalStore_->request(*found);
}
void EditorController::ensureHydroBootstrap() {
    if(!physicalStore_)return;
    for(const auto& path:{QStringLiteral("hydro/v0.13.0/index.bin.gz"),
                          QStringLiteral("hydro/v0.13.1/metadata-core.json.gz")})
        if(!physicalAssetReady(path))requestPhysicalAsset(path);
}

QString EditorController::labelSourceId(const std::string& ownerId) const {
    QString first;
    for(const auto& range:worldRanges_)if(range.ownerId==ownerId) {
        const auto source=text(range.sourceId);if(range.sourceId==ownerId)return source;if(first.isEmpty())first=source;
    }
    return first.isEmpty()?text(ownerId):first;
}
void EditorController::scheduleDerivedLabelAnchor(const pandoeditor::ObjectRef& owner) {
    if(!labelAnchors_||owner.domain!="territorial")return;
    const auto object=project_.index().objects.find(owner);if(object==project_.index().objects.end()){labelAnchors_->invalidateOwner(text(owner.id));return;}
    const auto& unit=project_.document().units.at(object->second);const auto binding=pandoeditor::staticGeometryBinding(project_.document(),unit.id).geometryRef;const auto geometry=project_.document().geometries.get(binding);
    labelAnchors_->setProjectScope(text(project_.instanceId()));
    if(geometry)labelAnchors_->recompute(text(owner.id),*geometry,binding);
    else labelAnchors_->invalidateOwner(text(owner.id));
}
QVariantMap EditorController::renderQuality() const {
    resourceCoordinator_.setDomain("geometry",packetCache_.resourceCacheSnapshot());
    resourceCoordinator_.setDomain("terrain",terrainProvider_?terrainProvider_->resourceCacheSnapshot():pandoeditor::ResourceCacheSnapshot{},"bounded",{{"assetPending",terrainAssetPending_},{"pendingKind","requested payloads not yet decoded"}});
    resourceCoordinator_.setDomain("hydro",hydroRuntime_.resourceCacheSnapshot(),"bounded",{{"activeFrameBytes",qulonglong(hydroRuntime_.activeFrameBytes())},{"frameBytesAvailable",true}});
    resourceCoordinator_.setDomain("world",worldResources_.snapshot(),worldResources_.compatibilityBudget()?"compatibilityWorkingSet":"bounded");
    auto labels=labelEngine_.resourceCacheSnapshot();QVariantMap labelExtra;
    if(labelAnchors_) {
        const auto anchors=labelAnchors_->resourceCacheSnapshot();
        labels.residentCount+=anchors.residentCount;labels.residentBytes+=anchors.residentBytes;
        labels.protectedBytes+=anchors.protectedBytes;labels.activeBytes+=anchors.activeBytes;
        labels.pendingCount+=anchors.pendingCount;labels.pendingUnknownCount+=anchors.pendingUnknownCount;
        for(std::size_t i=0;i<labels.protectionCounts.size();++i)labels.protectionCounts[i]+=anchors.protectionCounts[i];
        labels.failureCount+=anchors.failureCount;labels.invalidationCount+=anchors.invalidationCount;labels.staleCompletionCount+=anchors.staleCompletionCount;
        if(labelEngine_.compatibilityResourceBudget())labels.budgetBytes+=anchors.budgetBytes;
        labels.overBudgetBytes=labels.residentBytes>labels.budgetBytes?labels.residentBytes-labels.budgetBytes:0;
        labels.protectedOverBudgetBytes=labels.protectedBytes>labels.budgetBytes?labels.protectedBytes-labels.budgetBytes:0;
        labelExtra={{"anchorFixedBytes",qulonglong(labelAnchors_->fixedStorageBytes())},{"anchorDerivedBytes",qulonglong(labelAnchors_->derivedStorageBytes())},{"anchorPending",qulonglong(anchors.pendingCount)}};
    }
    resourceCoordinator_.setDomain("label",labels,labelEngine_.compatibilityResourceBudget()?"compatibilityWorkingSet":"bounded",labelExtra);
    const auto profile=quality_.profile();
    const auto stats=packetCache_.stats();
    const auto tier=profile.tier==RenderQualityTier::Coarse?"coarse":
        profile.tier==RenderQualityTier::Medium?"medium":"high";
    return {{"resourceCaches",resourceCoordinator_.snapshot()},{"tier",tier},{"revision",qulonglong(profile.revision)},
        {"worldDetailRequested",worldDetailCanonical_?"canonical":"preview"},
        {"worldDetailDisplayed",worldBase_&&worldBase_->mesh?(worldBase_->mesh->preview?"preview":"canonical"):"none"},
        {"interaction",profile.interaction},{"dprCap",profile.dprCap},
        {"labelDensity",profile.labelDensity},
        {"uploadBudgetBytes",qulonglong(profile.uploadBudgetBytes)},
        {"overlayGpuBudgetBytes",qulonglong(profile.overlayGpuBudgetBytes)},
        {"packetCacheBytes",qulonglong(stats.residentBytes)},
        {"packetCacheHits",qulonglong(stats.hits)},
        {"packetCacheBuilds",qulonglong(stats.builds)},
        {"packetCacheEvictions",qulonglong(stats.evictions)},
        {"packetCacheBudgetBytes",qulonglong(packetCache_.budget())},
        {"scenePatchCount",qulonglong(scenePatchCount_)},
        {"sceneFullBuildCount",qulonglong(sceneFullBuildCount_)},
        {"scenePreparationCount",qulonglong(sceneBuilder_.preparationCount())},
        {"sceneDeltaUpdateCount",qulonglong(sceneBuilder_.deltaUpdateCount())},
        {"scenePresentationUpdateCount",qulonglong(sceneBuilder_.presentationUpdateCount())},
        {"sceneTransientUpdateCount",qulonglong(sceneBuilder_.transientUpdateCount())},
        {"sceneUnchangedCount",qulonglong(sceneBuilder_.unchangedCount())},
        {"scenePublicationCount",qulonglong(sceneBridge_.scenePublicationCount())},
        {"interactionFrameCount",qulonglong(sceneBridge_.interactionFrameCount())},
        {"viewFrameCount",qulonglong(sceneBridge_.viewFrameCount())},
        {"spatialIncrementalUpdateCount",qulonglong(mapPicker_.incrementalUpdateCount())},
        {"viewportResourceGeneration",qulonglong(viewportResources_.lastIssuedGeneration())},
        {"viewportResourceUpdates",qulonglong(viewportResources_.stats().viewportUpdates)},
        {"viewportResourceInvalidations",qulonglong(viewportResources_.stats().invalidations)},
        {"viewportResourceDeferredUpdates",qulonglong(viewportResources_.stats().deferredUpdates)},
        {"viewportResourceIssuedRequests",qulonglong(viewportResources_.stats().issuedRequests)},
        {"viewportResourceCoalescedUpdates",qulonglong(viewportResources_.stats().coalescedUpdates)},
        {"viewportResourcePending",viewportResources_.pending()},
        {"lastEditAffectedObjects",qulonglong(lastEditAffectedObjects_)},
        {"lastEditRetainedGeometries",qulonglong(lastEditRetainedGeometries_)},
        {"lastEditEstimatedNewGeometryBytes",qulonglong(lastEditNewGeometryBytes_)},
        {"terrainCacheBudgetBytes",qulonglong(profile.terrainCacheBudgetBytes)},
        {"terrainCacheBytes",qulonglong(terrainProvider_?terrainProvider_->cachedBytes():0)},
        {"hydroCacheBytes",qulonglong(hydroRuntime_.cachedBytes())},
        {"builtinHydroGpuRevision",qulonglong(builtinHydroRevision_)},
        {"builtinHydroGpuFeatures",qulonglong(builtinHydroScene_?builtinHydroScene_->features.size():0)},
        {"labelSourceRevision",qulonglong(labelEngine_.stats().sourceRevision)},
        {"labelLayoutRevision",qulonglong(labelEngine_.stats().layoutRevision)},
        {"labelSourceCount",qulonglong(labelEngine_.stats().sourceCount)},
        {"labelShardCount",qulonglong(labelEngine_.stats().cellCount)},
        {"labelQueries",qulonglong(labelEngine_.stats().queries)},
        {"labelLayouts",qulonglong(labelEngine_.stats().layouts)},
        {"labelReprojects",qulonglong(labelEngine_.stats().reprojects)},
        {"labelCandidatesExamined",qulonglong(labelEngine_.stats().candidatesExamined)},
        {"labelPlacements",qulonglong(labelEngine_.stats().placements)},
        {"p95FrameMs",profile.p95FrameMs},{"p99FrameMs",profile.p99FrameMs},
        {"qualityChangeCount",qulonglong(quality_.changeCount())},
        {"longFrameCount",qulonglong(profile.longFrameCount)}};
}
void EditorController::recordMapFrame(double milliseconds) {
    if(quality_.recordFrame(milliseconds,qualityClock_.elapsed())) {
        const auto profile=quality_.profile();
        packetCache_.setBudget(profile.renderPacketCacheBudgetBytes);
        hydroRuntime_.setCacheBudget(profile.hydroCacheBudgetBytes);
        if(terrainProvider_)terrainProvider_->setCacheBudget(profile.terrainCacheBudgetBytes);
        invalidateViewportResources(ViewportResourceKind::Labels);
        emit renderQualityChanged();refreshTypedScene();
    }
}
void EditorController::beginMapInteraction() {
    if(activeMapInteractions_==0)viewportResourceTimer_.stop();
    ++activeMapInteractions_;
    viewportResources_.beginInteraction();
    if(activeMapInteractions_==1&&quality_.beginInteraction()) {
        viewportResources_.invalidate(ViewportResourceKind::Labels);
        emit renderQualityChanged();
    }
}
void EditorController::endMapInteraction() {
    if(activeMapInteractions_<=0)return;
    --activeMapInteractions_;
    if(activeMapInteractions_==0&&quality_.endInteraction()) {
        viewportResources_.invalidate(ViewportResourceKind::Labels);
        emit renderQualityChanged();
    }
    const bool resourceReady=viewportResources_.endInteraction();
    if(resourceReady) {
        viewportResourceTimer_.stop();
        flushViewportResources();
    }
}
void EditorController::refreshBuiltinHydroScene() {
    try {
        const auto frame=hydroRuntime_.frame();
        if(!frame) {
            builtinHydroScene_.reset();
            if(builtinHydroRevision_!=std::numeric_limits<std::uint64_t>::max())++builtinHydroRevision_;
            return;
        }
        if(builtinHydroRevision_==std::numeric_limits<std::uint64_t>::max()) {
            emit errorOccurred(QStringLiteral("Built-in hydro GPU revision overflow"));
            return;
        }
        std::vector<BuiltinHydroFeaturePacket> features;
        features.reserve(frame->features.size());
        std::set<std::uint32_t> seen;
        for(const auto& feature:frame->features) {
            if(!seen.insert(feature.fid).second)continue;
            const auto record=hydroRuntime_.recordByFid(feature.fid);
            if(!record)continue;
            const auto category=record->category.toStdString();
            if(category!="river"&&category!="lake")continue;
            features.push_back(prepareBuiltinHydroFeature(
                {"hydroBuiltin",record->awId.toStdString()},category,feature));
        }
        ++builtinHydroRevision_;
        builtinHydroScene_=makeBuiltinHydroRenderFrame(builtinHydroRevision_,features);
    } catch(const std::exception& error) {
        builtinHydroScene_.reset();
        emit errorOccurred(QStringLiteral("Built-in hydro GPU preparation failed: ")+
                           QString::fromUtf8(error.what()));
    }
}

void EditorController::scheduleViewportResources(ViewportResourceKind resources) {
    try {
        if(viewportResources_.noteViewport(camera_.display(),mapCameraMetrics(),resources))
            viewportResourceTimer_.start(ViewportResourceScheduler::SettleDelayMs);
    } catch(const std::exception& error) {
        emit errorOccurred(QStringLiteral("Viewport resource scheduling failed: ")+
            QString::fromUtf8(error.what()));
    }
}
void EditorController::invalidateViewportResources(ViewportResourceKind resources) {
    try {
        const bool arm=viewportResources_.invalidate(resources);
        if(!viewportResources_.pending()) {
            scheduleViewportResources(resources);
            return;
        }
        if(arm)viewportResourceTimer_.start(ViewportResourceScheduler::SettleDelayMs);
    } catch(const std::exception& error) {
        emit errorOccurred(QStringLiteral("Viewport resource invalidation failed: ")+
            QString::fromUtf8(error.what()));
    }
}
void EditorController::flushViewportResources() {
    try {
        const auto request=viewportResources_.takeReady();
        if(!request)return;
        if(anyViewportResource(request->resources&ViewportResourceKind::Terrain))
            executeTerrainResources(*request);
        if(anyViewportResource(request->resources&ViewportResourceKind::Hydro))
            executeHydroResources(*request);
        if(anyViewportResource(request->resources&ViewportResourceKind::Labels))
            executeLabelResources(*request);
        emit renderQualityChanged();
    } catch(const std::exception& error) {
        emit errorOccurred(QStringLiteral("Viewport resource request failed: ")+
            QString::fromUtf8(error.what()));
    }
}
QString EditorController::labelFlagSource(const pandoeditor::ObjectRef& ref) const {
    return resolveDefaultFlag(project_.document(),ref).source;
}

QVariantMap EditorController::colors() const
{
    QVariantMap result;
    for(const auto& unit:project_.document().units) {
        const auto style=project_.document().presentation.objectStyles.find(pandoeditor::territorialRef(unit.id));
        if(style!=project_.document().presentation.objectStyles.end()) result[text(unit.id)]=rgb(pandoeditor::effectiveObjectColor(project_.document(),pandoeditor::territorialRef(unit.id)));
    }
    return result;
}
QString EditorController::selectedFlagSource() const {
    const auto* unit=selectedUnit();
    return unit?labelFlagSource(pandoeditor::territorialRef(unit->id)):QString{};
}
QVariantMap EditorController::countryVisuals() const
{
    QVariantMap result;
    const auto distributionRows=pandoeditor::visibleDistributionEntries(project_.document());
    const std::set<pandoeditor::ObjectRef> visibleDistribution(distributionRows.begin(),distributionRows.end());
    std::map<std::string,std::optional<pandoeditor::DistributionValueRange>> distributionRanges;
    for(const auto& layer:project_.document().distributionLayers)
        distributionRanges.emplace(layer.id,pandoeditor::distributionValueRange(project_.document(),layer.id));
    for(const auto& unit:project_.document().units) {
        const auto ref=pandoeditor::territorialRef(unit.id);
        const auto style=project_.document().presentation.objectStyles.find(ref);
        if(style==project_.document().presentation.objectStyles.end()) continue;
        const auto resolved=pandoeditor::resolvedTerritorialPresentation(project_.document(),ref);
        const auto nativeLayer=pandoeditor::nativeLayerId(project_.document(),ref);double nativeOpacity=1;int nativeOrder=-1;
        for(std::size_t i=0;i<project_.document().presentation.userLayers.size();++i)if(project_.document().presentation.userLayers[i].id==nativeLayer){nativeOpacity=project_.document().presentation.userLayers[i].opacity;nativeOrder=int(i);break;}
        const auto resolvedFlag=resolveDefaultFlag(project_.document(),ref);
        const auto& flag=resolvedFlag.source;const auto flagAvailable=resolvedFlag.available;
        const auto& flagReason=resolvedFlag.reason;
        result[text(unit.id)]=QVariantMap{{"color",rgb(resolved.colorVisible?pandoeditor::effectiveObjectColor(project_.document(),pandoeditor::territorialRef(unit.id)):0xa8c7db)},
            {"name",text(project_.propertyView(ref)->displayName)},{"nameVisible",resolved.nameVisible},{"flagVisible",resolved.flagVisible},{"flagSource",flag},{"flagAvailable",flagAvailable},{"flagReason",flagReason},
            {"boundary",pandoeditor::resolvedTerritorialPresentation(project_.document(),ref).boundaryVisible},
            {"blendMode",text(resolved.blendMode)},
            {"kind",pandoeditor::isRootGeneral(project_.document(),unit)?QStringLiteral("country"):unit.kind==pandoeditor::UnitKind::General?QStringLiteral("subunit"):QStringLiteral("region")},
            {"layerId",text(nativeLayer)},{"layerOpacity",nativeOpacity},{"layerOrder",nativeOrder},
            {"drawFillPass",pandoeditor::mapRenderOrder(project_.document(),ref,pandoeditor::RenderPrimitiveRole::Fill).pass},
            {"drawLinePass",pandoeditor::mapRenderOrder(project_.document(),ref,pandoeditor::RenderPrimitiveRole::Line).pass},
            {"drawBoundaryPass",pandoeditor::mapRenderOrder(project_.document(),ref,pandoeditor::RenderPrimitiveRole::Boundary).pass},
            {"drawGroup",pandoeditor::mapRenderOrder(project_.document(),ref,pandoeditor::RenderPrimitiveRole::Fill).group},
            {"drawObject",pandoeditor::mapRenderOrder(project_.document(),ref,pandoeditor::RenderPrimitiveRole::Fill).object},
            {"visible",objectVisible(ref)},
            {"opacity",(text(unit.id)==selected_&&opacityPreview_?*opacityPreview_:style->second.opacity)*resolved.opacity}};
    }
    for(const auto& [ref,index]:project_.index().objects) if(ref.domain!="territorial") {
        const auto properties=project_.propertyView(ref); if(!properties) continue;
        const auto group=pandoeditor::contentGroup(project_.document(),ref);
        double opacity=1; const auto style=project_.document().presentation.webPresentation.styles.find(group);
        if(style!=project_.document().presentation.webPresentation.styles.end()) opacity=style->second.opacity.value_or(1);
        const auto layerId=pandoeditor::nativeLayerId(project_.document(),ref); double layerOpacity=1; int layerOrder=-1;
        for(std::size_t i=0;i<project_.layers().size();++i) if(project_.layers()[i].id==layerId) {layerOrder=int(i);layerOpacity=project_.layers()[i].opacity;}
        if(ref.domain=="distributionEntry") {
            if(!visibleDistribution.count(ref))continue;
            const auto& entry=project_.document().distributionEntries.at(index);
            opacity=pandoeditor::distributionValueAlpha(entry.value,distributionRanges.at(entry.layerId),opacity);
        }
        result[QStringLiteral("content/")+text(ref.domain)+"/"+text(ref.id)]=QVariantMap{
            {"color",rgb(properties->effectiveColor)},{"name",text(properties->displayName)},
            {"nameVisible",ref.domain=="label"},{"visible",objectVisible(ref)},{"opacity",opacity},
            {"kind",text(ref.domain)},
            {"drawFillPass",pandoeditor::mapRenderOrder(project_.document(),ref,pandoeditor::RenderPrimitiveRole::Fill).pass},
            {"drawLinePass",pandoeditor::mapRenderOrder(project_.document(),ref,pandoeditor::RenderPrimitiveRole::Line).pass},
            {"drawBoundaryPass",pandoeditor::mapRenderOrder(project_.document(),ref,pandoeditor::RenderPrimitiveRole::Boundary).pass},
            {"drawGroup",pandoeditor::mapRenderOrder(project_.document(),ref,pandoeditor::RenderPrimitiveRole::Fill).group},
            {"drawObject",pandoeditor::mapRenderOrder(project_.document(),ref,pandoeditor::RenderPrimitiveRole::Fill).object},
            {"layerId",text(layerId)},{"layerOrder",layerOrder},{"layerOpacity",layerOpacity},{"boundary",ref.domain=="distributionEntry"&&project_.document().presentation.webPresentation.distributionSettings.boundaryVisible},{"blendMode","normal"}};
    }
    return result;
}
QVariantMap EditorController::layerVisuals() const
{
    QVariantMap result;
    for(const auto& l:project_.layers()) result[text(l.id)]=QVariantMap{
        {"visible",l.visible},{"locked",l.locked},{"opacity",text(l.id)==selectedLayer_&&layerOpacityPreview_?*layerOpacityPreview_:l.opacity}};
    return result;
}
QVariantList EditorController::layers() const
{
    if(layersCache_)return *layersCache_;
    QVariantList result;
    const auto& layers=project_.layers();
    QVariantList unassigned;
    for(const auto& path:projection_.paths)if(pandoeditor::nativeLayerId(project_.document(),pandoeditor::territorialRef(path.toMap()["countryId"].toString().toStdString())).empty())unassigned.append(path);
    if(!unassigned.empty())result.append(QVariantMap{{"id",""},{"name",QStringLiteral("웹 기본 표시")},{"visible",true},{"locked",false},{"opacity",1.},{"order",-1},{"paths",unassigned},{"count",unassigned.size()}});
    for(int i=static_cast<int>(layers.size())-1;i>=0;--i) {
        const auto& l=layers[static_cast<std::size_t>(i)];
        QVariantList paths;
        for(const auto& path:projection_.paths) {
            const auto id=path.toMap()["countryId"].toString();
            const auto member=project_.document().presentation.membership.find(pandoeditor::territorialRef(id.toStdString()));
            if(member!=project_.document().presentation.membership.end()&&member->second==l.id) paths.push_back(path);
        }
        result.append(QVariantMap{{"id",text(l.id)},{"name",text(l.name)},{"visible",l.visible},
            {"locked",l.locked},{"opacity",l.opacity},{"order",i},{"paths",paths},{"count",paths.size()}});
    }
    layersCache_=result;
    return result;
}
QVariantList EditorController::countryRows() const
{
    if(countryRowsSnapshot_&&countryRowsSnapshot_->matches(project_))return countryRowsCache_;
    QVariantList result;
    for(const auto& c:project_.countries()) {
        const auto l=project_.layer(c.layerId);
        result.append(QVariantMap{{"id",text(c.id)},{"name",text(project_.propertyView(pandoeditor::territorialRef(c.id))->displayName)},{"layerId",text(c.layerId)},
            {"visible",objectVisible(pandoeditor::territorialRef(c.id))},{"locked",(l&&l->locked)||c.locked},
            {"limited",!pandoeditor::effectAllowed(project_.document(),pandoeditor::territorialRef(c.id),"color")}});
    }
    countryRowsSnapshot_=project_.snapshot();countryRowsCache_=result;
    return result;
}
QString EditorController::selectedName() const
{
    const auto ref=selection_.primary();if(!ref)return {};
    if(ref->domain=="hydroBuiltin")if(const auto record=hydroRuntime_.recordById(text(ref->id)))return record->name;
    const auto view=project_.propertyView(*ref);return view?text(view->displayName):QString{};
}
QString EditorController::countryLayerId() const
{
    const auto it=project_.document().presentation.membership.find(pandoeditor::territorialRef(selected_.toStdString()));
    return it==project_.document().presentation.membership.end()?QString():text(it->second);
}
double EditorController::countryOpacity() const
{
    if(opacityPreview_) return *opacityPreview_;
    const auto c=project_.country(selected_.toStdString());return c?c->opacity:1;
}
double EditorController::layerOpacity() const
{
    if(layerOpacityPreview_) return *layerOpacityPreview_;
    const auto l=project_.layer(selectedLayer_.toStdString());return l?l->opacity:1;
}
bool EditorController::canDeleteLayer() const
{
    if(project_.layers().size()<=1) return false;
    const auto dependents=project_.index().dependents.find({"userLayer",selectedLayer_.toStdString()});
    if(dependents!=project_.index().dependents.end()&&!dependents->second.empty()) return false;
    if(!pandoeditor::effectAllowed(project_.document(),{"userLayer",selectedLayer_.toStdString()},"delete")) return false;
    return project_.layer(selectedLayer_.toStdString())!=nullptr;
}
QString EditorController::fileName() const {return filePath_.isEmpty()?QStringLiteral("새 프로젝트"):QFileInfo(filePath_).fileName();}
QString EditorController::documentNotice() const
{
    QString notice=QStringLiteral("저장 형식: Qt v9 · 이전 앱에서는 열 수 없습니다. 열기만으로 원본 파일은 변경되지 않습니다.");
    const auto& d=project_.document();
    if(d.units.size()>project_.countries().size())
        notice+=QStringLiteral(" 하위단위·지방 %1개: 정보·편집·관계 메뉴에서 편집할 수 있습니다.").arg(d.units.size()-project_.countries().size());
    if(!d.extensions.empty())
        notice+=QStringLiteral(" 미해석 데이터 %1개 보존 중: 관련 편집이 제한될 수 있습니다.").arg(d.extensions.size());
    return notice;
}
bool EditorController::dirty() const {return importedDirty_||project_.dirty()||hasPendingEdits();}
bool EditorController::hasPendingEdits() const
{
    // GeometryEditSession is intentionally not document data.  Treat it as a
    // pending draft so document-level undo/navigation cannot silently replace
    // its baseline while a user is dragging vertices.
    if(geometryEdit_||(contentSession_&&(contentSession_->edit.create||contentSession_->preview||!contentSession_->pendingFields.empty()))) return true;
    if(!parkedCountryDrafts_.empty()||!parkedLayerDrafts_.empty()) return true;
    const auto u=selectedUnit();
    if(u && (nameDraft_!=QString::fromStdString(u->kind==pandoeditor::UnitKind::General?pandoeditor::objectDisplayName(*u):u->name)
       || memoDraft_!=text(u->notes) || colorDraft_!=rgb(pandoeditor::effectiveObjectColor(project_.document(),pandoeditor::territorialRef(u->id)))
       || validFromDraft_!=text(pandoeditor::staticLifetime(project_.document(),u->id).validity.from.value_or("")) || validToDraft_!=text(pandoeditor::staticLifetime(project_.document(),u->id).validity.to.value_or(""))
       || (opacityPreview_&&*opacityPreview_!=project_.document().presentation.objectStyles.at(pandoeditor::territorialRef(u->id)).opacity)))return true;
    const auto l=project_.layer(selectedLayer_.toStdString());
    return l&&(layerNameDraft_!=text(l->name)||(layerOpacityPreview_&&*layerOpacityPreview_!=l->opacity));
}
void EditorController::setNameDraft(const QString& value) {if(!selectionTransition_&&selectedEditable()){cancelPreview();nameDraft_=value;emit draftsChanged();emit dirtyChanged();}}
void EditorController::setMemoDraft(const QString& value) {if(!selectionTransition_&&selectedEditable()){cancelPreview();memoDraft_=value;emit draftsChanged();emit dirtyChanged();}}
void EditorController::setColorDraft(const QString& value) {if(!selectionTransition_&&selectedEditable()){cancelPreview();colorDraft_=value;emit draftsChanged();emit dirtyChanged();}}
void EditorController::setLayerNameDraft(const QString& value) {cancelPreview();layerNameDraft_=value;emit draftsChanged();emit dirtyChanged();}
void EditorController::reloadDrafts()
{
    const auto u=selectedUnit();
    nameDraft_=u?text(u->kind==pandoeditor::UnitKind::General?pandoeditor::objectDisplayName(*u):u->name):QString();
    memoDraft_=u?text(u->notes):QString();
    colorDraft_=u?rgb(pandoeditor::effectiveObjectColor(project_.document(),pandoeditor::territorialRef(u->id))):QString();
    validFromDraft_=u?text(pandoeditor::staticLifetime(project_.document(),u->id).validity.from.value_or("")):QString();
    validToDraft_=u?text(pandoeditor::staticLifetime(project_.document(),u->id).validity.to.value_or("")):QString();
    const auto l=project_.layer(selectedLayer_.toStdString());layerNameDraft_=l?text(l->name):QString();
    opacityPreview_.reset();layerOpacityPreview_.reset();
    restoreParkedDrafts();
}
void EditorController::publish(bool pruneSelection)
{
    Q_UNUSED(pruneSelection);
    if(labelAnchors_)labelAnchors_->setProjectScope(text(project_.instanceId()));
    QScopedValueRollback<bool> guard(selectionTransition_,true);
    // Hidden/locked is not missing. A selected object remains addressable from a list.
    reconcileSelection();
    refreshContentSession();
    if(!project_.layer(selectedLayer_.toStdString())) selectedLayer_=project_.layers().empty()?QString():text(project_.layers().back().id);
    reloadDrafts();
    emit stateChanged();emit selectionChanged();emit searchChanged();emit hoverChanged();
    emit visualChanged();emit draftsChanged();emit dirtyChanged();
    emit structureChanged();
    emit presentationChanged();
    if(presentationSaveInstance_==project_.instanceId()) {
        if(project_.dirty()||importedDirty_)presentationSaveTimer_.start();
        else discardOwnPresentationRecovery();
    }
    if(autosave_&&!startupBusy_&&worldStatus_!="recovery-failed"&&
       (autosaveInstance_!=project_.instanceId()||autosaveRevision_!=project_.revision())) {
        try {
            autosave_->scheduleDocument(project_.snapshot());
            autosaveInstance_=project_.instanceId();autosaveRevision_=project_.revision();
        } catch(const std::exception& error) {
            emit errorOccurred(QStringLiteral("자동저장을 준비하지 못했습니다: %1")
                               .arg(QString::fromUtf8(error.what())));
        }
    }
}
void EditorController::previewCountryOpacity(double value)
{
    if(!selectedEditable()||!std::isfinite(value)||value<0||value>1) return;
    cancelPreview();opacityPreview_=value;emit visualChanged();emit draftsChanged();emit dirtyChanged();
}
void EditorController::previewLayerOpacity(double value)
{
    if(!std::isfinite(value)||value<0||value>1) return;
    cancelPreview();layerOpacityPreview_=value;emit visualChanged();emit draftsChanged();emit dirtyChanged();
}
void EditorController::addLayer()
{
    const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    if(executeCommand("layer.add",pandoeditor::AddLayer{id.toStdString(),"새 레이어"})) {selectedLayer_=id;publish(false);}
}
void EditorController::removeLayer() {if(executeCommand("layer.remove",pandoeditor::RemoveLayer{selectedLayer_.toStdString()}))publish();}
void EditorController::moveLayer(int delta) {if(executeCommand("layer.move",pandoeditor::MoveLayer{selectedLayer_.toStdString(),delta}))publish(false);}
void EditorController::setLayerVisible(bool value) {if(executeCommand("layer.visibility",pandoeditor::SetLayerVisible{selectedLayer_.toStdString(),value}))publish();}
void EditorController::setLayerLocked(bool value) {if(executeCommand("layer.lock",pandoeditor::SetLayerLocked{selectedLayer_.toStdString(),value}))publish();}
void EditorController::moveCountry(const QString& layerId) {if(executeCommand("country.move",pandoeditor::MoveCountry{selected_.toStdString(),layerId.toStdString()}))publish();}
void EditorController::undo()
{
    if(hasPendingEdits()){emit errorOccurred(QStringLiteral("PENDING_EDITS: 편집 중인 내용을 먼저 적용하거나 취소하세요."));return;}
    const auto before=project_.document();
    cancelPreview();
    if(project_.undo()) {
        bool changed=true,hydroChanged=true;
        try {
            const auto impact=pandoeditor::calculateChangeImpact(before,project_.document());
            changed=impact.sceneDirty.geometry;hydroChanged=impact.sceneDirty.datasetResource;
            noteAppliedImpact(impact);
        } catch(...) {pendingSceneImpact_.reset();}
        if(changed)projection_.rebuild(project_.document());
        publish();if(hydroChanged)syncHydroData();if(changed)emit geometryChanged();
    }
}
void EditorController::redo()
{
    if(hasPendingEdits()){emit errorOccurred(QStringLiteral("PENDING_EDITS: 편집 중인 내용을 먼저 적용하거나 취소하세요."));return;}
    const auto before=project_.document();
    cancelPreview();
    if(project_.redo()) {
        bool changed=true,hydroChanged=true;
        try {
            const auto impact=pandoeditor::calculateChangeImpact(before,project_.document());
            changed=impact.sceneDirty.geometry;hydroChanged=impact.sceneDirty.datasetResource;
            noteAppliedImpact(impact);
        } catch(...) {pendingSceneImpact_.reset();}
        if(changed)projection_.rebuild(project_.document());
        publish();if(hydroChanged)syncHydroData();if(changed)emit geometryChanged();
    }
}
bool EditorController::openFile(const QUrl& url)
{
    if(geometryEdit_){emit errorOccurred(QStringLiteral("GEOMETRY_EDIT_ACTIVE: 도형 편집을 확인하거나 취소한 뒤 프로젝트를 여세요."));return false;}
    try {
        if(!url.isLocalFile())throw std::runtime_error("Please choose a local file");
        const auto bytes=storage_.read(url);
        const auto opened=replaceFromBytes(bytes,false,url.toLocalFile());
        if(opened&&projectPreview_)projectPreview_->schedule(bytes,projectPreviewSourceSha_);
        return opened;
    }catch(const std::exception& e){emit errorOccurred(QString::fromUtf8(e.what()));return false;}
}
bool EditorController::newProject()
{
    if(startupBusy_||jobBusy()||geometryEdit_||hasWebImportPreview())return false;
    try {
        if(!bootstrapWorldEnabled_){
            QFile sample(":/assets/sample.pando.json");
            if(!sample.open(QIODevice::ReadOnly))return false;
            if(!replaceFromBytes(sample.readAll(),false,{}))return false;
        }else{
            pandoeditor::Project initial;
            initial.replace(pandoeditor::ProjectDocument(std::vector<pandoeditor::Country>{},
                std::vector<pandoeditor::Layer>{{"countries","국가"}}));
            if(!replaceFromBytes(projectcodec::encode(initial),false,{}))return false;
            startWorldBootstrap();
        }
        fitMapCamera();return true;
    }catch(const std::exception& error){emit errorOccurred(QString::fromUtf8(error.what()));return false;}
}
bool EditorController::saveFile(const QUrl& url)
{
    if(startupBusy_||worldStatus_=="recovery-failed")return false;
    if(worldStatus_=="loading-preview"||worldStatus_=="preview"||worldStatus_=="unavailable") {
        emit errorOccurred(QStringLiteral("세계지도 문서가 준비되기 전에는 저장할 수 없습니다."));return false;
    }
    if(geometryEdit_){emit errorOccurred(QStringLiteral("GEOMETRY_EDIT_ACTIVE: 미확정 도형은 저장되지 않습니다. 먼저 확인하거나 취소하세요."));return false;}
    if(isProtectedWebSource(url)){webImportFailure(QStringLiteral("SOURCE_OVERWRITE_BLOCKED: 웹 원본은 덮어쓰지 않습니다. 다른 이름으로 저장하세요."));return false;}
    if(!commitPendingEdits())return false;
    if(!url.isLocalFile()){emit errorOccurred(QStringLiteral("로컬 파일을 선택해 주세요."));return false;}
    try {
        const auto persisted=projectcodec::encode(project_);storage_.write(url,persisted);
        if(projectPreview_)projectPreview_->schedule(persisted,projectPreviewSourceSha_);
        filePath_=url.toLocalFile();importedDirty_=false;project_.markSaved();publish(false);return true;
    }catch(const std::exception& e){emit errorOccurred(QString::fromUtf8(e.what()));return false;}
}
bool EditorController::save(){if(mobileMode_)return savePrivate();return saveFile(QUrl::fromLocalFile(filePath_));}
bool EditorController::replaceFromBytes(const QByteArray& bytes,bool imported,const QString& path)
{
    if(startupBusy_)return false;
    if(geometryEdit_){emit errorOccurred(QStringLiteral("GEOMETRY_EDIT_ACTIVE: 도형 편집을 확인하거나 취소한 뒤 프로젝트를 바꾸세요."));return false;}
    auto document=projectcodec::decode(bytes);pandoeditor::requireStaticTimeline(document);
    pandoeditor::Project candidate;candidate.replace(std::move(document));
    MapProjection nextProjection;nextProjection.rebuild(candidate.document());
    cancelWorldBootstrap();
    cancelPreview();cancelStructureMutation();project_=std::move(candidate);projection_=std::move(nextProjection);
    filePath_=path;importedDirty_=imported;selected_.clear();selectedLayer_=project_.layers().empty()?QString():text(project_.layers().back().id);
    refreshHistoricalCatalog();
    emit geometryChanged();publish(false);syncHydroData();
    // A saved built-in world keeps canonical document geometry. Attach its
    // optional render meshes on a worker after validating actual coordinates.
    if(bootstrapWorldEnabled_&&std::any_of(project_.document().units.begin(),project_.document().units.end(),
            [this](const auto& unit){return pandoeditor::staticGeometryBinding(project_.document(),unit.id).geometryRef.id.rfind("world-country-",0)==0;})) {
        using DetailFrames=std::pair<std::shared_ptr<const WorldBaseFrame>,std::shared_ptr<const WorldBaseFrame>>;
        const auto initial=project_.snapshot();const auto generation=worldGeneration_;
        auto* watcher=new QFutureWatcher<DetailFrames>(this);
        connect(watcher,&QFutureWatcher<DetailFrames>::finished,this,[this,watcher,initial,generation] {
            watcher->deleteLater();
            if(generation!=worldGeneration_||!initial.matches(project_))return;
            try {
                auto frames=watcher->result();if(!frames.first)return;
                worldBase_=std::move(frames.first);cacheWorldFrame(worldBase_);cacheWorldFrame(std::move(frames.second));
                worldRanges_=worldBase_->ranges;
                updateWorldDetail();worldStatus_=QStringLiteral("canonical");
                refreshTypedScene();emit worldStatusChanged();
            }catch(const std::exception&) {
                // Missing or mismatched optional assets retain exact document packets.
            }
        });
        watcher->setFuture(QtConcurrent::run([initial,root=worldDataRoot_] {
            DetailFrames frames;frames.first=WorldDatasetLoader::matchingBaseFrame(initial.document(),root);
            if(frames.first)try {
                auto preview=std::make_shared<WorldBaseFrame>(*WorldDatasetLoader::preview(root).frame);
                if(preview->ranges==frames.first->ranges) {
                    preview->documentReady=true;frames.second=std::move(preview);
                }
            }catch(const std::exception&) {}
            return frames;
        }));
    }
    return true;
}
bool EditorController::restorePrivateProject()
{
    if(!mobileMode_||!storage_.privateProjectExists())return true;
    try {
        const auto restored=replaceFromBytes(storage_.readPrivate(),false);
        if(privateRecoveryRequired_){privateRecoveryRequired_=false;emit privateRecoveryRequiredChanged();}
        return restored;
    }catch(const std::exception& e){
        const bool newlyBlocked=!privateRecoveryRequired_;privateRecoveryRequired_=true;
        emit errorOccurred(QStringLiteral("저장된 프로젝트를 복원하지 못했습니다. 원본 파일은 보존되며, 명시적으로 복구를 허용하기 전에는 덮어쓰지 않습니다.\n%1").arg(QString::fromUtf8(e.what())));
        if(newlyBlocked)emit privateRecoveryRequiredChanged();return false;
    }
}
bool EditorController::importProject(const QUrl& url)
{
    if(url.isEmpty())return true;
    try {return replaceFromBytes(storage_.read(url),true);}
    catch(const std::exception& e){emit errorOccurred(QString::fromUtf8(e.what()));return false;}
}
bool EditorController::savePrivate()
{
    if(startupBusy_||worldStatus_=="recovery-failed")return false;
    if(worldStatus_=="loading-preview"||worldStatus_=="preview"||worldStatus_=="unavailable") {
        emit errorOccurred(QStringLiteral("세계지도 문서가 준비되기 전에는 저장할 수 없습니다."));return false;
    }
    if(geometryEdit_){emit errorOccurred(QStringLiteral("GEOMETRY_EDIT_ACTIVE: 미확정 도형은 저장되지 않습니다. 먼저 확인하거나 취소하세요."));return false;}
    if(isProtectedWebSource(QUrl::fromLocalFile(storage_.privateProjectPath()))){webImportFailure(QStringLiteral("SOURCE_OVERWRITE_BLOCKED: 웹 원본은 덮어쓰지 않습니다."));return false;}
    if(privateRecoveryRequired_){emit errorOccurred(QStringLiteral("손상된 저장 파일이 보존되어 있습니다. 덮어쓰기를 허용한 뒤 다시 저장해 주세요."));return false;}
    if(!commitPendingEdits())return false;
    try {const auto persisted=projectcodec::encode(project_);storage_.writePrivateAtomic(persisted);if(projectPreview_)projectPreview_->schedule(persisted,projectPreviewSourceSha_);importedDirty_=false;project_.markSaved();publish(false);return true;}
    catch(const std::exception& e){emit errorOccurred(QString::fromUtf8(e.what()));return false;}
}
bool EditorController::exportProject(const QUrl& url)
{
    if(isProtectedWebSource(url)){webImportFailure(QStringLiteral("SOURCE_OVERWRITE_BLOCKED: 웹 원본은 덮어쓰지 않습니다."));return false;}
    if(url.isEmpty())return true;
    try {const auto snapshot=storage_.readPrivate();storage_.write(url,snapshot);return true;}
    catch(const std::exception& e){emit errorOccurred(QString::fromUtf8(e.what()));return false;}
}
bool EditorController::confirmPrivateRecovery()
{
    if(!privateRecoveryRequired_)return true;
    try {storage_.preserveCorruptPrivate();privateRecoveryRequired_=false;emit privateRecoveryRequiredChanged();return true;}
    catch(const std::exception& e){emit errorOccurred(QString::fromUtf8(e.what()));return false;}
}

QObject* EditorController::terrainResourceBridge() const {return terrainResourceBridge_.get();}

void EditorController::recordGpuResourceStats(QObject* source) {
    auto* item=qobject_cast<GpuMapItem*>(source);if(!item)return;
    if(gpuResourceSource_&&gpuResourceSource_!=source)return;
    if(!gpuResourceSource_) {
        gpuResourceSource_=source;resourceCoordinator_.setQsgSource(reinterpret_cast<quintptr>(source));
        connect(source,&QObject::destroyed,this,[this]{if(!gpuResourceSource_)resourceCoordinator_.setQsgSource(0);});
    }
    resourceCoordinator_.acceptQsgSnapshot(reinterpret_cast<quintptr>(source),item->resourceGeneration(),item->resourceCacheStats());
    // No renderQualityChanged here: per-frame diagnostics must not rebuild CPU cache snapshots.
}

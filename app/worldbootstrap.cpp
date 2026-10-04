#include "editorcontroller.h"
#include "worlddatasetloader.h"
#include "worlddataset.h"
#include <QFutureWatcher>
#include <QtConcurrent>
#include <stdexcept>

namespace {
struct RecoveredStartup {
    pandoeditor::Project project;
    MapProjection projection;
    GeometryPacketCache packets;
    std::shared_ptr<const WorldBaseFrame> base;
    std::shared_ptr<const WorldBaseFrame> preview;
    std::optional<MapViewState> view;
    QString error;
};
}

void EditorController::cacheWorldFrame(std::shared_ptr<const WorldBaseFrame> frame) {
    if(!frame||!frame->mesh)return;
    const auto detail=frame->mesh->preview?WorldDetail::Preview:WorldDetail::Canonical;
    worldResources_.protect(detail,pandoeditor::ResourceProtection::Visible,worldBase_==frame);
    worldResources_.protect(detail,pandoeditor::ResourceProtection::Fallback,
                            worldDetailCanonical_==(detail==WorldDetail::Canonical));
    const auto token=worldResources_.beginRequest(detail);
    worldResources_.admit(detail,std::move(frame),token);
}
void EditorController::setWorldResourceBudget(std::size_t bytes) {
    updateWorldDetail();worldResources_.setBudget(bytes);emit renderQualityChanged();
}
void EditorController::updateWorldDetail() {
    const auto display=camera_.display();
    const bool globe=camera_.mode()==ProjectionMode::Globe;
    auto& canonical=worldDetailCanonical_;
    const bool before=canonical;
    const bool editing=mapEditorActive_||geometryEdit_||contentSession_||colorSession_||
        !fieldSessions_.empty()||structureDialogOpen();
    if(worldFocusDetail_||editing)canonical=true;
    else if(display.zoom<=(globe?1.8:2.2))canonical=false;
    else if(display.zoom>=(globe?2.2:2.8))canonical=true;
    else if(worldBase_&&worldBase_->mesh)canonical=!worldBase_->mesh->preview;
    const auto wanted=canonical?WorldDetail::Canonical:WorldDetail::Preview;
    if(before!=canonical)worldReloadFailed_[static_cast<int>(wanted)]=false;
    worldResources_.protect(WorldDetail::Canonical,pandoeditor::ResourceProtection::Editing,editing);
    worldResources_.protect(WorldDetail::Canonical,pandoeditor::ResourceProtection::Selected,worldFocusDetail_);
    const auto requested=worldResources_.get(wanted);
    if(requested) {
        worldResources_.protect(wanted,pandoeditor::ResourceProtection::Visible,true);
        worldBase_=requested;
        const auto other=canonical?WorldDetail::Preview:WorldDetail::Canonical;
        worldResources_.protect(other,pandoeditor::ResourceProtection::Visible,false);
        worldResources_.protect(other,pandoeditor::ResourceProtection::Fallback,false);
        worldResources_.protect(wanted,pandoeditor::ResourceProtection::Fallback,false);
    } else if(worldStatus_=="canonical"&&worldBase_&&worldBase_->documentReady)reloadWorldDetail(wanted);
    if(before!=canonical)emit renderQualityChanged();
}
void EditorController::reloadWorldDetail(WorldDetail detail) {
    const auto slot=static_cast<int>(detail);
    if(worldReloading_[slot]||worldReloadFailed_[slot]||worldRanges_.empty())return;
    const auto token=worldResources_.beginRequest(detail);
    const auto generation=worldGeneration_;const auto instance=project_.instanceId();const auto ranges=worldRanges_;
    worldReloading_[slot]=true;
    auto* watcher=new QFutureWatcher<std::shared_ptr<const CountryBaseMesh>>(this);
    connect(watcher,&QFutureWatcher<std::shared_ptr<const CountryBaseMesh>>::finished,this,
        [this,watcher,detail,slot,token,generation,instance,ranges] {
        watcher->deleteLater();
        if(generation!=worldGeneration_||instance!=project_.instanceId()||ranges!=worldRanges_) {
            worldResources_.fail(token);return;
        }
        worldReloading_[slot]=false;
        try {
            auto frame=std::make_shared<WorldBaseFrame>();frame->mesh=watcher->result();frame->ranges=ranges;
            if(!frame->mesh||frame->mesh->preview!=(detail==WorldDetail::Preview))throw std::runtime_error("Wrong world reload mesh");
            const bool stillWanted=worldDetailCanonical_==(detail==WorldDetail::Canonical);
            worldResources_.protect(detail,pandoeditor::ResourceProtection::Fallback,stillWanted);
            if(!worldResources_.admit(detail,frame,token))return;
            updateWorldDetail();refreshTypedScene();emit renderQualityChanged();
        } catch(const std::exception& error) {
            worldResources_.fail(token);worldReloadFailed_[slot]=true;
            emit errorOccurred(QStringLiteral("World render resource reload unavailable: ")+QString::fromUtf8(error.what()));
        }
    });
    watcher->setFuture(QtConcurrent::run([detail,root=worldDataRoot_] {
        return detail==WorldDetail::Canonical?WorldDatasetLoader::canonicalMesh(root):WorldDatasetLoader::preview(root).frame->mesh;
    }));
}

void EditorController::startAutosaveRecovery(bool useWorldBase) {
    const auto initial=project_.snapshot();
    const auto profile=quality_.profile();
    const auto projectPath=autosave_->projectPath(),viewPath=autosave_->viewPath();
    auto* watcher=new QFutureWatcher<std::shared_ptr<RecoveredStartup>>(this);
    connect(watcher,&QFutureWatcher<std::shared_ptr<RecoveredStartup>>::finished,this,
        [this,watcher,initial] {
        const auto result=watcher->result();watcher->deleteLater();
        if(!initial.matches(project_)) {
            startupBusy_=false;emit startupBusyChanged();return;
        }
        if(!result->error.isEmpty()) {
            worldStatus_=QStringLiteral("recovery-failed");
            startupBusy_=false;emit worldStatusChanged();emit startupBusyChanged();
            emit errorOccurred(QStringLiteral("자동저장 프로젝트를 복원하지 못했습니다: %1")
                .arg(result->error));
            return;
        }
        const auto viewport=sceneBridge_.viewState();
        project_=std::move(result->project);
        projection_=std::move(result->projection);
        packetCache_=std::move(result->packets);sceneInstance_=project_.instanceId();
        worldBase_=std::move(result->base);cacheWorldFrame(worldBase_);
        cacheWorldFrame(std::move(result->preview));
        if(worldBase_)worldRanges_=worldBase_->ranges;
        worldStatus_=worldBase_?QStringLiteral("canonical"):QStringLiteral("disabled");
        camera_.setMetrics(mapCameraMetrics());
        if(result->view)camera_.adoptView(*result->view);
        if(viewport.viewportWidth>1&&viewport.viewportHeight>1)
            camera_.resize(viewport.viewportWidth,viewport.viewportHeight,viewport.devicePixelRatio);
        sceneBridge_.publishView(camera_.view());
        // Loading and viewport setup must never autosave the temporary empty document/view.
        autosaveInstance_=project_.instanceId();autosaveRevision_=project_.revision();
        startupBusy_=false;
        publish(false);emit geometryChanged();emit worldStatusChanged();emit startupBusyChanged();
        ensureHydroBootstrap();syncHydroData();
    });
    watcher->setFuture(QtConcurrent::run([projectPath,viewPath,profile,useWorldBase,root=worldDataRoot_] {
        auto result=std::make_shared<RecoveredStartup>();
        try {
            ProjectAutosave source(projectPath,viewPath);
            const auto bytes=source.restoreDocument();
            if(bytes.isEmpty())throw std::runtime_error("자동저장 파일이 비어 있거나 손상되었습니다. 원본은 보존됩니다.");
            result->project.replace(projectcodec::decode(bytes));
            result->view=source.restoreView();
            result->projection.rebuild(result->project.document());
            if(useWorldBase)try {
                result->base=WorldDatasetLoader::matchingBaseFrame(result->project.document(),root);
                if(result->base)try {
                    auto preview=std::make_shared<WorldBaseFrame>(*WorldDatasetLoader::preview(root).frame);
                    if(preview->ranges==result->base->ranges) {
                        preview->documentReady=true;result->preview=std::move(preview);
                    }
                }catch(const std::exception&) {
                    // Preview is optional; retain the matching precise base.
                }
            }catch(const std::exception&) {
                // A missing optional base mesh must not discard a valid saved document.
            }
            result->packets.setBudget(profile.renderPacketCacheBudgetBytes);
            MapSceneBuilder builder(result->packets);
            builder.setWorldBase(result->base);builder.setQuality(profile);
            const auto view=result->view.value_or(MapViewState{});
            // Warm the fallback packets on this worker too, including edited/imported shapes.
            builder.build(result->project.snapshot(),view,{},{});
        } catch(const std::exception& error) {result->error=QString::fromUtf8(error.what());}
          catch(...) {result->error=QStringLiteral("지도 복구 중 알 수 없는 오류가 발생했습니다.");}
        return result;
    }));
}

void EditorController::startWorldBootstrap() {
    if(worldStatus_!="disabled")return;
    const auto generation=++worldGeneration_;
    worldStatus_=QStringLiteral("loading-preview");emit worldStatusChanged();
    auto* watcher=new QFutureWatcher<WorldPreviewResult>(this);
    connect(watcher,&QFutureWatcher<WorldPreviewResult>::finished,this,
        [this,watcher,generation] {
        watcher->deleteLater();
        if(generation!=worldGeneration_)return;
        try {
            auto prepared=watcher->result();
            if(project_.revision()!=0) {cancelWorldBootstrap();return;}
            worldBase_=std::move(prepared.frame);cacheWorldFrame(worldBase_);
            worldRanges_=worldBase_->ranges;
            projection_=*prepared.projection;
            try {
                WorldDataset source;
                const auto terrainRoot=!physicalRoot_.isEmpty()?physicalRoot_:source.optionalDataRoot();
                terrainProvider_=std::make_shared<TerrainTileProvider>(
                    source.read("terrain"),terrainRoot,[this](const QString& relative) {
                        return physicalAssetPath(relative);
                    });
                terrainProvider_->setCacheBudget(quality_.profile().terrainCacheBudgetBytes);
            } catch(const std::exception&) {
                terrainProvider_.reset(); // The optional terrain channel is unavailable.
            }
            emit terrainChanged();
            invalidateViewportResources(ViewportResourceKind::Terrain);
            worldStatus_=QStringLiteral("preview");emit worldStatusChanged();
            emit geometryChanged();
            startCanonicalWorld(generation);
        } catch(const std::exception& error) {
            worldStatus_=QStringLiteral("unavailable");emit worldStatusChanged();
            emit errorOccurred(QStringLiteral("Pinned world preview unavailable: ")+
                QString::fromUtf8(error.what()));
        }
    });
    watcher->setFuture(QtConcurrent::run([root=worldDataRoot_] {return WorldDatasetLoader::preview(root);}));
}

void EditorController::startCanonicalWorld(std::uint64_t generation) {
    auto* watcher=new QFutureWatcher<WorldCanonicalResult>(this);
    connect(watcher,&QFutureWatcher<WorldCanonicalResult>::finished,this,
        [this,watcher,generation] {
        watcher->deleteLater();
        if(generation!=worldGeneration_)return;
        try {
            const auto prepared=watcher->result();
            if(!worldBase_||prepared.ranges!=worldRanges_||project_.revision()!=0)
                throw std::runtime_error("World bootstrap country order or project changed");
            pandoeditor::Project candidate;
            candidate.replace(*prepared.document);
            cancelPreview();cancelStructureMutation();
            project_=std::move(candidate);
            projection_=*prepared.projection;
            auto preview=std::make_shared<WorldBaseFrame>(*worldResources_.get(WorldDetail::Preview));
            preview->documentReady=true;worldBase_=preview;cacheWorldFrame(std::move(preview));
            worldHydroNotice_=prepared.hydroAvailability;
            filePath_.clear();importedDirty_=false;
            refreshHistoricalCatalog();
            worldStatus_=QStringLiteral("canonical-pending-mesh");emit worldStatusChanged();
            // While preview is displayed, no canonical country override is
            // shown on top of it. Edits made now stay in ProjectDocument.
            emit geometryChanged();publish(false);ensureHydroBootstrap();syncHydroData();
            startCanonicalWorldMesh(generation);
        } catch(const std::exception& error) {
            worldStatus_=QStringLiteral("unavailable");emit worldStatusChanged();
            emit errorOccurred(QStringLiteral("Canonical world document unavailable: ")+
                QString::fromUtf8(error.what()));
        }
    });
    watcher->setFuture(QtConcurrent::run([root=worldDataRoot_] {return WorldDatasetLoader::canonical(root);}));
}

void EditorController::startCanonicalWorldMesh(std::uint64_t generation) {
    auto* watcher=new QFutureWatcher<std::shared_ptr<const CountryBaseMesh>>(this);
    connect(watcher,&QFutureWatcher<std::shared_ptr<const CountryBaseMesh>>::finished,this,
        [this,watcher,generation] {
        watcher->deleteLater();
        if(generation!=worldGeneration_)return;
        try {
            auto frame=std::make_shared<WorldBaseFrame>();
            frame->mesh=watcher->result();frame->ranges=worldRanges_;
            if(!frame->mesh||frame->mesh->preview||frame->ranges.size()!=258)
                throw std::runtime_error("Wrong canonical world mesh");
            // Select against the latest view, including edits committed while loading.
            cacheWorldFrame(std::move(frame));
            updateWorldDetail();
            worldStatus_=QStringLiteral("canonical");emit worldStatusChanged();
            refreshTypedScene();emit geometryChanged();
        } catch(const std::exception& error) {
            worldStatus_=QStringLiteral("canonical-mesh-unavailable");emit worldStatusChanged();
            emit errorOccurred(QStringLiteral("Canonical world mesh unavailable: ")+
                QString::fromUtf8(error.what()));
        }
    });
    watcher->setFuture(QtConcurrent::run([root=worldDataRoot_] {return WorldDatasetLoader::canonicalMesh(root);}));
}

void EditorController::cancelWorldBootstrap() {
    ++worldGeneration_;
    worldBase_.reset();worldResources_.reset();worldRanges_.clear();
    worldReloading_[0]=worldReloading_[1]=false;worldReloadFailed_[0]=worldReloadFailed_[1]=false;
    worldFocusDetail_=false;worldDetailCanonical_=false;
    terrainTiles_.clear();terrainProvider_.reset();worldHydroNotice_.clear();
    emit terrainChanged();
    worldStatus_=QStringLiteral("disabled");emit worldStatusChanged();
}

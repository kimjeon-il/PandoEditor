#include "editorcontroller.h"
#include "worlddatasetloader.h"
#include "worlddataset.h"
#include <QFutureWatcher>
#include <QtConcurrent>
#include <stdexcept>

namespace {
const QString bundledRoot=QStringLiteral(":/world");
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
            worldBase_=std::move(prepared.frame);
            worldRanges_=worldBase_->ranges;
            projection_=*prepared.projection;
            try {
                WorldDataset source;
                terrainProvider_=std::make_shared<TerrainTileProvider>(
                    source.read("terrain"),source.optionalDataRoot());
                terrainProvider_->setCacheBudget(quality_.profile().terrainCacheBudgetBytes);
            } catch(const std::exception&) {
                terrainProvider_.reset(); // The optional terrain channel is unavailable.
            }
            emit terrainChanged();
            worldStatus_=QStringLiteral("preview");emit worldStatusChanged();
            emit geometryChanged();
            startCanonicalWorld(generation);
        } catch(const std::exception& error) {
            worldStatus_=QStringLiteral("unavailable");emit worldStatusChanged();
            emit errorOccurred(QStringLiteral("Pinned world preview unavailable: ")+
                QString::fromUtf8(error.what()));
        }
    });
    watcher->setFuture(QtConcurrent::run([] {return WorldDatasetLoader::preview(bundledRoot);}));
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
            worldHydroNotice_=prepared.hydroAvailability;
            filePath_.clear();importedDirty_=false;
            worldStatus_=QStringLiteral("canonical-pending-mesh");emit worldStatusChanged();
            // While preview is displayed, no canonical country override is
            // shown on top of it. Edits made now stay in ProjectDocument.
            emit geometryChanged();publish(false);syncHydroData();
            startCanonicalWorldMesh(generation);
        } catch(const std::exception& error) {
            worldStatus_=QStringLiteral("unavailable");emit worldStatusChanged();
            emit errorOccurred(QStringLiteral("Canonical world document unavailable: ")+
                QString::fromUtf8(error.what()));
        }
    });
    watcher->setFuture(QtConcurrent::run([] {return WorldDatasetLoader::canonical(bundledRoot);}));
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
            // One immutable scene publication replaces preview base and adds
            // every country edit already committed to the canonical document.
            worldBase_=std::move(frame);
            worldStatus_=QStringLiteral("canonical");emit worldStatusChanged();
            refreshTypedScene();emit geometryChanged();
        } catch(const std::exception& error) {
            worldStatus_=QStringLiteral("canonical-mesh-unavailable");emit worldStatusChanged();
            emit errorOccurred(QStringLiteral("Canonical world mesh unavailable: ")+
                QString::fromUtf8(error.what()));
        }
    });
    watcher->setFuture(QtConcurrent::run([] {return WorldDatasetLoader::canonicalMesh(bundledRoot);}));
}

void EditorController::cancelWorldBootstrap() {
    ++worldGeneration_;
    worldBase_.reset();worldRanges_.clear();
    terrainTiles_.clear();terrainProvider_.reset();worldHydroNotice_.clear();
    emit terrainChanged();
    worldStatus_=QStringLiteral("disabled");emit worldStatusChanged();
}

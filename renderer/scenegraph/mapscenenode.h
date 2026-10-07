#pragma once

#include <pandoeditor/map/renderscene.h>
#include <pandoeditor/map/mapviewstate.h>
#include "uploadscheduler.h"
#include <QSGNode>
#include <QVariantList>
#include <cstdint>
#include <memory>
#include <vector>

struct MapGpuStats {
    std::uint64_t sceneRevision=0,geometryUploadCount=0;
    // Counts CPU QSGGeometry allocations, not driver/RHI buffer allocations.
    std::uint64_t baseGeometryUploadCount=0,interactionGeometryUploadCount=0;
    std::uint64_t resourceCreationCount=0,resourceRetirementCount=0;
    std::size_t liveResourceCount=0,liveResourceBytes=0;
    std::uint64_t materialUpdateCount=0,viewUniformUpdateCount=0;
    std::size_t geometryBytes=0;
    std::size_t visibleCountryCount=0,drawIndexCount=0,fullIndexCount=0;
    std::size_t uploadBytesThisFrame=0;
    bool uploadsPending=false;
    std::uint64_t syncCount=0,treeRebuildCount=0,uploadedBytes=0;
    std::uint64_t nodeAttachmentCount=0;
    std::size_t drawNodes=0,strokeBytes=0;
    double syncMilliseconds=0,uploadMilliseconds=0,strokeUploadMilliseconds=0;
};

struct MapFlatViewport {
    float originX=0,originY=0,mapScale=1,cosLatitude=1,minX=0,maxLatitude=0;
};

// A render-thread-only owner of scene graph nodes and immutable packet identities.
class MapSceneNode final : public QSGNode {
public:
    void sync(const std::shared_ptr<const RenderScene>& scene,const MapViewState& view,
              const MapFlatViewport& flat,MapGpuStats& stats,std::size_t uploadBudgetBytes,
              const WorldRenderPlan* framePlan=nullptr);
    // Actual retained QSG stroke buffers, not the candidate scene's packets.
    QVariantList strokeInventory() const;
private:
    std::shared_ptr<const RenderScene> lastScene_;
    std::vector<double> lastOffsets_;
    ProjectionMode lastMode_=ProjectionMode::Flat;
    std::vector<bool> lastFillVisibility_,lastStrokeVisibility_;
    MapUploadScheduler scheduler_;
    bool pending_=false;
};

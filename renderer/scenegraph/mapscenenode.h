#pragma once

#include "renderscene.h"
#include "mapviewstate.h"
#include "countryculling.h"
#include "uploadscheduler.h"
#include <QSGNode>
#include <cstdint>
#include <memory>
#include <vector>

struct MapGpuStats {
    std::uint64_t sceneRevision=0,geometryUploadCount=0;
    std::uint64_t materialUpdateCount=0,viewUniformUpdateCount=0;
    std::size_t geometryBytes=0;
    std::size_t visibleCountryCount=0,drawIndexCount=0,fullIndexCount=0;
    std::size_t uploadBytesThisFrame=0;
    bool uploadsPending=false;
};

struct MapFlatViewport {
    float originX=0,originY=0,mapScale=1,cosLatitude=1,minX=0,maxLatitude=0;
};

// A render-thread-only owner of scene graph nodes and immutable packet identities.
class MapSceneNode final : public QSGNode {
public:
    void sync(const std::shared_ptr<const RenderScene>& scene,const MapViewState& view,
              const MapFlatViewport& flat,MapGpuStats& stats,std::size_t uploadBudgetBytes);
private:
    std::shared_ptr<const RenderScene> lastScene_;
    std::vector<double> lastOffsets_;
    ProjectionMode lastMode_=ProjectionMode::Flat;
    std::vector<bool> lastFillVisibility_,lastStrokeVisibility_;
    MapUploadScheduler scheduler_;
    bool pending_=false;
};

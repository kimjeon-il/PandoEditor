#pragma once
#include <pandoeditor/map/mapviewstate.h>
#include <pandoeditor/map/renderscene.h>
#include <pandoeditor/map/framepipeline.h>
#include <atomic>
#include <QObject>
#include <memory>
#include <mutex>

class MapSceneBridge final : public QObject {
    Q_OBJECT
public:
    explicit MapSceneBridge(QObject* parent=nullptr,bool cullingEnabled=true)
        :QObject(parent),cullingEnabled_(cullingEnabled){}
    std::shared_ptr<const RenderScene> sceneSnapshot() const;
    MapViewState viewState() const;
    std::shared_ptr<const MapFrame> frameSnapshot() const;
    std::uint64_t scenePublicationCount() const {return scenePublications_.load();}
    std::uint64_t interactionFrameCount() const {return interactionFrames_.load();}
    std::uint64_t viewFrameCount() const {return viewFrames_.load();}
    void publishScene(std::shared_ptr<const RenderScene> scene);
    void publishView(const MapViewState& view);
signals:
    void sceneChanged(qulonglong revision);
    void viewChanged(qulonglong revision);
private:
    // C++17 atomic shared_ptr free functions provide cross-thread immutable publication.
    std::shared_ptr<const MapFrame> frame_=FramePipeline::compose({},MapViewState{});
    mutable std::mutex frameMutex_;
    const bool cullingEnabled_;
    std::atomic<std::uint64_t> scenePublications_{0},interactionFrames_{0},viewFrames_{0};
};

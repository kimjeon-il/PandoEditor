#pragma once
#include <pandoeditor/map/mapviewstate.h>
#include <pandoeditor/map/renderscene.h>
#include <QObject>
#include <memory>
#include <mutex>

class MapSceneBridge final : public QObject {
    Q_OBJECT
public:
    explicit MapSceneBridge(QObject* parent=nullptr):QObject(parent){}
    std::shared_ptr<const RenderScene> sceneSnapshot() const;
    MapViewState viewState() const;
    void publishScene(std::shared_ptr<const RenderScene> scene);
    void publishView(const MapViewState& view);
signals:
    void sceneChanged(qulonglong revision);
    void viewChanged(qulonglong revision);
private:
    // C++17 atomic shared_ptr free functions provide cross-thread immutable publication.
    std::shared_ptr<const RenderScene> scene_;
    mutable std::mutex sceneMutex_;
    mutable std::mutex viewMutex_;
    MapViewState view_;
};

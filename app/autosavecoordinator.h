#pragma once

#include "platformstorage.h"
#include <pandoeditor/map/mapviewstate.h>
#include <QByteArray>
#include <QObject>
#include <QTimer>
#include <QFutureWatcher>
#include <pandoeditor/project.h>
#include <functional>
#include <optional>

class ProjectAutosave final : public QObject {
    Q_OBJECT
public:
    static constexpr int SaveDelayMs=650;
    explicit ProjectAutosave(QString projectPath={},QString viewPath={},QObject* parent=nullptr);
    ~ProjectAutosave() override;
    QString projectPath() const {return projectPath_;}
    QString viewPath() const {return viewPath_;}
    void scheduleDocument(QByteArray persistedProject);
    void scheduleDocument(pandoeditor::ProjectSnapshot snapshot);
    void scheduleView(const MapViewState& view);
    bool flushNow();
    QByteArray restoreDocument();
    std::optional<MapViewState> restoreView();
signals:
    void saved();
    void saveFailed(const QString& message);
private:
    struct WriteResult { QString error; };
    std::function<WriteResult()> takePendingWrite();
    void startWrite();
    bool reportWrite(const WriteResult& result);
    static QByteArray documentEnvelope(const QByteArray& project);
    static QByteArray viewEnvelope(const MapViewState& view);
    static QString defaultPath(const QString& name);
    QString projectPath_,viewPath_;
    QTimer timer_;
    QByteArray pendingDocument_;
    std::optional<pandoeditor::ProjectSnapshot> pendingSnapshot_;
    QFutureWatcher<WriteResult>* write_=nullptr;
    std::optional<MapViewState> pendingView_;
    bool documentPending_=false,viewPending_=false;
};

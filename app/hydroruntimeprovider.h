#pragma once
#include "hydroloadscheduler.h"
#include "hydromanifest.h"
#include "hydrometadata.h"
#include "hydroshardreader.h"
#include <pandoeditor/hydroviewport.h>
#include <QObject>
#include <memory>

class HydroRuntimeProvider : public QObject {
    Q_OBJECT
public:
    explicit HydroRuntimeProvider(QObject* parent=nullptr);
    bool open(const QString& path,const QString& projectInstance,bool mobile,QString& error);
    void close(const QString& projectInstance);
    void requestViewport(const pandoeditor::HydroFlatWindow& view);
    std::shared_ptr<const HydroRuntimeFrame> frame() const {return scheduler_.frame();}
    bool isOpen() const {return bool(dataset_);}
    const HydroMetadata* coreMetadata() const;
    bool pinLogical(quint32 logicalFid);
    void clearPinned();
    std::size_t cachedPackCount() const;
    std::size_t cachedBytes() const;
signals:
    void frameChanged();
    void loadFailed(const QString& error);
private:
    struct Dataset;
    std::shared_ptr<Dataset> dataset_;
    HydroLoadScheduler scheduler_;
};

#pragma once
#include <pandoeditor/hydroformat.h>
#include <QObject>
#include <QString>
#include <functional>
#include <memory>
#include <vector>

struct HydroRuntimeFrame {
    std::vector<std::uint32_t> packIds;
    std::vector<pandoeditor::HydroPhysicalFeature> features;
    QString error;
};

class HydroLoadScheduler : public QObject {
    Q_OBJECT
public:
    using Job=std::function<std::shared_ptr<const HydroRuntimeFrame>()>;
    explicit HydroLoadScheduler(QObject* parent=nullptr):QObject(parent){}
    void resetDataset(const QString& projectInstance);
    quint64 requestViewport(Job job);
    std::shared_ptr<const HydroRuntimeFrame> frame() const {return frame_;}
    quint64 generation() const {return generation_;}
    quint64 revision() const {return revision_;}
signals:
    void frameAccepted();
    void loadFailed(const QString& error);
private:
    QString projectInstance_;
    quint64 generation_=0,revision_=0;
    std::shared_ptr<const HydroRuntimeFrame> frame_;
};

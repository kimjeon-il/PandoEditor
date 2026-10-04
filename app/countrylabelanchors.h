#pragma once

#include <pandoeditor/document.h>
#include <pandoeditor/map/resourcecachepolicy.h>
#include <QSet>
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <optional>

class CountryLabelAnchors final : public QObject {
    Q_OBJECT
public:
    explicit CountryLabelAnchors(QByteArray pinnedJson={},QObject* parent=nullptr);
    QString version() const {return version_;}
    QString method() const {return method_;}
    int fixedCount() const {return fixed_.size();}
    std::optional<pandoeditor::Point> fixed(const QString& sourceId) const;
    std::optional<pandoeditor::Point> anchor(const QString& ownerId,const QString& sourceId) const;
    void setProjectScope(QString projectInstance);
    void invalidateOwner(const QString& ownerId);
    quint64 recompute(QString ownerId,pandoeditor::Geometry geometry,pandoeditor::GeometryRef ref);
    bool commitDerived(quint64 token,const QString& ownerId,const pandoeditor::GeometryRef& ref,
                       std::optional<pandoeditor::Point> anchor);
    pandoeditor::ResourceCacheSnapshot resourceCacheSnapshot() const;
    std::size_t fixedStorageBytes() const {return fixedBytes_;}
    std::size_t derivedStorageBytes() const;

    quint64 recompute(QString ownerId,pandoeditor::Geometry geometry,std::uint32_t geometryVersion);
    bool commitDerived(quint64 token,const QString& ownerId,std::uint32_t geometryVersion,
                       std::optional<pandoeditor::Point> anchor);
    static std::optional<pandoeditor::Point> derive(const pandoeditor::Geometry& geometry);
signals:
    void changed();
    void recomputeFailed(const QString& ownerId);
private:
    struct Request {quint64 token=0;pandoeditor::GeometryRef ref;};
    struct Derived {pandoeditor::GeometryRef ref;pandoeditor::Point point;};
    QString version_,method_;
    QHash<QString,pandoeditor::Point> fixed_;
    QHash<QString,Derived> derived_;
    QSet<QString> invalidated_;
    std::size_t fixedBytes_=0;
    std::uint64_t scopeEpoch_=1,invalidations_=0,failures_=0,stale_=0;
    QHash<QString,Request> requests_;
    quint64 generation_=0;
    QString projectInstance_;
};

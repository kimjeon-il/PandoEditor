#pragma once

#include <pandoeditor/document.h>
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
    quint64 recompute(QString ownerId,pandoeditor::Geometry geometry,std::uint32_t geometryVersion);
    bool commitDerived(quint64 token,const QString& ownerId,std::uint32_t geometryVersion,
                       std::optional<pandoeditor::Point> anchor);
    static std::optional<pandoeditor::Point> derive(const pandoeditor::Geometry& geometry);
signals:
    void changed();
    void recomputeFailed(const QString& ownerId);
private:
    struct Request {quint64 token=0;std::uint32_t version=0;};
    QString version_,method_;
    QHash<QString,pandoeditor::Point> fixed_,derived_;
    QHash<QString,Request> requests_;
    quint64 generation_=0;
};

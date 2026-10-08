#pragma once
#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <map>
#include <mutex>

namespace pandoeditor {
// Verified latest-lineage source catalog. Returned JSON is a value snapshot;
// this owner never creates project IDs, edits documents or touches history.
class TerritorialLibraryCatalog final {
public:
    using Reader=std::function<QByteArray(const QString& file)>;
    TerritorialLibraryCatalog(const QByteArray& indexBytes,const QByteArray& expectedIndexSha256,
                              const QString& dataRoot,Reader reader={});
    QJsonArray entries() const {return index_.value("entities").toArray();}
    QJsonArray lineages() const {return index_.value("lineages").toArray();}
    QJsonArray snapshots() const {return index_.value("snapshots").toArray();}
    QJsonObject entry(const QString& entityId) const;
    QJsonArray search(const QString& query,const QString& referenceDate) const;
    QJsonArray events(const QString& query) const;
    QJsonObject resolveSelection(const QString& entityId,const QString& referenceDate) const;
    QString selectedVersionId(const QString& entityId,const QString& referenceDate) const;
    QJsonObject loadEntity(const QString& entityId) const;
    // A version gap is an explicit error; preview never chooses nearby dates.
    QJsonObject preview(const QString& entityId,const QString& referenceDate) const;
    QStringList entityRefsWithChildren(const QStringList& roots,const QString& referenceDate,
                                      const QString& depth=QStringLiteral("none")) const;
    QJsonArray instantiateDescriptors(const QStringList& roots,const QString& referenceDate,
                                     const QString& depth=QStringLiteral("none")) const;
    std::size_t loadedEntityCount() const;
private:
    QJsonObject index_;
    QString root_;
    Reader reader_;
    std::map<QString,QJsonObject> entries_;
    mutable std::mutex mutex_;
    mutable std::map<QString,QJsonObject> loaded_;
};
}

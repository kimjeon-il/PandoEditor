#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>

struct WorldAsset {
    QString path,version,gitBlobSha,sha256;
    bool required=false;
    qint64 bytes=0;
};

class WorldDataset final {
public:
    explicit WorldDataset(QString root=QStringLiteral(":/world"));
    const QString& root() const { return root_; }
    const QJsonObject& manifest() const { return manifest_; }
    WorldAsset asset(const QString& key) const;
    QByteArray read(const QString& key) const;
    QByteArray decompress(const QString& key,int maximumBytes) const;
    QString optionalDataRoot() const;
private:
    QString root_;
    QJsonObject manifest_;
};

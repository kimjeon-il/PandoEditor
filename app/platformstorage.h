#pragma once

#include <QByteArray>
#include <QIODevice>
#include <QString>
#include <QUrl>

class ProjectStorage {
public:
    static constexpr qint64 MaximumProjectBytes = 64LL * 1024 * 1024;

    explicit ProjectStorage(QString privateProjectPath = {});

    static QByteArray readBounded(QIODevice& device);
    QByteArray read(const QUrl& url) const;
    void write(const QUrl& url, const QByteArray& data) const;

    QString privateProjectPath() const { return privateProjectPath_; }
    bool privateProjectExists() const;
    QByteArray readPrivate() const;
    void writePrivateAtomic(const QByteArray& data) const;
    QString preserveCorruptPrivate() const;

private:
    static void writeLocalAtomic(const QString& path, const QByteArray& data);
    QString privateProjectPath_;
};

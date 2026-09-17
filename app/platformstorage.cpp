#include "platformstorage.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <stdexcept>

namespace {
[[noreturn]] void fail(const QString& message)
{
    throw std::runtime_error(message.toUtf8().constData());
}

} // namespace

ProjectStorage::ProjectStorage(QString privateProjectPath)
    : privateProjectPath_(std::move(privateProjectPath))
{
    if (privateProjectPath_.isEmpty()) {
        const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(directory);
        privateProjectPath_ = QDir(directory).filePath(QStringLiteral("current.pando.json"));
    }
}

QByteArray ProjectStorage::readBounded(QIODevice& device)
{
    if (!device.isOpen() || !device.isReadable())
        fail(QStringLiteral("Project stream is not readable"));
    if (!device.isSequential() && device.size() > MaximumProjectBytes)
        fail(QStringLiteral("Project exceeds the 64 MiB prototype limit"));

    QByteArray result;
    if (!device.isSequential() && device.size() > 0)
        result.reserve(static_cast<qsizetype>(device.size()));
    char buffer[64 * 1024];
    while (true) {
        const auto remaining = MaximumProjectBytes - result.size();
        const auto count = device.read(buffer, std::min<qint64>(sizeof(buffer), remaining + 1));
        if (count < 0)
            fail(device.errorString().isEmpty() ? QStringLiteral("Project stream read failed") : device.errorString());
        if (count == 0 && !device.atEnd())
            fail(QStringLiteral("Project stream ended before EOF"));
        if (count == 0)
            break;
        if (count > remaining)
            fail(QStringLiteral("Project exceeds the 64 MiB prototype limit"));
        result.append(buffer, count);
    }
    return result;
}

QByteArray ProjectStorage::read(const QUrl& url) const
{
    if (url.isLocalFile()) {
        QFile file(url.toLocalFile());
        if (!file.open(QIODevice::ReadOnly))
            fail(file.errorString());
        return readBounded(file);
    }
#ifdef Q_OS_ANDROID
    if (url.scheme() == QStringLiteral("content")) {
        QFile file(url.toString(QUrl::FullyEncoded));
        if (!file.open(QIODevice::ReadOnly))
            fail(file.errorString());
        return readBounded(file);
    }
#endif
    fail(QStringLiteral("지원하지 않는 프로젝트 위치입니다."));
}

void ProjectStorage::writeLocalAtomic(const QString& path, const QByteArray& data)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        fail(file.errorString());
    if (file.write(data) != data.size()) {
        const auto message = file.errorString();
        file.cancelWriting();
        fail(message);
    }
    if (!file.commit())
        fail(file.errorString());
}

void ProjectStorage::write(const QUrl& url, const QByteArray& data) const
{
    if (url.isLocalFile()) {
        writeLocalAtomic(url.toLocalFile(), data);
        return;
    }
#ifdef Q_OS_ANDROID
    if (url.scheme() == QStringLiteral("content")) {
        QFile file(url.toString(QUrl::FullyEncoded));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            fail(file.errorString());
        qsizetype offset = 0;
        while (offset < data.size()) {
            const auto count = file.write(data.constData() + offset, data.size() - offset);
            if (count <= 0)
                fail(file.errorString().isEmpty() ? QStringLiteral("프로젝트 내보내기에 실패했습니다.") : file.errorString());
            offset += count;
        }
        if (!file.flush())
            fail(file.errorString());
        file.close();
        if (file.error() != QFileDevice::NoError)
            fail(file.errorString());
        return;
    }
#endif
    fail(QStringLiteral("지원하지 않는 프로젝트 위치입니다."));
}

bool ProjectStorage::privateProjectExists() const
{
    return QFile::exists(privateProjectPath_);
}

QByteArray ProjectStorage::readPrivate() const
{
    return read(QUrl::fromLocalFile(privateProjectPath_));
}

void ProjectStorage::writePrivateAtomic(const QByteArray& data) const
{
    writeLocalAtomic(privateProjectPath_, data);
}

QString ProjectStorage::preserveCorruptPrivate() const
{
    if (!privateProjectExists())
        fail(QStringLiteral("보존할 손상 파일이 없습니다."));
    auto backup = privateProjectPath_ + QStringLiteral(".corrupt");
    for (int suffix = 1; QFile::exists(backup); ++suffix)
        backup = privateProjectPath_ + QStringLiteral(".corrupt.%1").arg(suffix);
    if (!QFile::copy(privateProjectPath_, backup))
        fail(QStringLiteral("손상된 저장 파일의 복구용 사본을 만들 수 없습니다."));
    return backup;
}

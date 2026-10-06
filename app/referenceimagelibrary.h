#pragma once

#include "referencewarp.h"
#include "referenceimagemodel.h"
#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <optional>

class ReferenceImageLibrary : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList images READ images NOTIFY imagesChanged)
    Q_PROPERTY(QAbstractItemModel *imageModel READ imageModel CONSTANT)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    explicit ReferenceImageLibrary(QObject *parent = nullptr);

    QVariantList images() const;
    QAbstractItemModel *imageModel() { return &imageModel_; }
    bool canUndo() const { return !undo_.isEmpty(); }
    bool canRedo() const { return !redo_.isEmpty(); }
    QString lastError() const { return lastError_; }

    Q_INVOKABLE bool importImage(const QUrl &source, const QString &name = {});
    Q_INVOKABLE bool removeImage(const QString &id);
    Q_INVOKABLE bool updateImage(const QString &id, const QVariantMap &changes);
    Q_INVOKABLE bool moveImage(const QString &id, int destination);
    Q_INVOKABLE bool beginGesture(const QString &id);
    Q_INVOKABLE bool updateGesture(const QVariantMap &changes);
    Q_INVOKABLE bool commitGesture();
    Q_INVOKABLE void cancelGesture();
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE bool reload();
    Q_INVOKABLE QVariantMap solveWarp(const QString &id, const QVariantList &controlPoints,
                                      const QString &mode) const;
    Q_INVOKABLE QVariantList traceLine(const QString &id,double startX,double startY,double endX,double endY) const;

signals:
    void imagesChanged();
    void historyChanged();
    void lastErrorChanged();

private:
    struct Record {
        QString id, name, fileName, blend = QStringLiteral("normal"), warpMode = QStringLiteral("auto");
        bool visible = true, locked = false, flipX = false, flipY = false;
        double opacity = 1, x = 0, y = 0, width = 0, height = 0, rotation = 0;
        QVariantList controlPoints;
    };
    using State = QVector<Record>;
    QString directory() const;
    QString indexPath() const;
    bool save(const State &state);
    bool setState(State state, bool recordHistory);
    void fail(const QString &message);
    static QVariantMap variant(const Record &record, const QString &directory);
    static QJsonObject json(const Record &record);
    static std::optional<Record> record(const QJsonObject &json);
    const Record *find(const QString &id) const;
    ReferenceImageModel imageModel_;
    State state_;
    QVector<State> undo_, redo_;
    std::optional<State> gestureBefore_;
    QString gestureId_;
    QString lastError_;
};

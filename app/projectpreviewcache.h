#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>

struct ProjectPreviewIdentity {
    QString sourceSha;
    QString algorithmVersion;
    QString projectSha;
    static ProjectPreviewIdentity from(const QByteArray& sourceSha,const QByteArray& algorithmVersion,
                                       const QByteArray& persistedProject);
    QString key() const;
    bool operator==(const ProjectPreviewIdentity& other) const {
        return sourceSha==other.sourceSha&&algorithmVersion==other.algorithmVersion&&
               projectSha==other.projectSha;
    }
};

class ProjectPreviewCache final {
public:
    static constexpr qsizetype MaximumPreviewBytes=16*1024*1024;
    explicit ProjectPreviewCache(QString path={});
    QString path() const {return path_;}
    bool store(const ProjectPreviewIdentity&,const QByteArray& preview);
    QByteArray load(const ProjectPreviewIdentity&);
    QByteArray loadOrCanonical(const ProjectPreviewIdentity& identity,const QByteArray& canonical);
private:
    static QString defaultPath();
    QString path_;
};

class ProjectPreviewService final : public QObject {
    Q_OBJECT
public:
    static constexpr int IdleDelayMs=1500;
    static constexpr auto AlgorithmVersion="project-preview-v1";
    explicit ProjectPreviewService(QString cachePath={},QObject* parent=nullptr);
    void schedule(QByteArray persistedProject,QByteArray sourceSha);
    quint64 generation() const {return generation_;}
    bool commitGenerated(quint64 generation,const ProjectPreviewIdentity&,const QByteArray& preview);
    QByteArray cachedOrCanonical(const QByteArray& persistedProject,const QByteArray& sourceSha);
    static QByteArray derivePreview(const QByteArray& persistedProject);
signals:
    void previewReady(const QByteArray& project);
    void generationFailed(const QString& message);
private:
    void beginGeneration();
    ProjectPreviewCache cache_;
    QTimer timer_;
    QByteArray pendingProject_,pendingSourceSha_;
    quint64 generation_=0;
};

#include "autosavecoordinator.h"
#include "projectcodec.h"
#include <QtConcurrent>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <stdexcept>

namespace {
constexpr auto autosaveFormat="pandoeditor-autosave";
constexpr auto viewFormat="pandoeditor-view-state";
constexpr int envelopeVersion=1;
QString digest(const QByteArray& bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
}
double number(const QJsonObject& object,const char* key) {
    const auto value=object.value(QLatin1String(key));
    if(!value.isDouble())throw std::runtime_error("invalid autosave view field");
    return value.toDouble();
}
}

QString ProjectAutosave::defaultPath(const QString& name) {
    const auto directory=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(directory);
    return QDir(directory).filePath(name);
}

ProjectAutosave::ProjectAutosave(QString projectPath,QString viewPath,QObject* parent)
    :QObject(parent),projectPath_(projectPath.isEmpty()?defaultPath("autosave-project.json"):std::move(projectPath)),
     viewPath_(viewPath.isEmpty()?defaultPath("autosave-view.json"):std::move(viewPath)) {
    timer_.setSingleShot(true);timer_.setInterval(SaveDelayMs);
    connect(&timer_,&QTimer::timeout,this,&ProjectAutosave::startWrite);
}
ProjectAutosave::~ProjectAutosave() { flushNow(); }

void ProjectAutosave::scheduleDocument(QByteArray persistedProject) {
    pendingSnapshot_.reset();
    pendingDocument_=std::move(persistedProject);documentPending_=true;timer_.start();
}
void ProjectAutosave::scheduleDocument(pandoeditor::ProjectSnapshot snapshot) {
    pendingDocument_.clear();pendingSnapshot_=std::move(snapshot);
    documentPending_=true;timer_.start();
}

void ProjectAutosave::scheduleView(const MapViewState& view) {
    if(!validMapViewState(view))return;
    pendingView_=view;viewPending_=true;timer_.start();
}

QByteArray ProjectAutosave::documentEnvelope(const QByteArray& project) {
    if(project.isEmpty()||project.size()>ProjectStorage::MaximumProjectBytes)
        throw std::runtime_error("autosave project exceeds the supported limit");
    const QJsonObject envelope{{"format",autosaveFormat},{"version",envelopeVersion},
        {"sha256",digest(project)},{"project",QString::fromLatin1(project.toBase64())}};
    return QJsonDocument(envelope).toJson(QJsonDocument::Compact);
}

QByteArray ProjectAutosave::viewEnvelope(const MapViewState& view) {
    const QJsonObject state{{"projection",view.mode==ProjectionMode::Globe?"globe":"flat"},
        {"viewportWidth",view.viewportWidth},{"viewportHeight",view.viewportHeight},
        {"centerLongitude",view.centerLongitude},{"centerLatitude",view.centerLatitude},
        {"rotationLongitude",view.rotationLongitude},{"rotationLatitude",view.rotationLatitude},
        {"rotationRoll",view.rotationRoll},{"scale",view.scale},{"translateX",view.translateX},
        {"translateY",view.translateY},{"devicePixelRatio",view.devicePixelRatio},
        {"revision",double(view.revision)}};
    return QJsonDocument(QJsonObject{{"format",viewFormat},{"version",envelopeVersion},
        {"view",state}}).toJson(QJsonDocument::Compact);
}

std::function<ProjectAutosave::WriteResult()> ProjectAutosave::takePendingWrite() {
    const bool document=documentPending_,view=viewPending_;
    documentPending_=viewPending_=false;
    auto bytes=std::move(pendingDocument_);
    auto snapshot=std::move(pendingSnapshot_);pendingSnapshot_.reset();
    auto state=std::move(pendingView_);pendingView_.reset();
    return [document,view,bytes=std::move(bytes),snapshot=std::move(snapshot),state,
            projectPath=projectPath_,viewPath=viewPath_]() -> WriteResult {
        try {
            if(document)ProjectStorage(projectPath).writePrivateAtomic(
                documentEnvelope(snapshot?projectcodec::encode(*snapshot):bytes));
            if(view&&state)ProjectStorage(viewPath).writePrivateAtomic(viewEnvelope(*state));
            return {};
        } catch(const std::exception& error) {return {QString::fromUtf8(error.what())};}
          catch(...) {return {QStringLiteral("자동저장 처리 중 알 수 없는 오류가 발생했습니다.")};}
    };
}
bool ProjectAutosave::reportWrite(const WriteResult& result) {
    if(!result.error.isEmpty()){emit saveFailed(result.error);return false;}
    emit saved();return true;
}
void ProjectAutosave::startWrite() {
    if(write_||(!documentPending_&&!viewPending_))return;
    auto* watcher=new QFutureWatcher<WriteResult>(this);write_=watcher;
    connect(watcher,&QFutureWatcher<WriteResult>::finished,this,[this,watcher] {
        write_=nullptr;
        const auto result=watcher->result();watcher->deleteLater();
        reportWrite(result);
        if(!timer_.isActive())startWrite();
    });
    // Only one writer can commit at a time. Later edits replace the queued snapshot.
    watcher->setFuture(QtConcurrent::run(takePendingWrite()));
}
bool ProjectAutosave::flushNow() {
    timer_.stop();
    bool success=true;
    if(write_) {
        auto* watcher=write_;write_=nullptr;
        disconnect(watcher,nullptr,this,nullptr);
        watcher->waitForFinished();
        success=reportWrite(watcher->result());delete watcher;
    }
    if(documentPending_||viewPending_)success=reportWrite(takePendingWrite()())&&success;
    return success;
}

QByteArray ProjectAutosave::restoreDocument() {
    if(!QFile::exists(projectPath_))return {};
    try {
        const auto bytes=ProjectStorage(projectPath_).readPrivate();
        QJsonParseError error;const auto parsed=QJsonDocument::fromJson(bytes,&error);
        if(error.error!=QJsonParseError::NoError||!parsed.isObject())
            throw std::runtime_error("invalid autosave envelope");
        const auto object=parsed.object();
        if(object.value("format")!=autosaveFormat||object.value("version").toInt()!=envelopeVersion)
            throw std::runtime_error("unsupported autosave envelope");
        const auto project=QByteArray::fromBase64(object.value("project").toString().toLatin1());
        if(project.isEmpty()||object.value("sha256").toString()!=digest(project))
            throw std::runtime_error("autosave checksum mismatch");
        return project;
    } catch(const std::exception&) {
        try {ProjectStorage(projectPath_).preserveCorruptPrivate();}catch(const std::exception&) {}
        return {};
    }
}

std::optional<MapViewState> ProjectAutosave::restoreView() {
    if(!QFile::exists(viewPath_))return std::nullopt;
    try {
        const auto parsed=QJsonDocument::fromJson(ProjectStorage(viewPath_).readPrivate());
        if(!parsed.isObject())throw std::runtime_error("invalid autosave view envelope");
        const auto object=parsed.object();
        if(object.value("format")!=viewFormat||object.value("version").toInt()!=envelopeVersion||
           !object.value("view").isObject())throw std::runtime_error("unsupported autosave view envelope");
        const auto state=object.value("view").toObject();MapViewState view;
        const auto projection=state.value("projection").toString();
        if(projection!="flat"&&projection!="globe")throw std::runtime_error("invalid projection");
        view.mode=projection=="globe"?ProjectionMode::Globe:ProjectionMode::Flat;
        view.viewportWidth=number(state,"viewportWidth");view.viewportHeight=number(state,"viewportHeight");
        view.centerLongitude=number(state,"centerLongitude");view.centerLatitude=number(state,"centerLatitude");
        view.rotationLongitude=number(state,"rotationLongitude");view.rotationLatitude=number(state,"rotationLatitude");
        view.rotationRoll=number(state,"rotationRoll");view.scale=number(state,"scale");
        view.translateX=number(state,"translateX");view.translateY=number(state,"translateY");
        view.devicePixelRatio=number(state,"devicePixelRatio");view.revision=std::uint64_t(number(state,"revision"));
        if(!validMapViewState(view))throw std::runtime_error("invalid autosave view state");
        return view;
    } catch(const std::exception&) {
        try {ProjectStorage(viewPath_).preserveCorruptPrivate();}catch(const std::exception&) {}
        return std::nullopt;
    }
}

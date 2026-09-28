#include "autosavecoordinator.h"
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
    connect(&timer_,&QTimer::timeout,this,[this]{flushNow();});
}

void ProjectAutosave::scheduleDocument(QByteArray persistedProject) {
    pendingDocument_=std::move(persistedProject);documentPending_=true;timer_.start();
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

bool ProjectAutosave::flushNow() {
    timer_.stop();
    try {
        if(documentPending_)ProjectStorage(projectPath_).writePrivateAtomic(documentEnvelope(pendingDocument_));
        if(viewPending_&&pendingView_)ProjectStorage(viewPath_).writePrivateAtomic(viewEnvelope(*pendingView_));
        documentPending_=false;viewPending_=false;pendingDocument_.clear();pendingView_.reset();
        emit saved();return true;
    } catch(const std::exception& error) {
        emit saveFailed(QString::fromUtf8(error.what()));return false;
    }
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

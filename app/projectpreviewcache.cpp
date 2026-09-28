#include "projectpreviewcache.h"
#include "platformstorage.h"
#include "projectcodec.h"
#include <pandoeditor/project.h>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QtConcurrent>
#include <algorithm>
#include <stdexcept>

namespace {
constexpr auto cacheFormat="pandoeditor-project-preview";
constexpr int cacheVersion=1;
QString sha256(const QByteArray& value) {
    return QString::fromLatin1(QCryptographicHash::hash(value,QCryptographicHash::Sha256).toHex());
}
pandoeditor::Ring thin(const pandoeditor::Ring& input,std::size_t limit,bool closed) {
    if(input.size()<=limit)return input;
    const auto logical=closed?input.size()-1:input.size();
    const auto stride=std::max<std::size_t>(1,(logical+limit-2)/(limit-1));
    pandoeditor::Ring out;out.reserve(logical/stride+2);
    for(std::size_t i=0;i<logical;i+=stride)out.push_back(input[i]);
    if(out.empty()||out.back().x!=input[logical-1].x||out.back().y!=input[logical-1].y)
        out.push_back(input[logical-1]);
    if(closed) {
        if(out.size()<3)return input;
        out.push_back(out.front());
    }
    return out;
}
pandoeditor::Geometry simplified(const pandoeditor::Geometry& input) {
    auto result=input;
    for(auto& line:result.lines)line=thin(line,128,false);
    for(auto& polygon:result.polygons)for(auto& ring:polygon)ring=thin(ring,96,true);
    return result;
}
struct GeneratedPreview {
    quint64 generation=0;
    ProjectPreviewIdentity identity;
    QByteArray bytes;
    QString error;
};
}

ProjectPreviewIdentity ProjectPreviewIdentity::from(const QByteArray& source,const QByteArray& algorithm,
                                                    const QByteArray& persistedProject) {
    return {QString::fromLatin1(source),QString::fromLatin1(algorithm),sha256(persistedProject)};
}
QString ProjectPreviewIdentity::key() const {
    return sha256(sourceSha.toUtf8()+'\0'+algorithmVersion.toUtf8()+'\0'+projectSha.toUtf8());
}

QString ProjectPreviewCache::defaultPath() {
    const auto directory=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(directory);
    return QDir(directory).filePath("project-preview-cache.json");
}
ProjectPreviewCache::ProjectPreviewCache(QString path):path_(path.isEmpty()?defaultPath():std::move(path)) {}
bool ProjectPreviewCache::store(const ProjectPreviewIdentity& identity,const QByteArray& preview) {
    if(preview.isEmpty()||preview.size()>MaximumPreviewBytes)return false;
    try {
        const auto compressed=qCompress(preview,9);
        const QJsonObject object{{"format",cacheFormat},{"version",cacheVersion},
            {"sourceSha",identity.sourceSha},{"algorithm",identity.algorithmVersion},
            {"projectSha",identity.projectSha},{"identity",identity.key()},
            {"previewSha",sha256(preview)},{"preview",QString::fromLatin1(compressed.toBase64())}};
        const QFileInfo info(path_);QDir().mkpath(info.absolutePath());
        ProjectStorage(path_).writePrivateAtomic(QJsonDocument(object).toJson(QJsonDocument::Compact));
        return true;
    }catch(const std::exception&){return false;}
}
QByteArray ProjectPreviewCache::load(const ProjectPreviewIdentity& identity) {
    if(!QFile::exists(path_))return {};
    try {
        const auto document=QJsonDocument::fromJson(ProjectStorage(path_).readPrivate());
        if(!document.isObject())throw std::runtime_error("invalid project preview cache");
        const auto object=document.object();
        if(object.value("format")!=cacheFormat||object.value("version").toInt()!=cacheVersion)
            throw std::runtime_error("unsupported project preview cache");
        if(object.value("sourceSha").toString()!=identity.sourceSha||
           object.value("algorithm").toString()!=identity.algorithmVersion||
           object.value("projectSha").toString()!=identity.projectSha||
           object.value("identity").toString()!=identity.key())return {};
        const auto compressed=QByteArray::fromBase64(object.value("preview").toString().toLatin1());
        const auto preview=qUncompress(compressed);
        if(preview.isEmpty()||preview.size()>MaximumPreviewBytes||
           object.value("previewSha").toString()!=sha256(preview))
            throw std::runtime_error("project preview checksum mismatch");
        return preview;
    }catch(const std::exception&) {
        try {ProjectStorage(path_).preserveCorruptPrivate();}catch(const std::exception&) {}
        return {};
    }
}
QByteArray ProjectPreviewCache::loadOrCanonical(const ProjectPreviewIdentity& identity,const QByteArray& canonical) {
    const auto cached=load(identity);return cached.isEmpty()?canonical:cached;
}

ProjectPreviewService::ProjectPreviewService(QString cachePath,QObject* parent)
    :QObject(parent),cache_(std::move(cachePath)) {
    timer_.setSingleShot(true);timer_.setInterval(IdleDelayMs);
    connect(&timer_,&QTimer::timeout,this,&ProjectPreviewService::beginGeneration);
}
void ProjectPreviewService::schedule(QByteArray persistedProject,QByteArray sourceSha) {
    pendingProject_=std::move(persistedProject);pendingSourceSha_=std::move(sourceSha);
    ++generation_;timer_.start();
}
QByteArray ProjectPreviewService::cachedOrCanonical(const QByteArray& persistedProject,const QByteArray& sourceSha) {
    return cache_.loadOrCanonical(ProjectPreviewIdentity::from(sourceSha,AlgorithmVersion,persistedProject),persistedProject);
}
QByteArray ProjectPreviewService::derivePreview(const QByteArray& persistedProject) {
    auto document=projectcodec::decode(persistedProject);
    const auto originals=document.geometries.versions();
    document.geometries=pandoeditor::GeometryStore{};
    for(const auto& [ref,geometry]:originals) {
        try {document.geometries.insert(ref,simplified(*geometry));}
        catch(const std::exception&) {document.geometries.insert(ref,*geometry);}
    }
    pandoeditor::Project preview;preview.replace(std::move(document));
    return projectcodec::encode(preview);
}
void ProjectPreviewService::beginGeneration() {
    const auto generation=generation_;
    const auto project=pendingProject_;
    const auto identity=ProjectPreviewIdentity::from(pendingSourceSha_,AlgorithmVersion,project);
    auto* watcher=new QFutureWatcher<GeneratedPreview>(this);
    connect(watcher,&QFutureWatcher<GeneratedPreview>::finished,this,[this,watcher] {
        const auto result=watcher->result();watcher->deleteLater();
        if(!result.error.isEmpty()) {if(result.generation==generation_)emit generationFailed(result.error);return;}
        commitGenerated(result.generation,result.identity,result.bytes);
    });
    watcher->setFuture(QtConcurrent::run([generation,identity,project] {
        GeneratedPreview result{generation,identity};
        try {result.bytes=derivePreview(project);}
        catch(const std::exception& error){result.error=QString::fromUtf8(error.what());}
        return result;
    }));
}
bool ProjectPreviewService::commitGenerated(quint64 generation,const ProjectPreviewIdentity& identity,
                                            const QByteArray& preview) {
    if(generation!=generation_)return false;
    if(!cache_.store(identity,preview))return false;
    emit previewReady(preview);return true;
}

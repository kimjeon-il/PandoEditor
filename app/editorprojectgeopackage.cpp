#include "editorcontroller.h"
#include "projectgeopackage.h"
#include <QtConcurrent>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QTemporaryFile>
#include <stdexcept>

QVariantMap EditorController::projectGpkgState() const {
    return {{"stage",projectGpkgStage_},{"error",projectGpkgError_},
            {"fileName",projectGpkgFileName_},{"session",projectGpkgToken_}};
}

bool EditorController::openProjectGeoPackage(const QUrl& url) {
    try {
        if(url.isEmpty()||!url.fileName().endsWith(".gpkg",Qt::CaseInsensitive))
            throw std::invalid_argument("INVALID_PROJECT_GPKG_EXTENSION");
        if(geometryEdit_)throw std::invalid_argument("GEOMETRY_EDIT_ACTIVE");
        QByteArray bytes;
        if(url.isLocalFile())bytes=pandoeditor::readProjectGeoPackage(url.toLocalFile());
        else {
            // Android SAF content URIs need a local SQLite file for the read-only driver.
            const auto package=storage_.read(url);
            QTemporaryFile temporary;
            if(!temporary.open()||temporary.write(package)!=package.size()||!temporary.flush())
                throw std::runtime_error("PROJECT_GPKG_TEMP_FAILED");
            bytes=pandoeditor::readProjectGeoPackage(temporary.fileName());
        }
        return replaceFromBytes(bytes,true); // Save must ask for a JSON path.
    }catch(const std::exception& error) {
        emit errorOccurred(QString::fromUtf8(error.what()));return false;
    }
}

bool EditorController::exportProjectGeoPackage(const QUrl& url) {
    if(projectGpkgStage_==QStringLiteral("working"))return false;
    try {
        if(url.isEmpty()||!url.fileName().endsWith(".gpkg",Qt::CaseInsensitive))
            throw std::invalid_argument("INVALID_PROJECT_GPKG_EXTENSION");
        if(isProtectedWebSource(url))throw std::invalid_argument("SOURCE_OVERWRITE_BLOCKED");
        if(url.isLocalFile()&&!filePath_.isEmpty()&&
           QFileInfo(url.toLocalFile()).absoluteFilePath()==QFileInfo(filePath_).absoluteFilePath())
            throw std::invalid_argument("PROJECT_GPKG_SOURCE_OVERWRITE_BLOCKED");
        if(hasPendingEdits()||geometryEdit_||contentEditState().value("active").toBool())
            throw std::invalid_argument("PENDING_EDITS: 확정되지 않은 편집이 있습니다");
        const auto base=project_.snapshot();
        const auto token=++projectGpkgToken_;
        projectGpkgStage_=QStringLiteral("working");projectGpkgError_.clear();
        projectGpkgFileName_=url.fileName();emit projectGpkgChanged();
        auto* watcher=new QFutureWatcher<QByteArray>(this);
        connect(watcher,&QFutureWatcher<QByteArray>::finished,this,[this,watcher,base,url,token]() {
            watcher->deleteLater();
            if(token!=projectGpkgToken_)return;
            try {
                const auto bytes=watcher->result();
                if(!base.matches(project_))throw std::invalid_argument("STALE_PROJECT_GPKG_EXPORT");
                storage_.write(url,bytes);
                projectGpkgStage_=QStringLiteral("done");projectGpkgError_.clear();
            }catch(const std::exception& error) {
                projectGpkgStage_=QStringLiteral("error");
                projectGpkgError_=QString::fromUtf8(error.what());
            }
            emit projectGpkgChanged();
        });
        watcher->setFuture(QtConcurrent::run([base]() {
            pandoeditor::Project copy;copy.replace(base.document());
            return pandoeditor::exportProjectGeoPackage(copy);
        }));
        return true;
    }catch(const std::exception& error) {
        projectGpkgStage_=QStringLiteral("error");
        projectGpkgError_=QString::fromUtf8(error.what());emit projectGpkgChanged();
        return false;
    }
}

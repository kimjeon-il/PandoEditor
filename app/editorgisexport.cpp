#include "editorcontroller.h"
#include "gisdocumentexport.h"
#include "gisgeopackage.h"
#include <QtConcurrent>
#include <QDateTime>
#include <QFileInfo>
#include <QFutureWatcher>
#include <stdexcept>

QVariantList EditorController::gisExportLayers() const {
    const auto& document=project_.document();
    qsizetype countries=0,subunits=0,regions=0;
    for(const auto& unit:document.units) {
        if(pandoeditor::isRootGeneral(document,unit))++countries;
        else if(unit.kind==pandoeditor::UnitKind::General)++subunits;
        else ++regions;
    }
    return {QVariantMap{{"category","countries"},{"name","국가"},{"count",countries}},
        QVariantMap{{"category","subunits"},{"name","하위단위"},{"count",subunits}},
        QVariantMap{{"category","regions"},{"name","지방"},{"count",regions}},
        QVariantMap{{"category","genericFeatures"},{"name","기타 객체"},
                    {"count",qsizetype(document.genericFeatures.size())}},
        QVariantMap{{"category","distributions"},{"name","분포"},
                    {"count",qsizetype(document.distributionEntries.size())}},
        QVariantMap{{"category","labels"},{"name","지명"},
                    {"count",qsizetype(document.labels.size())}}};
}
QVariantMap EditorController::gisExportState() const {
    return {{"stage",gisExportStage_},{"error",gisExportError_},
        {"fileName",gisExportFileName_},{"session",gisExportToken_}};
}
void EditorController::cancelGisExport() {
    ++gisExportToken_;
    gisExportStage_=QStringLiteral("idle");gisExportError_.clear();
    gisExportFileName_.clear();emit gisExportChanged();
}
bool EditorController::exportGisData(const QUrl& destination,const QString& format,
                                     const QStringList& selected) {
    if(gisExportStage_==QStringLiteral("working"))return false;
    try {
        if(destination.isEmpty()||selected.isEmpty()||
           (format!="geojson-zip"&&format!="geopackage"))
            throw std::invalid_argument("INVALID_GIS_EXPORT_SELECTION");
        const auto expected=format=="geojson-zip"?QStringLiteral(".zip"):QStringLiteral(".gpkg");
        if(!destination.fileName().endsWith(expected,Qt::CaseInsensitive))
            throw std::invalid_argument("INVALID_GIS_EXPORT_EXTENSION");
        if(destination.isLocalFile()&&!filePath_.isEmpty()&&
           QFileInfo(destination.toLocalFile()).absoluteFilePath()==QFileInfo(filePath_).absoluteFilePath())
            throw std::invalid_argument("GIS_EXPORT_PROJECT_OVERWRITE_BLOCKED");
        if(hasPendingEdits()||geometryEdit_||contentEditState().value("active").toBool())
            throw std::invalid_argument("PENDING_EDITS: 확정되지 않은 편집이 있습니다");
        const auto base=project_.snapshot();
        std::vector<std::string> categories;
        for(const auto& item:selected)categories.push_back(item.toStdString());
        const auto token=++gisExportToken_;
        gisExportStage_=QStringLiteral("working");gisExportError_.clear();
        gisExportFileName_=destination.fileName();emit gisExportChanged();
        auto* watcher=new QFutureWatcher<QByteArray>(this);
        connect(watcher,&QFutureWatcher<QByteArray>::finished,this,
            [this,watcher,base,destination,token]() {
                watcher->deleteLater();
                if(token!=gisExportToken_)return;
                try {
                    const auto bytes=watcher->result();
                    if(!base.matches(project_))throw std::invalid_argument("STALE_GIS_EXPORT");
                    storage_.write(destination,bytes);
                    gisExportStage_=QStringLiteral("done");gisExportError_.clear();
                }catch(const std::exception& error) {
                    gisExportStage_=QStringLiteral("error");
                    gisExportError_=QString::fromUtf8(error.what());
                }
                emit gisExportChanged();
            });
        watcher->setFuture(QtConcurrent::run([base,format,categories]() {
            if(format=="geojson-zip")return pandoeditor::exportGisGeoJsonZip(
                base.document(),categories,QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString());
            return pandoeditor::exportGisGeoPackage(base.document(),categories);
        }));
        return true;
    }catch(const std::exception& error) {
        gisExportStage_=QStringLiteral("error");
        gisExportError_=QString::fromUtf8(error.what());emit gisExportChanged();
        return false;
    }
}

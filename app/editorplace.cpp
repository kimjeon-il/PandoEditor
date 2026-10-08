#include "editorcontroller.h"
#include "placenamedisplay.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QFont>
#include <QUuid>
#include <algorithm>
#include <limits>
using namespace pandoeditor;

void EditorController::initializePlaceRuntime() {
    if(placeRuntime_.isOpen())return;
    Q_INIT_RESOURCE(place_runtime);
    QString error;
    if(!placeRuntime_.open(placeManifestPath_,QString::fromStdString(project_.instanceId()),error)) {
        emit errorOccurred(error);emit placeDataChanged();return;
    }
    requestPlaceResources();
}
bool EditorController::configurePlaceData(const QUrl& url) {
    QString path;
    if(url.isLocalFile())path=url.toLocalFile();
    else if(url.scheme()=="qrc"&&!url.hasQuery()&&!url.hasFragment())path=":"+url.path();
    if(path.isEmpty()){emit errorOccurred(QStringLiteral("PL-PLACE-LOAD: local manifest URL required"));return false;}
    QString error;
    if(!placeRuntime_.open(path,QString::fromStdString(project_.instanceId()),error)) {
        emit errorOccurred(error);return false;
    }
    placeManifestPath_=path;reconcileSelection();
    emit selectionChanged();emit hoverChanged();emit placeDataChanged();
    requestPlaceResources();invalidateViewportResources(ViewportResourceKind::Labels);
    if(!searchQuery_.isEmpty())placeRuntime_.search(searchQuery_);
    return true;
}
void EditorController::requestPlaceResources() {
    if(!placeRuntime_.isOpen())return;
    QStringList protectedIds;
    if(const auto primary=selection_.primary();primary&&primary->domain=="placeBuiltin")protectedIds.append(QString::fromStdString(primary->id));
    for(const auto& ref:selection_.items())if(ref.domain=="placeBuiltin"&&!protectedIds.contains(QString::fromStdString(ref.id)))protectedIds.append(QString::fromStdString(ref.id));
    if(hover_&&hover_->domain=="placeBuiltin"&&!protectedIds.contains(QString::fromStdString(hover_->id)))protectedIds.append(QString::fromStdString(hover_->id));
    placeRuntime_.setProtectedIds(protectedIds);
    PlaceViewport view;view.view=sceneBridge_.viewState();view.zoom=camera_.display().zoom;
    view.safeBottom=mobileMode_?96.:32.;placeRuntime_.requestViewport(view);
}
void EditorController::refreshBuiltinPlaceLabels() {
    std::vector<PlaceRecord> records;records.reserve(PlaceRuntimeLimits::Candidates);
    std::set<QString> seen;
    const auto copied=copiedPlaceSourceIds(project_.document());
    const bool visible=groupVisible(project_.document().presentation.webPresentation,"labels");
    const auto append=[&](const PlaceRecord& record) {
        if(visible&&records.size()<PlaceRuntimeLimits::Candidates&&!copied.count(record.id.toStdString())&&seen.insert(record.id).second)records.push_back(record);
    };
    if(const auto primary=selection_.primary();primary&&primary->domain=="placeBuiltin")if(const auto record=placeRuntime_.recordById(QString::fromStdString(primary->id)))append(*record);
    for(const auto& ref:selection_.items())if(ref.domain=="placeBuiltin")if(const auto record=placeRuntime_.recordById(QString::fromStdString(ref.id)))append(*record);
    if(const auto snapshot=placeRuntime_.snapshot())for(const auto& record:snapshot->records)append(record);
    QFont primaryFont=QGuiApplication::font();primaryFont.setPixelSize(12);primaryFont.setWeight(QFont::DemiBold);
    QFont secondaryFont=primaryFont;secondaryFont.setPixelSize(10);secondaryFont.setWeight(QFont::Normal);
    const QFontMetricsF primaryMetrics(primaryFont),secondaryMetrics(secondaryFont);
    QByteArray signatureBytes;QDataStream stream(&signatureBytes,QIODevice::WriteOnly);
    const auto identity=placeRuntime_.sourceIdentity();
    stream<<quint64(identity?identity->generation:0)<<primaryFont.toString()<<secondaryFont.toString()<<quint64(records.size());
    std::vector<MapLabelSource> sources;sources.reserve(records.size());
    for(const auto& record:records) {
        const auto rows=resolvePlaceDisplayRows(record,placeLanguages_);
        if(rows.empty())continue; // Unavailable language is omitted, never synthesized.
        const auto settings=automaticLabelSettings(record.kind.toStdString());MapLabelSource source;
        source.ref={"placeBuiltin",record.id.toStdString()};
        source.text=rows.front().text.toStdString();source.geographic=record.coordinates;
        source.collisionGroup=settings.collisionGroup;source.priority=settings.priority.value_or(40);
        source.minZoom=std::max(record.minZoom,settings.minZoom.value_or(0));
        source.maxZoom=settings.maxZoom.value_or(std::numeric_limits<double>::infinity());
        source.width=22;source.height=std::max(19.,primaryMetrics.height());
        for(std::size_t i=0;i<rows.size();++i) {
            const auto& row=rows[i];
            const auto& metrics=i==0?primaryMetrics:secondaryMetrics;
            source.width=std::max(source.width,metrics.horizontalAdvance(row.text)+16);
            if(i>0)source.height+=std::max(14.,secondaryMetrics.height());
            source.lines.push_back({row.language.toStdString(),row.text.toStdString()});
            stream<<row.language<<row.text;
        }
        // Fonts are measured in Qt, unlike Web's conservative CSS-pixel boxes;
        // all language rows share one logical label and collision rectangle.
        stream<<record.id<<record.kind<<record.coordinates.x<<record.coordinates.y<<
            source.minZoom<<source.width<<source.height<<quint64(source.lines.size());
        sources.push_back(std::move(source));
    }
    const auto signature=QString::fromLatin1(QCryptographicHash::hash(signatureBytes,QCryptographicHash::Sha256).toHex());
    if(signature==placeLabelSignature_)return;
    if(placeLabelRevision_==std::numeric_limits<quint64>::max())throw std::overflow_error("PL-PLACE-LAYOUT: revision exhausted");
    placeLabelSignature_=signature;labelEngine_.setBuiltinSources(std::move(sources),++placeLabelRevision_);
}
bool EditorController::copySelectedPlaceForEditing() {
    if(hasPendingEdits()||jobBusy()||hasWebImportPreview())return false;
    const auto selected=selection_.primary();if(!selected||selected->domain!="placeBuiltin"||!objectVisible(*selected))return false;
    const auto record=placeRuntime_.recordById(QString::fromStdString(selected->id));if(!record)return false;
    try {
        const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();const GeometryRef geometryRef{"content:place:"+id,1};
        PlaceLabel copy;copy.id=id;copy.name=record->name.toStdString();copy.kind=record->kind.toStdString();copy.geometry=geometryRef;copy.sourcePlaceId=record->id.toStdString();
        Geometry geometry;geometry.type="Point";geometry.points.push_back(record->coordinates);
        LabelSettings pinned;pinned.pinned=true;pinned.manualPosition=record->coordinates;
        ContentEdit edit;edit.target={"label",id};edit.value=copy;edit.create=true;edit.geometry=std::make_pair(geometryRef,std::move(geometry));
        auto stored=automaticLabelSettings(copy.kind,pinned);
        // The automatic layout policy uses Infinity. Web serialization stores
        // null for that unbounded maximum; the native typed store represents
        // the same maximum by an absent optional, not a non-finite number.
        stored.maxZoom.reset();edit.initialLabelSettings=std::move(stored);
        CommandArguments arguments;arguments.action=std::move(edit);
        auto prepared=CommandProcessor::prepare(project_,CommandProcessor::makeRequest(project_,"content.edit",arguments));
        if(!prepared.ok()||!prepared.preview){emit errorOccurred(QString::fromStdString(prepared.detail));return false;}
        MapProjection next;next.rebuild(prepared.preview->change().after());
        const auto committed=CommandProcessor::confirm(project_,*prepared.preview);
        if(!committed.ok()){emit errorOccurred(QString::fromLatin1(commandErrorCode(committed.error)));return false;}
        noteAppliedImpact(committed.impact);projection_=std::move(next);publish(false);emit geometryChanged();
        selectObject({{"domain","label"},{"id",QString::fromStdString(id)}},"replace","map");return true;
    }catch(const std::exception& error){emit errorOccurred(QString::fromUtf8(error.what()));return false;}
}
QVariantMap EditorController::placeDataStatus() const {
    const auto identity=placeRuntime_.sourceIdentity();const auto store=placeRuntime_.storeStats();const auto runtime=placeRuntime_.stats();
    return {{"loaded",placeRuntime_.isOpen()},{"revision",identity?identity->revision:QString()},
        {"manifestSha256",identity?identity->manifestSha256:QString()},{"generation",qulonglong(identity?identity->generation:0)},
        {"stage",!identity?QString("unloaded"):store.manifestTileCount==0?QString("empty-v1"):QString("loaded-v1")},
        {"manifestTileCount",qulonglong(store.manifestTileCount)},{"manifestShardCount",qulonglong(store.manifestShardCount)},
        {"manifestStageCount",qulonglong(store.manifestStageCount)},{"sourceRecordCount",store.sourceRecordCount?QVariant::fromValue(qulonglong(*store.sourceRecordCount)):QVariant()},
        {"snapshotRecords",qulonglong(runtime.snapshotRecords)},{"retainedRecords",qulonglong(runtime.retainedRecords)},
        {"pendingCount",qulonglong(runtime.pendingCount)},{"sourceOpens",qulonglong(runtime.sourceOpens)},
        {"moving",runtime.moving},{"searchTruncated",placeRuntime_.searchTruncated()},
        {"dataSpecificBlocked",true},{"dataSpecificReason",QStringLiteral("Fixed Web production manifest is empty-v1; explicit synthetic sources verify mechanics only.")},
        {"fontMetrics",QStringLiteral("Qt application font shaping; fixed Web uses Unicode-character estimates")}};
}

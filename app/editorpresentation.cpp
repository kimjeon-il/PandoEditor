#include "editorcontroller.h"
#include <pandoeditor/presentationcommands.h>
#include "losslessjson.h"
#include "hydrodataprovider.h"
#include "../renderer/projectionengine.h"
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QCryptographicHash>
#include <QDir>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QStringList>
#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>
using namespace pandoeditor;
namespace { QString displayText(const std::string& value){return QString::fromStdString(value);} }
QVariantList EditorController::presentationGroups() const {
    QVariantList rows;const auto& p=project_.document().presentation.webPresentation;
    for(auto kind:{UnitKind::Country,UnitKind::Subunit,UnitKind::Region}) {
        const auto group=territorialGroup(kind);
        const auto name=kind==UnitKind::Country?"basemapLabels":kind==UnitKind::Subunit?"subunitLabels":"regionLabels";
        const auto flag=kind==UnitKind::Country?"countryFlags":kind==UnitKind::Subunit?"subunitFlags":"regionFlags";
        const auto found=p.styles.find(group);auto s=found==p.styles.end()?PresentationStyle{}:found->second;
        rows.append(QVariantMap{{"key",QString::fromStdString(group)},{"title",kind==UnitKind::Country?QStringLiteral("국가"):kind==UnitKind::Subunit?QStringLiteral("하위단위"):QStringLiteral("지방")},
            {"visible",groupVisible(p,group)},{"nameKey",name},{"flagKey",flag},{"names",groupVisible(p,name)},{"flags",groupVisible(p,flag)},{"opacity",s.opacity.value_or(1)},{"boundary",s.boundaryVisible.value_or(true)}});
    }
    for(const auto& entry:std::vector<std::pair<std::string,QString>>{{"labels",QStringLiteral("지명")},{"rivers",QStringLiteral("강")},{"lakes",QStringLiteral("호수")},{"languages",QStringLiteral("언어")},{"ethnicities",QStringLiteral("민족")},{"religions",QStringLiteral("종교")},{"genericFeatures",QStringLiteral("기타 객체")}}) {
        const auto found=p.styles.find(entry.first);const auto style=found==p.styles.end()?PresentationStyle{}:found->second;
        rows.append(QVariantMap{{"key",QString::fromStdString(entry.first)},{"title",entry.second},{"visible",groupVisible(p,entry.first)},{"opacity",style.opacity.value_or(1)},{"content",true}});
    }
    return rows;
}
QVariantMap EditorController::distributionDisplay() const {
    const auto& settings=project_.document().presentation.webPresentation.distributionSettings;
    QString selected;
    if(const auto primary=selection_.primary()) {
        if(primary->domain=="distributionLayer")selected=displayText(primary->id);
        else if(primary->domain=="distributionEntry")for(const auto& entry:project_.document().distributionEntries)if(entry.id==primary->id){selected=displayText(entry.layerId);break;}
    }
    return {{"mode",settings.renderMode==DistributionRenderMode::Intensity?"intensity":"dominant"},{"boundaryVisible",settings.boundaryVisible},{"selectedLayerId",selected},{"intensityAvailable",!selected.isEmpty()}};
}
QVariantMap EditorController::hydroDataStatus() const {
    const auto& settings=project_.document().physicalData;if(settings.source.empty()&&!hydroRuntime_.isOpen())return QVariantMap{{"ready",false},{"version",displayText(settings.version)},{"dataset",displayText(settings.dataset)},{"error",physicalActive_+physicalQueued_>0?QStringLiteral("필요한 수계 자료를 받는 중입니다."):(physicalError_.isEmpty()?(worldHydroNotice_.isEmpty()?QStringLiteral("수계 자료가 아직 준비되지 않았습니다."):worldHydroNotice_):physicalError_)}};
    if(hydroRuntime_.isOpen())return QVariantMap{{"ready",true},{"root",displayText(settings.source)},
        {"version",displayText(settings.version)},{"dataset",displayText(settings.dataset)},
        {"viewportLoaded",hydroViewportLoaded()},{"error",QString()}};
    const auto inspected=inspectHydroData(displayText(settings.source));return QVariantMap{{"ready",inspected.ready},{"root",inspected.root},{"version",inspected.version},{"dataset",inspected.dataset},{"error",inspected.error}};
}
QVariantMap EditorController::terrainDataStatus() const {
    return {{"available",terrainProvider_&&terrainProvider_->available()},
            {"version",QStringLiteral("0.12.6")},
            {"missingTiles",terrainMissingTiles_},
            {"activeDownloads",physicalActive_},{"queuedDownloads",physicalQueued_},
            {"error",!physicalError_.isEmpty()?physicalError_:(terrainProvider_?terrainProvider_->error():
                QStringLiteral("Terrain package not installed"))}};
}
void EditorController::executeTerrainResources(const ViewportResourceRequest& request) {
    if(terrainMode_=="none") {
        if(terrainProvider_)terrainProvider_->protectVisible({});
        if(!terrainTiles_.isEmpty()){terrainTiles_.clear();emit terrainChanged();}
        return;
    }
    if(!terrainProvider_||!terrainProvider_->available()||!validMapViewState(request.view))return;
    QVariantList visible;
    int missing=0;
    const auto tileSpecs=terrainProvider_->tilesForView(request.view);
    terrainProvider_->protectVisible(tileSpecs);
    for(const auto& tile:tileSpecs) {
        const auto relative=QString("terrain/v0.12.6/%1/%2-%3.webp")
            .arg(tile.level).arg(tile.column).arg(tile.row);
        if(!physicalAssetReady(relative)){
            ++missing;requestPhysicalAsset(relative);continue;
        }
        const auto source=QUrl::fromLocalFile(physicalAssetPath(relative));
        visible.push_back(QVariantMap{{"source",source},
            {"west",tile.west+tile.worldOffsetDegrees},{"east",tile.east+tile.worldOffsetDegrees},
            {"south",tile.south},{"north",tile.north}});
    }
    if(visible!=terrainTiles_||missing!=terrainMissingTiles_) {
        terrainTiles_=std::move(visible);terrainMissingTiles_=missing;emit terrainChanged();
    }
}
QVariantMap EditorController::hydroProjection() const{return projection_.hydroParameters();}
QVariantMap EditorController::hydroStyle() const {
    const auto& presentation=project_.document().presentation.webPresentation;
    const auto rivers=presentation.styles.find("rivers"),lakes=presentation.styles.find("lakes");
    return {{"riversVisible",groupVisible(presentation,"rivers")},
        {"lakesVisible",groupVisible(presentation,"lakes")},
        {"lakeBoundaryVisible",lakes==presentation.styles.end()?true:lakes->second.boundaryVisible.value_or(true)},
        {"riverOpacity",rivers==presentation.styles.end()?1.:rivers->second.opacity.value_or(1.)},
        {"lakeOpacity",lakes==presentation.styles.end()?1.:lakes->second.opacity.value_or(1.)}};
}
QVariantList EditorController::hiddenHydroIds() const {
    QVariantList result;for(const auto& id:project_.document().physicalData.hiddenHydroIds)
        result.append(QString::fromStdString(id));
    return result;
}
void EditorController::executeHydroResources(const ViewportResourceRequest& request) {
    if(!hydroRuntime_.isOpen()){ensureHydroBootstrap();return;}
    bool missing=false;
    for(const auto& path:hydroRuntime_.requiredAssetPaths(request.hydroWindow))
        if(!physicalAssetReady(path)){missing=true;requestPhysicalAsset(path);}
    if(missing)return;
    hydroRuntime_.requestViewport(request.hydroWindow);
}
bool EditorController::configureHydroData(const QUrl& value) {
    if(hasPendingEdits()||jobBusy()||hasWebImportPreview())return false;
    const auto path=value.isLocalFile()?value.toLocalFile():value.toString();const auto inspected=inspectHydroData(path);if(!inspected.ready){emit errorOccurred(inspected.error);return false;}
    HydroRuntimeProvider verified;QString hydroError;
    if(!verified.open(path,projectInstanceId(),mobileMode_,hydroError)){
        emit errorOccurred(hydroError);return false;
    }
    auto settings=project_.document().physicalData;settings.dataset=inspected.dataset.toStdString();settings.version=inspected.version.toStdString();settings.source=inspected.root.toStdString();
    CommandArguments args;args.action=SetPhysicalData{settings};auto request=CommandProcessor::makeRequest(project_,"physical-data.configure",args);auto prepared=CommandProcessor::prepare(project_,request);if(!prepared.ok()||!prepared.preview)return false;const auto result=CommandProcessor::confirm(project_,*prepared.preview);if(!result.ok())return false;
    noteAppliedImpact(result.impact);
    syncHydroData();
    publish(false);return true;
}
void EditorController::syncHydroData() {
    hydroRuntime_.close(projectInstanceId());
    auto source=displayText(project_.document().physicalData.source);
    if(source.isEmpty()&&!physicalRoot_.isEmpty()) {
        ensureHydroBootstrap();
        const auto candidate=QDir(physicalRoot_).filePath("hydro/v0.13.1/manifest.json");
        if(inspectHydroData(candidate).ready)source=QFileInfo(candidate).absolutePath();
    }
    if(source.isEmpty())return;
    QString error;
    if(!hydroRuntime_.open(source,projectInstanceId(),mobileMode_,error))
        emit errorOccurred(error);
    else {
        hydroRuntime_.setCacheBudget(quality_.profile().hydroCacheBudgetBytes);
        if(const auto selected=selection_.primary();selected&&selected->domain=="hydroBuiltin")
            if(const auto record=hydroRuntime_.recordById(displayText(selected->id)))
                hydroRuntime_.setSelectedLogical(record->logicalFid);
        invalidateViewportResources(ViewportResourceKind::Hydro);
    }
}
bool EditorController::setDistributionDisplay(const QString& mode,bool boundaryVisible) {
    if(mode!="dominant"&&mode!="intensity")return false;
    DistributionSettings settings;settings.renderMode=mode=="intensity"?DistributionRenderMode::Intensity:DistributionRenderMode::Dominant;settings.boundaryVisible=boundaryVisible;
    const auto result=PresentationCommandProcessor::apply(project_,SetDistributionSettings{settings});if(result==PresentationResult::Applied)publishPresentation();return result==PresentationResult::Applied||result==PresentationResult::NoOp;
}
namespace {
pandoeditor::Point geometryCenter(const pandoeditor::Geometry& geometry) {
    double west=180,east=-180,south=90,north=-90;bool any=false;
    const auto add=[&](pandoeditor::Point point) {
        if(!std::isfinite(point.x)||!std::isfinite(point.y))return;
        west=std::min(west,point.x);east=std::max(east,point.x);
        south=std::min(south,point.y);north=std::max(north,point.y);any=true;
    };
    for(const auto point:geometry.points)add(point);
    for(const auto& line:geometry.lines)for(const auto point:line)add(point);
    for(const auto& polygon:geometry.polygons)for(const auto& ring:polygon)
        for(const auto point:ring)add(point);
    return any?pandoeditor::Point{(west+east)/2,(south+north)/2}:pandoeditor::Point{};
}
}

void EditorController::rebuildLabelSources() {
    if(labelSourceRevision_==std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("label source revision overflow");
    const QFontMetricsF metrics(QGuiApplication::font());
    std::vector<MapLabelSource> sources;
    sources.reserve(project_.document().units.size()+project_.document().labels.size());
    labelFlagSources_.clear();
    const auto& document=project_.document();
    for(const auto& unit:document.units) {
        const auto ref=territorialRef(unit.id);
        if(!effectiveMapVisibility(document,ref))continue;
        const auto properties=project_.propertyView(ref);if(!properties)continue;
        const auto resolved=resolvedTerritorialPresentation(document,ref);
        const auto flag=labelFlagSource(ref);
        const bool nameVisible=resolved.nameVisible;
        const bool flagVisible=resolved.flagVisible&&!flag.isEmpty();
        if(!nameVisible&&!flagVisible)continue;
        const auto geometry=document.geometries.get(unit.geometry);if(!geometry)continue;
        auto geographic=geometryCenter(*geometry);
        if(labelAnchors_)if(const auto anchor=labelAnchors_->anchor(
            displayText(ref.id),labelSourceId(ref.id)))geographic=*anchor;

        LabelSettings stored;
        if(const auto found=document.presentation.webPresentation.labelSettings.find(ref);
           found!=document.presentation.webPresentation.labelSettings.end())stored=found->second;
        const auto kind=unit.kind==UnitKind::Country?std::string("country"):std::string("region");
        const auto settings=automaticLabelSettings(kind,stored);
        if(settings.pinned&&settings.manualPosition)geographic=*settings.manualPosition;
        const auto name=QString::fromStdString(properties->displayName);
        MapLabelSource source;
        source.ref=ref;source.text=properties->displayName;source.geographic=geographic;
        source.collisionGroup=settings.collisionGroup;
        source.width=nameVisible?std::max(22.,metrics.horizontalAdvance(name)+16):24.;
        source.height=nameVisible?std::max(19.,metrics.height()):16.;
        source.priority=settings.priority.value_or(0);
        source.minZoom=settings.minZoom.value_or(0);
        source.maxZoom=settings.maxZoom.value_or(std::numeric_limits<double>::infinity());
        source.pinned=settings.pinned;source.nameVisible=nameVisible;source.flagVisible=flagVisible;
        sources.push_back(std::move(source));
        if(flagVisible)labelFlagSources_[ref]=flag;
    }

    for(const auto& label:document.labels) {
        const ObjectRef ref{"label",label.id};
        if(!effectiveMapVisibility(document,ref))continue;
        const auto geometry=document.geometries.get(label.geometry);
        if(!geometry||geometry->points.empty())continue;
        const auto properties=project_.propertyView(ref);if(!properties)continue;
        LabelSettings stored;
        if(const auto found=document.presentation.webPresentation.labelSettings.find(ref);
           found!=document.presentation.webPresentation.labelSettings.end())stored=found->second;
        const auto settings=automaticLabelSettings(label.kind,stored);
        auto geographic=geometry->points.front();
        if(settings.pinned&&settings.manualPosition)geographic=*settings.manualPosition;
        const auto name=QString::fromStdString(properties->displayName);
        MapLabelSource source;
        source.ref=ref;source.text=properties->displayName;source.geographic=geographic;
        source.collisionGroup=settings.collisionGroup;
        source.width=std::max(22.,metrics.horizontalAdvance(name)+16);
        source.height=std::max(19.,metrics.height());
        source.priority=settings.priority.value_or(0);
        source.minZoom=settings.minZoom.value_or(0);
        source.maxZoom=settings.maxZoom.value_or(std::numeric_limits<double>::infinity());
        source.pinned=settings.pinned;source.nameVisible=true;source.flagVisible=false;
        sources.push_back(std::move(source));
    }

    ++labelSourceRevision_;
    labelEngine_.setSources(std::move(sources),labelSourceRevision_);
    labelSourcesDirty_=false;
}

void EditorController::refreshPlacedLabelRows() {
    QVariantList rows;rows.reserve(static_cast<qsizetype>(labelEngine_.placements().size()));
    for(const auto& placement:labelEngine_.placements()) {
        const auto flag=labelFlagSources_.find(placement.ref);
        rows.append(QVariantMap{
            {"ref",objectRefValue(placement.ref)},
            {"x",placement.x},{"y",placement.y},
            {"name",QString::fromStdString(placement.text)},
            {"nameVisible",placement.nameVisible},{"pinned",placement.pinned},
            {"flagSource",flag==labelFlagSources_.end()?QString():flag->second},
            {"flagVisible",placement.flagVisible&&flag!=labelFlagSources_.end()}
        });
    }
    if(rows==placedLabels_)return;
    placedLabels_=std::move(rows);
    emit labelLayoutChanged();
}

void EditorController::reprojectLabelPlacements() {
    if(labelEngine_.placements().empty())return;
    try {
        labelEngine_.reproject(sceneBridge_.viewState());
        refreshPlacedLabelRows();
    } catch(const std::exception& error) {
        emit errorOccurred(QStringLiteral("Label reprojection failed: ")+
                           QString::fromUtf8(error.what()));
    }
}

void EditorController::executeLabelResources(const ViewportResourceRequest& request) {
    if(labelSourcesDirty_)rebuildLabelSources();
    MapLabelLayoutOptions options;
    options.zoom=request.flatZoom;
    options.viewportWidth=request.view.viewportWidth;
    options.viewportHeight=request.view.viewportHeight;
    options.bottomInset=mobileMode_?96.:32.;
    options.collisionPadding=std::max(1.,std::ceil(
        (mobileMode_?5.:3.)*quality_.profile().labelDensity));
    options.maxCandidates=mobileMode_?4096:8192;
    options.maxPlaced=mobileMode_?2048:4096;
    const auto selectedItems=selection_.items();
    const std::set<ObjectRef> selected(selectedItems.begin(),selectedItems.end());
    labelEngine_.layout(request.view,options,selected);
    refreshPlacedLabelRows();
}

bool EditorController::setLabelPinned(const QVariantMap& value,bool pinned,double longitude,double latitude,bool hasPosition) {
    const auto ref=existingObjectRef(value);if(!ref||(ref->domain!="territorial"&&ref->domain!="label"))return false;
    LabelSettings settings;if(const auto found=project_.document().presentation.webPresentation.labelSettings.find(*ref);found!=project_.document().presentation.webPresentation.labelSettings.end())settings=found->second;
    settings.pinned=pinned;if(hasPosition){if(!std::isfinite(longitude)||!std::isfinite(latitude))return false;settings.manualPosition=Point{longitude,latitude};}else if(!pinned)settings.manualPosition.reset();
    const auto result=PresentationCommandProcessor::apply(project_,SetLabelSettings{*ref,settings});if(result==PresentationResult::Applied)publishPresentation();return result==PresentationResult::Applied||result==PresentationResult::NoOp;
}
bool EditorController::resetLabelPosition(const QVariantMap& value) {
    const auto ref=existingObjectRef(value);if(!ref)return false;LabelSettings settings;if(const auto found=project_.document().presentation.webPresentation.labelSettings.find(*ref);found!=project_.document().presentation.webPresentation.labelSettings.end())settings=found->second;settings.pinned=false;settings.manualPosition.reset();
    const auto result=PresentationCommandProcessor::apply(project_,SetLabelSettings{*ref,settings});if(result==PresentationResult::Applied)publishPresentation();return result==PresentationResult::Applied||result==PresentationResult::NoOp;
}
bool EditorController::setLabelMapPosition(const QVariantMap& value,double mapX,double mapY) {
    if(!std::isfinite(mapX)||!std::isfinite(mapY))return false;const auto position=projection_.unproject(mapX,mapY);return setLabelPinned(value,true,position.x,position.y,true);
}
void EditorController::publishPresentation() {
    closeObjectChooser();
    if(hover_&&!objectVisible(*hover_)) {
        hover_.reset();hoverSource_.clear();++hoverRevision_;emit hoverChanged();
    }
    emit presentationChanged();emit visualChanged();emit stateChanged();emit searchChanged();emit dirtyChanged();
    presentationSaveInstance_=project_.instanceId();presentationSaveTimer_.start();
}
QString EditorController::presentationRecoveryPath() const {
    const auto fingerprint=QCryptographicHash::hash(
        QByteArray::fromStdString(project_.document().documentId),QCryptographicHash::Sha256).toHex();
    return storage_.privateProjectPath()+".presentation-recovery."+QString::fromLatin1(fingerprint)+".json";
}
QString EditorController::availablePresentationRecoveryPath() const {
    const QFileInfo privateFile(storage_.privateProjectPath());
    QDir directory(privateFile.absolutePath());
    const auto files=directory.entryInfoList(
        QStringList{privateFile.fileName()+".presentation-recovery.*.json"},QDir::Files,QDir::Time);
    return files.empty()?QString():files.front().absoluteFilePath();
}
bool EditorController::presentationRecoveryAvailable() const { return !availablePresentationRecoveryPath().isEmpty(); }
bool EditorController::flushPresentationRecovery() {
    presentationSaveTimer_.stop();
    if(presentationSaveInstance_!=project_.instanceId())return false;
    try {
        using V=losslessjson::Value;V envelope=V::obj();
        envelope.object["format"]=V::str("pandoeditor-presentation-recovery");
        envelope.object["sourcePath"]=V::str(filePath_.toStdString());
        envelope.object["documentId"]=V::str(project_.document().documentId);
        envelope.object["contentRevision"]=V::num(project_.revision());
        envelope.object["presentationRevision"]=V::num(project_.presentationRevision());
        envelope.object["project"]=losslessjson::parse(projectcodec::encode(project_));
        storage_.write(QUrl::fromLocalFile(presentationRecoveryPath()),envelope.encode());
        emit presentationRecoveryChanged();return true;
    }catch(const std::exception& e){emit errorOccurred(QStringLiteral("표시 설정 복구본을 저장하지 못했습니다: ")+QString::fromUtf8(e.what()));return false;}
}
bool EditorController::discardPresentationRecovery() {
    presentationSaveTimer_.stop();presentationSaveInstance_.clear();
    const auto current=presentationRecoveryPath();
    const auto target=QFileInfo::exists(current)?current:availablePresentationRecoveryPath();
    const bool ok=target.isEmpty()||QFile::remove(target);
    if(ok)emit presentationRecoveryChanged();else emit errorOccurred(QStringLiteral("복구본을 삭제하지 못했습니다."));return ok;
}
bool EditorController::discardOwnPresentationRecovery() {
    const auto path=presentationRecoveryPath();
    if(!QFileInfo::exists(path))return true;
    if(!QFile::remove(path)){emit errorOccurred(QStringLiteral("복구본을 삭제하지 못했습니다."));return false;}
    emit presentationRecoveryChanged();return true;
}
bool EditorController::restorePresentationRecovery() {
    try {
        const auto recovery=availablePresentationRecoveryPath();
        losslessjson::require(!recovery.isEmpty(),"RECOVERY_NOT_FOUND");
        auto root=losslessjson::parse(storage_.read(QUrl::fromLocalFile(recovery)));
        losslessjson::require(root.object.at("format").string=="pandoeditor-presentation-recovery","INVALID_RECOVERY");
        losslessjson::require(root.object.at("contentRevision").kind==losslessjson::Value::Number,"INVALID_RECOVERY");
        losslessjson::require(root.object.at("presentationRevision").kind==losslessjson::Value::Number,"INVALID_RECOVERY");
        auto bytes=root.object.at("project").encode();auto candidate=projectcodec::decode(bytes);
        losslessjson::require(candidate.documentId==root.object.at("documentId").string,"INVALID_RECOVERY_DOCUMENT");
        replaceFromBytes(bytes,true,QString::fromStdString(root.object.at("sourcePath").string));
        return true;
    }catch(const std::exception& e){emit errorOccurred(QStringLiteral("복구본을 열지 못했습니다: ")+QString::fromUtf8(e.what()));return false;}
}
bool EditorController::setPresentationVisibility(const QString& key,bool visible) {
    auto r=PresentationCommandProcessor::apply(project_,SetPresentationVisibility{key.toStdString(),visible});if(r==PresentationResult::Applied)publishPresentation();return r==PresentationResult::Applied||r==PresentationResult::NoOp;
}
bool EditorController::setPresentationOpacity(const QString& group,double opacity) {
    PresentationStyle s;s.opacity=opacity;auto r=PresentationCommandProcessor::apply(project_,PatchGroupPresentation{group.toStdString(),s});if(r==PresentationResult::Applied)publishPresentation();return r==PresentationResult::Applied||r==PresentationResult::NoOp;
}
bool EditorController::setPresentationBoundary(const QString& group,bool visible) {
    PresentationStyle s;s.boundaryVisible=visible;auto r=PresentationCommandProcessor::apply(project_,PatchGroupPresentation{group.toStdString(),s});if(r==PresentationResult::Applied)publishPresentation();return r==PresentationResult::Applied||r==PresentationResult::NoOp;
}
bool EditorController::toggleSelectionVisibility() {
    auto r=PresentationCommandProcessor::apply(project_,SetBatchVisibility{selection_.items(),{}});if(r==PresentationResult::Applied)publishPresentation();return r==PresentationResult::Applied||r==PresentationResult::NoOp;
}
bool EditorController::setScopedObjectVisibility(const QVariantMap& value,bool visible) {
    auto ref=existingObjectRef(value);if(!ref)return false;
    auto group=ref->domain=="territorial"?territorialGroup(project_.document().units.at(project_.index().objects.at(*ref)).kind):contentGroup(project_.document(),*ref);
    auto r=PresentationCommandProcessor::apply(project_,SetScopedVisibility{group,{*ref},visible});if(r==PresentationResult::Applied)publishPresentation();return r==PresentationResult::Applied||r==PresentationResult::NoOp;
}

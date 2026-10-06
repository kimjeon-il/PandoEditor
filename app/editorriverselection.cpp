#include "editorcontroller.h"
#include "hydroassetreader.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <algorithm>
using namespace pandoeditor;
namespace {
void encodeGeometry(QDataStream& stream,const Geometry& g) {
    stream<<QString::fromStdString(g.type)<<quint64(g.polygons.size());
    for(const auto& polygon:g.polygons){stream<<quint64(polygon.size());for(const auto& ring:polygon){stream<<quint64(ring.size());for(const auto p:ring)stream<<p.x<<p.y;}}
    stream<<quint64(g.lines.size());for(const auto& line:g.lines){stream<<quint64(line.size());for(const auto p:line)stream<<p.x<<p.y;}
}
QByteArray requestKey(const RiverPartitionRequest& request,const HydroSourceIdentity& identity) {
    QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);stream.setVersion(QDataStream::Qt_6_8);
    stream<<identity.dataset<<identity.version<<identity.indexSha256<<quint64(identity.generation);
    stream<<QJsonDocument(request.configOverrides).toJson(QJsonDocument::Compact)<<request.algorithmRevision.value_or(QStringLiteral("pinned-default"));
    stream<<quint64(request.donors.size());for(const auto& d:request.donors){stream<<d.countryId;encodeGeometry(stream,d.geometry);}
    stream<<quint64(request.signatureEdits.size());for(const auto& e:request.signatureEdits){stream<<e.id<<e.sourceFeatureId.value_or(QString());encodeGeometry(stream,e.geometry);}
    return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
}
std::vector<TerritorySelectionComponent> selectionComponents(RiverPartitionResult& result,const TerritorySelectionState& state) {
    std::vector<TerritorySelectionComponent> rows;
    for(auto& cell:result.presentationComponents){
        const auto base=std::find_if(state.components.begin(),state.components.end(),[&](const auto& b){return b.countryId==cell.countryId.toStdString()&&b.polygonIndex==std::size_t(cell.polygonIndex)&&b.sourcePolygonIndex==std::size_t(cell.sourcePolygonIndex);});
        if(base==state.components.end())throw std::runtime_error("RIVER_COMPONENT_BASE_MISMATCH");
        TerritorySelectionComponent row;row.countryId=base->countryId;row.countryName=base->countryName;row.polygonIndex=base->polygonIndex;row.sourcePolygonIndex=base->sourcePolygonIndex;row.key=cell.key.toStdString();row.componentKey=cell.componentKey.toStdString();row.geometry=std::move(cell.geometry);row.partitionKind=cell.isRiver?"river":"original";row.usesRiverBoundary=cell.isRiver;row.provenanceJson=QJsonDocument(cell.attributes).toJson(QJsonDocument::Compact).toStdString();rows.push_back(std::move(row));
    }return rows;
}
}
bool EditorController::promotePhysicalAsset(const QString& path) {
    if(!physicalStore_)return false;const auto it=physicalAssets_.constFind(path);if(it==physicalAssets_.cend())return false;
    const auto existing=physicalStore_->resolveExisting(*it);if(existing.isEmpty())return false;
    if(QFileInfo(existing).absoluteFilePath()==QFileInfo(physicalStore_->cachePath(*it)).absoluteFilePath())return true;
    QFile input(existing);return input.open(QIODevice::ReadOnly)&&physicalStore_->installVerified(*it,input.readAll());
}
void EditorController::cancelRiverPreparation() {
    if(!geometryEdit_)return;auto& edit=*geometryEdit_;++edit.riverEpoch;edit.riverPreparation.reset();
}
bool EditorController::geometryToggleRiverBoundaries(bool enabled) {
    if(!geometryEdit_||!geometryEdit_->territorySelection||geometryEdit_->stage!="selection"||geometryEdit_->applying||geometryEdit_->sourceChange)return false;
    auto& edit=*geometryEdit_;if(edit.territorySelection->state().activePhase!=TerritorySelectionPhase::Components||edit.territorySelection->state().methodChangeConfirmation)return false;
    if(!edit.territorySelection->toggleRiverBoundaries(enabled))return false;
    edit.riverWarning.clear();scheduleTerritorySelection();return true;
}
bool EditorController::geometryRetryRiverPartitions() {
    if(!geometryEdit_||!geometryEdit_->territorySelection||geometryEdit_->stage!="selection"||geometryEdit_->applying||geometryEdit_->sourceChange||geometryEdit_->territorySelection->state().methodChangeConfirmation)return false;
    auto& edit=*geometryEdit_;const auto& state=edit.territorySelection->state();if(!state.useRiverBoundaries||state.activePhase!=TerritorySelectionPhase::Components)return false;
    edit.riverRetry=true;edit.territorySelection->setRiverStatus(TerritoryRiverStatus::Pending);scheduleTerritorySelection();return true;
}
void EditorController::prepareRiverPartitions() {
    if(!geometryEdit_||!geometryEdit_->territorySelection)return;auto& edit=*geometryEdit_;const auto& state=edit.territorySelection->state();
    if(edit.stage!="selection"||edit.applying||edit.job||!edit.territorySelection->derivedReady()||!state.useRiverBoundaries||state.activePhase!=TerritorySelectionPhase::Components||state.riverStatus!=TerritoryRiverStatus::Pending||edit.riverPreparation)return;
    GeometryEditSession::RiverPreparation pending;pending.epoch=++edit.riverEpoch;pending.revision=state.revision;pending.sourceChoice=QString::fromStdString(project_.document().physicalData.source);pending.bypassCache=edit.riverRetry;edit.riverRetry=false;
    for(const auto& feature:state.componentFeatures){pending.request.donors.push_back({QString::fromStdString(feature.source.ref.id),feature.source.geometry,RiverRevisionMode::LiveCoordinates,{}});const auto bounds=riverPartitionQueryBounds(feature.source.geometry);pending.bounds.insert(pending.bounds.end(),bounds.begin(),bounds.end());}
    for(const auto& component:state.components)pending.request.components.push_back({QString::fromStdString(component.key),QString::fromStdString(component.countryId),QString::fromStdString(component.componentKey),int(component.polygonIndex),int(component.sourcePolygonIndex),component.geometry,{}});
    for(const auto& row:project_.document().hydro)if(row.kind=="river")if(const auto geometry=project_.document().geometries.get(row.geometry))pending.request.signatureEdits.push_back({QString::fromStdString(row.id),*geometry,row.sourceFeatureId?std::optional<QString>(QString::fromStdString(*row.sourceFeatureId)):std::nullopt});
    edit.riverPreparation=std::move(pending);continueRiverPreparation(edit.generation,edit.riverEpoch);
}
void EditorController::riverAssetFinished(const QString& path,bool success) {
    if(!geometryEdit_||!geometryEdit_->riverPreparation)return;auto& request=*geometryEdit_->riverPreparation;
    if(!request.pendingAssets.remove(path))return;if(success&&request.sourceChoice.isEmpty())success=promotePhysicalAsset(path);if(!success)request.failedAssets.insert(path);
    const auto generation=geometryEdit_->generation,epoch=request.epoch;QTimer::singleShot(0,this,[this,generation,epoch]{continueRiverPreparation(generation,epoch);});
}
void EditorController::continueRiverPreparation(std::uint64_t generation,std::uint64_t epoch) {
    if(!geometryEdit_||!geometryEdit_->territorySelection||geometryEdit_->generation!=generation||geometryEdit_->riverEpoch!=epoch||!geometryEdit_->riverPreparation)return;
    const auto stillOwned=[&]{return geometryEdit_&&geometryEdit_->territorySelection&&geometryEdit_->generation==generation&&geometryEdit_->riverEpoch==epoch&&geometryEdit_->riverPreparation.has_value();};
    auto& edit=*geometryEdit_;auto& pending=*edit.riverPreparation;const auto& state=edit.territorySelection->state();
    const auto fail=[&](TerritoryRiverStatus status,const QString& detail){edit.riverPreparation.reset();edit.territorySelection->setRiverStatus(status,detail.toStdString());emit geometryEditChanged();};
    if(!edit.base.matches(project_)||edit.stage!="selection"||!state.useRiverBoundaries||state.activePhase!=TerritorySelectionPhase::Components||state.revision!=pending.revision||pending.sourceChoice!=QString::fromStdString(project_.document().physicalData.source)){cancelRiverPreparation();if(edit.territorySelection->state().useRiverBoundaries)edit.territorySelection->setRiverStatus(TerritoryRiverStatus::Error,"하천 선택의 기준이 바뀌었습니다. 다시 시도해 주세요.");emit geometryEditChanged();return;}
    if(pending.dispatched||!pending.pendingAssets.empty())return;
    if(!hydroRuntime_.isOpen()){
        if(pending.sourceChoice.isEmpty()){
            const QStringList bootstrap{"hydro/v0.13.1/manifest.json","hydro/v0.13.0/index.bin.gz","hydro/v0.13.1/metadata-core.json.gz"};
            if(!physicalStore_){fail(TerritoryRiverStatus::SourceError,"하천 자료 목록을 사용할 수 없습니다.");return;}
            for(const auto& path:bootstrap)if(!physicalAssets_.contains(path)){fail(TerritoryRiverStatus::SourceError,"하천 기본 자료가 목록에 없습니다.");return;}
            QStringList requests;
            for(const auto& path:bootstrap)if(!promotePhysicalAsset(path)&&!pending.failedAssets.contains(path)){pending.pendingAssets.insert(path);requests.append(path);}
            for(const auto& path:requests){requestPhysicalAsset(path);if(!stillOwned())return;}
            if(!pending.pendingAssets.empty())return;
            if(!pending.failedAssets.empty()){fail(TerritoryRiverStatus::SourceError,QStringLiteral("하천 기본 자료를 불러오지 못했습니다. 다시 시도해 주세요."));return;}
        }
        auto source=pending.sourceChoice;if(source.isEmpty())source=QDir(physicalRoot_).filePath("hydro/v0.13.1/manifest.json");QString error;
        pending.openingSource=true;
        const auto opened=hydroRuntime_.open(source,projectInstanceId(),mobileMode_,error);
        // open publishes synchronously; observers may cancel or replace this
        // session, including a nested project/source open before it returns.
        if(!stillOwned())return;
        pending.openingSource=false;
        if(!opened){fail(TerritoryRiverStatus::SourceError,error);return;}
        hydroRuntime_.setCacheBudget(quality_.profile().hydroCacheBudgetBytes);
    }
    const auto identity=hydroRuntime_.sourceIdentity();if(!identity){fail(TerritoryRiverStatus::SourceError,"하천 자료가 준비되지 않았습니다.");return;}
    if(pending.identity&&!(*pending.identity==*identity)){fail(TerritoryRiverStatus::Error,"하천 자료가 바뀌었습니다. 다시 시도해 주세요.");return;}
    pending.identity=identity;
    if(riverCacheProject_!=projectInstanceId()||!riverCacheSource_||!(*riverCacheSource_==*identity)){riverCache_.clear();riverCacheProject_=projectInstanceId();riverCacheSource_=identity;}
    pending.cacheKey=requestKey(pending.request,*identity);
    if(pending.bypassCache)riverCache_.erase(std::remove_if(riverCache_.begin(),riverCache_.end(),[&](const auto& e){return e.key==pending.cacheKey;}),riverCache_.end());
    std::shared_ptr<const RiverSelectionPreparationResult> cached;
    for(const auto& entry:riverCache_)if(entry.key==pending.cacheKey&&!pending.bypassCache){cached=entry.value;break;}
    if(!cached&&!pending.assetsRegistered){
        pending.assetsRegistered=true;
        if(pending.sourceChoice.isEmpty()){
            QStringList requests;
            try{for(const auto& requirement:hydroRuntime_.riverPartitionAssetRequirements(pending.bounds)){
                const auto it=physicalAssets_.constFind(requirement.physicalRelativePath);
                if(it==physicalAssets_.cend()||it->sha256!=requirement.asset.sha256||it->bytes!=requirement.asset.bytes||QFileInfo(physicalStore_->cachePath(*it)).absoluteFilePath()!=QFileInfo(requirement.asset.path).absoluteFilePath()){fail(TerritoryRiverStatus::SourceError,"하천 자료 경로 또는 검증 정보가 일치하지 않습니다.");return;}
                if(!promotePhysicalAsset(requirement.physicalRelativePath)){pending.pendingAssets.insert(requirement.physicalRelativePath);requests.append(requirement.physicalRelativePath);}
            }}catch(const std::exception& error){fail(TerritoryRiverStatus::SourceError,QString::fromUtf8(error.what()));return;}
            for(const auto& path:requests){requestPhysicalAsset(path);if(!stillOwned())return;}
            if(!pending.pendingAssets.empty())return;
        }
    }
    // Completed downloads must exist at the provider's own manifest-resolved paths.
    // The source job performs per-logical verification and preserves partial errors.
    auto source=hydroRuntime_.riverPartitionSourceJob(pending.bounds,pending.request.signatureEdits);if(!source){fail(TerritoryRiverStatus::SourceError,"하천 자료를 읽을 수 없습니다.");return;}
    auto request=pending.request;request.liveHydroRevisionPrefix=identity->version+":"+identity->indexSha256+":";
    const auto revision=pending.revision;const auto key=pending.cacheKey;const auto frozen=*identity;pending.dispatched=true;edit.riverCacheHit=bool(cached);if(!cached){++edit.riverSourceDispatches;}
    edit.job=jobs_->submitGeometry(edit.base,"territorial:river-selection",[source,request,cached](const ProjectSnapshot&,const JobToken& token)->GeometryJobResult{
        RiverSelectionPreparationResult result;
        if(cached){result=*cached;const auto composition=composeRiverPartitionComponents(request.components,result.partition.presentationCandidates,result.partition.donors,[&]{return token.cancelled();});result.partition.status=composition.status;result.partition.detail=composition.detail;result.partition.presentationComponents=composition.components;auto presentation=QJsonDocument::fromJson(result.partition.presentationJson).object();presentation["composed"]=QJsonDocument::fromJson(composition.json).object();result.partition.presentationJson=QJsonDocument(presentation).toJson(QJsonDocument::Compact);return result;}
        result.source=source(token);if(result.source.status!=HydroRiverSourceStatus::Ready)return result;
        result.kernelInvoked=true;auto input=request;input.riverFeatures=result.source.features;result.partition=calculateRiverPartitions(input,[&]{return token.cancelled();});return result;
    },[this,generation,epoch,revision,key,frozen,cached](std::uint64_t id,JobDisposition disposition,GeometryJobResult value){
        if(!geometryEdit_||!geometryEdit_->territorySelection||geometryEdit_->generation!=generation||geometryEdit_->riverEpoch!=epoch||!geometryEdit_->riverPreparation||!geometryEdit_->job||geometryEdit_->job->id()!=id)return;
        auto& edit=*geometryEdit_;const auto currentIdentity=hydroRuntime_.sourceIdentity();const auto sourceChoice=edit.riverPreparation->sourceChoice;edit.job.reset();edit.riverPreparation.reset();
        if(disposition!=JobDisposition::Accepted||!edit.base.matches(project_)||edit.stage!="selection"||!edit.territorySelection->state().useRiverBoundaries||edit.territorySelection->state().revision!=revision||!currentIdentity||!(*currentIdentity==frozen)||sourceChoice!=QString::fromStdString(project_.document().physicalData.source)){if(edit.territorySelection->state().useRiverBoundaries)edit.territorySelection->setRiverStatus(TerritoryRiverStatus::Error,"하천 선택의 기준이 바뀌었습니다. 다시 시도해 주세요.");emit geometryEditChanged();return;}
        auto result=std::get_if<RiverSelectionPreparationResult>(&value);
        if(!result){const auto failure=std::get_if<GeometryJobFailure>(&value);edit.territorySelection->setRiverStatus(TerritoryRiverStatus::Error,failure?failure->detail:"RIVER_CALCULATION_FAILED");emit geometryEditChanged();return;}
        if(!cached&&result->kernelInvoked)++edit.riverKernelDispatches;
        if(result->source.status!=HydroRiverSourceStatus::Ready){edit.territorySelection->setRiverStatus(result->source.status==HydroRiverSourceStatus::SourceError?TerritoryRiverStatus::SourceError:TerritoryRiverStatus::Error,result->source.detail.toStdString());emit geometryEditChanged();return;}
        if(!result->partition.succeeded()){edit.territorySelection->setRiverStatus(TerritoryRiverStatus::Error,result->partition.detail.toStdString());emit geometryEditChanged();return;}
        try {auto rows=selectionComponents(result->partition,edit.territorySelection->state());if(!edit.territorySelection->installRiverComponents(std::move(rows),key.toHex().toStdString())){emit geometryEditChanged();return;}}
        catch(const std::exception& error){edit.territorySelection->setRiverStatus(TerritoryRiverStatus::Error,error.what());emit geometryEditChanged();return;}
        edit.riverWarning=result->source.diagnostics.failedRiverLoads?QStringLiteral("일부 하천 자료를 읽지 못했습니다 (%1개). 다시 시도할 수 있습니다.").arg(result->source.diagnostics.failedRiverLoads):QString();
        if(!result->partition.invalidDonorIds.empty())edit.riverWarning+=QStringLiteral(" 일부 제공국은 하천 경계를 계산할 수 없어 제외했습니다.");
        if(!cached){riverCache_.erase(std::remove_if(riverCache_.begin(),riverCache_.end(),[&](const auto& e){return e.key==key;}),riverCache_.end());RiverSelectionPreparationResult saved;saved.source.status=result->source.status;saved.source.identity=result->source.identity;saved.source.diagnostics=result->source.diagnostics;saved.source.discoveredLogicalIds=result->source.discoveredLogicalIds;saved.source.failedLogicalIds=result->source.failedLogicalIds;saved.source.failures=result->source.failures;saved.source.detail=result->source.detail;saved.partition.status=result->partition.status;saved.partition.presentationCandidates=std::move(result->partition.presentationCandidates);saved.partition.donors=std::move(result->partition.donors);saved.partition.diagnostics=result->partition.diagnostics;saved.partition.invalidDonorIds=result->partition.invalidDonorIds;saved.partition.donorRevisionStrings=result->partition.donorRevisionStrings;saved.partition.editedRiverSignature=result->partition.editedRiverSignature;const auto savedPresentation=QJsonDocument::fromJson(result->partition.presentationJson).object();saved.partition.presentationJson=QJsonDocument(QJsonObject{{"candidates",savedPresentation["candidates"]}}).toJson(QJsonDocument::Compact);riverCache_.push_back({key,frozen,std::make_shared<const RiverSelectionPreparationResult>(std::move(saved))});if(riverCache_.size()>8)riverCache_.pop_front();}
        scheduleTerritorySelection();emit geometryEditChanged();
    });emit geometryEditChanged();
}

QVariantMap EditorController::riverSelectionObservation() const {
    if(!geometryEdit_||!geometryEdit_->territorySelection)return {};
    const auto encode=[](const Geometry& g){QVariantList polygons;for(const auto& p:g.polygons){QVariantList rings;for(const auto& r:p){QVariantList xy;for(const auto pt:r)xy.append(QVariant(QVariantList{pt.x,pt.y}));rings.append(QVariant(xy));}polygons.append(QVariant(rings));}return QVariantMap{{"type",QString::fromStdString(g.type)},{"coordinates",g.type=="Polygon"?polygons.value(0):QVariant(polygons)}};};
    const auto& selection=*geometryEdit_->territorySelection;const auto& state=selection.state();QVariantList cells,parts,contexts,features,snapshots;
    for(const auto& c:selection.activeComponents())cells.append(QVariantMap{{"key",QString::fromStdString(c.key)},{"countryId",QString::fromStdString(c.countryId)},{"componentKey",QString::fromStdString(c.componentKey)},{"polygonIndex",int(c.polygonIndex)},{"sourcePolygonIndex",int(c.sourcePolygonIndex)},{"partitionKind",QString::fromStdString(c.partitionKind)},{"geometry",encode(c.geometry)},{"provenance",QString::fromStdString(c.provenanceJson)}});
    for(const auto& part:state.parts) {
        const auto method=part.method==TerritorySelectionMethod::Line?"line":part.method==TerritorySelectionMethod::Polygon?"polygon":part.method==TerritorySelectionMethod::Components?"components":"";
        QVariantMap row{{"id",QString::fromStdString(part.id)},{"method",method},{"geometry",encode(part.geometry)}};
        if(part.component){const auto& c=*part.component;row["component"]=QVariantMap{{"key",QString::fromStdString(c.key)},{"countryId",QString::fromStdString(c.countryId)},{"componentKey",QString::fromStdString(c.componentKey)},{"polygonIndex",int(c.polygonIndex)},{"sourcePolygonIndex",int(c.sourcePolygonIndex)},{"partitionKind",QString::fromStdString(c.partitionKind)},{"geometry",encode(c.geometry)},{"provenance",QString::fromStdString(c.provenanceJson)}};}
        parts.append(row);
    }
    for(const auto& feature:state.componentFeatures){QVariantList indices;for(const auto i:feature.sourcePolygonIndices)indices.append(int(i));features.append(QVariantMap{{"id",QString::fromStdString(feature.source.ref.id)},{"geometry",encode(feature.source.geometry)},{"sourcePolygonIndices",indices}});}
    if(selection.derivedReady())for(const auto& context:selection.riverSliverContext()){QVariantList unselected;for(const auto& g:context.unselectedGeometries)unselected.append(encode(g));contexts.append(QVariantMap{{"donorId",QString::fromStdString(context.donorId)},{"polygonIndex",int(context.polygonIndex)},{"unselectedGeometries",unselected}});}
    for(const auto& snapshot:state.componentSnapshots){QVariantList items;for(const auto& c:snapshot.items)items.append(QVariantMap{{"key",QString::fromStdString(c.key)},{"countryId",QString::fromStdString(c.countryId)},{"componentKey",QString::fromStdString(c.componentKey)},{"polygonIndex",int(c.polygonIndex)},{"sourcePolygonIndex",int(c.sourcePolygonIndex)},{"partitionKind",QString::fromStdString(c.partitionKind)},{"geometry",encode(c.geometry)},{"provenance",QString::fromStdString(c.provenanceJson)}});snapshots.append(QVariantMap{{"items",items}});}
    QVariantMap result{{"state",territorySelectionState()},{"components",cells},{"parts",parts},{"componentFeatures",features},{"riverSliverContext",contexts},{"componentSnapshots",snapshots}};
    for(const auto& entry:riverCache_)if(entry.key.toHex().toStdString()==state.riverPreparationKey){QVariantList failedIds;for(const auto id:entry.value->source.failedLogicalIds)failedIds.append(qulonglong(id));result["sourceDiagnostics"]=QVariantMap{{"loadedRivers",qulonglong(entry.value->source.diagnostics.loadedRivers)},{"failedLogicalIds",failedIds},{"indexSha256",entry.identity.indexSha256},{"version",entry.identity.version},{"generation",qulonglong(entry.identity.generation)}};result["donorRevisionStrings"]=entry.value->partition.donorRevisionStrings;result["editedRiverSignature"]=entry.value->partition.editedRiverSignature;result["hydroRevision"]=entry.value->partition.diagnostics.hydroRevision.toVariant();break;}
    // Owned diagnostic snapshots preserve raw coordinates; callers must not
    // reconstruct editing geometry from rounded screen/SVG representations.
    QVariantList candidates,inputLine;
    for(const auto& candidate:state.candidates){QVariantMap row{{"id",QString::fromStdString(candidate.id)},{"geometry",encode(candidate.geometry)}};if(candidate.area)row["area"]=*candidate.area;candidates.append(row);}
    for(const auto point:geometryEdit_->lineDraft)inputLine.append(QVariant(QVariantList{point.x,point.y}));
    result["candidates"]=candidates;result["inputLine"]=inputLine;
    if(geometryEdit_->splitIntent){
        result["combinedGeometry"]=QVariant();result["remainingGeometry"]=QVariant();result["splitPreview"]=QVariant();result["splitPreviewPresent"]=false;result["splitPreviewReceiptPresent"]=false;result["currentGeometry"]=state.currentGeometry?QVariant(encode(*state.currentGeometry)):QVariant();
    }
    if(state.remainingGeometry)result["remainingGeometry"]=encode(*state.remainingGeometry);
    if(geometryEdit_->splitPreview&&geometryEdit_->previewSelectionRevision==state.revision){
        const auto& receipt=*geometryEdit_->splitPreview;QVariantList rows;
        // A failed calculation remains an owned diagnostic receipt, but did
        // not produce a preview. Completed blocking previews still exist.
        result["splitPreviewReceiptPresent"]=true;
        result["splitPreviewPresent"]=receipt.status==GeometryOperationStatus::Completed;
        const auto status=receipt.status==GeometryOperationStatus::Completed?"completed":receipt.status==GeometryOperationStatus::Empty?"empty":receipt.status==GeometryOperationStatus::Cancelled?"cancelled":"failed";
        for(const auto& row:receipt.rows){QVariantMap item{{"owner",objectRefValue(row.owner)},{"before",encode(row.before)}};if(row.after){item["after"]=encode(*row.after);const auto parent=receipt.parentIds.find(row.owner);if(parent!=receipt.parentIds.end())item["parentId"]=QString::fromStdString(parent->second);}else item["after"]=QVariant();rows.append(item);}
        result["splitPreview"]=QVariantMap{{"status",status},{"ok",receipt.ok()},{"blocking",receipt.blocking()},{"detail",QString::fromStdString(receipt.detail)},{"transferredGeometry",encode(receipt.transferredGeometry)},{"remainingGeometry",encode(receipt.remainingGeometry)},{"rows",rows}};
    }
    if(state.combinedGeometry)result["combinedGeometry"]=encode(*state.combinedGeometry);
    if(state.workingSourceGeometry)result["workingSourceGeometry"]=encode(*state.workingSourceGeometry);
    if(geometryEdit_->territoryPreview&&geometryEdit_->previewSelectionRevision==state.revision&&geometryEdit_->territoryPreview->ok())result["transferredGeometry"]=encode(geometryEdit_->territoryPreview->transferredGeometry);
    return result;
}

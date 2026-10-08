#include "editorcontroller.h"
#include "historicallibraryloader.h"
#include "historicaltransaction.h"
#include "territorialcatalogadapter.h"
#include "defaultflagresolver.h"
#include "geometrycalculator.h"
#include <pandoeditor/project.h>
#include <pandoeditor/temporal.h>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QFileInfo>
#include <QFile>
#include <QCryptographicHash>
#include <QCollator>
#include <QLocale>
#include <algorithm>
#include <functional>
#include <limits>

static void initializeCatalogResources(){Q_INIT_RESOURCE(territorial_catalog);}
namespace {
QString qs(const std::string& value){return QString::fromStdString(value);}
QString label(const pandoeditor::HistoricalEntity& entity) {
    const auto ko=entity.displayNames.find("ko");
    return qs(ko==entity.displayNames.end()?entity.canonicalName:ko->second);
}
QString kindName(const std::string& catalogKind) {
    return catalogKind=="country"?QStringLiteral("국가"):catalogKind=="subunit"?QStringLiteral("하위단위"):QStringLiteral("지방");
}
QString opt(const std::optional<std::string>& value){return value?qs(*value):QString();}
QString catalogName(const QJsonObject& entity) {
    const auto names=entity.value("names").toObject();
    for(const auto& key:{"ko","en"})if(!names.value(key).toString().isEmpty())return names.value(key).toString();
    return names.isEmpty()?QString():names.begin().value().toString();
}
QVariantMap catalogPreview(const pandoeditor::TerritorialLibraryCatalog& catalog,const QString& id,const QString& date) {
    const auto selected=catalog.preview(id,date);const auto entity=selected.value("entity").toObject(),version=selected.value("version").toObject();
    auto result=entity.toVariantMap();result["id"]=id;result["name"]=catalogName(entity);
    result["geometryVersionId"]=version.value("versionId").toVariant();result["selectedVersionId"]=version.value("versionId").toVariant();
    result["certainty"]=version.value("certainty").toVariant();result["datePrecision"]=version.value("datePrecision").toVariant();
    result["sourceId"]=version.value("sourceId").toVariant();
    result["parentLibraryId"]=entity.value("parentEntityId").toVariant();
    result["mode"]=entity.value("instantiation").toObject().value("mode").toVariant();
    bool hasChildren=false;for(const auto& candidate:catalog.entries())if(candidate.toObject().value("parentEntityId")==id){hasChildren=true;break;}
    result["hasChildren"]=hasChildren;QVariantList versions;
    for(const auto& raw:entity.value("geometryVersions").toArray()) {
        auto row=raw.toObject().toVariantMap();row["id"]=row.value("versionId");
        row["label"]=QStringLiteral("경계 %1 · %2 ~ %3").arg(versions.size()+1).arg(row.value("validFrom").toString()).arg(row.value("validTo").toString());versions.push_back(row);
    }
    result["versions"]=versions;
    const auto geometry=pandoeditor::territorialCatalogGeometry(version.value("geometry").toObject());
    double minX=std::numeric_limits<double>::infinity(),minY=minX,maxX=-minX,maxY=-minX;
    for(const auto& polygon:geometry.polygons)for(const auto& ring:polygon)for(const auto& point:ring) {
        minX=std::min(minX,point.x);maxX=std::max(maxX,point.x);minY=std::min(minY,point.y);maxY=std::max(maxY,point.y);
    }
    const double width=std::max(maxX-minX,1e-9),height=std::max(maxY-minY,1e-9);QVariantList polygons;
    for(const auto& polygon:geometry.polygons) {
        QVariantList rings;for(const auto& ring:polygon) {
            QVariantList points;for(const auto& point:ring)points.push_back(QVariantMap{{"x",(point.x-minX)/width},{"y",1-(point.y-minY)/height}});
            rings.push_back(points);
        }polygons.push_back(rings);
    }
    result["polygons"]=polygons;return result;
}
}

void EditorController::initializeTerritorialCatalog() {
    initializeCatalogResources();
    QFile file(QStringLiteral(":/territorial-library-v2/index.json"));
    if(!file.open(QIODevice::ReadOnly)){historicalError_=QStringLiteral("PL-LIB-READ: bundled index missing");historicalStage_=QStringLiteral("error");return;}
    installTerritorialCatalog(file.readAll(),QByteArrayLiteral("63c072095fd6c95034365f6d7f9d4e4ff99f71684890bffc8e678fe741b4334c"),
        QStringLiteral(":/territorial-library-v2"),QStringLiteral("Territorial Library v2"),true);
}
bool EditorController::installTerritorialCatalog(const QByteArray& bytes,const QByteArray& pin,const QString& root,const QString& name,bool bundled) {
    try {
        // Validate before replacing any published source or cancelling its work.
        auto candidate=std::make_shared<pandoeditor::TerritorialLibraryCatalog>(bytes,pin,root);
        cancelHistoricalAdd();++historicalCatalogGeneration_;historicalCatalog_=std::move(candidate);
        historicalCatalogSha256_=pin;historicalCatalogRoot_=root;historicalSource_.reset();historicalLibrary_.reset();
        historicalCatalogName_=name;historicalCatalogBundled_=bundled;historicalPreviewCache_.clear();
        historicalSelectedId_.clear();historicalVersionId_.clear();historicalReferenceDate_.clear();
        historicalFilter_.referenceDate.clear();historicalStage_=QStringLiteral("ready");historicalError_.clear();
        emit historicalChanged();return true;
    }catch(const std::exception& error){historicalError_=QString::fromUtf8(error.what());emit historicalChanged();return false;}
}
QVariantMap EditorController::historicalCatalogStatus() const {
    int versions=0;if(historicalCatalog_)for(const auto& entity:historicalCatalog_->entries())versions+=entity.toObject().value("geometryVersions").toArray().size();
    return {{"indexSha256",historicalCatalogSha256_},{"entities",historicalCatalog_?historicalCatalog_->entries().size():0},
        {"lineages",historicalCatalog_?historicalCatalog_->lineages().size():0},{"geometryVersions",versions},
        {"loadedEntities",historicalCatalog_?static_cast<qulonglong>(historicalCatalog_->loadedEntityCount()):0},
        {"generation",historicalCatalogGeneration_},{"pendingJobs",historicalPendingJobs_},{"previewLoading",historicalPreviewLoading_}};
}
QVariantList EditorController::historicalLineages() const {
    if(!historicalCatalog_)return {};
    try {return historicalCatalog_->search(qs(historicalFilter_.query),qs(historicalFilter_.referenceDate)).toVariantList();}
    catch(const std::exception&){return {};}
}
QString normalizedCatalogDate(const QString& source) {
    if(source.isEmpty())return {};
    const auto date=pandoeditor::parseTemporal(source.toStdString());
    return QString::fromStdString(date.canonical)
        +(date.precision=="year"?QStringLiteral("-01-01"):date.precision=="month"?QStringLiteral("-01"):QString());
}
QVariantList EditorController::historicalEvents(const QString& query) const {
    return historicalCatalog_?historicalCatalog_->events(query).toVariantList():QVariantList{};
}

QVariantList EditorController::historicalResults() const {
    QVariantList rows;
    if(historicalCatalog_) {
        for(const auto& group:historicalLineages()) {
            const auto lineage=group.toMap();for(const auto& raw:lineage.value("entities").toList()) {
                auto row=raw.toMap();row["id"]=row.value("entityId");row["name"]=catalogName(QJsonObject::fromVariantMap(row));
                row["lineageNames"]=lineage.value("names");
                row["lineageName"]=catalogName(QJsonObject{{"names",QJsonObject::fromVariantMap(lineage.value("names").toMap())}});
                row["kind"]=row.value("entityKind");row["type"]=row.value("entityKind")=="regional"?QStringLiteral("지방"):QStringLiteral("국가/하위단위");
                row["parentId"]=row.value("parentEntityId");
                row["flagSource"]=resolveDefaultFlagSource(row.value("metadata").toMap().value("defaultFlagDataUrl").toString());rows.push_back(row);
            }
        }return rows;
    }
    if(!historicalLibrary_)return rows;
    for(const auto* entity:historicalLibrary_->search(historicalFilter_)) {
        rows.push_back(QVariantMap{{"id",qs(entity->libraryId)},{"name",label(*entity)},
            {"canonicalName",qs(entity->canonicalName)},{"type",kindName(entity->catalogKind)},
            {"validFrom",opt(entity->validity.from)},{"validTo",opt(entity->validity.to)},
            {"region",qs(entity->geographicRegion)},{"kind",qs(entity->catalogKind)},{"parentId",qs(entity->parentLibraryId)},
            {"flagSource",QJsonDocument::fromJson(qs(entity->metadata).toUtf8()).object().value("defaultFlagDataUrl").toString()}});
    }
    return rows;
}
QVariantList EditorController::historicalRegionOptions() const {
    QVariantList rows;std::set<std::string> regions;
    if(historicalLibrary_)for(const auto* entity:historicalLibrary_->search({}))
        if(!entity->geographicRegion.empty())regions.insert(entity->geographicRegion);
    for(const auto& region:regions)rows.append(QVariantMap{{"id",qs(region)},{"name",qs(region)}});
    return rows;
}
QVariantList EditorController::historicalSnapshots() const {
    QVariantList rows;
    if(historicalCatalog_) {
        for(const auto& raw:historicalCatalog_->snapshots()) {auto row=raw.toObject().toVariantMap();row["count"]=row.value("entityRefs").toList().size();rows.push_back(row);}return rows;
    }
    if(historicalLibrary_)for(const auto* snapshot:historicalLibrary_->listSnapshots())
        rows.push_back(QVariantMap{{"id",qs(snapshot->id)},{"name",qs(snapshot->name)},
            {"referenceDate",opt(snapshot->referenceDate)},{"count",static_cast<int>(snapshot->entityRefs.size())}});
    return rows;
}
QVariantList EditorController::historicalCountries() const {
    QVariantList result;
    for(const auto& unit:project_.document().units)if(pandoeditor::isRootGeneral(project_.document(),unit))
        result.push_back(QVariantMap{{"id",qs(unit.id)},{"name",qs(unit.name)}});
    return result;
}
QVariantList EditorController::historicalParents(const QString& countryId) const {
    QVariantList result;
    if(countryId.isEmpty())return result;
    const auto& document=project_.document();const auto rootId=countryId.toStdString();
    const auto root=std::find_if(document.units.begin(),document.units.end(),[&](const auto& unit){return unit.id==rootId;});
    if(root==document.units.end()||!pandoeditor::isRootGeneral(document,*root))return result;
    const auto name=[](const pandoeditor::TerritorialUnit& unit){return qs(unit.name.empty()?unit.id:unit.name);};
    std::map<std::string,std::vector<const pandoeditor::TerritorialUnit*>> children;
    for(const auto& unit:document.units)if(unit.kind==pandoeditor::UnitKind::General) {
        const auto& parent=pandoeditor::staticParentRelation(document,unit.id).parentId;
        if(!parent.empty())children[parent].push_back(&unit);
    }
    QCollator compare{QLocale{QLocale::Korean}};
    for(auto& [parent,rows]:children)std::sort(rows.begin(),rows.end(),[&](const auto* a,const auto* b){
        const int order=compare.compare(name(*a),name(*b));
        return order!=0?order<0:compare.compare(qs(a->id),qs(b->id))<0;
    });
    result.push_back(QVariantMap{{"id",countryId},{"name",name(*root)}});
    std::set<std::string> seen{rootId};
    const std::function<void(const std::string&,int)> visit=[&](const auto& parent,int depth){
        const auto found=children.find(parent);if(found==children.end())return;
        for(const auto* unit:found->second)if(seen.insert(unit->id).second) {
            result.push_back(QVariantMap{{"id",qs(unit->id)},{"name",QString(depth,QChar(0x3000))+name(*unit)}});
            visit(unit->id,depth+1);
        }
    };
    visit(rootId,1);
    return result;
}
QVariantMap EditorController::historicalPreview() const {
    if(historicalCatalog_)return historicalPreviewCache_;
    if(!historicalLibrary_||historicalSelectedId_.isEmpty())return {};
    const auto* entity=historicalLibrary_->get(historicalSelectedId_.toStdString());
    if(!entity)return {};
    bool hasChildren=false;for(const auto* candidate:historicalLibrary_->search({}))if(candidate->parentLibraryId==entity->libraryId){hasChildren=true;break;}
    QVariantList versions;
    for(const auto& version:entity->geometryVersions)
        versions.push_back(QVariantMap{{"id",qs(version.id)},{"label",QStringLiteral("경계 %1 · %2 ~ %3").arg(versions.size()+1).arg(opt(version.validity.from).isEmpty()?QStringLiteral("시작 미정"):opt(version.validity.from)).arg(opt(version.validity.to).isEmpty()?QStringLiteral("종료 미정"):opt(version.validity.to))},
            {"validFrom",opt(version.validity.from)},{"validTo",opt(version.validity.to)},
            {"certainty",qs(version.certainty)},{"datePrecision",qs(version.datePrecision)},
            {"sourceId",qs(version.sourceId)}});
    QVariantMap result{{"id",historicalSelectedId_},{"name",label(*entity)},
        {"canonicalName",qs(entity->canonicalName)},{"type",kindName(entity->catalogKind)},
        {"validFrom",opt(entity->validity.from)},{"validTo",opt(entity->validity.to)},
        {"region",qs(entity->geographicRegion)},{"versions",versions},{"hasChildren",hasChildren},
        {"selectedVersionId",historicalVersionId_},{"mode",qs(entity->instantiation.mode)},
        {"parentLibraryId",qs(entity->parentLibraryId)},
        {"sovereignLibraryId",qs(entity->sovereignLibraryId)},
        {"metadata",qs(entity->metadata)},{"sourceInfo",qs(entity->sourceInfo)}};
    const auto metadata=QJsonDocument::fromJson(qs(entity->metadata).toUtf8()).object();
    const auto source=QJsonDocument::fromJson(qs(entity->sourceInfo).toUtf8()).object();
    result["approximateGeometry"]=metadata.value("approximateGeometry").toBool();
    result["sourceTitle"]=source.value("title").toString();
    result["sourceLicense"]=source.value("license").toString();
    try {
        auto selected=historicalLibrary_->instantiate(entity->libraryId,
            historicalReferenceDate_.toStdString(),historicalVersionId_.toStdString());
        result["geometryVersionId"]=qs(selected.geometryVersionId);
        result["certainty"]=qs(selected.certainty);
        result["datePrecision"]=qs(selected.datePrecision);
        result["sourceId"]=qs(selected.sourceId);
        result["partial"]=selected.partial;
        QVariantList missing;for(const auto& id:selected.missingSourceIds)missing.push_back(qs(id));
        result["missingSources"]=missing;
        QVariantList polygons;
        double minX=std::numeric_limits<double>::infinity(),minY=minX;
        double maxX=-minX,maxY=-minX;
        for(const auto& polygon:selected.geometry.polygons)for(const auto& ring:polygon)for(const auto& p:ring) {
            minX=std::min(minX,p.x);minY=std::min(minY,p.y);
            maxX=std::max(maxX,p.x);maxY=std::max(maxY,p.y);
        }
        const double width=std::max(maxX-minX,1e-9),height=std::max(maxY-minY,1e-9);
        for(const auto& polygon:selected.geometry.polygons) {
            QVariantList rings;
            for(const auto& ring:polygon) {
                QVariantList points;
                for(const auto& p:ring)points.push_back(QVariantMap{{"x",(p.x-minX)/width},{"y",1-(p.y-minY)/height}});
                rings.push_back(points);
            }
            polygons.push_back(rings);
        }
        result["polygons"]=polygons;
    }catch(const std::exception& error){result["error"]=QString::fromUtf8(error.what());}
    return result;
}
bool EditorController::installHistoricalSource(const QByteArray& bytes,const QString& name,bool bundled) {
    try {
        auto candidate=std::make_shared<pandoeditor::HistoricalSource>(
            pandoeditor::parseHistoricalLibrarySource(bytes));
        cancelHistoricalAdd();historicalCatalog_.reset();++historicalCatalogGeneration_;historicalCatalogSha256_.clear();historicalPreviewCache_.clear();
        historicalSource_=std::move(candidate);
        historicalCatalogName_=name;historicalCatalogBundled_=bundled;
        return refreshHistoricalCatalog();
    }catch(const std::exception& error) {
        historicalError_=QString::fromUtf8(error.what());historicalStage_=QStringLiteral("error");
        emit historicalChanged();return false;
    }
}
bool EditorController::refreshHistoricalCatalog() {
    if(historicalCatalog_) {cancelHistoricalAdd();historicalPreviewCache_.clear();historicalSelectedId_.clear();historicalVersionId_.clear();return true;}
    if(!historicalSource_)return false;
    try {
        auto materialized=pandoeditor::materializeHistoricalSource(*historicalSource_,
            [this](const std::string& id)->std::optional<pandoeditor::Geometry> {
                const auto it=project_.index().objects.find(pandoeditor::territorialRef(id));
                if(it==project_.index().objects.end())return std::nullopt;
                const auto& unit=project_.document().units.at(it->second);
                if(unit.kind!=pandoeditor::UnitKind::General)return std::nullopt;
                return *project_.document().geometries.get(pandoeditor::staticGeometryBinding(project_.document(),unit.id).geometryRef);
            },pandoeditor::makeTransactionGeometryCalculator());
        historicalLibrary_=std::make_shared<pandoeditor::HistoricalLibrary>(std::move(materialized.library));
        historicalSelectedId_.clear();historicalVersionId_.clear();
        historicalError_.clear();historicalStage_=QStringLiteral("ready");
        if(!materialized.missingEntityIds.empty())
            historicalError_=QStringLiteral("원본 자료가 없어 %1개 항목을 사용할 수 없습니다.").arg(materialized.missingEntityIds.size());
        emit historicalChanged();return true;
    }catch(const std::exception& error) {
        historicalError_=QString::fromUtf8(error.what());historicalStage_=QStringLiteral("error");
        emit historicalChanged();return false;
    }
}
bool EditorController::loadHistoricalLibrary(const QUrl& url) {
    try {
        const auto name=url.isLocalFile()?QFileInfo(url.toLocalFile()).fileName():url.fileName();
        const auto bytes=storage_.read(url);QJsonParseError parseError;const auto document=QJsonDocument::fromJson(bytes,&parseError);
        if(parseError.error!=QJsonParseError::NoError||!document.isObject())throw std::invalid_argument("PL-LIB-JSON: invalid catalog object");
        const auto object=document.object();
        if(object.contains("lineages")) {
            if(!url.isLocalFile())throw std::invalid_argument("PL-LIB-READ: local catalog index required");
            return installTerritorialCatalog(bytes,QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex(),QFileInfo(url.toLocalFile()).absolutePath(),QStringLiteral("로컬: ")+name,false);
        }
        return installHistoricalSource(bytes,QStringLiteral("로컬: ")+name,false);
    }catch(const std::exception& error) {
        historicalError_=QString::fromUtf8(error.what());historicalStage_=QStringLiteral("error");
        emit historicalChanged();return false;
    }
}
void EditorController::searchHistorical(const QString& query,const QString& type,
    const QString& status,const QString& referenceDate,const QString& region) {
    if(historicalCatalog_) {
        try {const auto date=normalizedCatalogDate(referenceDate);
            if(date!=historicalReferenceDate_) {
                cancelHistoricalAdd();historicalPreviewCache_.clear();historicalVersionId_.clear();historicalReferenceDate_=date;
            }
            historicalFilter_={query.toStdString(),{},pandoeditor::HistoricalStatus::All,date.toStdString(),{}};historicalError_.clear();
        }catch(const std::exception& error){
            cancelHistoricalAdd();historicalPreviewCache_.clear();historicalSelectedId_.clear();historicalVersionId_.clear();
            historicalFilter_.query=query.toStdString();historicalFilter_.referenceDate=referenceDate.toStdString();
            historicalError_=QString::fromUtf8(error.what());
        }
        emit historicalChanged();return;
    }
    historicalFilter_={query.toStdString(),type.toStdString(),
        status==QStringLiteral("current")?pandoeditor::HistoricalStatus::Current:
        status==QStringLiteral("past")?pandoeditor::HistoricalStatus::Past:pandoeditor::HistoricalStatus::All,
        referenceDate.toStdString(),region.toStdString()};
    try {if(!referenceDate.isEmpty())pandoeditor::parseTemporal(referenceDate.toStdString());historicalError_.clear();}
    catch(const std::exception& error){historicalError_=QString::fromUtf8(error.what());historicalFilter_.referenceDate.clear();}
    emit historicalChanged();
}
void EditorController::selectHistorical(const QString& id,const QString& versionId,const QString& referenceDate) {
    cancelHistoricalAdd();
    if(historicalCatalog_) {
        historicalSelectedId_=id;historicalReferenceDate_.clear();historicalPreviewCache_.clear();
        try {
            const auto entity=historicalCatalog_->entry(id);
            const auto resolution=historicalCatalog_->resolveSelection(id,normalizedCatalogDate(referenceDate));
            historicalReferenceDate_=resolution.value("referenceDate").toString();
            const auto selected=resolution.value("geometryVersionId").toString();
            historicalVersionId_=selected;historicalPreviewCache_={{"id",id},{"name",catalogName(entity)},{"selectedVersionId",selected},{"loading",false}};
            if(selected.isEmpty())throw std::invalid_argument("PL-LIB-GEOMETRY-GAP: no boundary at selected date");
            if(!versionId.isEmpty()&&versionId!=selected)throw std::invalid_argument("PL-LIB-GEOMETRY-GAP: requested version is not selected at date");
            const auto catalog=historicalCatalog_;const auto generation=historicalCatalogGeneration_,token=historicalSession_;
            const auto base=project_.snapshot();const auto date=historicalReferenceDate_;
            auto cancelled=std::make_shared<std::atomic_bool>(false);historicalCatalogCancel_=cancelled;
            historicalPreviewLoading_=true;historicalPreviewCache_["loading"]=true;++historicalPendingJobs_;
            auto* watcher=new QFutureWatcher<QVariantMap>(this);
            connect(watcher,&QFutureWatcher<QVariantMap>::finished,this,[this,watcher,catalog,generation,token,base,cancelled]() {
                watcher->deleteLater();--historicalPendingJobs_;
                if(cancelled->load()||token!=historicalSession_||generation!=historicalCatalogGeneration_||catalog!=historicalCatalog_||!base.matches(project_)) {
                    if(token==historicalSession_&&generation==historicalCatalogGeneration_&&catalog==historicalCatalog_) {
                        historicalPreviewLoading_=false;historicalPreviewCache_["loading"]=false;
                        historicalPreviewCache_["error"]=QStringLiteral("PL-LIB-STALE: project changed");
                    }emit historicalChanged();return;
                }
                historicalPreviewLoading_=false;
                try {historicalPreviewCache_=watcher->result();historicalPreviewCache_["loading"]=false;historicalError_.clear();}
                catch(const std::exception& error){historicalPreviewCache_["error"]=QString::fromUtf8(error.what());historicalPreviewCache_["loading"]=false;historicalError_=QString::fromUtf8(error.what());}
                catch(...){historicalPreviewCache_["error"]=QStringLiteral("PL-LIB-PREVIEW: worker failed");historicalPreviewCache_["loading"]=false;}
                emit historicalChanged();
            });
            watcher->setFuture(QtConcurrent::run([catalog,id,date,cancelled]() {
                if(cancelled->load())throw std::runtime_error("CANCELLED");auto result=catalogPreview(*catalog,id,date);
                if(cancelled->load())throw std::runtime_error("CANCELLED");return result;
            }));
        }catch(const std::exception& error){historicalPreviewCache_["error"]=QString::fromUtf8(error.what());historicalError_=QString::fromUtf8(error.what());}
        emit historicalChanged();return;
    }
    if(id!=historicalSelectedId_)historicalVersionId_.clear();
    historicalSelectedId_=id;
    if(!versionId.isEmpty())historicalVersionId_=versionId;
    historicalReferenceDate_=referenceDate;
    emit historicalChanged();
}
bool EditorController::prepareHistoricalAdd(const QVariantMap& options) {
    cancelHistoricalAdd();
    if(historicalCatalog_) {
        try {
            auto date=normalizedCatalogDate(options.value("referenceDate").toString());
            const auto selected=options.value("libraryId",historicalSelectedId_).toString();
            QStringList roots;const auto snapshotId=options.value("snapshotId").toString();
            if(snapshotId.isEmpty())roots.push_back(selected);
            else {
                for(const auto& raw:historicalCatalog_->snapshots()) {
                    const auto snapshot=raw.toObject();if(snapshot.value("id")==snapshotId) {
                        if(!options.contains("referenceDate")||options.value("referenceDate").toString().isEmpty())date=normalizedCatalogDate(snapshot.value("referenceDate").toString());
                        for(const auto& id:snapshot.value("entityRefs").toArray())roots.push_back(id.toString());
                    }
                }
                if(roots.isEmpty())throw std::invalid_argument("PL-LIB-SNAPSHOT: missing snapshot");
            }
            if(date.isEmpty()&&snapshotId.isEmpty()) {
                const auto resolution=historicalCatalog_->resolveSelection(selected,{});
                date=resolution.value("referenceDate").toString();
            }
            if(date.isEmpty())throw std::invalid_argument("PL-LIB-DATE: select a reference date");
            const auto depth=options.value("childDepth",QStringLiteral("none")).toString();
            const auto ids=historicalCatalog_->entityRefsWithChildren(roots,date,depth);const auto ownership=options.value("ownership").toMap();
            for(const auto& id:ids) {
                const auto entity=historicalCatalog_->entry(id);const auto parent=entity.value("parentEntityId").toString();
                if(historicalCatalog_->selectedVersionId(id,date).isEmpty())throw std::invalid_argument("PL-LIB-GEOMETRY-GAP: no boundary at selected date");
                if(entity.value("entityKind")=="general"&&!parent.isEmpty()&&!ids.contains(parent)&&ownership.value(id).toMap().isEmpty())
                    historicalOwnershipNeeded_.push_back(QVariantMap{{"id",id},{"entityId",id},{"name",catalogName(entity)},{"parentId",parent}});
            }
            if(!historicalOwnershipNeeded_.isEmpty()){historicalStage_=QStringLiteral("ownership");historicalError_=QStringLiteral("소속 국가와 상위 단위를 선택하세요.");emit historicalChanged();return false;}
            const auto catalog=historicalCatalog_;const auto base=project_.snapshot();const auto generation=historicalCatalogGeneration_,token=historicalSession_;
            const auto cachedVersion=selected==historicalSelectedId_&&date==historicalReferenceDate_?historicalVersionId_:QString();
            const auto version=options.value("geometryVersionId",cachedVersion).toString();
            if(date!=historicalReferenceDate_){historicalPreviewCache_.clear();historicalVersionId_.clear();}
            historicalReferenceDate_=date;
            auto cancelled=std::make_shared<std::atomic_bool>(false);historicalCatalogCancel_=cancelled;
            ++historicalPendingJobs_;historicalError_.clear();historicalStage_=QStringLiteral("preparing");emit historicalChanged();
            auto* watcher=new QFutureWatcher<pandoeditor::HistoricalInstantiationPlan>(this);
            connect(watcher,&QFutureWatcher<pandoeditor::HistoricalInstantiationPlan>::finished,this,[this,watcher,catalog,base,generation,token,cancelled]() {
                watcher->deleteLater();--historicalPendingJobs_;
                if(cancelled->load()||token!=historicalSession_||generation!=historicalCatalogGeneration_||catalog!=historicalCatalog_||!base.matches(project_)) {
                    if(token==historicalSession_&&generation==historicalCatalogGeneration_&&catalog==historicalCatalog_) {
                        historicalStage_=QStringLiteral("ready");historicalError_=QStringLiteral("PL-LIB-STALE: project changed");
                    }emit historicalChanged();return;
                }
                try {
                    auto plan=watcher->result();pandoeditor::CommandArguments args;args.action=plan;
                    auto result=pandoeditor::CommandProcessor::prepare(project_,pandoeditor::CommandProcessor::makeRequest(project_,"historical.instantiate",std::move(args)));
                    if(!result.ok()||!result.preview)throw std::runtime_error(result.detail.empty()?"PL-LIB-ADD: prepare failed":result.detail);
                    historicalCommandPreview_=std::move(result.preview);QVariantList added,adjusted,updated;
                    for(const auto& item:plan.additions)added.push_back(qs(item.selection.name));
                    const auto display=[&](const pandoeditor::ObjectRef& ref){const auto view=project_.propertyView(ref);return view?qs(view->displayName):qs(ref.id);};
                    for(const auto& patch:plan.territoryReplacements)adjusted.push_back(display(patch.owner));
                    for(const auto& [donor,target]:plan.territoryTransfers)adjusted.push_back(display(donor)+QStringLiteral(" → ")+display(target));
                    for(const auto& [id,name]:plan.countryNameUpdates)updated.push_back(display(pandoeditor::territorialRef(id))+QStringLiteral(" → ")+qs(name));
                    historicalImpact_={{"added",added},{"adjusted",adjusted},{"updated",updated},{"summary",QStringLiteral("%1개 추가 · 영토 %2개 조정 · 국가 이름 %3개 변경").arg(added.size()).arg(adjusted.size()).arg(updated.size())}};
                    historicalStage_=QStringLiteral("impact");historicalError_.clear();
                }catch(const std::exception& error){historicalStage_=QStringLiteral("ready");historicalError_=QString::fromUtf8(error.what());}
                catch(...){historicalStage_=QStringLiteral("ready");historicalError_=QStringLiteral("PL-LIB-ADD: worker failed");}
                emit historicalChanged();
            });
            watcher->setFuture(QtConcurrent::run([catalog,base,roots,date,depth,ownership,selected,version,cancelled]() {
                const pandoeditor::GeometryCancellation check=[cancelled](){return cancelled->load();};
                if(check())throw std::runtime_error("CANCELLED");
                auto additions=pandoeditor::territorialCatalogSelections(*catalog,base,roots,date,depth,ownership,selected,version);
                return pandoeditor::prepareHistoricalTransaction(base,std::move(additions),pandoeditor::makeTransactionGeometryCalculator(),check);
            }));return true;
        }catch(const std::exception& error){historicalStage_=QStringLiteral("ready");historicalError_=QString::fromUtf8(error.what());emit historicalChanged();return false;}
    }
    if(!historicalLibrary_) {historicalError_=QStringLiteral("역사 라이브러리 파일을 먼저 선택하세요.");emit historicalChanged();return false;}
    try {
        const auto snapshotId=options.value("snapshotId").toString().toStdString();
        const auto libraryId=options.value("libraryId",historicalSelectedId_).toString().toStdString();
        auto referenceDate=options.value("referenceDate",historicalReferenceDate_).toString().toStdString();
        std::vector<std::string> ids;
        if(!snapshotId.empty()) {
            const auto* snapshot=historicalLibrary_->getSnapshot(snapshotId);
            if(!snapshot)throw std::invalid_argument("INVALID_LIBRARY: missing snapshot");
            ids=snapshot->entityRefs;
            if(referenceDate.empty())referenceDate=snapshot->referenceDate.value_or("");
        } else ids={libraryId};
        ids=historicalLibrary_->entityRefsWithChildren(ids,
            options.value("childDepth",QStringLiteral("none")).toString().toStdString());
        std::vector<pandoeditor::HistoricalAddRequest> requests;
        const auto ownership=options.value("ownership").toMap();
        historicalOwnershipNeeded_.clear();
        for(const auto& id:ids) {
            const auto* entity=historicalLibrary_->get(id);
            if(!entity)throw std::invalid_argument("INVALID_LIBRARY: missing selected entity");
            const auto choice=ownership.value(qs(id)).toMap();
            pandoeditor::HistoricalAddRequest request;
            request.libraryId=id;request.referenceDate=referenceDate;
            request.geometryVersionId=choice.value("geometryVersionId",
                id==libraryId?options.value("geometryVersionId",historicalVersionId_):QVariant()).toString().toStdString();
            request.approvePartial=choice.value("approvePartial",options.value("approvePartial")).toBool();
            request.asIndependentCountry=choice.value("mode").toString()==QStringLiteral("country");
            request.countryName=choice.value("name").toString().toStdString();
            if(!choice.value("countryId").toString().isEmpty())throw std::invalid_argument("UNSUPPORTED_POLITICAL_RELATION");
            auto parent=choice.value("parentId").toString().toStdString();
            if(entity->type==pandoeditor::UnitKind::General&&!request.asIndependentCountry) {
                if(parent.empty() && !entity->parentLibraryId.empty() &&
                   std::find(ids.begin(),ids.end(),entity->parentLibraryId)!=ids.end())
                    parent=entity->parentLibraryId;
                if(parent.empty()&&!entity->parentLibraryId.empty()) {
                    historicalOwnershipNeeded_.push_back(QVariantMap{{"id",qs(id)},
                        {"name",label(*entity)},
                        {"parentId",qs(parent)}});
                    continue;
                }
                if(!parent.empty())request.parent=pandoeditor::territorialRef(parent);
            }
            requests.push_back(std::move(request));
        }
        if(!historicalOwnershipNeeded_.isEmpty()) {
            historicalStage_=QStringLiteral("ownership");
            historicalError_=QStringLiteral("누락된 역사 단위의 소속을 선택하세요.");
            emit historicalChanged();return false;
        }
        const auto base=project_.snapshot();
        const auto catalog=historicalLibrary_;
        const auto token=historicalSession_;
        historicalError_.clear();historicalStage_=QStringLiteral("preparing");emit historicalChanged();
        auto* watcher=new QFutureWatcher<pandoeditor::HistoricalInstantiationPlan>(this);
        connect(watcher,&QFutureWatcher<pandoeditor::HistoricalInstantiationPlan>::finished,this,
            [this,watcher,token]() {
                watcher->deleteLater();
                if(token!=historicalSession_)return;
                try {
                    auto plan=watcher->result();
                    pandoeditor::CommandArguments args;args.action=plan;
                    auto result=pandoeditor::CommandProcessor::prepare(project_,
                        pandoeditor::CommandProcessor::makeRequest(project_,"historical.instantiate",std::move(args)));
                    if(!result.ok()||!result.preview)throw std::runtime_error(result.detail.empty()?"INVALID_LIBRARY: prepare failed":result.detail);
                    historicalCommandPreview_=std::move(result.preview);
                    auto display=[&](const pandoeditor::ObjectRef& ref){const auto view=project_.propertyView(ref);return view?qs(view->displayName):QStringLiteral("새 객체");};
                    QVariantList added,adjusted,updated;
                    for(const auto& item:plan.additions)added.push_back(qs(item.selection.name));
                    for(const auto& patch:plan.territoryReplacements)adjusted.push_back(display(patch.owner));
                    for(const auto& [donor,target]:plan.territoryTransfers)
                        adjusted.push_back(display(donor)+QStringLiteral(" → ")+display(target));
                    for(const auto& [id,name]:plan.countryNameUpdates)updated.push_back(display(pandoeditor::territorialRef(id))+QStringLiteral(" → ")+qs(name));
                    historicalImpact_={{"added",added},{"adjusted",adjusted},{"updated",updated},
                        {"summary",QStringLiteral("%1개 추가 · 영토 %2개 조정 · 국가 이름 %3개 변경")
                            .arg(added.size()).arg(adjusted.size()).arg(updated.size())}};
                    historicalStage_=QStringLiteral("impact");historicalError_.clear();
                }catch(const std::exception& error){historicalStage_=QStringLiteral("ready");historicalError_=QString::fromUtf8(error.what());}
                emit historicalChanged();
            });
        watcher->setFuture(QtConcurrent::run([base,catalog,requests=std::move(requests)]() {
            return pandoeditor::prepareHistoricalTransaction(base,*catalog,requests,pandoeditor::makeTransactionGeometryCalculator());
        }));
        return true;
    }catch(const std::exception& error){historicalStage_=QStringLiteral("ready");historicalError_=QString::fromUtf8(error.what());emit historicalChanged();return false;}
}
bool EditorController::confirmHistoricalAdd(qulonglong token) {
    if(token!=historicalSession_||historicalStage_!=QStringLiteral("impact")||!historicalCommandPreview_)return false;
    auto result=pandoeditor::CommandProcessor::confirm(project_,*historicalCommandPreview_);
    historicalCommandPreview_.reset();++historicalSession_;
    historicalImpact_.clear();historicalStage_=QStringLiteral("ready");
    if(!result.changed()) {historicalError_=QString::fromStdString(result.detail);emit historicalChanged();return false;}
    noteAppliedImpact(result.impact);
    historicalError_.clear();publish(false);emit historicalChanged();return true;
}
void EditorController::cancelHistoricalAdd() {
    if(historicalCatalogCancel_)historicalCatalogCancel_->store(true);
    historicalCatalogCancel_.reset();historicalPreviewLoading_=false;
    if(historicalPreviewCache_.contains("loading"))historicalPreviewCache_["loading"]=false;
    ++historicalSession_;
    if(historicalCommandPreview_)pandoeditor::CommandProcessor::cancel(*historicalCommandPreview_);
    historicalCommandPreview_.reset();historicalImpact_.clear();
    historicalOwnershipNeeded_.clear();
    historicalStage_=(historicalLibrary_||historicalCatalog_)?QStringLiteral("ready"):QStringLiteral("unloaded");
    emit historicalChanged();
}

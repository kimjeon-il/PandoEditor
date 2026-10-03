#include "editorcontroller.h"
#include "historicallibraryloader.h"
#include "historicaltransaction.h"
#include "geometrycalculator.h"
#include <pandoeditor/project.h>
#include <pandoeditor/temporal.h>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QFileInfo>
#include <algorithm>
#include <limits>

namespace {
QString qs(const std::string& value){return QString::fromStdString(value);}
QString label(const pandoeditor::HistoricalEntity& entity) {
    const auto ko=entity.displayNames.find("ko");
    return qs(ko==entity.displayNames.end()?entity.canonicalName:ko->second);
}
QString kindName(pandoeditor::UnitKind type) {
    switch(type){case pandoeditor::UnitKind::Country:return QStringLiteral("국가");
        case pandoeditor::UnitKind::Subunit:return QStringLiteral("하위 단위");
        default:return QStringLiteral("지방");}
}
QString opt(const std::optional<std::string>& value){return value?qs(*value):QString();}
}

QVariantList EditorController::historicalResults() const {
    QVariantList rows;
    if(!historicalLibrary_)return rows;
    for(const auto* entity:historicalLibrary_->search(historicalFilter_)) {
        rows.push_back(QVariantMap{{"id",qs(entity->libraryId)},{"name",label(*entity)},
            {"canonicalName",qs(entity->canonicalName)},{"type",kindName(entity->type)},
            {"validFrom",opt(entity->validity.from)},{"validTo",opt(entity->validity.to)},
            {"region",qs(entity->geographicRegion)},{"kind",entity->type==pandoeditor::UnitKind::Country?QStringLiteral("country"):entity->type==pandoeditor::UnitKind::Subunit?QStringLiteral("subunit"):QStringLiteral("region")},{"parentId",qs(entity->parentLibraryId)},
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
    if(historicalLibrary_)for(const auto* snapshot:historicalLibrary_->listSnapshots())
        rows.push_back(QVariantMap{{"id",qs(snapshot->id)},{"name",qs(snapshot->name)},
            {"referenceDate",opt(snapshot->referenceDate)},{"count",static_cast<int>(snapshot->entityRefs.size())}});
    return rows;
}
QVariantList EditorController::historicalCountries() const {
    QVariantList result;
    for(const auto& unit:project_.document().units)if(unit.kind==pandoeditor::UnitKind::Country)
        result.push_back(QVariantMap{{"id",qs(unit.id)},{"name",qs(unit.name)}});
    return result;
}
QVariantList EditorController::historicalParents(const QString& countryId) const {
    QVariantList result;
    if(countryId.isEmpty())return result;
    for(const auto& unit:project_.document().units) {
        if(unit.id==countryId.toStdString()&&unit.kind==pandoeditor::UnitKind::Country)
            result.push_back(QVariantMap{{"id",qs(unit.id)},{"name",qs(unit.name)}});
        else if(unit.kind==pandoeditor::UnitKind::Subunit) {
            for(const auto& relation:project_.document().relations)
                if(!relation.dated&&relation.unit==pandoeditor::territorialRef(unit.id)&&relation.sovereign&&
                   relation.sovereign->id==countryId.toStdString()) {
                    result.push_back(QVariantMap{{"id",qs(unit.id)},{"name",qs(unit.name)}});break;
                }
        }
    }
    return result;
}
QVariantMap EditorController::historicalPreview() const {
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
        {"canonicalName",qs(entity->canonicalName)},{"type",kindName(entity->type)},
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
    cancelHistoricalAdd();
    try {
        historicalSource_=std::make_shared<pandoeditor::HistoricalSource>(
            pandoeditor::parseHistoricalLibrarySource(bytes));
        historicalCatalogName_=name;historicalCatalogBundled_=bundled;
        return refreshHistoricalCatalog();
    }catch(const std::exception& error) {
        historicalSource_.reset();historicalLibrary_.reset();historicalCatalogName_.clear();
        historicalError_=QString::fromUtf8(error.what());historicalStage_=QStringLiteral("error");
        emit historicalChanged();return false;
    }
}
bool EditorController::refreshHistoricalCatalog() {
    if(!historicalSource_)return false;
    try {
        auto materialized=pandoeditor::materializeHistoricalSource(*historicalSource_,
            [this](const std::string& id)->std::optional<pandoeditor::Geometry> {
                const auto it=project_.index().objects.find(pandoeditor::territorialRef(id));
                if(it==project_.index().objects.end())return std::nullopt;
                const auto& unit=project_.document().units.at(it->second);
                if(unit.kind!=pandoeditor::UnitKind::Country)return std::nullopt;
                return *project_.document().geometries.get(unit.geometry);
            },pandoeditor::calculateGeometry);
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
        return installHistoricalSource(storage_.read(url),QStringLiteral("로컬: ")+name,false);
    }catch(const std::exception& error) {
        historicalError_=QString::fromUtf8(error.what());historicalStage_=QStringLiteral("error");
        emit historicalChanged();return false;
    }
}
void EditorController::searchHistorical(const QString& query,const QString& type,
    const QString& status,const QString& referenceDate,const QString& region) {
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
    if(id!=historicalSelectedId_)historicalVersionId_.clear();
    historicalSelectedId_=id;
    if(!versionId.isEmpty())historicalVersionId_=versionId;
    historicalReferenceDate_=referenceDate;
    emit historicalChanged();
}
bool EditorController::prepareHistoricalAdd(const QVariantMap& options) {
    cancelHistoricalAdd();
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
            auto sovereign=choice.value("countryId").toString().toStdString();
            auto parent=choice.value("parentId").toString().toStdString();
            if(entity->type==pandoeditor::UnitKind::Subunit&&!request.asIndependentCountry) {
                if(sovereign.empty() && !entity->sovereignLibraryId.empty() &&
                   std::find(ids.begin(),ids.end(),entity->sovereignLibraryId)!=ids.end())
                    sovereign=entity->sovereignLibraryId;
                if(parent.empty() && !entity->parentLibraryId.empty() &&
                   std::find(ids.begin(),ids.end(),entity->parentLibraryId)!=ids.end())
                    parent=entity->parentLibraryId;
                if(sovereign.empty()||parent.empty()) {
                    historicalOwnershipNeeded_.push_back(QVariantMap{{"id",qs(id)},
                        {"name",label(*entity)},{"countryId",qs(sovereign)},
                        {"parentId",qs(parent)}});
                    continue;
                }
                request.sovereign=pandoeditor::territorialRef(sovereign);
                request.parent=pandoeditor::territorialRef(parent);
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
            return pandoeditor::prepareHistoricalTransaction(base,*catalog,requests,pandoeditor::calculateGeometry);
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
    ++historicalSession_;
    if(historicalCommandPreview_)pandoeditor::CommandProcessor::cancel(*historicalCommandPreview_);
    historicalCommandPreview_.reset();historicalImpact_.clear();
    historicalOwnershipNeeded_.clear();
    historicalStage_=historicalLibrary_?QStringLiteral("ready"):QStringLiteral("unloaded");
    emit historicalChanged();
}

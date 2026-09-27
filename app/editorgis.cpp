#include "editorcontroller.h"
#include "giszip.h"
#include "gisgeopackage.h"
#include "gisterritorial.h"
#include "geometrycalculator.h"
#include <pandoeditor/project.h>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QTemporaryFile>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace {
QString qs(const std::string& value){return QString::fromStdString(value);}
QString sourceKind(const QString& name) {
    const auto lower=name.toLower();
    if(lower.endsWith(".gpkg"))return "geopackage";
    if(lower.endsWith(".zip"))return "geojson-zip";
    if(lower.endsWith(".geojson")||lower.endsWith(".json"))return "geojson";
    throw std::invalid_argument("UNSUPPORTED_GIS_SOURCE");
}
std::string option(const QVariantMap& values,const char* key,const QString& fallback={}) {
    return values.value(QString::fromLatin1(key),fallback).toString().trimmed().toStdString();
}
}

QVariantMap EditorController::gisImportState() const {
    QVariantList layers;
    if(gisImport_)for(std::size_t i=0;i<gisImport_->layers.size();++i) {
        const auto& layer=gisImport_->layers[i];
        layers.push_back(QVariantMap{{"index",int(i)},{"name",layer.name},
            {"target",layer.target},{"distributionType",layer.distributionType},
            {"count",int(layer.collection.features.size())}});
    }
    return {{"stage",gisImport_?gisImport_->stage:QStringLiteral("empty")},
        {"fileName",gisImport_?gisImport_->fileName:QString()},
        {"sourceKind",gisImport_?gisImport_->sourceKind:QString()},
        {"layers",layers},{"error",gisImport_?gisImport_->error:QString()},
        {"summary",gisImport_?gisImport_->summary:QString()},
        {"selectedLayer",gisImport_?gisImport_->selectedLayer:-1},
        {"session",gisImportToken_}};
}
void EditorController::cancelGisImport() {
    ++gisImportToken_;
    if(gisImport_&&gisImport_->preview)
        pandoeditor::CommandProcessor::cancel(*gisImport_->preview);
    gisImport_.reset();emit gisImportChanged();
}
bool EditorController::loadGisSource(const QUrl& url) {
    cancelGisImport();
    try {
        const auto name=url.fileName();
        const auto kind=sourceKind(name);
        const auto bytes=storage_.read(url);
        const auto base=project_.snapshot();
        gisImport_.emplace(GisImportSession{base,name,kind});
        const auto token=gisImportToken_;
        emit gisImportChanged();
        auto* watcher=new QFutureWatcher<std::vector<GisLoadedLayer>>(this);
        connect(watcher,&QFutureWatcher<std::vector<GisLoadedLayer>>::finished,this,
            [this,watcher,token]() {
                watcher->deleteLater();
                if(token!=gisImportToken_||!gisImport_)return;
                try {
                    if(!gisImport_->base.matches(project_))throw std::invalid_argument("STALE_GIS_SOURCE");
                    gisImport_->layers=watcher->result();
                    gisImport_->stage=QStringLiteral("mapping");gisImport_->error.clear();
                }catch(const std::exception& error){
                    gisImport_->stage=QStringLiteral("error");
                    gisImport_->error=QString::fromUtf8(error.what());
                }
                emit gisImportChanged();
            });
        watcher->setFuture(QtConcurrent::run([bytes,kind]() {
            std::vector<GisLoadedLayer> layers;
            if(kind=="geojson") {
                layers.push_back({QStringLiteral("GeoJSON"),{}, {},
                    pandoeditor::parseGisGeoJson(bytes)});
            } else if(kind=="geojson-zip") {
                const auto archive=pandoeditor::parseGisGeoJsonZip(bytes);
                for(const auto& layer:archive.layers)
                    layers.push_back({qs(layer.path),qs(layer.targetType),
                        qs(layer.distributionType),layer.collection});
            } else {
                QTemporaryFile temp(QDir::tempPath()+"/pando-gis-XXXXXX.gpkg");
                if(!temp.open()||temp.write(bytes)!=bytes.size()||!temp.flush())
                    throw std::invalid_argument("GIS_TEMP_FILE_FAILED");
                temp.close();
                const auto archive=pandoeditor::readGisGeoPackage(temp.fileName());
                for(const auto& layer:archive.layers) {
                    QString distribution;
                    for(const auto& type:{"language","ethnicity","religion"})
                        if(layer.tableName==std::string(type)+"_distribution")distribution=type;
                    layers.push_back({qs(layer.tableName),qs(layer.targetType),
                        distribution,layer.collection});
                }
            }
            if(layers.empty())throw std::invalid_argument("GIS_NO_LAYERS");
            return layers;
        }));
        return true;
    }catch(const std::exception& error) {
        gisImport_.emplace(GisImportSession{project_.snapshot()});
        gisImport_->stage=QStringLiteral("error");gisImport_->error=QString::fromUtf8(error.what());
        emit gisImportChanged();return false;
    }
}
bool EditorController::prepareGisImport(int index,const QVariantMap& values) {
    if(!gisImport_||gisImport_->stage!=QStringLiteral("mapping")||
       index<0||std::size_t(index)>=gisImport_->layers.size())return false;
    if(gisImport_->preview)pandoeditor::CommandProcessor::cancel(*gisImport_->preview);
    gisImport_->preview.reset();gisImport_->selectedLayer=index;
    const auto token=++gisImportToken_;
    const auto base=gisImport_->base;
    const auto layer=gisImport_->layers[std::size_t(index)];
    const pandoeditor::GisSource source{gisImport_->fileName.toStdString(),
        gisImport_->sourceKind.toStdString()};
    const auto target=option(values,"target",layer.target);
    const auto planId=std::string("gis-import:")+std::to_string(token);
    gisImport_->stage=QStringLiteral("preparing");gisImport_->error.clear();
    emit gisImportChanged();
    using Plan=std::variant<pandoeditor::GisTerritorialImportPlan,
        pandoeditor::GisGenericImportPlan,pandoeditor::GisDistributionImportPlan>;
    auto* watcher=new QFutureWatcher<Plan>(this);
    connect(watcher,&QFutureWatcher<Plan>::finished,this,[this,watcher,token,index]() {
        watcher->deleteLater();
        if(token!=gisImportToken_||!gisImport_)return;
        try {
            if(!gisImport_->base.matches(project_))throw std::invalid_argument("STALE_GIS_PLAN");
            auto plan=watcher->result();
            pandoeditor::CommandArguments args;
            std::string command;
            std::visit([&](auto& value){using T=std::decay_t<decltype(value)>;
                if constexpr(std::is_same_v<T,pandoeditor::GisTerritorialImportPlan>)
                    command="gis.import.territorial";
                else if constexpr(std::is_same_v<T,pandoeditor::GisGenericImportPlan>)
                    command="gis.import.generic";
                else command="gis.import.distribution";
                args.action=std::move(value);
            },plan);
            auto result=pandoeditor::CommandProcessor::prepare(project_,
                pandoeditor::CommandProcessor::makeRequest(project_,command,std::move(args)));
            if(!result.ok()||!result.preview)
                throw std::invalid_argument(result.detail.empty()?"GIS_PREVIEW_FAILED":result.detail.c_str());
            gisImport_->preview=std::move(result.preview);
            const auto& change=gisImport_->preview->change();
            const auto& before=change.before();const auto& after=change.after();
            gisImport_->summary=QStringLiteral("국가·영역 %1 → %2 · 분포 %3 → %4 · 기타 객체 %5 → %6")
                .arg(before.units.size()).arg(after.units.size())
                .arg(before.distributionEntries.size()).arg(after.distributionEntries.size())
                .arg(before.genericFeatures.size()).arg(after.genericFeatures.size());
            gisImport_->stage=QStringLiteral("impact");gisImport_->selectedLayer=index;
        }catch(const std::exception& error){
            gisImport_->stage=QStringLiteral("mapping");
            gisImport_->error=QString::fromUtf8(error.what());
        }
        emit gisImportChanged();
    });
    watcher->setFuture(QtConcurrent::run([base,layer,source,target,planId,values]() ->Plan {
        const auto targetType=pandoeditor::normalizeExchangeTarget(target,std::nullopt);
        if(!targetType)throw std::invalid_argument("INVALID_GIS_TARGET");
        if(*targetType==pandoeditor::GisExchangeTarget::Country||
           *targetType==pandoeditor::GisExchangeTarget::Subunit||
           *targetType==pandoeditor::GisExchangeTarget::Region) {
            if(!layer.target.isEmpty()&&layer.target!=QString::fromStdString(target))
                throw std::invalid_argument("GIS_LAYER_TARGET_MISMATCH");
            pandoeditor::GisTerritorialMapping mapping;mapping.target=*targetType;
            mapping.idField=option(values,"idField",QStringLiteral("__fid__"));
            mapping.nameField=option(values,"nameField",QStringLiteral("name"));
            mapping.sovereignField=option(values,"sovereignField",QStringLiteral("sovereign_id"));
            mapping.parentField=option(values,"parentField",QStringLiteral("parent_id"));
            auto country=option(values,"countryId"),parent=option(values,"parentId");
            if(!country.empty())mapping.commonSovereign=pandoeditor::territorialRef(country);
            if(!parent.empty())mapping.commonParent=pandoeditor::territorialRef(parent);
            const auto coast=option(values,"coast");
            if(coast=="imported")mapping.coast=pandoeditor::GisTerritorialMapping::CoastDecision::ImportedGeometry;
            else if(coast=="country")mapping.coast=pandoeditor::GisTerritorialMapping::CoastDecision::CountryGeometry;
            else if(coast!="reject")throw std::invalid_argument("INVALID_GIS_COAST_CHOICE");
            return pandoeditor::prepareGisTerritorialImport(base,layer.collection,planId,
                source,mapping,pandoeditor::calculateGeometry);
        }
        pandoeditor::GisContentMapping mapping;mapping.target=*targetType;
        mapping.idField=option(values,"idField");mapping.nameField=option(values,"nameField",QStringLiteral("name"));
        mapping.layerId=option(values,"layerId");mapping.layerName=option(values,"layerName");
        mapping.distributionType=option(values,"distributionType",layer.distributionType.isEmpty()?QStringLiteral("language"):layer.distributionType);
        if(!layer.target.isEmpty()&&layer.target!=QString::fromStdString(target))
            throw std::invalid_argument("GIS_LAYER_TARGET_MISMATCH");
        auto result=pandoeditor::planGisContentImport(base,layer.collection,planId,source,mapping);
        return std::visit([](auto&& value)->Plan{return std::move(value);},std::move(result));
    }));
    return true;
}
bool EditorController::confirmGisImport(qulonglong token) {
    if(token!=gisImportToken_||!gisImport_||gisImport_->stage!=QStringLiteral("impact")||
       !gisImport_->preview)return false;
    const auto baseline=project_.snapshot();
    const auto& before=baseline.document();
    const auto oldCount=before.units.size();
    auto result=pandoeditor::CommandProcessor::confirm(project_,*gisImport_->preview);
    gisImport_->preview.reset();++gisImportToken_;
    if(!result.changed()) {
        gisImport_->stage=QStringLiteral("mapping");
        gisImport_->error=QString::fromLatin1(pandoeditor::commandErrorCode(result.error));
        emit gisImportChanged();return false;
    }
    noteAppliedImpact(result.impact);
    // The command owns one ChangeSet; no draft or file write occurs here.
    const bool geographyChanged=oldCount!=project_.document().units.size()||
        std::any_of(project_.document().units.begin(),project_.document().units.end(),
            [&](const auto& unit){
                auto found=std::find_if(before.units.begin(),before.units.end(),
                    [&](const auto& old){return old.id==unit.id;});
                return found==before.units.end()||!(found->geometry==unit.geometry);
            });
    if(geographyChanged)projection_.rebuild(project_.document());
    gisImport_.reset();publish(false);
    if(geographyChanged)emit geometryChanged();
    emit gisImportChanged();return true;
}

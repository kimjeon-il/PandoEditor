#include "editorcontroller.h"
#include <QDir>
#include <QLocale>
#include "defaultflagresolver.h"
#include <QFile>
#include <QMimeDatabase>
#include <QColor>
#include <QUuid>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <type_traits>
#include <cmath>
using namespace pandoeditor;
namespace { QString q(const std::string& s){return QString::fromStdString(s);} }
bool EditorController::copyBuiltinHydro() {
    if(hydroCopyBusy_||hasPendingEdits()||jobBusy()||hasWebImportPreview())return false;
    const auto selected=selection_.primary();
    if(!selected||selected->domain!="hydroBuiltin"||!objectVisible(*selected))return false;
    const auto record=hydroRuntime_.recordById(q(selected->id));
    if(!record||!hydroRuntime_.pinLogical(record->logicalFid))return false;
    auto job=hydroRuntime_.logicalGeometryJob(record->logicalFid);
    if(!job){hydroRuntime_.clearPinned();return false;}
    const auto revision=project_.revision();
    const auto instance=project_.instanceId();
    const auto source=project_.document().physicalData;
    const auto original=*selected;
    using Result=std::pair<std::optional<HydroLogicalCopy>,QString>;
    auto* watcher=new QFutureWatcher<Result>(this);
    hydroCopyBusy_=true;emit contentEditChanged();
    connect(watcher,&QFutureWatcher<Result>::finished,this,[this,watcher,revision,instance,source,original,record]{
        const auto result=watcher->result();watcher->deleteLater();
        hydroRuntime_.clearPinned();hydroCopyBusy_=false;emit contentEditChanged();
        if(project_.revision()!=revision||project_.instanceId()!=instance||
           project_.document().physicalData.source!=source.source||
           !selection_.primary()||!(*selection_.primary()==original))return;
        if(!result.first){emit errorOccurred(result.second);return;}
        try{
            const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
            const GeometryRef geometryRef{"content:hydro:"+id,1};
            HydroFeature copy;copy.id=id;copy.name=record->name.toStdString();
            copy.kind=record->category=="lake"?"lake":"river";copy.geometry=geometryRef;
            copy.source.kind="user";copy.source.dataset=source.dataset;copy.source.version=source.version;
            copy.source.sourceId=result.first->sourceId.toStdString();
            copy.sourceFeatureId=original.id;
            ContentEdit edit;edit.target={"hydro",id};edit.value=copy;edit.create=true;
            edit.geometry=std::make_pair(geometryRef,std::move(result.first->geometry));
            CommandArguments args;args.action=std::move(edit);
            const auto request=CommandProcessor::makeRequest(project_,"content.edit",args);
            auto prepared=CommandProcessor::prepare(project_,request);
            if(!prepared.ok()||!prepared.preview){emit errorOccurred(QString::fromStdString(prepared.detail));return;}
            MapProjection next;next.rebuild(prepared.preview->change().after());
            const auto committed=CommandProcessor::confirm(project_,*prepared.preview);
            if(!committed.ok()){emit errorOccurred(QString::fromStdString(commandErrorCode(committed.error)));return;}
            noteAppliedImpact(committed.impact);
            projection_=std::move(next);publish(false);emit geometryChanged();
            selectObject({{"domain","hydro"},{"id",q(id)}},"replace","map");
        }catch(const std::exception& exception){emit errorOccurred(QString::fromUtf8(exception.what()));}
    });
    watcher->setFuture(QtConcurrent::run([job=std::move(job)]() -> Result {
        try{return {job(),{}};}
        catch(const std::exception& exception){return {{},QString::fromUtf8(exception.what())};}
        catch(...){return {{},QStringLiteral("수계 편집용 복사에 실패했습니다.")};}
    }));
    return true;
}
QVariantMap EditorController::contentEditState() const {
    if(!contentSession_) return {{"active",false}};
    const auto& s=*contentSession_;
    QVariantMap out{{"active",true},{"domain",q(s.edit.target.domain)},{"id",q(s.edit.target.id)},
        {"create",s.edit.create},{"previewReady",bool(s.preview)},{"error",s.error},
        {"stale",!s.base.matches(project_)},{"drawing",bool(geometryEdit_)}};
    std::visit([&](const auto& v) {
        using T=std::decay_t<decltype(v)>;
        if constexpr(std::is_same_v<T,CountryDetails>) out["capital"]=q(v.capital);
        else if constexpr(std::is_same_v<T,TerritorialSymbolStyle>) {out["flagPolicy"]=v.policy==FlagPolicy::Default?"default":v.policy==FlagPolicy::None?"none":"embedded";out["flagSource"]=q(v.embeddedDataUrl);auto candidate=project_.document();candidate.symbols[s.edit.target]=v;const auto resolved=resolveDefaultFlag(candidate,s.edit.target);out["flagAvailable"]=resolved.available;out["flagReason"]=resolved.reason;}
        else if constexpr(!std::is_same_v<T,std::monostate>) {
            if constexpr(!std::is_same_v<T,DistributionEntry>) out["name"]=q(v.name);
            if constexpr(std::is_same_v<T,PlaceLabel>||std::is_same_v<T,HydroFeature>||std::is_same_v<T,GenericFeature>) out["notes"]=q(v.notes);
            if constexpr(std::is_same_v<T,PlaceLabel>||std::is_same_v<T,HydroFeature>) out["kind"]=q(v.kind);
            if constexpr(std::is_same_v<T,HydroFeature>||std::is_same_v<T,GenericFeature>||std::is_same_v<T,DistributionLayer>) {out["locked"]=v.locked;out["color"]=QString("#%1").arg(v.color,6,16,QChar('0'));}
            if constexpr(std::is_same_v<T,DistributionLayer>) {out["unit"]=q(v.unit);out["valueScaleMode"]=v.valueScale.manual?"manual":"auto";out["valueMin"]=v.valueScale.min;out["valueMax"]=v.valueScale.max;out["parentId"]=q(v.parentId.value_or(""));}
            if constexpr(std::is_same_v<T,DistributionEntry>) {out["value"]=v.value;out["layerId"]=q(v.layerId);out["territoryId"]=v.territory?q(v.territory->id):QString{};out["certainty"]=q(v.certainty);}
            if constexpr(std::is_same_v<T,DistributionLayer>||std::is_same_v<T,DistributionEntry>) {out["validFrom"]=q(v.validity.from.value_or(""));out["validTo"]=q(v.validity.to.value_or(""));}
        }
    },s.edit.value);
    return out;
}
bool EditorController::beginContentEdit(const QString& domain,const QString& type,bool create) {
    if(contentSession_||hasPendingEdits()||structureDialogOpen()||jobBusy()||hasWebImportPreview()) return false;
    ContentEdit edit; edit.create=create;
    if(create) edit.target={domain.toStdString(),QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()};
    else {const auto ref=selection_.primary();if(!ref||ref->domain=="placeBuiltin"||ref->domain!=domain.toStdString())return false;edit.target=*ref;}
    const auto& d=project_.document(); const auto found=project_.index().objects.find(edit.target);
    const auto i=found==project_.index().objects.end()?0:found->second;
    if(domain=="label") {PlaceLabel v=create?PlaceLabel{}:d.labels.at(i);v.id=edit.target.id;if(create)v.kind=type.isEmpty()?"custom":type.toStdString();edit.value=v;}
    else if(domain=="hydro") {HydroFeature v=create?HydroFeature{}:d.hydro.at(i);v.id=edit.target.id;if(create)v.kind=type=="lake"?"lake":"river";edit.value=v;}
    else if(domain=="distributionLayer") {DistributionLayer v=create?DistributionLayer{}:d.distributionLayers.at(i);v.id=edit.target.id;edit.value=v;}
    else if(domain=="distributionEntry") {DistributionEntry v=create?DistributionEntry{}:d.distributionEntries.at(i);v.id=edit.target.id;edit.value=v;}
    else if(domain=="generic"&&!create) edit.value=d.genericFeatures.at(i);
    else if(domain=="territorial"&&!create) {
        if(type=="capital") {if(d.units.at(i).kind!=UnitKind::General)return false;auto it=d.countryDetails.find(edit.target);edit.value=it==d.countryDetails.end()?CountryDetails{}:it->second;}
        else if(type=="flag") {auto it=d.symbols.find(edit.target);edit.value=it==d.symbols.end()?TerritorialSymbolStyle{}:it->second;}
        else return false;
    } else return false;
    if(auto hydro=std::get_if<HydroFeature>(&edit.value);hydro&&!create&&hydro->source.kind=="builtin") {
        hydro->sourceFeatureId=hydro->id;hydro->source.kind="user";hydro->locked=false;
        hydro->id=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();edit.target.id=hydro->id;edit.create=true;
    }
    contentSession_=ContentSession{project_.snapshot(),std::move(edit),{},QString{}};
    emit contentEditChanged();emit draftsChanged();emit dirtyChanged();return true;
}
namespace {
bool updateContentValue(ContentValue& next,const QString& field,const QVariant& input) {
    bool handled=false;
    const auto text=input.toString().toStdString();
    std::visit([&](auto& v) {
        using T=std::decay_t<decltype(v)>;
        if constexpr(std::is_same_v<T,CountryDetails>) {if(field=="capital"){v.capital=text;handled=true;}}
        else if constexpr(std::is_same_v<T,TerritorialSymbolStyle>) {if(field=="flagPolicy"&&(text=="default"||text=="none")){v.policy=text=="none"?FlagPolicy::None:FlagPolicy::Default;v.embeddedDataUrl.clear();handled=true;}}
        else if constexpr(!std::is_same_v<T,std::monostate>) {
            if constexpr(!std::is_same_v<T,DistributionEntry>) if(field=="name"){v.name=text;handled=true;}
            if constexpr(std::is_same_v<T,PlaceLabel>||std::is_same_v<T,HydroFeature>||std::is_same_v<T,GenericFeature>) if(field=="notes"){v.notes=text;handled=true;}
            if constexpr(std::is_same_v<T,PlaceLabel>||std::is_same_v<T,HydroFeature>) if(field=="kind"){v.kind=text;handled=true;}
            if constexpr(std::is_same_v<T,HydroFeature>||std::is_same_v<T,GenericFeature>||std::is_same_v<T,DistributionLayer>) {
                if(field=="locked"){v.locked=input.toBool();handled=true;}
                if(field=="color"){QColor c(input.toString());if(c.isValid()){v.color=c.rgb()&0xffffff;handled=true;}}
            }
            if constexpr(std::is_same_v<T,DistributionLayer>) {
                if(field=="unit"){v.unit=text;handled=true;}
                if(field=="valueScaleMode"&&(text=="auto"||text=="manual")){v.valueScale.manual=text=="manual";handled=true;}
                if(field=="valueMin"||field=="valueMax"){bool ok=false;const auto n=input.toDouble(&ok);if(ok&&std::isfinite(n)){(field=="valueMin"?v.valueScale.min:v.valueScale.max)=n;handled=true;}}
                if(field=="parentId"){v.parentId=text.empty()?std::nullopt:std::optional<std::string>(text);handled=true;}
            }
            if constexpr(std::is_same_v<T,DistributionEntry>) {
                if(field=="layerId"){v.layerId=text;handled=true;}
                if(field=="territoryId"){v.territory=text.empty()?std::nullopt:std::optional<ObjectRef>(territorialRef(text));if(v.territory)v.geometry.reset();handled=true;}
                if(field=="value"){bool ok=false;auto n=input.toDouble(&ok);if(ok&&std::isfinite(n)){v.value=n;handled=true;}}
                if(field=="certainty"){v.certainty=text;handled=true;}
            }
            if constexpr(std::is_same_v<T,DistributionLayer>||std::is_same_v<T,DistributionEntry>) if(field=="validFrom"||field=="validTo") {
                (field=="validFrom"?v.validity.from:v.validity.to)=text.empty()?std::nullopt:std::optional<std::string>(text);handled=true;
            }
        }
    },next);
    return handled;
}
ContentValue storedContentValue(const ProjectSnapshot& snapshot,const ContentEdit& edit) {
    const auto& d=snapshot.document();const auto i=snapshot.index().objects.at(edit.target);
    return std::visit([&](const auto& value)->ContentValue {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,PlaceLabel>)return d.labels.at(i);
        else if constexpr(std::is_same_v<T,HydroFeature>)return d.hydro.at(i);
        else if constexpr(std::is_same_v<T,DistributionLayer>)return d.distributionLayers.at(i);
        else if constexpr(std::is_same_v<T,DistributionEntry>)return d.distributionEntries.at(i);
        else if constexpr(std::is_same_v<T,GenericFeature>)return d.genericFeatures.at(i);
        else if constexpr(std::is_same_v<T,CountryDetails>) {const auto found=d.countryDetails.find(edit.target);return found==d.countryDetails.end()?CountryDetails{}:found->second;}
        else if constexpr(std::is_same_v<T,TerritorialSymbolStyle>) {const auto found=d.symbols.find(edit.target);return found==d.symbols.end()?TerritorialSymbolStyle{}:found->second;}
        else return std::monostate{};
    },edit.value);
}
}
bool EditorController::updateContentField(const QString& field,const QVariant& input) {
    if(!contentSession_||contentSession_->preview||geometryEdit_)return false;
    auto next=contentSession_->edit.value;if(!updateContentValue(next,field,input))return false;
    contentSession_->edit.value=std::move(next);contentSession_->pendingFields.insert(field.toStdString());
    contentSession_->error.clear();emit contentEditChanged();emit dirtyChanged();return true;
}
bool EditorController::commitContentField(const QString& field) {
    if(!contentSession_||contentSession_->edit.create||contentSession_->preview||geometryEdit_)return false;
    auto& s=*contentSession_;if(!s.base.matches(project_))return false;
    const bool scale=field=="valueScaleMode"||field=="valueMin"||field=="valueMax";
    if(scale&&!std::holds_alternative<DistributionLayer>(s.edit.value))return false;
    if(field=="flagSource"&&!std::holds_alternative<TerritorialSymbolStyle>(s.edit.value))return false;
    auto edit=s.edit;edit.value=storedContentValue(s.base,s.edit);edit.geometry.reset();
    if(scale)std::get<DistributionLayer>(edit.value).valueScale=std::get<DistributionLayer>(s.edit.value).valueScale;
    else if(field=="flagSource")edit.value=s.edit.value;
    else if(!updateContentValue(edit.value,field,contentEditState().value(field)))return false;
    CommandArguments args;args.action=edit;
    auto result=CommandProcessor::prepare(project_,CommandProcessor::makeRequest(project_,"content.edit",args));
    if(!result.ok()){s.error=q(result.detail.empty()?commandErrorCode(result.error):result.detail);emit contentEditChanged();return false;}
    if(result.preview) {
        const auto committed=CommandProcessor::confirm(project_,*result.preview);
        if(!committed.ok()){s.error=q(commandErrorCode(committed.error));emit contentEditChanged();return false;}
        noteAppliedImpact(committed.impact);
    }
    s.base=project_.snapshot();s.error.clear();
    if(scale){s.pendingFields.erase("valueScaleMode");s.pendingFields.erase("valueMin");s.pendingFields.erase("valueMax");}
    else s.pendingFields.erase(field.toStdString());
    if(s.pendingFields.empty())s.edit.value=storedContentValue(s.base,s.edit);
    publish(false);emit contentEditChanged();return true;
}
void EditorController::refreshContentSession() {
    if(!contentSession_||contentSession_->edit.create||contentSession_->preview||!contentSession_->pendingFields.empty()||geometryEdit_)return;
    if(!project_.index().objects.count(contentSession_->edit.target)){contentSession_.reset();emit contentEditChanged();return;}
    contentSession_->base=project_.snapshot();contentSession_->edit.value=storedContentValue(contentSession_->base,contentSession_->edit);
    emit contentEditChanged();
}
QVariantList EditorController::flagLibrary() const {
    QVariantList result;
    for(const auto& family:{QString("native"),QString("legacy"),QString("political")}) {
    QDir directory(":/defaults/flags/"+family);
    for(const auto& file:directory.entryList({"*.svg"},QDir::Files,QDir::Name)) {
        const auto code=file.chopped(4);const QLocale locale("ko_"+code.toUpper());
        const auto name=code=="cyn"?QStringLiteral("북키프로스"):code=="sol"?QStringLiteral("소말릴란드"):code=="xk"?QStringLiteral("코소보"):code.size()==2&&QLocale::territoryToCode(locale.territory())==code.toUpper()?locale.nativeTerritoryName():code.toUpper();
        result.append(QVariantMap{{"name",name},{"code",code},{"source","qrc:/defaults/flags/"+family+"/"+file}});
    }
    }
    return result;
}
bool EditorController::loadContentFlag(const QUrl& url) {
    if(!contentSession_||contentSession_->preview||!std::holds_alternative<TerritorialSymbolStyle>(contentSession_->edit.value))return false;
    QByteArray bytes;
    if(url.scheme()=="data") {
        const auto data=url.toString().toUtf8();const auto comma=data.indexOf(',');
        if(comma<0||!data.left(comma).contains(";base64")||data.size()>24*1024*1024)return false;
        bytes=QByteArray::fromBase64(data.mid(comma+1));
    }else {QFile file(url.scheme()=="qrc"?":"+url.path():url.isLocalFile()?url.toLocalFile():url.toString());if(!file.open(QIODevice::ReadOnly)||file.size()>16*1024*1024)return false;bytes=file.readAll();}
    const auto mime=QMimeDatabase().mimeTypeForData(bytes).name();
    if(!mime.startsWith("image/"))return false;
    const auto dataUrl="data:"+mime.toLatin1()+";base64,"+bytes.toBase64();
    try{projectcodec::validateFlagDataUrl(dataUrl.toStdString());}
    catch(const std::invalid_argument&){return false;}
    auto& symbol=std::get<TerritorialSymbolStyle>(contentSession_->edit.value);
    symbol.policy=FlagPolicy::Embedded;symbol.embeddedDataUrl=dataUrl.toStdString();
    contentSession_->pendingFields.insert("flagSource");emit contentEditChanged();emit dirtyChanged();return true;
}
bool EditorController::beginContentGeometry() {
    if(!contentSession_||contentSession_->preview||geometryEdit_||!contentSession_->base.matches(project_))return false;
    const auto& s=*contentSession_; Geometry draft; draft.type=s.edit.target.domain=="label"?"Point":"Polygon";
    if(const auto hydro=std::get_if<HydroFeature>(&s.edit.value)) draft.type=hydro->kind=="river"?"LineString":"Polygon";
    if(s.edit.target.domain=="distributionLayer"||s.edit.target.domain=="territorial")return false;
    auto ref=objectGeometry(s.base.document(),s.base.index(),s.edit.target);
    if(s.edit.geometry) draft=s.edit.geometry->second;
    else if(ref) draft=*s.base.document().geometries.get(*ref);
    else if(const auto hydro=std::get_if<HydroFeature>(&s.edit.value);hydro&&s.base.document().geometries.get(hydro->geometry))draft=*s.base.document().geometries.get(hydro->geometry);
    const bool empty=draft.points.empty()&&draft.lines.empty()&&draft.polygons.empty();
    geometryEdit_=GeometryEditSession{s.base,s.edit.target,draft,empty?QStringLiteral("draw"):QStringLiteral("edit"),{}};
    geometryEdit_->content=true;emit geometryEditChanged();emit contentEditChanged();return true;
}
bool EditorController::previewContentEdit(bool remove) {
    if(!contentSession_||contentSession_->preview||geometryEdit_)return false;
    auto& s=*contentSession_; if(!s.base.matches(project_)){s.error="문서가 변경되었습니다. 취소 후 다시 편집하세요.";emit contentEditChanged();return false;}
    auto edit=s.edit;if(remove){edit.value=std::monostate{};edit.geometry.reset();edit.create=false;}
    CommandArguments args;args.action=edit;auto request=CommandProcessor::makeRequest(project_,"content.edit",args);
    auto result=CommandProcessor::prepare(project_,request);
    if(!result.ok()||!result.preview){s.error=QString::fromStdString(result.detail.empty()?commandErrorCode(result.error):result.detail);emit contentEditChanged();return false;}
    s.preview=std::move(result.preview);emit contentEditChanged();return true;
}
bool EditorController::confirmContentEdit() {
    if(!contentSession_||!contentSession_->preview)return false;
    MapProjection next;try{next.rebuild(contentSession_->preview->change().after());}catch(...){return false;}
    auto result=CommandProcessor::confirm(project_,*contentSession_->preview);
    if(!result.ok()){contentSession_->preview.reset();contentSession_->error=q(commandErrorCode(result.error));emit contentEditChanged();return false;}
    noteAppliedImpact(result.impact);
    projection_=std::move(next);contentSession_.reset();hover_.reset();++hoverRevision_;publish(false);emit geometryChanged();emit contentEditChanged();return true;
}
void EditorController::cancelContentEdit(){if(geometryEdit_&&geometryEdit_->content)cancelGeometryEdit();contentSession_.reset();emit contentEditChanged();emit draftsChanged();emit dirtyChanged();}

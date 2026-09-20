#include "editorcontroller.h"
#include "defaultflagresolver.h"
#include <QFile>
#include <QImage>
#include <QBuffer>
#include <QColor>
#include <QUuid>
#include <type_traits>
#include <cmath>
using namespace pandoeditor;
namespace { QString q(const std::string& s){return QString::fromStdString(s);} }
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
            if constexpr(std::is_same_v<T,DistributionLayer>) {out["type"]=q(v.type);out["parentId"]=q(v.parentId.value_or(""));}
            if constexpr(std::is_same_v<T,DistributionEntry>) {out["share"]=v.share;out["layerId"]=q(v.layerId);out["territoryId"]=v.territory?q(v.territory->id):QString{};out["certainty"]=q(v.certainty);}
            if constexpr(std::is_same_v<T,DistributionLayer>||std::is_same_v<T,DistributionEntry>) {out["validFrom"]=q(v.validity.from.value_or(""));out["validTo"]=q(v.validity.to.value_or(""));}
        }
    },s.edit.value);
    return out;
}
bool EditorController::beginContentEdit(const QString& domain,const QString& type,bool create) {
    if(contentSession_||hasPendingEdits()||structureDialogOpen()||jobBusy()||hasWebImportPreview()) return false;
    ContentEdit edit; edit.create=create;
    if(create) edit.target={domain.toStdString(),QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()};
    else {const auto ref=selection_.primary();if(!ref||ref->domain!=domain.toStdString())return false;edit.target=*ref;}
    const auto& d=project_.document(); const auto found=project_.index().objects.find(edit.target);
    const auto i=found==project_.index().objects.end()?0:found->second;
    if(domain=="label") {PlaceLabel v=create?PlaceLabel{}:d.labels.at(i);v.id=edit.target.id;if(create)v.kind=type.isEmpty()?"custom":type.toStdString();edit.value=v;}
    else if(domain=="hydro") {HydroFeature v=create?HydroFeature{}:d.hydro.at(i);v.id=edit.target.id;if(create)v.kind=type=="lake"?"lake":"river";edit.value=v;}
    else if(domain=="distributionLayer") {DistributionLayer v=create?DistributionLayer{}:d.distributionLayers.at(i);v.id=edit.target.id;if(create)v.type=type.isEmpty()?"language":type.toStdString();edit.value=v;}
    else if(domain=="distributionEntry") {DistributionEntry v=create?DistributionEntry{}:d.distributionEntries.at(i);v.id=edit.target.id;edit.value=v;}
    else if(domain=="generic"&&!create) edit.value=d.genericFeatures.at(i);
    else if(domain=="territorial"&&!create) {
        if(type=="capital") {if(d.units.at(i).kind!=UnitKind::Country)return false;auto it=d.countryDetails.find(edit.target);edit.value=it==d.countryDetails.end()?CountryDetails{}:it->second;}
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
bool EditorController::updateContentField(const QString& field,const QVariant& input) {
    if(!contentSession_||contentSession_->preview||geometryEdit_) return false;
    auto next=contentSession_->edit.value; bool handled=false;
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
                if(field=="type"){v.type=text;handled=true;}
                if(field=="parentId"){v.parentId=text.empty()?std::nullopt:std::optional<std::string>(text);handled=true;}
            }
            if constexpr(std::is_same_v<T,DistributionEntry>) {
                if(field=="layerId"){v.layerId=text;handled=true;}
                if(field=="territoryId"){v.territory=text.empty()?std::nullopt:std::optional<ObjectRef>(territorialRef(text));if(v.territory)v.geometry.reset();handled=true;}
                if(field=="share"){bool ok=false;auto n=input.toDouble(&ok);if(ok&&std::isfinite(n)&&n>=0&&n<=100){v.share=n;handled=true;}}
                if(field=="certainty"){v.certainty=text;handled=true;}
            }
            if constexpr(std::is_same_v<T,DistributionLayer>||std::is_same_v<T,DistributionEntry>) if(field=="validFrom"||field=="validTo") {
                (field=="validFrom"?v.validity.from:v.validity.to)=text.empty()?std::nullopt:std::optional<std::string>(text);handled=true;
            }
        }
    },next);
    if(!handled)return false; contentSession_->edit.value=std::move(next);contentSession_->error.clear();emit contentEditChanged();return true;
}
bool EditorController::loadContentFlag(const QUrl& url) {
    if(!contentSession_||contentSession_->preview||!std::holds_alternative<TerritorialSymbolStyle>(contentSession_->edit.value))return false;
    QFile file(url.isLocalFile()?url.toLocalFile():url.toString());if(!file.open(QIODevice::ReadOnly)||file.size()>16*1024*1024)return false;
    const auto image=QImage::fromData(file.readAll());if(image.isNull())return false;
    QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);if(!image.save(&buffer,"PNG"))return false;
    contentSession_->edit.value=TerritorialSymbolStyle{FlagPolicy::Embedded,("data:image/png;base64,"+png.toBase64()).toStdString()};emit contentEditChanged();return true;
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
    geometryEdit_=GeometryEditSession{s.base,s.edit.target,draft,{},{},0,0,-1,empty?QStringLiteral("draw"):QStringLiteral("edit"),{}};
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
    projection_=std::move(next);contentSession_.reset();hover_.reset();++hoverRevision_;publish(false);emit geometryChanged();emit contentEditChanged();return true;
}
void EditorController::cancelContentEdit(){if(geometryEdit_&&geometryEdit_->content)cancelGeometryEdit();contentSession_.reset();emit contentEditChanged();emit draftsChanged();emit dirtyChanged();}

#include "editorcontroller.h"
#include <QCollator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QScopedValueRollback>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace pandoeditor;
namespace {
QString q(const std::string& value) { return QString::fromStdString(value); }
QString rgb(std::uint32_t color) { return QString("#%1").arg(color,6,16,QChar('0')); }
QString typeName(UnitKind kind) { return kind==UnitKind::Country?"country":kind==UnitKind::Subunit?"subunit":"region"; }
QString typeLabel(UnitKind kind) { return kind==UnitKind::Country?QStringLiteral("국가"):kind==UnitKind::Subunit?QStringLiteral("하위단위"):QStringLiteral("지방"); }
QString groupName(UnitKind kind) { return kind==UnitKind::Country?"countries":kind==UnitKind::Subunit?"subunits":"regions"; }
QJsonValue retained(const ProjectDocument& document,const QString& pointer) {
    for(const auto& e:document.extensions) {
        if(e.status!="unsupported" || q(e.jsonPointer)!=pointer) continue;
        QJsonParseError error;
        const auto value=QJsonDocument::fromJson("["+QByteArray::fromStdString(e.payload)+"]",&error);
        if(error.error==QJsonParseError::NoError && value.isArray() && value.array().size()==1) return value.array().at(0);
    }
    return {};
}
}
std::optional<ObjectRef> EditorController::existingObjectRef(const QVariantMap& value) const {
    const auto domain=value.value("domain").toString().trimmed();
    const auto id=value.value("id").toString().trimmed();
    if(domain!="territorial" || id.isEmpty()) return {};
    const auto ref=territorialRef(id.toStdString());
    const auto found=project_.index().objects.find(ref);
    if(found==project_.index().objects.end()) return {};
    const auto expected=typeName(project_.document().units.at(found->second).kind);
    const auto requested=value.value("type").toString().trimmed();
    if(!requested.isEmpty() && requested!=expected) return {};
    return ref;
}
QVariantMap EditorController::objectRefValue(const ObjectRef& ref) const {
    const auto it=project_.index().objects.find(ref);
    if(it==project_.index().objects.end()) return {{"domain",q(ref.domain)},{"id",q(ref.id)}};
    const auto type=typeName(project_.document().units.at(it->second).kind);
    const auto id=q(ref.id);
    return {{"domain",q(ref.domain)},{"type",type},{"id",id},
        {"key",q(ref.domain)+":"+type+":"+QString::fromLatin1(QUrl::toPercentEncoding(id,"-_.!~*'()"))}};
}
QVariantList EditorController::selectionItems() const {
    QVariantList result;for(const auto& ref:selection_.items()) result.append(objectRefValue(ref));return result;
}
QVariantMap EditorController::primaryObject() const { return selection_.primary()?objectRefValue(*selection_.primary()):QVariantMap{}; }
QVariantMap EditorController::rangeAnchor(const QString& scope) const {
    const auto ref=selection_.rangeAnchor(scope.toStdString());return ref?objectRefValue(*ref):QVariantMap{};
}
bool EditorController::objectVisible(const ObjectRef& ref) const {
    const auto& document=project_.document();
    const auto found=project_.index().objects.find(ref);
    if(found==project_.index().objects.end()) return false;
    const auto member=document.presentation.membership.find(ref);
    if(member==document.presentation.membership.end()) return false;
    const auto layer=project_.layer(member->second);
    if(!layer || !layer->visible) return false;
    const auto group=groupName(document.units.at(found->second).kind);
    // Read preserved web visibility without promoting or rewriting an extension.
    const auto groups=retained(document,"/layerVisibility").toObject();
    const auto items=retained(document,"/itemVisibility").toObject();
    return groups[group]!=QJsonValue(false) && items[group].toObject()[q(ref.id)]!=QJsonValue(false);
}
QVariantList EditorController::objectRows() const {
    QVariantList rows;
    for(const auto& unit:project_.document().units) {
        const auto ref=territorialRef(unit.id);
        auto row=objectRefValue(ref);
        row["name"]=q(unit.name);row["typeLabel"]=typeLabel(unit.kind);
        row["visible"]=objectVisible(ref);row["locked"]=unit.locked;
        const auto member=project_.document().presentation.membership.find(ref);
        if(member!=project_.document().presentation.membership.end()) {
            row["layerId"]=q(member->second);
            if(const auto layer=project_.layer(member->second)) row["locked"]=unit.locked||layer->locked;
        }
        row["editable"]=unit.kind==UnitKind::Country && project_.editable(unit.id);
        row["selectionOnly"]=unit.kind!=UnitKind::Country;
        rows.append(row);
    }
    return rows;
}
void EditorController::setSearchQuery(const QString& query) {
    if(query==searchQuery_) return;
    searchQuery_=query;emit searchChanged();
}
QVariantList EditorController::searchResults() const {
    const auto query=searchQuery_.trimmed().toLower();
    if(query.isEmpty()) return {};
    QVariantList rows;
    for(const auto& value:objectRows()) {
        const auto row=value.toMap();
        if((row["name"].toString()+" "+row["typeLabel"].toString()+" "+row["id"].toString()).toLower().contains(query)) rows.append(row);
    }
    QCollator compare(QLocale(QLocale::Korean));
    std::stable_sort(rows.begin(),rows.end(),[&](const QVariant& a,const QVariant& b){
        const auto left=a.toMap(),right=b.toMap();
        const int result=compare.compare(left["name"].toString(),right["name"].toString());
        return result!=0?result<0:left["key"].toString()<right["key"].toString();
    });
    return rows;
}
void EditorController::parkDrafts() {
    if(const auto c=project_.country(selected_.toStdString())) {
        if(nameDraft_!=q(c->name)||memoDraft_!=q(c->memo)||colorDraft_!=rgb(c->color)||(opacityPreview_&&*opacityPreview_!=c->opacity))
            parkedCountryDrafts_[selected_]={nameDraft_,memoDraft_,colorDraft_,opacityPreview_};
        else parkedCountryDrafts_.erase(selected_);
    }
    if(const auto layer=project_.layer(selectedLayer_.toStdString())) {
        if(layerNameDraft_!=q(layer->name)||(layerOpacityPreview_&&*layerOpacityPreview_!=layer->opacity))
            parkedLayerDrafts_[selectedLayer_]={layerNameDraft_,layerOpacityPreview_};
        else parkedLayerDrafts_.erase(selectedLayer_);
    }
}
void EditorController::restoreParkedDrafts() {
    if(const auto it=parkedCountryDrafts_.find(selected_);it!=parkedCountryDrafts_.end()) {
        const auto draft=it->second;parkedCountryDrafts_.erase(it);
        nameDraft_=draft.name;memoDraft_=draft.memo;colorDraft_=draft.color;opacityPreview_=draft.opacity;
    }
    if(const auto it=parkedLayerDrafts_.find(selectedLayer_);it!=parkedLayerDrafts_.end()) {
        const auto draft=it->second;parkedLayerDrafts_.erase(it);
        layerNameDraft_=draft.name;layerOpacityPreview_=draft.opacity;
    }
}
void EditorController::clearParkedDrafts() { parkedCountryDrafts_.clear();parkedLayerDrafts_.clear(); }
void EditorController::applySelection(SelectionState next) {
    const bool changed=selection_.revision()!=next.revision();
    if(!changed) {selection_=std::move(next);return;} // anchor-only changes are silent in the web reducer
    QScopedValueRollback<bool> guard(selectionTransition_,true);
    parkDrafts();selection_=std::move(next);
    selected_=selection_.primary()?q(selection_.primary()->id):QString();
    reloadDrafts();
    emit selectionChanged();emit stateChanged();emit draftsChanged();emit visualChanged();
    // No dirtyChanged, project revision, history, import epoch, preview or worker mutation.
}
bool EditorController::selectObject(const QVariantMap& value,const QString& mode,const QString& scope,const QVariantList& ordered,bool additive) {
    const auto ref=existingObjectRef(value);if(!ref) return false;
    auto next=selection_;
    if(mode=="replace") next.replace(*ref,scope.toStdString());
    else if(mode=="toggle") next.toggle(*ref,scope.toStdString());
    else if(mode=="range") {
        std::vector<ObjectRef> refs;
        for(const auto& v:ordered) if(const auto candidate=existingObjectRef(v.toMap())) refs.push_back(*candidate);
        next.selectRange(*ref,refs,scope.isEmpty()?"default":scope.toStdString(),additive);
    } else if(mode=="remove") next.remove(*ref);
    else return false;
    applySelection(std::move(next));return true;
}
bool EditorController::setSelection(const QVariantList& values,const QVariantMap& primary,const QString& scope) {
    std::vector<ObjectRef> refs;
    for(const auto& v:values) if(const auto ref=existingObjectRef(v.toMap())) refs.push_back(*ref);
    auto next=selection_;next.setMany(refs,existingObjectRef(primary),scope.toStdString());applySelection(std::move(next));return true;
}
void EditorController::clearSelection() { auto next=selection_;next.clear();applySelection(std::move(next)); }
void EditorController::selectCountry(const QString& id) {
    selectObject({{"domain","territorial"},{"type","country"},{"id",id}},"replace","countries");
}
void EditorController::selectLayer(const QString& id) {
    if(!project_.layer(id.toStdString())||selectedLayer_==id) return;
    QScopedValueRollback<bool> guard(selectionTransition_,true);
    parkDrafts();selectedLayer_=id;reloadDrafts();
    emit stateChanged();emit draftsChanged();emit visualChanged();
}
QVariantMap EditorController::pickObject(double x,double y) const {
    if(!std::isfinite(x)||!std::isfinite(y)) return {};
    const auto point=projection_.unproject(x,y);
    // Match the existing renderer: layers bottom-to-top, paths in document order.
    for(auto layer=project_.layers().rbegin();layer!=project_.layers().rend();++layer) {
        if(!layer->visible) continue;
        for(auto unit=project_.document().units.rbegin();unit!=project_.document().units.rend();++unit) {
            const auto ref=territorialRef(unit->id);
            const auto member=project_.document().presentation.membership.find(ref);
            if(member==project_.document().presentation.membership.end()||member->second!=layer->id||!objectVisible(ref)) continue;
            const auto geometry=project_.document().geometries.get(unit->geometry);
            if(geometry && pointInCountry(point,geometry->polygons)) return objectRefValue(ref);
        }
    }
    return {};
}
void EditorController::selectAt(double x,double y) { selectMapAt(x,y,false); }
void EditorController::selectMapAt(double x,double y,bool additive) {
    if(!std::isfinite(x)||!std::isfinite(y)) return;
    const auto ref=pickObject(x,y);
    if(!ref.isEmpty()) selectObject(ref,additive?"toggle":"replace","map");
    else if(!additive) clearSelection();
}
QVariantMap EditorController::hoverObject() const { return hover_?objectRefValue(*hover_):QVariantMap{}; }
bool EditorController::setHoverObject(const QVariantMap& value,const QString& source,const QString& expectedKey) {
    std::optional<ObjectRef> next;
    if(value.isEmpty()) {
        if((!source.isEmpty()&&!hoverSource_.isEmpty()&&source!=hoverSource_) ||
           (!expectedKey.isEmpty()&&hoverObject().value("key").toString()!=expectedKey)) return false;
    } else {next=existingObjectRef(value);if(!next) return false;}
    if(next==hover_) {if(next) hoverSource_=source;return true;}
    hover_=next;hoverSource_=next?source:QString();++hoverRevision_;emit hoverChanged();return true;
}
bool EditorController::focusObject(const QVariantMap& value) {
    const auto ref=value.isEmpty()?selection_.primary():existingObjectRef(value);
    if(!ref) return false;
    for(const auto& path:projection_.paths) {
        const auto p=path.toMap();if(p["countryId"].toString()!=q(ref->id)) continue;
        emit focusRequested(p["left"].toDouble(),p["top"].toDouble(),p["width"].toDouble(),p["height"].toDouble(),mobileMode_?12:10);
        return true;
    }
    return false;
}
void EditorController::reconcileSelection() {
    if(selectionInstance_!=project_.instanceId()) {
        selectionInstance_=project_.instanceId();selection_.reset();hover_.reset();hoverSource_.clear();hoverRevision_=0;
        searchQuery_.clear();clearParkedDrafts();
    } else {
        selection_.prune([this](const ObjectRef& ref){return project_.index().objects.count(ref)!=0;});
        if(hover_&&!project_.index().objects.count(*hover_)) {hover_.reset();hoverSource_.clear();++hoverRevision_;}
    }
    selected_=selection_.primary()?q(selection_.primary()->id):QString();
}

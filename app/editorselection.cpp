#include "editorcontroller.h"
#include <QCollator>
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
    // Presentation migration promotes the supported web fields into the common
    // model.  Keeping a second extension-based check here made rendering and
    // picking disagree whenever a retained payload was only partially known.
    return effectiveMapVisibility(project_.document(),ref);
}
QVariantList EditorController::objectRows() const {
    QVariantList rows;
    for(const auto& unit:project_.document().units) {
        const auto ref=territorialRef(unit.id);
        auto row=objectRefValue(ref);
        row["name"]=q(project_.propertyView(ref)->displayName);row["typeLabel"]=typeLabel(unit.kind);
        row["visible"]=objectVisible(ref);row["locked"]=unit.locked;
        const auto member=project_.document().presentation.membership.find(ref);
        if(member!=project_.document().presentation.membership.end()) {
            row["layerId"]=q(member->second);
            if(const auto layer=project_.layer(member->second)) row["locked"]=unit.locked||layer->locked;
        }
        row["editable"]=!row["locked"].toBool();
        row["selectionOnly"]=false;
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
    if(const auto u=selectedUnit()) {
        const auto ref=territorialRef(u->id);const auto& style=project_.document().presentation.objectStyles.at(ref);
        CountryDraft d{nameDraft_,memoDraft_,colorDraft_,opacityPreview_,validFromDraft_,validToDraft_};
        if(nameDraft_!=q(u->kind==UnitKind::Country?objectDisplayName(*u):u->name))d.fields.insert("name");
        if(memoDraft_!=q(u->notes))d.fields.insert("notes");
        if(colorDraft_!=rgb(effectiveObjectColor(project_.document(),ref)))d.fields.insert("color");
        if(opacityPreview_&&*opacityPreview_!=style.opacity)d.fields.insert("opacity");
        if(validFromDraft_!=q(u->validity.from.value_or("")))d.fields.insert("validFrom");
        if(validToDraft_!=q(u->validity.to.value_or("")))d.fields.insert("validTo");
        if(d.fields.empty())parkedCountryDrafts_.erase(selected_);else parkedCountryDrafts_[selected_]=std::move(d);
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
        if(draft.fields.count("name"))nameDraft_=draft.name;
        if(draft.fields.count("notes"))memoDraft_=draft.memo;
        if(draft.fields.count("color"))colorDraft_=draft.color;
        if(draft.fields.count("opacity"))opacityPreview_=draft.opacity;
        if(draft.fields.count("validFrom"))validFromDraft_=draft.from;
        if(draft.fields.count("validTo"))validToDraft_=draft.to;
    }
    if(const auto it=parkedLayerDrafts_.find(selectedLayer_);it!=parkedLayerDrafts_.end()) {
        const auto draft=it->second;parkedLayerDrafts_.erase(it);
        layerNameDraft_=draft.name;layerOpacityPreview_=draft.opacity;
    }
}
void EditorController::clearParkedDrafts() { parkedCountryDrafts_.clear();parkedLayerDrafts_.clear();fieldSessions_.clear(); }
void EditorController::applySelection(SelectionState next) {
    const bool changed=selection_.revision()!=next.revision();
    if(!changed) {selection_=std::move(next);return;} // anchor-only changes are silent in the web reducer
    QScopedValueRollback<bool> guard(selectionTransition_,true);
    cancelColorEdit();fieldSessions_.clear();parkDrafts();selection_=std::move(next);
    selected_=selection_.primary()?q(selection_.primary()->id):QString();
    reloadDrafts();
    emit selectionChanged();emit stateChanged();emit draftsChanged();emit visualChanged();
    // No dirtyChanged, project revision, history, import epoch, preview or worker mutation.
}
bool EditorController::selectObject(const QVariantMap& value,const QString& mode,const QString& scope,const QVariantList& ordered,bool additive) {
    const auto ref=existingObjectRef(value);if(!ref) return false;
    closeObjectChooser();
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
    closeObjectChooser();
    std::vector<ObjectRef> refs;
    for(const auto& v:values) if(const auto ref=existingObjectRef(v.toMap())) refs.push_back(*ref);
    auto next=selection_;next.setMany(refs,existingObjectRef(primary),scope.toStdString());applySelection(std::move(next));return true;
}
void EditorController::clearSelection() { closeObjectChooser();auto next=selection_;next.clear();applySelection(std::move(next)); }
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
    auto renderLayers=project_.layers();renderLayers.insert(renderLayers.begin(),Layer{"",""});
    for(auto layer=renderLayers.rbegin();layer!=renderLayers.rend();++layer) {
        if(!layer->visible) continue;
        std::optional<ObjectRef> top;
        double topRank=-std::numeric_limits<double>::infinity();
        for(const auto& unit:project_.document().units) {
            const auto ref=territorialRef(unit.id);
            if(nativeLayerId(project_.document(),ref)!=layer->id||!objectVisible(ref)) continue;
            const auto geometry=project_.document().geometries.get(unit.geometry);
            if(geometry && pointInCountry(point,geometry->polygons)) {
                const auto rank=territorialRenderOrder(project_.document(),ref);
                  if(!top || rank>=topRank) { top=ref;topRank=rank; }
            }
        }
        if(top) return objectRefValue(*top);
    }
    return {};
}
void EditorController::selectAt(double x,double y) { selectMapAt(x,y,false); }
void EditorController::selectMapAt(double x,double y,bool additive) {
    beginMapSelection(x,y,additive);
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
    closeObjectChooser();cancelColorEdit();fieldSessions_.clear();
    if(selectionInstance_!=project_.instanceId()) {
        selectionInstance_=project_.instanceId();selection_.reset();hover_.reset();hoverSource_.clear();hoverRevision_=0;
        searchQuery_.clear();clearParkedDrafts();
    } else {
        selection_.prune([this](const ObjectRef& ref){return project_.index().objects.count(ref)!=0;});
        if(hover_&&!project_.index().objects.count(*hover_)) {hover_.reset();hoverSource_.clear();++hoverRevision_;}
    }
    selected_=selection_.primary()?q(selection_.primary()->id):QString();
}

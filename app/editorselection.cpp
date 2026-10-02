#include "editorcontroller.h"
#include <pandoeditor/maprenderorder.h>
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
    if(id.isEmpty()) return {};
    const ObjectRef ref{domain.toStdString(),id.toStdString()};
    if(ref.domain=="hydroBuiltin"){
        const auto record=hydroRuntime_.recordById(id);
        const auto requested=value.value("type").toString().trimmed();
        if(!record||(!requested.isEmpty()&&requested!="hydroBuiltin"))return {};
        return ref;
    }
    const auto found=project_.index().objects.find(ref);
    if(found==project_.index().objects.end()) return {};
    const auto expected=ref.domain=="territorial"?typeName(project_.document().units.at(found->second).kind):q(ref.domain);
    const auto requested=value.value("type").toString().trimmed();
    if(!requested.isEmpty() && requested!=expected) return {};
    return ref;
}
QVariantMap EditorController::objectRefValue(const ObjectRef& ref) const {
    if(ref.domain=="hydroBuiltin"){
        const auto id=q(ref.id);
        return {{"domain","hydroBuiltin"},{"type","hydroBuiltin"},{"id",id},
            {"key",QStringLiteral("hydroBuiltin:hydroBuiltin:")+QString::fromLatin1(QUrl::toPercentEncoding(id,"-_.!~*'()"))}};
    }
    const auto it=project_.index().objects.find(ref);
    if(it==project_.index().objects.end()) return {{"domain",q(ref.domain)},{"id",q(ref.id)}};
    const auto type=ref.domain=="territorial"?typeName(project_.document().units.at(it->second).kind):q(ref.domain);
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
    if(ref.domain=="hydroBuiltin"){
        const auto record=hydroRuntime_.recordById(q(ref.id));
        if(!record)return false;
        const auto& hidden=project_.document().physicalData.hiddenHydroIds;
        if(std::find(hidden.begin(),hidden.end(),ref.id)!=hidden.end())return false;
        return groupVisible(project_.document().presentation.webPresentation,
            record->category=="lake"?"lakes":"rivers");
    }
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
    for(const auto& [ref,index]:project_.index().objects) if(ref.domain!="territorial") {
        auto row=objectRefValue(ref); const auto properties=project_.propertyView(ref);
        row["name"]=properties?q(properties->displayName):q(ref.id);
        row["typeLabel"]=ref.domain=="label"?QStringLiteral("지명"):ref.domain=="hydro"?QStringLiteral("수계"):
            ref.domain=="generic"?QStringLiteral("기타 객체"):ref.domain=="distributionLayer"?QStringLiteral("분포 레이어"):QStringLiteral("분포 항목");
        row["visible"]=objectVisible(ref); row["locked"]=objectLocked(project_.document(),project_.index(),ref);
        row["editable"]=!row["locked"].toBool(); row["selectionOnly"]=false; rows.append(row);
    }
    if(const auto frame=hydroRuntime_.frame()){
        std::set<QString> seen;
        for(const auto& feature:frame->features)if(const auto record=hydroRuntime_.recordByFid(feature.fid)){
            if(!seen.insert(record->awId).second)continue;
            const ObjectRef ref{"hydroBuiltin",record->awId.toStdString()};
            auto row=objectRefValue(ref);row["name"]=record->name;
            row["typeLabel"]=record->category=="lake"?QStringLiteral("호수"):QStringLiteral("강");
            row["visible"]=objectVisible(ref);row["locked"]=true;
            row["editable"]=false;row["selectionOnly"]=false;rows.append(row);
        }
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
    if(const auto metadata=hydroRuntime_.coreMetadata()){
        std::set<QString> seen;
        for(auto it=metadata->cbegin();it!=metadata->cend();++it){
            const auto& record=it.value();if(!seen.insert(record.awId).second)continue;
            if(!(record.name+" "+record.systemId+" "+record.awId).toLower().contains(query))continue;
            const ObjectRef ref{"hydroBuiltin",record.awId.toStdString()};
            auto row=objectRefValue(ref);row["name"]=record.name;
            row["typeLabel"]=record.category=="lake"?QStringLiteral("호수"):QStringLiteral("강");
            row["visible"]=objectVisible(ref);row["locked"]=true;
            row["editable"]=false;row["selectionOnly"]=false;rows.append(row);
        }
    }
    std::set<QString> resultIds;
    rows.erase(std::remove_if(rows.begin(),rows.end(),[&](const QVariant& value){
        const auto row=value.toMap();const auto key=row["key"].toString();return !resultIds.insert(key).second;
    }),rows.end());
    QCollator compare{QLocale{QLocale::Korean}};
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
QVariantMap EditorController::pickObject(double x,double y,double pixelsPerUnit,double zoom) const {
    if(!std::isfinite(x)||!std::isfinite(y)) return {};
    return pickObjectFromCandidates(mapCandidates(x,y,pixelsPerUnit,zoom));
}
QVariantMap EditorController::pickObjectScreen(double x,double y,double zoom) const {
    if(!std::isfinite(x)||!std::isfinite(y))return {};
    return pickObjectFromCandidates(mapCandidatesScreen(x,y,zoom));
}
QVariantMap EditorController::pickObjectFromCandidates(const std::vector<ObjectRef>& hits) const {
    const auto visuals=countryVisuals();
    std::optional<ObjectRef> top;int topLayer=std::numeric_limits<int>::min();double topRank=-std::numeric_limits<double>::infinity();
    for(const auto& path:projection_.paths) {
        const auto row=path.toMap();const ObjectRef ref=row.contains("domain")?ObjectRef{row["domain"].toString().toStdString(),row["objectId"].toString().toStdString()}:territorialRef(row["countryId"].toString().toStdString());
        if(std::find(hits.begin(),hits.end(),ref)==hits.end())continue;
        const auto visual=visuals[row["countryId"].toString()].toMap();const auto layer=visual["layerOrder"].toInt();
        const auto rank=visual["drawFillPass"].toDouble()+visual["drawObject"].toDouble();
        if(!top||layer>topLayer||(layer==topLayer&&rank>=topRank)){top=ref;topLayer=layer;topRank=rank;}
    }
    for(const auto& ref:hits)if(ref.domain=="hydroBuiltin" &&
        (!top||pandoeditor::mapBuiltinHydroPickOrder()>pandoeditor::mapPickOrder(project_.document(),*top))) {
        top=ref;break;
    }
    if(top&&top->domain=="distributionEntry")for(const auto& entry:project_.document().distributionEntries)if(entry.id==top->id){top=ObjectRef{"distributionLayer",entry.layerId};break;}
    return top?objectRefValue(*top):QVariantMap{};
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
    if(ref->domain=="hydroBuiltin"){
        const auto record=hydroRuntime_.recordById(q(ref->id));
        if(!record||record->bounds.size()!=4)return false;
        const auto topLeft=projection_.project({record->bounds[0],record->bounds[3]});
        const auto bottomRight=projection_.project({record->bounds[2],record->bounds[1]});
        const auto width=std::max(.001,bottomRight.x-topLeft.x);
        const auto height=std::max(.001,bottomRight.y-topLeft.y);
        focusMapCameraRect(topLeft.x,topLeft.y,width,height,mobileMode_?12:10);
        emit focusRequested(topLeft.x,topLeft.y,width,height,mobileMode_?12:10);
        return true;
    }
    for(const auto& path:projection_.paths) {
        const auto p=path.toMap();
        if(ref->domain=="territorial" ? p["countryId"].toString()!=q(ref->id) :
            (p["domain"].toString()!=q(ref->domain) || p["objectId"].toString()!=q(ref->id))) continue;
        focusMapCameraRect(p["left"].toDouble(),p["top"].toDouble(),p["width"].toDouble(),p["height"].toDouble(),mobileMode_?12:10);
        emit focusRequested(p["left"].toDouble(),p["top"].toDouble(),p["width"].toDouble(),p["height"].toDouble(),mobileMode_?12:10);
        return true;
    }
    return false;
}
void EditorController::reconcileSelection() {
    const bool replaced=selectionInstance_!=project_.instanceId();
    // Cancellation emits synchronous QML notifications. Drop references to the
    // old document before any of those notifications can read object properties.
    if(replaced) {
        selectionInstance_=project_.instanceId();selection_.reset();hover_.reset();hoverSource_.clear();hoverRevision_=0;
        searchQuery_.clear();clearParkedDrafts();
    } else {
        selection_.prune([this](const ObjectRef& ref){return ref.domain=="hydroBuiltin"?
            bool(hydroRuntime_.recordById(q(ref.id))):project_.index().objects.count(ref)!=0;});
        if(hover_&&!(hover_->domain=="hydroBuiltin"?bool(hydroRuntime_.recordById(q(hover_->id))):
            project_.index().objects.count(*hover_))) {hover_.reset();hoverSource_.clear();++hoverRevision_;}
    }
    selected_=selection_.primary()?q(selection_.primary()->id):QString();
    closeObjectChooser();cancelColorEdit();fieldSessions_.clear();
    if(replaced) {cancelContentEdit();cancelGeometryEdit();}
}

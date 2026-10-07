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
QString typeName(UnitKind kind) { return kind==UnitKind::General?"general":"regional"; }
QString typeLabel(UnitKind kind) { return kind==UnitKind::General?QStringLiteral("일반객체"):QStringLiteral("독립 권역"); }
}
std::optional<ObjectRef> EditorController::existingObjectRef(const QVariantMap& value) const {
    const auto domain=value.value("domain").toString().trimmed();
    const auto id=value.value("id").toString().trimmed();
    if(id.isEmpty()) return {};
    const ObjectRef ref{domain.toStdString(),id.toStdString()};
    if(ref.domain=="placeBuiltin"){
        const auto requested=value.value("type").toString().trimmed();
        if(!placeRuntime_.recordById(id)||copiedPlaceSourceIds(project_.document()).count(ref.id)||(!requested.isEmpty()&&requested!="placeBuiltin"))return {};
        return ref;
    }
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
    if(ref.domain=="placeBuiltin"){
        const auto id=q(ref.id);return {{"domain","placeBuiltin"},{"type","placeBuiltin"},{"id",id},
            {"key",QStringLiteral("placeBuiltin:placeBuiltin:")+QString::fromLatin1(QUrl::toPercentEncoding(id,"-_.!~*'()"))}};
    }
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
    if(ref.domain=="placeBuiltin")return placeRuntime_.recordById(q(ref.id)).has_value()&&
        !copiedPlaceSourceIds(project_.document()).count(ref.id)&&groupVisible(project_.document().presentation.webPresentation,"labels");
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
    if(objectRowsCache_)return *objectRowsCache_;
    QVariantList rows;
    for(const auto& unit:project_.document().units) {
        const auto ref=territorialRef(unit.id);
        auto row=objectRefValue(ref);
        row["flagSource"]=labelFlagSource(ref);row["color"]=rgb(effectiveObjectColor(project_.document(),ref));
        row["name"]=q(project_.propertyView(ref)->displayName);row["typeLabel"]=typeLabel(unit.kind);
        row["visible"]=objectVisible(ref);row["locked"]=unit.locked;row["lockEnabled"]=true;
        const auto member=project_.document().presentation.membership.find(ref);
        if(member!=project_.document().presentation.membership.end()) {
            row["layerId"]=q(member->second);
            if(const auto layer=project_.layer(member->second)) {row["locked"]=unit.locked||layer->locked;row["lockEnabled"]=!layer->locked;}
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
    std::set<QString> placeIds;const auto copied=copiedPlaceSourceIds(project_.document());
    const auto appendPlace=[&](const PlaceRecord& record) {
        if(copied.count(record.id.toStdString())||!placeIds.insert(record.id).second)return;
        const ObjectRef ref{"placeBuiltin",record.id.toStdString()};auto item=objectRefValue(ref);
        item["name"]=record.name;item["sourceName"]=record.name;item["kind"]=record.kind;item["typeLabel"]=QStringLiteral("기본 지명");
        item["visible"]=objectVisible(ref);item["locked"]=true;item["editable"]=false;item["lockEnabled"]=false;
        item["selectionOnly"]=false;item["builtinSource"]=true;item["copyAvailable"]=true;rows.append(item);
    };
    if(const auto snapshot=placeRuntime_.snapshot())for(const auto& record:snapshot->records)appendPlace(record);
    for(const auto& ref:selection_.items())if(ref.domain=="placeBuiltin")if(const auto record=placeRuntime_.recordById(q(ref.id)))appendPlace(*record);
    objectRowsCache_=rows;return rows;
}
void EditorController::setSearchQuery(const QString& query) {
    if(query==searchQuery_) return;
    searchQuery_=query;
    if(query.isEmpty())placeRuntime_.cancelSearch();else placeRuntime_.search(query);
    emit searchChanged();
}
QVariantList EditorController::searchResults() const {
    const auto query=searchQuery_.trimmed().toLower();
    if(query.isEmpty()) return {};
    QVariantList rows;
    for(const auto& value:objectRows()) {
        auto row=value.toMap();
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
    const auto copied=copiedPlaceSourceIds(project_.document());
    for(const auto& record:placeRuntime_.searchResults())if(!copied.count(record.id.toStdString())){
        const ObjectRef ref{"placeBuiltin",record.id.toStdString()};auto item=objectRefValue(ref);
        item["name"]=record.name;item["sourceName"]=record.name;item["kind"]=record.kind;item["typeLabel"]=QStringLiteral("기본 지명");
        item["visible"]=objectVisible(ref);item["locked"]=true;item["editable"]=false;item["lockEnabled"]=false;item["selectionOnly"]=false;
        item["builtinSource"]=true;item["copyAvailable"]=true;rows.append(item);
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
        if(nameDraft_!=q(u->kind==UnitKind::General?objectDisplayName(*u):u->name))d.fields.insert("name");
        if(memoDraft_!=q(u->notes))d.fields.insert("notes");
        if(colorDraft_!=rgb(effectiveObjectColor(project_.document(),ref)))d.fields.insert("color");
        if(opacityPreview_&&*opacityPreview_!=style.opacity)d.fields.insert("opacity");
        if(validFromDraft_!=q(pandoeditor::staticLifetime(project_.document(),u->id).validity.from.value_or("")))d.fields.insert("validFrom");
        if(validToDraft_!=q(pandoeditor::staticLifetime(project_.document(),u->id).validity.to.value_or("")))d.fields.insert("validTo");
        if(d.fields.empty())parkedCountryDrafts_.erase(selectedId());else parkedCountryDrafts_[selectedId()]=std::move(d);
    }
    if(const auto layer=project_.layer(selectedLayer_.toStdString())) {
        if(layerNameDraft_!=q(layer->name)||(layerOpacityPreview_&&*layerOpacityPreview_!=layer->opacity))
            parkedLayerDrafts_[selectedLayer_]={layerNameDraft_,layerOpacityPreview_};
        else parkedLayerDrafts_.erase(selectedLayer_);
    }
}
void EditorController::restoreParkedDrafts() {
    if(const auto it=parkedCountryDrafts_.find(selectedId());it!=parkedCountryDrafts_.end()) {
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
void EditorController::clearParkedDrafts() {
    parkedCountryDrafts_.clear();parkedLayerDrafts_.clear();
    const bool editing=!fieldSessions_.empty();fieldSessions_.clear();
    if(editing)refreshTypedScene();
}
void EditorController::applySelection(SelectionState next) {
    const bool changed=selection_.revision()!=next.revision();
    if(!changed) {selection_=std::move(next);return;} // anchor-only changes are silent in the web reducer
    QScopedValueRollback<bool> guard(selectionTransition_,true);
    cancelColorEdit();fieldSessions_.clear();parkDrafts();selection_=std::move(next);
    requestPlaceResources();refreshBuiltinPlaceLabels();
    reloadDrafts();
    emit selectionChanged();
    emit stateChanged();emit draftsChanged();
    emit visualChanged();
    // Child split previews bind to the selection identity observed at request
    // time. Refresh their read-only readiness when that identity changes.
    if(geometryEdit_&&geometryEdit_->splitIntent&&!staticParentRelation(geometryEdit_->base.document(),geometryEdit_->target.id).parentId.empty())emit geometryEditChanged();
    // No dirtyChanged, project revision, history, import epoch or content preview mutation.
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
    selectObject({{"domain","territorial"},{"type","general"},{"id",id}},"replace","countries");
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
    auto candidates=mapCandidatesScreen(x,y,zoom);
    if(geometryEdit_&&geometryEdit_->choosingProviders) {
        const auto& document=project_.document();const auto& index=project_.index();
        const auto target=index.objects.find(geometryEdit_->target);if(target==index.objects.end())return {};
        const auto kind=document.units.at(target->second).kind;
        candidates.erase(std::remove_if(candidates.begin(),candidates.end(),[&](const auto& ref){
            const auto found=index.objects.find(ref);
            return ref.domain!="territorial"||ref==geometryEdit_->target||found==index.objects.end()||
                document.units.at(found->second).kind!=kind||
                (geometryEdit_->territorySelection&&!isRootGeneral(document,document.units.at(found->second)))||objectLocked(document,index,ref);
        }),candidates.end());
    }
    return pickObjectFromCandidates(candidates);
}
QVariantMap EditorController::pickObjectFromCandidates(
    const std::vector<ObjectRef>& hits) const {
    const auto top=mapPicker_.topCandidate(project_.snapshot(),hits);
    for(const auto& ref:hits)if(ref.domain=="placeBuiltin"&&objectVisible(ref)&&(!top||mapPicker_.candidateRank(project_.snapshot(),*top)<=mapPickOrder(project_.document(),{"label",ref.id})))return objectRefValue(ref);
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
    hover_=next;hoverSource_=next?source:QString();requestPlaceResources();++hoverRevision_;emit hoverChanged();return true;
}
bool EditorController::focusObject(const QVariantMap& value) {
    const auto ref=value.isEmpty()?selection_.primary():existingObjectRef(value);
    if(!ref) return false;
    if(ref->domain=="placeBuiltin"){
        const auto record=placeRuntime_.recordById(q(ref->id));if(!record||!objectVisible(*ref))return false;
        const auto point=projection_.project(record->coordinates);focusMapCameraRect(point.x,point.y,.001,.001,mobileMode_?12:10);
        emit focusRequested(point.x,point.y,.001,.001,mobileMode_?12:10);return true;
    }
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
        labelEngine_.clear();++labelCounterGeneration_;labelFlagSources_.clear();labelSourcesDirty_=true;
        if(!placedLabels_.isEmpty()){placedLabels_.clear();emit labelLayoutChanged();}
    } else {
        selection_.prune([this](const ObjectRef& ref){if(ref.domain=="placeBuiltin")return placeRuntime_.recordById(q(ref.id)).has_value()&&!copiedPlaceSourceIds(project_.document()).count(ref.id);return ref.domain=="hydroBuiltin"?
            bool(hydroRuntime_.recordById(q(ref.id))):project_.index().objects.count(ref)!=0;});
        if(hover_&&!(hover_->domain=="placeBuiltin"?placeRuntime_.recordById(q(hover_->id)).has_value()&&!copiedPlaceSourceIds(project_.document()).count(hover_->id):hover_->domain=="hydroBuiltin"?bool(hydroRuntime_.recordById(q(hover_->id))):
            project_.index().objects.count(*hover_))) {hover_.reset();hoverSource_.clear();++hoverRevision_;}
    }
    closeObjectChooser();cancelColorEdit();fieldSessions_.clear();
    if(replaced) {cancelContentEdit();cancelGeometryEdit();}
}

#include "editorcontroller.h"
#include "geometrycalculator.h"
#include <pandoeditor/geometrypredicates.h>
#include <algorithm>
#include <cmath>

using namespace pandoeditor;
namespace {
QString methodName(TerritorySelectionMethod method) {
    switch(method){case TerritorySelectionMethod::Line:return "line";case TerritorySelectionMethod::Polygon:return "polygon";case TerritorySelectionMethod::Components:return "components";default:return {};}
}
TerritorySelectionMethod methodValue(const QString& value) {
    if(value=="line")return TerritorySelectionMethod::Line;
    if(value=="polygon")return TerritorySelectionMethod::Polygon;
    if(value=="components")return TerritorySelectionMethod::Components;
    return TerritorySelectionMethod::None;
}
bool selected(const std::vector<std::string>& ids,const std::string& id){return std::find(ids.begin(),ids.end(),id)!=ids.end();}
bool pointInRing(const Ring& ring,Point point){bool inside=false;for(std::size_t i=0,j=ring.size()?ring.size()-1:0;i<ring.size();j=i++) {const auto a=ring[i],b=ring[j];if((a.y>point.y)!=(b.y>point.y)&&point.x<(b.x-a.x)*(point.y-a.y)/(b.y-a.y)+a.x)inside=!inside;}return inside;}
// The existing two-crossing kernel cannot interpret hole crossings, wrapped
// edges or self-crossing strokes. Keep these explicit M9.7.3 gaps instead of
// publishing nominal candidates that polygon clipping silently repairs.
bool segmentsTouch(Point a,Point b,Point c,Point d) {
    const auto cross=[](Point p,Point q,Point r){return (q.x-p.x)*(r.y-p.y)-(q.y-p.y)*(r.x-p.x);};
    const auto within=[](double x,double a,double b){return x>=std::min(a,b)-1e-10&&x<=std::max(a,b)+1e-10;};
    const auto on=[&](Point p,Point a,Point b){return std::abs(cross(a,b,p))<=1e-10&&within(p.x,a.x,b.x)&&within(p.y,a.y,b.y);};
    const auto x=cross(a,b,c),y=cross(a,b,d),z=cross(c,d,a),w=cross(c,d,b);
    return ((x>0&&y<0||x<0&&y>0)&&(z>0&&w<0||z<0&&w>0))||on(a,c,d)||on(b,c,d)||on(c,a,b)||on(d,a,b);
}
std::string unsupportedSimpleCut(const Geometry& source,const Ring& line,const GeometryCancellation& cancelled) {
    for(std::size_t i=1;i<line.size();++i){
        if(cancelled&&cancelled())return "CANCELLED";
        if(std::abs(line[i].x-line[i-1].x)>180)return "CUT_DATELINE_UNSUPPORTED";
        for(std::size_t j=i+2;j<line.size();++j)if(segmentsTouch(line[i-1],line[i],line[j-1],line[j]))return "CUT_SELF_INTERSECTION";
        for(const auto& polygon:source.polygons)for(std::size_t r=0;r<polygon.size();++r)for(std::size_t p=1;p<polygon[r].size();++p){
            if(std::abs(polygon[r][p].x-polygon[r][p-1].x)>180)return "CUT_DATELINE_UNSUPPORTED";
            if(r&&segmentsTouch(line[i-1],line[i],polygon[r][p-1],polygon[r][p]))return "CUT_SPLITS_HOLE";
        }
    }
    return {};
}
bool onSegment(Point point,Point a,Point b) {
    const auto dx=b.x-a.x,dy=b.y-a.y,length=dx*dx+dy*dy;if(length==0)return false;
    const auto t=((point.x-a.x)*dx+(point.y-a.y)*dy)/length;
    return t>=-1e-10&&t<=1+1e-10&&std::abs(dx*(point.y-a.y)-dy*(point.x-a.x))<=1e-9;
}
bool onGeometryBoundary(const Geometry& geometry,Point point){for(const auto& polygon:geometry.polygons)for(const auto& ring:polygon)for(std::size_t i=1;i<ring.size();++i)if(onSegment(point,ring[i-1],ring[i]))return true;return false;}
void orderSimpleCutLikeWeb(const Geometry& source,SplitGeometryResult& split) {
    // createCutGraph inserts source ring edges in original order; bounded-face
    // tracing first visits the face incident to the first nonempty sub-edge.
    const auto& ring=source.polygons.at(split.componentIndex).front();
    for(std::size_t i=1;i<ring.size();++i){const auto a=ring[i-1],b=ring[i];const auto dx=b.x-a.x,dy=b.y-a.y,length=dx*dx+dy*dy;if(length==0)continue;
        std::vector<double> cuts{0,1};for(const auto& candidate:split.candidates)for(const auto& polygon:candidate.polygons)for(const auto& boundary:polygon)for(const auto point:boundary)if(onSegment(point,a,b))cuts.push_back(std::clamp(((point.x-a.x)*dx+(point.y-a.y)*dy)/length,0.,1.));
        std::sort(cuts.begin(),cuts.end());for(std::size_t j=1;j<cuts.size();++j){if(cuts[j]-cuts[j-1]<=1e-10)continue;const auto t=(cuts[j]+cuts[j-1])/2;const Point sample{a.x+dx*t,a.y+dy*t};const bool first=onGeometryBoundary(split.candidates[0],sample),second=onGeometryBoundary(split.candidates[1],sample);if(first!=second){if(second)std::swap(split.candidates[0],split.candidates[1]);return;}}
    }
}
bool containsPoint(const Geometry& geometry,Point point){for(const auto& polygon:geometry.polygons){if(polygon.empty()||!pointInRing(polygon.front(),point))continue;bool hole=false;for(std::size_t i=1;i<polygon.size();++i)if(pointInRing(polygon[i],point))hole=true;if(!hole)return true;}return false;}
}
bool EditorController::territoryGeometryReady() const {
    if(!geometryEdit_||!geometryEdit_->territorySelection)return false;
    const auto& edit=*geometryEdit_;const auto& selection=*edit.territorySelection;const auto& state=selection.state();
    if(edit.selectionPending||!selection.derivedReady()||(edit.stage!="selection"&&edit.stage!="review"))return false;
    const bool draftActive=!edit.lineDraft.empty()||!edit.draft.polygons.empty();
    if(edit.stage=="review")return state.combinedGeometry.has_value()&&!draftActive;
    if(state.activePhase==TerritorySelectionPhase::Components)
        return (!state.useRiverBoundaries||state.riverStatus==TerritoryRiverStatus::Ready)&&
            !state.selectedComponentKeys.empty()&&state.currentGeometry.has_value();
    if(state.activePhase==TerritorySelectionPhase::Candidate&&state.selectedCandidateIds.empty())return false;
    if(state.activePhase==TerritorySelectionPhase::Drawing||edit.choosingProviders)return false;
    return state.combinedGeometry.has_value()&&!draftActive;
}
QVariantMap EditorController::territorySelectionState() const {
    const auto& edit=*geometryEdit_;const auto& selection=*edit.territorySelection;const auto& state=selection.state();
    const bool ready=territoryGeometryReady()&&edit.territoryPreview&&edit.territoryPreview->ok()&&!edit.territoryPreview->blocking()&&edit.previewSelectionRevision==state.revision&&edit.base.matches(project_);
    QVariantList providers,candidates,components,parts,selectedCandidates,selectedComponents;
    for(const auto& id:state.selectedCandidateIds)selectedCandidates.append(QString::fromStdString(id));
    for(const auto& id:state.selectedComponentKeys)selectedComponents.append(QString::fromStdString(id));
    for(const auto& source:state.sources){auto row=objectRefValue(source.ref);row["name"]=QString::fromStdString(source.name);providers.append(row);}
    for(const auto& item:state.candidates)candidates.append(QVariantMap{{"id",QString::fromStdString(item.id)},{"label",QStringLiteral("선택 영역 %1").arg(candidates.size()+1)},{"selected",selected(state.selectedCandidateIds,item.id)},{"area",item.area.value_or(planarArea(item.geometry))}});
    for(const auto& item:selection.activeComponents())components.append(QVariantMap{{"key",QString::fromStdString(item.key)},{"label",QString::fromStdString(item.countryName)+QStringLiteral(" · %1").arg(item.sourcePolygonIndex+1)},{"selected",selected(state.selectedComponentKeys,item.key)},{"area",planarArea(item.geometry)},{"partitionKind",QString::fromStdString(item.partitionKind)},{"sourcePolygonIndex",int(item.sourcePolygonIndex)},{"polygonIndex",int(item.polygonIndex)},{"provenance",QString::fromStdString(item.provenanceJson)}});
    for(const auto& item:state.parts)parts.append(QVariantMap{{"id",QString::fromStdString(item.id)},{"label",QStringLiteral("선택 영토 %1").arg(parts.size()+1)},{"method",methodName(item.method)},{"area",planarArea(item.geometry)}});
    const auto phase=edit.choosingProviders?QString("sources"):state.activePhase==TerritorySelectionPhase::Drawing?QString("drawing"):state.activePhase==TerritorySelectionPhase::Candidate?QString("candidates"):state.activePhase==TerritorySelectionPhase::Components?QString("components"):state.activePhase==TerritorySelectionPhase::Result?QString("result"):QString();
    const bool idle=!edit.selectionPending&&!edit.previewPending&&!edit.applying&&!edit.riverPreparation;
    const auto riverStatus=state.riverStatus==TerritoryRiverStatus::Pending?"pending":state.riverStatus==TerritoryRiverStatus::Ready?"ready":state.riverStatus==TerritoryRiverStatus::SourceError?"sourceError":state.riverStatus==TerritoryRiverStatus::Error?"error":state.riverStatus==TerritoryRiverStatus::Unavailable?"unavailable":"idle";
    const bool confirmation=edit.sourceChange.has_value()||state.methodChangeConfirmation.has_value();
    const bool emptyDraftWithParts=edit.lineDraft.empty()&&edit.draft.polygons.empty()&&!state.parts.empty();
    const bool draftReady=state.activeMethod==TerritorySelectionMethod::Line?edit.lineDraft.size()>=2:!edit.draft.polygons.empty()&&!edit.draft.polygons.front().empty()&&edit.draft.polygons.front().front().size()>=4;
    auto target=objectRefValue(edit.target);for(const auto& unit:project_.document().units)if(unit.id==edit.target.id)target["name"]=QString::fromStdString(objectDisplayName(unit));
    return {{"active",true},{"territorySelection",true},{"tool","annex"},{"stage",edit.stage},{"choosingProviders",edit.choosingProviders},{"providers",providers},{"target",target},{"targets",QVariantList{target}},{"activeMethod",methodName(state.activeMethod)},{"requestedMethod",methodName(state.requestedMethod)},{"selectionPhase",phase},{"candidates",candidates},{"components",components},{"parts",parts},{"selectedCandidateIds",selectedCandidates},{"selectedComponentKeys",selectedComponents},
        {"confirmationKind",edit.sourceChange?QString("settings"):state.methodChangeConfirmation?QString("method"):QString()},{"selectionPending",edit.selectionPending},{"previewPending",edit.previewPending},{"previewReady",ready},{"previewBlocking",edit.territoryPreview&&edit.territoryPreview->blocking()},{"calculating",!idle},{"applying",edit.applying},{"error",edit.error},{"selectedVertex",-1},
        {"canFinishDraft",edit.stage=="selection"&&phase=="drawing"&&(draftReady||emptyDraftWithParts)&&selection.derivedReady()&&idle&&!confirmation},{"canAddPart",edit.stage=="selection"&&ready&&idle&&!confirmation&&selection.archiveReadiness()==TerritoryArchiveReadiness::Ready},
        {"canAdvance",!confirmation&&!edit.applying&&(edit.stage=="setup"?!state.sources.empty():edit.stage=="selection"&&ready&&idle&&!selection.candidateRequiresArchival())},{"canApply",edit.stage=="review"&&ready&&idle&&!confirmation},
        {"canUndoDraft",!edit.lineDraft.empty()||!edit.undo.empty()},{"canUndo",!edit.lineDraft.empty()||!edit.undo.empty()||!state.selectedCandidateIds.empty()||!state.selectedComponentKeys.empty()||!state.parts.empty()},{"canRedo",!edit.redo.empty()},{"riverCacheHit",edit.riverCacheHit},{"riverCacheEntries",int(riverCache_.size())},{"riverSourceDispatches",qulonglong(edit.riverSourceDispatches)},{"riverKernelDispatches",qulonglong(edit.riverKernelDispatches)},{"useRiverBoundaries",state.useRiverBoundaries},{"riverStatus",riverStatus},{"riverError",QString::fromStdString(state.riverDetail)},{"riverWarning",edit.riverWarning},{"canToggleRiverBoundaries",edit.stage=="selection"&&state.activePhase==TerritorySelectionPhase::Components&&!edit.applying&&!confirmation},{"canRetryRiverPartitions",state.useRiverBoundaries&&!edit.riverPreparation&&!edit.applying&&!confirmation&&edit.stage=="selection"},{"autoIncludedSliverCount",ready?int(edit.territoryPreview->autoIncludedSliverCount):0},{"autoIncludedSliverAreaM2",ready?edit.territoryPreview->autoIncludedSliverAreaM2:0},{"transferArea",ready?planarArea(edit.territoryPreview->transferredGeometry):0},{"transferAreaKm2",ready?edit.territoryPreview->transferAreaKm2:0}};
}
QVariantList EditorController::territorySelectionPaths() const {
    QVariantList paths;const auto& edit=*geometryEdit_;const auto& selection=*edit.territorySelection;const auto& state=selection.state();
    auto append=[&](const Geometry& geometry,const QString& kind,const QString& id,bool picked,bool preview=false){for(const auto& polygon:geometry.polygons){QString path;for(const auto& ring:polygon){for(std::size_t i=0;i<ring.size();++i){const auto point=projection_.project(ring[i]);path+=QString("%1%2 %3 ").arg(i?"L":"M").arg(point.x,0,'g',17).arg(point.y,0,'g',17);}path+="Z ";}paths.append(QVariantMap{{"path",path},{"vertices",QVariantList{}},{"id",id},{"key",id},{"selectionKind",kind},{"selected",picked},{"preview",preview},{"hole",false}});}};
    if(edit.stage=="review"&&edit.territoryPreview){for(const auto& row:edit.territoryPreview->rows)if(row.after)append(*row.after,"preview",QString::fromStdString(row.owner.id),true,true);if(edit.territoryPreview->autoIncludedSliverCount)append(edit.territoryPreview->transferredGeometry,"transfer-preview","receipt-transfer",true,true);return paths;}
    if(edit.territoryPreview&&edit.territoryPreview->ok()&&edit.previewSelectionRevision==state.revision&&edit.territoryPreview->autoIncludedSliverCount)append(edit.territoryPreview->transferredGeometry,"transfer-preview","receipt-transfer",true,true);
    for(const auto& part:state.parts)append(part.geometry,"part",QString::fromStdString(part.id),true);
    if(state.activePhase==TerritorySelectionPhase::Candidate)for(const auto& item:state.candidates)append(item.geometry,"candidate",QString::fromStdString(item.id),selected(state.selectedCandidateIds,item.id));
    if(state.activePhase==TerritorySelectionPhase::Components)for(const auto& item:selection.activeComponents())append(item.geometry,"component",QString::fromStdString(item.key),selected(state.selectedComponentKeys,item.key));
    if(state.activePhase==TerritorySelectionPhase::Drawing){if(state.activeMethod==TerritorySelectionMethod::Line){QString path;QVariantList vertices;for(std::size_t i=0;i<edit.lineDraft.size();++i){const auto point=projection_.project(edit.lineDraft[i]);path+=QString("%1%2 %3 ").arg(i?"L":"M").arg(point.x,0,'g',17).arg(point.y,0,'g',17);vertices.append(QVariantMap{{"x",point.x},{"y",point.y},{"vertex",int(i)}});}paths.append(QVariantMap{{"path",path},{"vertices",vertices},{"line",true},{"hole",false}});}else {for(const auto& polygon:edit.draft.polygons)for(const auto& ring:polygon){QString path;QVariantList vertices;const auto count=ring.size()>1&&ring.front().x==ring.back().x&&ring.front().y==ring.back().y?ring.size()-1:ring.size();for(std::size_t i=0;i<count;++i){const auto point=projection_.project(ring[i]);path+=QString("%1%2 %3 ").arg(i?"L":"M").arg(point.x,0,'g',17).arg(point.y,0,'g',17);vertices.append(QVariantMap{{"x",point.x},{"y",point.y},{"vertex",int(i)}});}if(count>=3)path+="Z";paths.append(QVariantMap{{"path",path},{"vertices",vertices},{"hole",false},{"selectionKind","draft"}});}}}
    return paths;
}
void EditorController::cancelTerritoryCalculation(bool discardPreview){
    if(!geometryEdit_||!geometryEdit_->territorySelection)return;auto& edit=*geometryEdit_;
    cancelRiverPreparation();++edit.computationEpoch;++edit.previewEpoch;if(edit.job){jobs_->cancel(edit.job->id());edit.job.reset();}edit.selectionPending=false;edit.previewPending=false;
    if(discardPreview)edit.territoryPreview.reset();
}
void EditorController::scheduleTerritorySelection(bool requestPreview){
    if(!geometryEdit_||!geometryEdit_->territorySelection)return;cancelTerritoryCalculation();auto& edit=*geometryEdit_;edit.error.clear();edit.selectionPending=true;
    const auto generation=edit.generation,epoch=edit.computationEpoch;
    QTimer::singleShot(0,this,[this,generation,epoch,requestPreview]{
        if(!geometryEdit_||!geometryEdit_->territorySelection||geometryEdit_->generation!=generation||geometryEdit_->computationEpoch!=epoch)return;
        auto& current=*geometryEdit_;const auto state=current.territorySelection->state();
        current.job=jobs_->submitGeometry(current.base,"territorial:selection",[state](const ProjectSnapshot&,const JobToken& token)->GeometryJobResult{return rebuildTerritorySelection(state,[&token]{return token.cancelled();});},
        [this,generation,epoch,requestPreview](std::uint64_t id,JobDisposition disposition,GeometryJobResult result){
            if(!geometryEdit_||!geometryEdit_->territorySelection||geometryEdit_->generation!=generation||geometryEdit_->computationEpoch!=epoch||!geometryEdit_->job||geometryEdit_->job->id()!=id)return;
            auto& edit=*geometryEdit_;edit.job.reset();edit.selectionPending=false;
            if(disposition!=JobDisposition::Accepted||!edit.base.matches(project_)){emit geometryEditChanged();return;}
            if(auto derived=std::get_if<TerritorySelectionDerivedResult>(&result)){if(!edit.territorySelection->installDerived(std::move(*derived))){edit.error=QString::fromStdString(edit.territorySelection->lastError());edit.territorySelection->cancelRequestedMethod();}else {prepareRiverPartitions();if(requestPreview&&!edit.riverPreparation)scheduleTerritoryPreview();}}
            else if(auto failure=std::get_if<GeometryJobFailure>(&result)){edit.error=QString::fromStdString(failure->detail);edit.territorySelection->cancelRequestedMethod();}
            emit geometryEditChanged();
        });emit geometryEditChanged();
    });emit geometryEditChanged();
}
void EditorController::scheduleTerritoryPreview(){
    if(!geometryEdit_||!geometryEdit_->territorySelection)return;auto& edit=*geometryEdit_;const auto& selection=*edit.territorySelection;
    if(!territoryGeometryReady()||edit.applying)return;
    ++edit.previewEpoch;edit.territoryPreview.reset();edit.previewPending=true;
    const auto generation=edit.generation,epoch=edit.previewEpoch,revision=selection.state().revision;
    QTimer::singleShot(300,this,[this,generation,epoch,revision]{
        if(!geometryEdit_||!geometryEdit_->territorySelection||geometryEdit_->generation!=generation||geometryEdit_->previewEpoch!=epoch||geometryEdit_->territorySelection->state().revision!=revision)return;
        auto& edit=*geometryEdit_;const auto& state=edit.territorySelection->state();if(!territoryGeometryReady()){edit.previewPending=false;emit geometryEditChanged();return;}
        AnnexGeometryPreviewRequest request;request.target=edit.target;request.selection=*state.combinedGeometry;request.riverSliverContext=edit.territorySelection->riverSliverContext();for(const auto& source:state.sources)request.donors.push_back(source.ref);
        edit.job=jobs_->submitGeometry(edit.base,"territorial:selection-preview",[request](const ProjectSnapshot& snapshot,const JobToken& token)->GeometryJobResult{return calculateAnnexGeometryPreview(snapshot,request,token);},
        [this,generation,epoch,revision](std::uint64_t id,JobDisposition disposition,GeometryJobResult result){
            if(!geometryEdit_||!geometryEdit_->territorySelection||geometryEdit_->generation!=generation||geometryEdit_->previewEpoch!=epoch||geometryEdit_->territorySelection->state().revision!=revision||!geometryEdit_->job||geometryEdit_->job->id()!=id)return;
            auto& edit=*geometryEdit_;edit.job.reset();edit.previewPending=false;
            if(disposition!=JobDisposition::Accepted||!edit.base.matches(project_)){emit geometryEditChanged();return;}
            if(auto preview=std::get_if<AnnexGeometryPreviewResult>(&result)){edit.error=QString::fromStdString(preview->detail);edit.territoryPreview=std::move(*preview);edit.previewSelectionRevision=revision;
                if(!edit.territoryPreview->ok()||edit.territoryPreview->blocking())edit.territorySelection->cancelRequestedMethod();
                if(edit.territoryPreview->ok()&&!edit.territoryPreview->blocking()&&edit.territorySelection->state().activePhase==TerritorySelectionPhase::Components&&edit.territorySelection->state().requestedMethod!=TerritorySelectionMethod::None&&edit.territorySelection->state().requestedMethod!=edit.territorySelection->state().activeMethod) {const auto requested=edit.territorySelection->state().requestedMethod;if(edit.territorySelection->addPart()){edit.territorySelection->requestMethod(requested);edit.draft={"Polygon",{},{},{}};edit.lineDraft.clear();scheduleTerritorySelection(false);}}
            }else if(auto failure=std::get_if<GeometryJobFailure>(&result)){edit.error=QString::fromStdString(failure->detail);edit.territorySelection->cancelRequestedMethod();}
            emit geometryEditChanged();
        });emit geometryEditChanged();
    });emit geometryEditChanged();
}
bool EditorController::toggleTerritorySource(const ObjectRef& ref,bool confirmed){
    if(!geometryEdit_||!geometryEdit_->territorySelection||geometryEdit_->applying||!geometryEdit_->base.matches(project_)||ref==geometryEdit_->target)return false;
    const auto found=project_.index().objects.find(ref);if(found==project_.index().objects.end()||ref.domain!="territorial"||objectLocked(project_.document(),project_.index(),ref))return false;
    const auto& unit=project_.document().units.at(found->second);if(!isRootGeneral(project_.document(),unit))return false;
    auto& edit=*geometryEdit_;const auto& state=edit.territorySelection->state();
    const bool hasWork=!state.parts.empty()||!state.selectedCandidateIds.empty()||!state.selectedComponentKeys.empty()||!edit.lineDraft.empty()||!edit.draft.polygons.empty();
    if(hasWork&&!confirmed){edit.sourceChange=ref;emit geometryEditChanged();return true;}
    auto sources=state.sources;const auto existing=std::find_if(sources.begin(),sources.end(),[&](const auto& source){return source.ref==ref;});
    if(existing!=sources.end())sources.erase(existing);else {const auto geometryRef=staticGeometryBinding(project_.document(),unit.id).geometryRef;const auto geometry=project_.document().geometries.get(geometryRef);if(!geometry)return false;sources.push_back({ref,*geometry,geometryRef,objectDisplayName(unit)});}
    cancelTerritoryCalculation();edit.sourceChange.reset();edit.draft={"Polygon",{},{},{}};edit.lineDraft.clear();edit.undo.clear();edit.redo.clear();edit.territorySelection->resetSources(std::move(sources));edit.stage="setup";edit.choosingProviders=true;scheduleTerritorySelection();emit visualChanged();return true;
}
bool EditorController::geometrySelectTerritoryMethod(const QString& name){
    if(!geometryEdit_||!geometryEdit_->territorySelection||geometryEdit_->stage!="selection"||geometryEdit_->applying||geometryEdit_->sourceChange)return false;
    const auto method=methodValue(name);if(method==TerritorySelectionMethod::None)return false;auto& edit=*geometryEdit_;
    if(edit.territorySelection->state().activeMethod==method&&edit.territorySelection->state().activePhase!=TerritorySelectionPhase::None){if(!edit.error.isEmpty())scheduleTerritorySelection();return true;}
    const auto result=edit.territorySelection->requestMethod(method,!edit.lineDraft.empty()||!edit.draft.polygons.empty());
    if(result==TerritoryMethodChange::Rejected)return false;
    if(result==TerritoryMethodChange::Activated){edit.draft={"Polygon",{},{},{}};edit.lineDraft.clear();edit.undo.clear();edit.redo.clear();scheduleTerritorySelection(false);}
    else if(result==TerritoryMethodChange::AwaitingComponentArchive)scheduleTerritorySelection();
    emit geometryEditChanged();return true;
}
bool EditorController::geometryFinishTerritoryDraft(){
    if(!geometryEdit_||!geometryEdit_->territorySelection||!territorySelectionState().value("canFinishDraft").toBool())return false;
    auto& edit=*geometryEdit_;
    if(edit.lineDraft.empty()&&edit.draft.polygons.empty()&&!edit.territorySelection->state().parts.empty()){if(!edit.territorySelection->finishArchivedDraft())return false;scheduleTerritorySelection();return true;}
    const auto state=edit.territorySelection->state();if(!state.workingSourceGeometry)return false;
    const auto ref=objectGeometry(edit.base.document(),edit.base.index(),edit.target);if(!ref)return false;const auto target=*edit.base.document().geometries.get(*ref);
    const auto draft=edit.draft;const auto line=edit.lineDraft;cancelTerritoryCalculation();edit.selectionPending=true;const auto generation=edit.generation,epoch=edit.computationEpoch,request=edit.request;
    edit.job=jobs_->submitGeometry(edit.base,"territorial:selection-draft",[state,target,draft,line](const ProjectSnapshot&,const JobToken& token)->GeometryJobResult {
        const auto cancelled=[&token]{return token.cancelled();};if(state.activeMethod==TerritorySelectionMethod::Polygon)return prepareTerritoryPolygonCandidates(draft,*state.workingSourceGeometry,target,cancelled);
        TerritorySelectionDraftResult out;out.detail=unsupportedSimpleCut(*state.workingSourceGeometry,line,cancelled);if(!out.detail.empty())return out;auto split=splitGeometryByLine(*state.workingSourceGeometry,line,cancelled);if(split.succeeded())orderSimpleCutLikeWeb(*state.workingSourceGeometry,split);out.status=split.status;out.detail=split.detail;if(split.succeeded())for(std::size_t i=0;i<split.candidates.size();++i)out.candidates.push_back({"candidate:"+std::to_string(i),split.candidates[i],planarArea(split.candidates[i])});return out;
    },[this,generation,epoch,request](std::uint64_t id,JobDisposition disposition,GeometryJobResult result){
        if(!geometryEdit_||!geometryEdit_->territorySelection||geometryEdit_->generation!=generation||geometryEdit_->computationEpoch!=epoch||!geometryEdit_->job||geometryEdit_->job->id()!=id)return;
        auto& edit=*geometryEdit_;edit.job.reset();edit.selectionPending=false;
        if(disposition!=JobDisposition::Accepted||!edit.base.matches(project_)||edit.request!=request){emit geometryEditChanged();return;}
        if(auto draft=std::get_if<TerritorySelectionDraftResult>(&result)){if(draft->status==GeometryOperationStatus::Empty||draft->candidates.empty()){edit.error=QString::fromStdString(draft->detail.empty()?"NO_TRANSFERABLE_SELECTION":draft->detail);}
            else if(draft->succeeded()&&edit.territorySelection->setCandidates(std::move(draft->candidates))){edit.draft={"Polygon",{},{},{}};edit.lineDraft.clear();edit.undo.clear();edit.redo.clear();scheduleTerritorySelection();}else edit.error=QString::fromStdString(draft->detail);}
        else if(auto failure=std::get_if<GeometryJobFailure>(&result)){edit.error=QString::fromStdString(failure->detail);edit.territorySelection->cancelRequestedMethod();}
        emit geometryEditChanged();
    });emit geometryEditChanged();return true;
}
bool EditorController::geometryToggleTerritoryCandidate(const QString& id){if(!geometryEdit_||!geometryEdit_->territorySelection||geometryEdit_->stage!="selection"||geometryEdit_->applying||geometryEdit_->sourceChange||geometryEdit_->territorySelection->state().methodChangeConfirmation)return false;if(!geometryEdit_->territorySelection->toggleCandidate(id.toStdString()))return false;scheduleTerritorySelection();return true;}
bool EditorController::geometryToggleTerritoryComponent(const QString& key){if(!geometryEdit_||!geometryEdit_->territorySelection||geometryEdit_->stage!="selection"||geometryEdit_->applying||geometryEdit_->sourceChange||geometryEdit_->territorySelection->state().methodChangeConfirmation)return false;if(!geometryEdit_->territorySelection->toggleComponent(key.toStdString()))return false;scheduleTerritorySelection();return true;}
bool EditorController::geometryAddTerritoryPart(){if(!geometryEdit_||!geometryEdit_->territorySelection||!territorySelectionState().value("canAddPart").toBool())return false;if(!geometryEdit_->territorySelection->addPart())return false;scheduleTerritorySelection();return true;}
bool EditorController::geometryRemoveTerritoryPart(const QString& id){if(!geometryEdit_||!geometryEdit_->territorySelection||geometryEdit_->stage!="selection"||geometryEdit_->applying||geometryEdit_->sourceChange||geometryEdit_->territorySelection->state().methodChangeConfirmation)return false;if(!geometryEdit_->territorySelection->removePart(id.toStdString()))return false;scheduleTerritorySelection();return true;}
bool EditorController::geometryUndoTerritoryPart(){if(!geometryEdit_||!geometryEdit_->territorySelection||geometryEdit_->stage!="selection"||geometryEdit_->applying||geometryEdit_->sourceChange||geometryEdit_->territorySelection->state().methodChangeConfirmation)return false;if(!geometryEdit_->territorySelection->undoPart())return false;scheduleTerritorySelection();return true;}
bool EditorController::geometryConfirmTerritoryChange(){if(!geometryEdit_||!geometryEdit_->territorySelection||geometryEdit_->applying)return false;auto& edit=*geometryEdit_;if(edit.sourceChange)return toggleTerritorySource(*edit.sourceChange,true);if(!edit.territorySelection->confirmMethodChange())return false;edit.draft={"Polygon",{},{},{}};edit.lineDraft.clear();edit.undo.clear();edit.redo.clear();scheduleTerritorySelection(false);return true;}
bool EditorController::geometryCancelTerritoryChange(){if(!geometryEdit_||!geometryEdit_->territorySelection)return false;if(geometryEdit_->sourceChange){geometryEdit_->sourceChange.reset();emit geometryEditChanged();return true;}const auto ok=geometryEdit_->territorySelection->cancelMethodChange();emit geometryEditChanged();return ok;}
bool EditorController::geometryPickTerritorySelection(double x,double y){if(!geometryEdit_||!geometryEdit_->territorySelection||!std::isfinite(x)||!std::isfinite(y))return false;const auto point=projection_.unproject(x,y);const auto& selection=*geometryEdit_->territorySelection;if(selection.state().activePhase==TerritorySelectionPhase::Candidate){for(const auto& item:selection.state().candidates)if(containsPoint(item.geometry,point))return geometryToggleTerritoryCandidate(QString::fromStdString(item.id));}else if(selection.state().activePhase==TerritorySelectionPhase::Components){for(const auto& item:selection.activeComponents())if(containsPoint(item.geometry,point))return geometryToggleTerritoryComponent(QString::fromStdString(item.key));}return false;}
bool EditorController::advanceTerritoryStage(){
    if(!territorySelectionState().value("canAdvance").toBool())return false;auto& edit=*geometryEdit_;
    if(edit.stage=="setup"){edit.stage="selection";edit.choosingProviders=false;if(edit.territorySelection->state().useRiverBoundaries&&edit.territorySelection->state().riverStatus==TerritoryRiverStatus::Idle)edit.territorySelection->setRiverStatus(TerritoryRiverStatus::Pending);if(!edit.territorySelection->derivedReady())scheduleTerritorySelection();else {prepareRiverPartitions();if(!edit.territoryPreview&&!edit.riverPreparation)scheduleTerritoryPreview();}}
    else if(edit.stage=="selection")edit.stage="review";else return false;
    emit geometryEditChanged();return true;
}
bool EditorController::backTerritoryStage(){
    if(!geometryEdit_||geometryEdit_->applying)return false;auto& edit=*geometryEdit_;if(edit.sourceChange||edit.territorySelection->state().methodChangeConfirmation)return geometryCancelTerritoryChange();
    cancelTerritoryCalculation(false);edit.error.clear();edit.snapPoint.reset();edit.dragBefore.reset();edit.vertex=-1;
    if(edit.stage=="review")edit.stage="selection";else if(edit.stage=="selection"){edit.stage="setup";edit.choosingProviders=true;if(edit.territorySelection->state().riverStatus==TerritoryRiverStatus::Pending)edit.territorySelection->setRiverStatus(TerritoryRiverStatus::Idle);}else {cancelGeometryEdit();return true;}emit geometryEditChanged();return true;
}
bool EditorController::applyTerritorySelection(){
    if(!territorySelectionState().value("canApply").toBool())return false;auto& edit=*geometryEdit_;const auto receipt=*edit.territoryPreview;edit.territoryPreview.reset();edit.applying=true;const auto generation=edit.generation;
    edit.job=jobs_->submit(edit.base,"territorial:selection-apply",[receipt](const ProjectSnapshot& snapshot,const JobToken& token){return prepareAnnexGeometryCommit(snapshot,receipt,token);},
    [this,generation](std::uint64_t id,JobDisposition disposition,PrepareResult result){
        if(!geometryEdit_||!geometryEdit_->territorySelection||geometryEdit_->generation!=generation||!geometryEdit_->job||geometryEdit_->job->id()!=id)return;
        auto& edit=*geometryEdit_;edit.job.reset();edit.applying=false;
        if(disposition!=JobDisposition::Accepted||!edit.base.matches(project_)){emit geometryEditChanged();return;}
        if(!result.ok()||!result.preview){edit.error=QString::fromStdString(result.detail);commandError(result.error,edit.error);emit geometryEditChanged();return;}
        MapProjection next;try{next.rebuild(result.preview->change().after());}catch(const std::exception& error){edit.error=QString::fromUtf8(error.what());emit geometryEditChanged();return;}
        const auto applied=CommandProcessor::confirm(project_,*result.preview);if(!applied.ok()){edit.error=QString::fromStdString(applied.detail);commandError(applied.error,edit.error);emit geometryEditChanged();return;}
        noteAppliedImpact(applied.impact);projection_=std::move(next);geometryEdit_.reset();hover_.reset();++hoverRevision_;closeObjectChooser();publish(false);emit geometryChanged();emit structureChanged();emit geometryEditChanged();
    });emit geometryEditChanged();return true;
}

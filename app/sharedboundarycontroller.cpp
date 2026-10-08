#include "editorcontroller.h"
#include "geometrysnapprovider.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>

using namespace pandoeditor;
namespace {
struct BoundaryEntry {std::vector<ObjectRef> owners;std::string autoSeed;};
BoundaryEntry entry(const ProjectDocument& document,const DocumentIndex& index,
                    const std::vector<ObjectRef>& selected,const std::set<std::string>& inactive) {
    if(selected.size()!=1)return sharedboundary::Session::eligibility(document,index,selected,false).empty()?BoundaryEntry{selected,{}}:BoundaryEntry{};
    const auto& source=selected.front();const auto found=index.objects.find(source);
    if(source.domain!="territorial"||found==index.objects.end()||document.units.at(found->second).kind!=UnitKind::General||objectLocked(document,index,source))return {};
    const auto parent=staticParentRelation(document,source.id).parentId;if(parent.empty())return {};
    const auto geometry=document.geometries.get(staticGeometryBinding(document,source.id).geometryRef);if(!geometry||geometry->polygons.empty())return {};
    BoundaryEntry result{{source},source.id};bool sibling=false;
    for(const auto& unit:document.units)if(!inactive.count(unit.id)&&unit.id!=source.id&&unit.kind==UnitKind::General&&staticParentRelation(document,unit.id).parentId==parent){sibling=true;const auto ref=territorialRef(unit.id);if(!objectLocked(document,index,ref))result.owners.push_back(ref);}
    // The web enters if a sibling exists, even when all siblings are locked;
    // preparation then rejects the one-owner/isolated seed without side effects.
    if(!sibling)return {};
    if(result.owners.size()>1&&!sharedboundary::Session::eligibility(document,index,result.owners,false).empty())return {};
    return result;
}
}
bool EditorController::canBeginSharedBoundaryGeometry() const {
    return !geometryEdit_&&!structureDialogOpen()&&!hasPendingEdits()
        &&!entry(project_.viewDocument(),project_.viewIndex(),selection_.items(),project_.inactiveEntityIds()).owners.empty();
}
bool EditorController::beginSharedBoundaryGeometry(){
    if(!canBeginSharedBoundaryGeometry())return false;
    const auto primary=selection_.primary();if(!primary)return false;
    const auto selected=entry(project_.viewDocument(),project_.viewIndex(),selection_.items(),project_.inactiveEntityIds());auto target=*primary;
    // Child entry uses the original first selected sibling as its return seed,
    // independently of the current selection primary or the dragged owners.
    if(!staticParentRelation(project_.viewDocument(),selected.owners.front().id).parentId.empty())target=selected.owners.front();
    const auto geometry=project_.viewDocument().geometries.get(staticGeometryBinding(project_.viewDocument(),target.id).geometryRef);if(!geometry)return false;
    geometryEdit_=GeometryEditSession{project_.snapshotForView(),target,*geometry,QStringLiteral("boundary"),{}};
    geometryEdit_->boundaryOwners=selected.owners;geometryEdit_->boundaryAutoSeed=selected.autoSeed;geometryEdit_->generation=++nextGeometrySession_;
    if(staticParentRelation(project_.viewDocument(),selected.owners.front().id).parentId.empty()) {
        // Explicit root entry preserves item order but makes the last item the
        // visible primary. Whole-tool cancel restores the original snapshot.
        geometryEdit_->boundaryInitialSelection=selection_;
        auto entered=selection_;entered.setMany(selected.owners,selected.owners.back(),"map");applySelection(std::move(entered));
    }
    prepareBoundaryGeometry();return true;
}
bool EditorController::boundaryGeometryReady() const {
    return geometryEdit_&&geometryEdit_->tool=="boundary"&&geometryEdit_->boundaryStatus=="ready"
        &&geometryEdit_->boundarySession&&geometryEdit_->boundarySession->valid()
        &&geometryEdit_->base.matches(project_);
}
void EditorController::prepareBoundaryGeometry(){
    if(!geometryEdit_||geometryEdit_->tool!="boundary")return;
    // The pinned Worker client synchronizes edit sources before boundary-prepare.
    const auto sourceEpoch=snapProvider_?snapProvider_->beginWorkerOperation(project_.snapshot()):0;
    auto& edit=*geometryEdit_;if(edit.boundaryCancelled)edit.boundaryCancelled->store(true);
    edit.boundaryCancelled=std::make_shared<std::atomic_bool>(false);edit.boundarySession.reset();edit.boundaryStatus="preparing";edit.error.clear();edit.vertex=-1;
    const auto cancelled=edit.boundaryCancelled;const auto base=edit.base;const auto owners=edit.boundaryOwners;const auto autoSeed=edit.boundaryAutoSeed;const auto generation=edit.generation;const auto epoch=++edit.computationEpoch;
    auto* watcher=new QFutureWatcher<std::shared_ptr<sharedboundary::Session>>(this);
    connect(watcher,&QFutureWatcherBase::finished,this,[this,watcher,cancelled,generation,epoch,sourceEpoch]{
        auto result=watcher->result();watcher->deleteLater();if(cancelled->load()||!geometryEdit_||geometryEdit_->tool!="boundary"||geometryEdit_->generation!=generation||geometryEdit_->computationEpoch!=epoch)return;
        const bool sourcesCurrent=!snapProvider_||snapProvider_->completeWorkerOperation(project_.snapshot(),sourceEpoch);
        // Fixed owners/auto-seed belong to this session. Ambient selection does
        // not change preparation identity; document/source epochs still do.
        auto& current=*geometryEdit_;if(!sourcesCurrent||!current.base.matches(project_)){current.boundaryStatus="error";current.error="BOUNDARY_STALE_PREPARATION";}
        else if(!result||!result->valid()){current.boundaryStatus="error";current.error=result?QString::fromStdString(result->error()):QStringLiteral("BOUNDARY_PREPARATION_FAILED");}
        else{current.boundaryOwners.clear();for(const auto& [id,geometry]:result->drafts())current.boundaryOwners.push_back(territorialRef(id));current.boundarySession=std::move(result);current.boundaryStatus="ready";current.error.clear();}
        emit geometryEditChanged();
    });
    watcher->setFuture(QtConcurrent::run([base,owners,cancelled,autoSeed]{return sharedboundary::Session::prepare(base.document(),base.index(),owners,[cancelled]{return cancelled->load();},autoSeed);}));
    emit geometryEditChanged();
}
bool EditorController::geometryRetryBoundaryPreparation(){
    if(!geometryEdit_||geometryEdit_->tool!="boundary"||geometryEdit_->boundaryStatus!="error"||!geometryEdit_->base.matches(project_))return false;
    prepareBoundaryGeometry();return true;
}

bool EditorController::geometryConfirmBoundaryImpacts(){
    if(!boundaryGeometryReady()||!geometryEdit_->preview||!geometryEdit_->boundaryImpactConfirmation)return false;
    // Web validation after the impact decision keeps the preview on rejection,
    // but the decision is closed and must be requested again before applying.
    if(geometryEdit_->boundaryPreviewSelectionId!=selectedId()) {
        geometryEdit_->boundaryImpactConfirmation=false;
        geometryEdit_->error=QStringLiteral("선택이 변경되어 변경을 적용하지 않았습니다.");emit geometryEditChanged();return false;
    }
    geometryEdit_->boundaryImpactsApproved=true;geometryEdit_->boundaryImpactConfirmation=false;
    return confirmGeometryEdit();
}
void EditorController::geometryCancelBoundaryImpacts(){
    if(!geometryEdit_||geometryEdit_->tool!="boundary"||!geometryEdit_->boundaryImpactConfirmation)return;
    if(geometryEdit_->preview)CommandProcessor::cancel(*geometryEdit_->preview);
    geometryEdit_->preview.reset();geometryEdit_->boundaryImpactConfirmation=false;geometryEdit_->boundaryImpactsApproved=false;
    if(geometryEdit_->boundarySession){geometryEdit_->boundarySession->resetDraft();geometryEdit_->draft=geometryEdit_->boundarySession->drafts().at(geometryEdit_->target.id);geometryEdit_->vertex=-1;}
    geometryEdit_->stage="selection";geometryEdit_->error.clear();++geometryEdit_->request;emit geometryEditChanged();
}

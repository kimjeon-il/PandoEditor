#include "geometrysnapprovider.h"
#include "commandjobrunner.h"
#include <QPointer>
#include <QThread>
#include <set>
#include <QStringList>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace geometrysnap {
namespace {
bool currentRootGeneral(const pandoeditor::ProjectSnapshot& snapshot,const pandoeditor::ObjectRef& ref) {
    if(ref.domain!="territorial")return false;
    const auto object=snapshot.index().objects.find(ref);
    if(object==snapshot.index().objects.end()||snapshot.document().units.at(object->second).kind!=pandoeditor::UnitKind::General)return false;
    const auto relations=snapshot.index().parentRelationsByUnit.find(ref);
    return relations!=snapshot.index().parentRelationsByUnit.end()&&relations->second.size()==1&&
        snapshot.document().timelineRecords.parentRelations.at(relations->second.front()).parentId.empty();
}
}
Provider::Provider(CommandJobRunner& runner,QObject* parent)
    :QObject(parent),runner_(&runner),index_(std::make_shared<Index>()) {}
void Provider::notifyWorkerStopped() {
    if(QThread::currentThread()!=thread())throw std::logic_error("snap source ordering is owner-thread only");
    workerStopped_=true;++workerEpoch_;++epoch_;
    if(status_=="pending") {
        key_.clear();candidates_.clear();diagnostics_={};status_="empty";
        if(job_){const auto id=job_->id();job_.reset();if(runner_)runner_->cancel(id);}
    }
}
std::uint64_t Provider::beginWorkerOperation(const pandoeditor::ProjectSnapshot& snapshot) {
    if(QThread::currentThread()!=thread())throw std::logic_error("snap source ordering is owner-thread only");
    updateSourceRanks(snapshot,workerStopped_);workerStopped_=false;return workerEpoch_;
}
bool Provider::completeWorkerOperation(const pandoeditor::ProjectSnapshot& snapshot,std::uint64_t lifecycleEpoch) {
    if(QThread::currentThread()!=thread())throw std::logic_error("snap source ordering is owner-thread only");
    if(workerStopped_||lifecycleEpoch!=workerEpoch_||ranksInstance_!=snapshot.instanceId()||snapshot.revision()<ranksRevision_)return false;
    synchronizeSources(snapshot);return true;
}
void Provider::synchronizeInstallation(const pandoeditor::ProjectSnapshot& snapshot) {
    if(QThread::currentThread()!=thread())throw std::logic_error("snap source ordering is owner-thread only");
    if(!sourceRanks_||ranksInstance_!=snapshot.instanceId()) {
        updateSourceRanks(snapshot,true);workerStopped_=false;
    }
}
bool Provider::requiresImmediateSynchronization(const pandoeditor::ProjectSnapshot& snapshot,const pandoeditor::ChangeImpact& impact) const {
    if(workerStopped_)return false;
    // Pinned app-object-commands / app-project-snapshots only immediately send
    // syncPatch for root-general membership or geometry changes. Generic-only
    // and child-only transitions wait until the next actual Worker request.
    for(const auto& ref:impact.changedObjects)if(ref.domain=="territorial"&&
        bool(rootGeneralIds_.count(ref.id))!=currentRootGeneral(snapshot,ref))return true;
    for(const auto& ref:impact.sceneDirty.geometryObjects)if(ref.domain=="territorial"&&
        (rootGeneralIds_.count(ref.id)||currentRootGeneral(snapshot,ref)))return true;
    return false;
}
void Provider::synchronizeSources(const pandoeditor::ProjectSnapshot& snapshot) {
    if(QThread::currentThread()!=thread())throw std::logic_error("snap source ordering is owner-thread only");
    if(!workerStopped_)updateSourceRanks(snapshot);
}
void Provider::updateSourceRanks(const pandoeditor::ProjectSnapshot& snapshot,bool rebase) {
    const bool installation=!sourceRanks_||ranksInstance_!=snapshot.instanceId();
    const bool replace=installation||rebase;
    if(!installation&&snapshot.revision()<ranksRevision_)throw std::invalid_argument("stale snap source-order transition");
    if(!replace&&snapshot.revision()==ranksRevision_)return;
    std::vector<pandoeditor::ObjectRef> ordered;
    std::set<std::string> roots;
    ordered.reserve(snapshot.document().units.size()+snapshot.document().genericFeatures.size());
    for(const auto& unit:snapshot.document().units){const auto ref=pandoeditor::territorialRef(unit.id);ordered.push_back(ref);if(currentRootGeneral(snapshot,ref))roots.insert(unit.id);}
    for(const auto& feature:snapshot.document().genericFeatures)ordered.push_back({"generic",feature.id});
    const std::set<pandoeditor::ObjectRef> present(ordered.begin(),ordered.end());
    bool changed=replace||sourceRanks_->size()!=present.size();
    if(!changed)for(const auto& ref:ordered)if(!sourceRanks_->count(ref)){changed=true;break;}
    if(!changed){rootGeneralIds_.swap(roots);ranksRevision_=snapshot.revision();return;}

    // Publish only after all allocations and checked increments have succeeded.
    // Old requests keep their immutable map, including removed source slots.
    auto next=replace?std::make_shared<SourceRanks>():std::make_shared<SourceRanks>(*sourceRanks_);
    auto sequence=replace?std::uint64_t(0):nextSourceRank_;
    for(auto it=next->begin();it!=next->end();)if(!present.count(it->first))it=next->erase(it);else ++it;
    for(const auto& ref:ordered)if(!next->count(ref)){
        if(sequence==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("snap source insertion sequence exhausted");
        next->emplace(ref,sequence++);
    }
    auto instance=snapshot.instanceId();
    const bool replacingExisting=sourceRanks_&&installation;
    sourceRanks_=std::move(next);ranksInstance_.swap(instance);
    rootGeneralIds_.swap(roots);
    ranksRevision_=snapshot.revision();nextSourceRank_=sequence;
    if(replace)++workerEpoch_;
    if(replacingExisting)reset();
}
Provider::~Provider() {++epoch_;if(job_&&runner_)runner_->cancel(job_->id());}
std::shared_ptr<const pandoeditor::Geometry> Provider::retainSource(const pandoeditor::Geometry& geometry,const QString& identity) {
    if(!source_||sourceIdentity_!=identity){source_=std::make_shared<const pandoeditor::Geometry>(geometry);sourceIdentity_=identity;sourceKey_="snap:"+std::to_string(++sourceSequence_)+":0";}
    return source_;
}
void Provider::reset() {
    ++epoch_;source_.reset();sourceIdentity_.clear();sourceKey_.clear();key_.clear();candidates_.clear();status_="empty";diagnostics_={};
    if(job_){const auto id=job_->id();job_.reset();if(runner_)runner_->cancel(id);}
}
const std::vector<Candidate>& Provider::candidates(const pandoeditor::ProjectSnapshot& snapshot,
    const Request& request,const QString& tool,double baseMargin,std::uint64_t sourceEpoch) {
    if(!std::isfinite(baseMargin)||baseMargin<=0||!std::isfinite(request.coordinate.x)||!std::isfinite(request.coordinate.y)) {
        reset();return candidates_;
    }
    QStringList parts{QString::fromStdString(snapshot.instanceId()),QString::number(snapshot.revision()),
        QString::number(sourceEpoch),tool};
    for(const auto& owner:request.activeOwnerIds)parts.append(QString::fromStdString(owner));
    parts<<QString::fromStdString(request.sourceKey)<<QString::number(request.sourceRevision)
        <<QString::number(baseMargin,'g',17)
        <<QString::number(std::floor(request.coordinate.x/baseMargin),'g',17)
        <<QString::number(std::floor(request.coordinate.y/baseMargin),'g',17);
    const auto key=parts.join(':');
    if(key==key_)return candidates_;
    // A READY cache hit above neither prepares a stopped Worker nor rebases it.
    const auto workerEpoch=beginWorkerOperation(snapshot);
    key_=key;candidates_.clear();diagnostics_={};status_="pending";const auto epoch=++epoch_;
    auto capturedRequest=request;
    capturedRequest.sourceRanks=sourceRanks_;
    capturedRequest.sourceRanksInstance=ranksInstance_;
    capturedRequest.sourceRanksRevision=ranksRevision_;
    const auto index=index_;const QPointer<Provider> self(this);++submitted_;
    if(!runner_){key_.clear();status_="empty";return candidates_;}
    job_=runner_->submitGeometry(snapshot,"territorial-snap",[index,request=std::move(capturedRequest)](const auto& current,const auto& token)->GeometryJobResult {
        if(token.cancelled())return {};
        auto result=index->prepareAndCollect(current,request);
        if(token.cancelled())return {};
        return result;
    },[self,epoch,workerEpoch](std::uint64_t,pandoeditor::JobDisposition disposition,GeometryJobResult result){
        if(!self||self->epoch_!=epoch||self->workerEpoch_!=workerEpoch||self->workerStopped_)return;
        self->job_.reset();
        if(disposition==pandoeditor::JobDisposition::Accepted)if(auto ready=std::get_if<CandidateBatch>(&result)) {
            if(self->runner_&&self->completeWorkerOperation(self->runner_->currentSnapshot(),workerEpoch)) {
                self->candidates_=std::move(ready->candidates);self->diagnostics_=ready->diagnostics;self->status_="ready";return;
            }
        }
        // Current errors are retryable; stale completion cannot clear a newer entry.
        self->key_.clear();self->candidates_.clear();self->status_="empty";
    },50);
    return candidates_;
}
}

#include "terrainrenderowner.h"
#include <QElapsedTimer>
#include <algorithm>
#include <cmath>
#include <pandoeditor/map/projectionengine.h>

namespace {
TerrainDisplayResourceId idOf(const TerrainDisplayResource& r){return {r.key,r.contentKey};}
bool contextMatches(const TerrainDisplayScope& a,const TerrainDisplayScope& b) {
    return a.windowEpoch==b.windowEpoch&&a.contextEpoch==b.contextEpoch&&a.projectGeneration==b.projectGeneration;
}
TerrainRenderResource tintResource(const TerrainRenderInput& input) {
    TerrainRenderResource r;r.frame.image=input.tint;r.frame.sourceEpoch=input.scope.sourceEpoch;
    r.resource={QStringLiteral("@terrain/tint"),input.tintContentKey,0,{-180,90,180,-90}};return r;
}
}
void TerrainUploadSchedule::beginFrame(quint64 bytes,double millis) {
    limit_=bytes;timeLimit_=std::max(0.,millis);bytes_=operations_=oversized_=reserved_=0;millis_=0;stopped_=false;
}
bool TerrainUploadSchedule::reserve(quint64 bytes) {
    if(stopped_||reserved_||!bytes)return false;
    if(operations_&&(bytes>limit_-std::min(limit_,bytes_)||millis_>=timeLimit_))return false;
    reserved_=bytes;return true;
}
void TerrainUploadSchedule::committed(quint64 bytes,double millis) {
    reserved_=0;++operations_;bytes_+=bytes;millis_+=std::max(0.,millis);
    if(bytes>limit_){++oversized_;stopped_=true;}
    if(bytes_>=limit_||millis_>=timeLimit_)stopped_=true;
}
TerrainRenderOwner::TerrainRenderOwner(QQuickWindow* window,TerrainImageBridge* bridge,TerrainRenderOwnerSnapshot owner)
    :window_(window),bridge_(bridge),owner_(owner) {
    beginConnection_=connect(window,&QQuickWindow::beforeFrameBegin,this,[this]{beforeFrameBegin();},Qt::DirectConnection);
    endConnection_=connect(window,&QQuickWindow::afterFrameEnd,this,[this]{afterFrameEnd();},Qt::DirectConnection);
    swapConnection_=connect(window,&QQuickWindow::frameSwapped,this,[this]{frameSwapped();},Qt::DirectConnection);
    invalidateConnection_=connect(window,&QQuickWindow::sceneGraphInvalidated,this,[this] {
        owner_.alive=false;inFlight_.clear();retainedDraws_.clear();backings_.clear();emptyMask_.reset();
    },Qt::DirectConnection);
    QImage empty(1,1,QImage::Format_RGBA8888);empty.fill(Qt::transparent);
    emptyMask_.reset(window->createTextureFromImage(empty));
    stats_.auxiliaryTextureNominalBytes=emptyMask_?4:0;
}
TerrainRenderOwner::~TerrainRenderOwner() {
    disconnect(beginConnection_);disconnect(endConnection_);disconnect(swapConnection_);disconnect(invalidateConnection_);
    // QObject/QSG node lifetime follows the render thread, including teardown.
    backings_.clear();emptyMask_.reset();
}
bool TerrainRenderOwner::scopeCurrent() const {
    if(!input_||!bridge_||!owner_.alive||input_->scope.windowEpoch!=owner_.windowEpoch||
        input_->scope.contextEpoch!=owner_.contextEpoch)return false;
    const auto latest=bridge_->renderInput();return latest&&latest->scope==input_->scope&&latest->enabled;
}
void TerrainRenderOwner::publish(TerrainRenderObservation observation) {
    stats_.pendingSubmittedFrames=inFlight_.size();
    stats_.uploadWorkPending=!pendingProbes().empty()||hasRunnablePreparation();
    if(!bridge_||!input_)return;updateCpuImageBytes();observation.scope=input_->scope;observation.stats=stats_;
    if(needsFallbackUnderlay())observation.fallbackDraws=fallbackDraws_;
    observation.diagnostic=failureDiagnostic_;
    bridge_->observeRender(std::move(observation));
}
void TerrainRenderOwner::updateCpuImageBytes() {
    // Actual QImage allocations retained by backing records, including tint
    // aliases. Bridge-input/GUI-prepared/provider caches have separate ledgers;
    // this is neither total process memory nor nominal QSG texture bytes.
    std::set<qint64> images;quint64 bytes=0;
    for(const auto& backing:backings_)for(const auto* image:{&backing.second.data.frame.image,&backing.second.data.frame.tint})
        if(!image->isNull()&&images.insert(image->cacheKey()).second)bytes+=quint64(image->sizeInBytes());
    stats_.cpuImageBytes=bytes;
}
std::set<TerrainDisplayResourceId> TerrainRenderOwner::protectedIds() const {
    std::set<TerrainDisplayResourceId> result;
    if(input_) {
        result.insert(input_->baseResources.begin(),input_->baseResources.end());
        result.insert(input_->protectedResources.begin(),input_->protectedResources.end());
        if(!input_->tint.isNull())result.insert({QStringLiteral("@terrain/tint"),input_->tintContentKey});
        if(input_->candidate)for(const auto& draw:input_->candidate->draws)result.insert(idOf(draw.resource));
    }
    result.insert(adoptedProtection_.begin(),adoptedProtection_.end());
    result.insert(adoptedBase_.begin(),adoptedBase_.end());
    if(!adoptedTint_.isEmpty())result.insert({QStringLiteral("@terrain/tint"),adoptedTint_});
    for(const auto& draw:retainedDraws_)result.insert(idOf(draw.resource));
    for(const auto& pending:inFlight_) {
        for(const auto& draw:pending.second.receipt.draws)result.insert(idOf(draw.resource));
        result.insert(pending.second.baseResources.begin(),pending.second.baseResources.end());
        if(!pending.second.tintContentKey.isEmpty())result.insert({QStringLiteral("@terrain/tint"),pending.second.tintContentKey});
    }
    return result;
}
void TerrainRenderOwner::trim(quint64 incomingBytes) {
    if(!input_)return;const auto protectedSet=protectedIds();
    const auto residentLimit=input_->budgetBytes-std::min(input_->budgetBytes,incomingBytes);
    while(stats_.textureNominalBytes>residentLimit) {
        auto victim=backings_.end();
        for(auto it=backings_.begin();it!=backings_.end();++it)
            if(!protectedSet.count(it->first)&&(victim==backings_.end()||it->second.lastUse<victim->second.lastUse))victim=it;
        if(victim==backings_.end())break;
        TerrainRenderObservation observation;observation.kind=TerrainRenderObservation::ResourceRetired;
        observation.retired.push_back(victim->first);
        const auto physical=victim->second.texture.get();
        const bool last=std::count_if(backings_.begin(),backings_.end(),[&](const auto& b){return b.second.texture.get()==physical;})==1;
        if(last&&physical) {
            stats_.textureNominalBytes-=victim->second.bytes;stats_.cpuImageBytes-=victim->second.bytes;
            if(!victim->second.committed)stats_.stagingNominalBytes-=victim->second.bytes;
            --stats_.textureCount;
        }
        backings_.erase(victim);++stats_.retiredResources;stats_.logicalResourceCount=backings_.size();publish(std::move(observation));
    }
    stats_.memoryPressure=stats_.memoryPressure||stats_.textureNominalBytes>input_->budgetBytes;
    stats_.mandatoryOverflowBytes=stats_.textureNominalBytes>input_->budgetBytes?stats_.textureNominalBytes-input_->budgetBytes:0;
}
bool TerrainRenderOwner::allocate(const TerrainRenderResource& resource,bool mandatory) {
    const auto id=idOf(resource.resource);if(backings_.count(id))return true;
    if(!window_||resource.frame.image.isNull()||id.contentKey.isEmpty()||
        resource.frame.sourceEpoch!=input_->scope.sourceEpoch)return false;
    if(!pendingProbes().empty()){++stats_.deferredResources;return false;}
    const quint64 bytes=quint64(resource.frame.image.width())*quint64(resource.frame.image.height())*4;
    for(const auto& existing:backings_)if(existing.first.contentKey==id.contentKey&&!existing.second.failed) {
        auto shared=existing.second;shared.data=resource;shared.data.frame.image=existing.second.data.frame.image;
        shared.announcedScope.reset();shared.lastUse=++tick_;shared.mandatory=mandatory;
        if(shared.committed&&scopeCurrent()) {
            shared.announcedScope=input_->scope;
            if(id.key==QStringLiteral("@terrain/tint"))newlyCommittedTint_=id.contentKey;
            else newlyCommitted_.push_back(resource.resource);
        }
        backings_.emplace(id,std::move(shared));stats_.logicalResourceCount=backings_.size();
        return true;
    }
    if(!mandatory&&!input_->inputActive)trim(bytes);
    // Reserve nominal backing before allocation; mandatory base/tint may exceed
    // configured memory, with an explicit pressure/overflow observation.
    if(!mandatory&&(input_->inputActive||bytes>input_->budgetBytes||stats_.textureNominalBytes>input_->budgetBytes-bytes)) {
        ++stats_.deferredResources;stats_.memoryPressure=true;return false;
    }
    if(!schedule_.reserve(bytes)){++stats_.deferredResources;return false;}
    std::shared_ptr<QSGTexture> texture(window_->createTextureFromImage(resource.frame.image));
    if(!texture) {
        schedule_.abandon();++stats_.allocationFailures;stats_.memoryPressure=true;
        failureDiagnostic_=QStringLiteral("Qt could not allocate terrain texture object");
        backings_.emplace(id,Backing{resource,{},0,++tick_,false,mandatory,input_->scope,true});
        stats_.logicalResourceCount=backings_.size();return false;
    }
    texture->setFiltering(QSGTexture::Linear);texture->setHorizontalWrapMode(QSGTexture::ClampToEdge);
    texture->setVerticalWrapMode(QSGTexture::ClampToEdge);
    Backing backing{resource,std::move(texture),bytes,++tick_,false,mandatory,{},false};
    backings_.emplace(id,std::move(backing));stats_.textureNominalBytes+=bytes;stats_.cpuImageBytes+=bytes;
    stats_.stagingNominalBytes+=bytes;++stats_.textureCount;stats_.logicalResourceCount=backings_.size();++stats_.allocationCount;
    // One outstanding allocation is admitted per synchronize. Its actual
    // commit duration decides admission of subsequent frames, never guessed.
    return true;
}
void TerrainRenderOwner::synchronize(std::shared_ptr<const TerrainRenderInput> input) {
    if(!input)return;
    if(input_&&!contextMatches(input_->scope,input->scope)) {
        retainedDraws_.clear();fallbackDraws_.clear();inFlight_.clear();adoptedProtection_.clear();
        adoptedBase_.clear();adoptedTint_.clear();adoptedReceipt_.reset();backings_.clear();lastAdoptedFrame_=0;
        stats_.cpuImageBytes=stats_.textureNominalBytes=stats_.stagingNominalBytes=stats_.textureCount=0;
    }
    input_=std::move(input);candidateSelected_=false;drawObserved_.clear();
    newlyCommitted_.clear();newlyCommittedTint_.clear();frameSubmitted_.reset();
    schedule_.beginFrame(input_->uploadBytesPerFrame,input_->uploadMillisPerFrame);
    stats_.frameUploadBytes=stats_.frameUploadOperations=0;stats_.frameUploadMillis=0;stats_.deferredResources=0;
    stats_.memoryPressure=false;
    frameResourceFailure_=false;
    // A changed request permits an explicit allocation retry; failed Qt handles
    // never enter submitted readiness and are never silently reused.
    for(auto it=backings_.begin();it!=backings_.end();) {
        if(it->second.failed&&it->second.announcedScope&&*it->second.announcedScope!=input_->scope) {
            const auto ptr=it->second.texture.get();const auto bytes=it->second.bytes;
            const bool last=std::count_if(backings_.begin(),backings_.end(),[&](const auto& b){return b.second.texture.get()==ptr;})==1;
            if(last&&ptr){stats_.textureNominalBytes-=bytes;stats_.cpuImageBytes-=bytes;stats_.stagingNominalBytes-=bytes;--stats_.textureCount;}
            it=backings_.erase(it);
        }else ++it;
    }
    stats_.logicalResourceCount=backings_.size();
    if(std::none_of(backings_.begin(),backings_.end(),[](const auto& b){return b.second.failed;}))failureDiagnostic_.clear();
    else stats_.memoryPressure=true;
    if(!input_->enabled) {
        // Hidden/cancelled input retains adopted reserve for resumption. None
        // explicitly requests retirement, independently of input visibility.
        if(!input_->releaseResources){receiptNeeded_=false;return;}
        // Explicit None/disable is acknowledged at scene-graph synchronization,
        // after the preceding submitted frame. Destruction stays on this owner
        // thread; Qt manages native deferred destruction, not a claimed fence.
        TerrainRenderObservation retired;retired.kind=TerrainRenderObservation::ResourceRetired;
        for(const auto& backing:backings_)retired.retired.push_back(backing.first);
        stats_.retiredResources+=backings_.size();backings_.clear();emptyMask_.reset();
        retainedDraws_.clear();fallbackDraws_.clear();adoptedBase_.clear();adoptedTint_.clear();
        adoptedProtection_.clear();adoptedReceipt_.reset();inFlight_.clear();receiptNeeded_=false;
        stats_.cpuImageBytes=stats_.textureNominalBytes=stats_.stagingNominalBytes=stats_.meshBytes=0;
        stats_.textureCount=stats_.logicalResourceCount=stats_.temporaryProtectedBytes=stats_.mandatoryOverflowBytes=stats_.auxiliaryTextureNominalBytes=0;
        stats_.memoryPressure=false;failureDiagnostic_.clear();
        publish(std::move(retired));return;
    }
    if(!emptyMask_&&window_) {
        QImage empty(1,1,QImage::Format_RGBA8888);empty.fill(Qt::transparent);
        emptyMask_.reset(window_->createTextureFromImage(empty));stats_.auxiliaryTextureNominalBytes=emptyMask_?4:0;
    }
    if(bridge_)if(const auto adoption=bridge_->renderAdoption();adoption&&contextMatches(adoption->receipt.scope,input_->scope)&&
        adoption->receipt.frameSequence>lastAdoptedFrame_) {
        const auto found=inFlight_.find(adoption->receipt.frameSequence);
        if(found!=inFlight_.end()&&found->second.receipt.scope==adoption->receipt.scope&&
           found->second.receipt.candidateId==adoption->receipt.candidateId&&found->second.receipt.draws==adoption->receipt.draws) {
            lastAdoptedFrame_=adoption->receipt.frameSequence;adoptedProtection_=adoption->protectedResources;
            retainedDraws_=adoption->receipt.draws;
            adoptedReceipt_=adoption->receipt;
            adoptedBase_=found->second.baseResources;adoptedTint_=found->second.tintContentKey;
            for(auto it=inFlight_.begin();it!=inFlight_.end();) {
                if(it->first<=lastAdoptedFrame_)it=inFlight_.erase(it);else ++it;
            }
        }
    }
    trim();if(!scopeCurrent())return;
    // Reproject only the last accepted reserve/detail, never viewport history.
    // It remains a fallback inventory and cannot create a current receipt.
    fallbackDraws_.clear();stats_.temporaryProtectedBytes=0;
    const auto offsets=visibleFlatWorldOffsets(input_->view);
    for(const auto& id:adoptedBase_)if(const auto found=backings_.find(id);found!=backings_.end()) {
        for(const auto offset:offsets) {
            auto bounds=found->second.data.resource.interior;bounds.west+=offset;bounds.east+=offset;
            fallbackDraws_.push_back({found->second.data.resource,bounds,offset});
        }
    }
    std::set<TerrainDisplayResourceId> fallbackSeen;
    for(const auto& draw:retainedDraws_)if(std::find(adoptedBase_.begin(),adoptedBase_.end(),idOf(draw.resource))==adoptedBase_.end()&&
        fallbackSeen.insert(idOf(draw.resource)).second) {
        for(const auto offset:offsets) {auto bounds=draw.resource.interior;bounds.west+=offset;bounds.east+=offset;
            fallbackDraws_.push_back({draw.resource,bounds,offset});}
    }
    std::set<TerrainDisplayResourceId> currentProtection(input_->protectedResources.begin(),input_->protectedResources.end());
    currentProtection.insert(input_->baseResources.begin(),input_->baseResources.end());
    if(!input_->tint.isNull())currentProtection.insert({QStringLiteral("@terrain/tint"),input_->tintContentKey});
    std::set<QSGTexture*> temporarySeen,currentPhysical;
    for(const auto& id:currentProtection)if(const auto found=backings_.find(id);found!=backings_.end())currentPhysical.insert(found->second.texture.get());
    for(const auto& id:protectedIds())if(!currentProtection.count(id))
        if(const auto found=backings_.find(id);found!=backings_.end()&&!currentPhysical.count(found->second.texture.get())&&
            temporarySeen.insert(found->second.texture.get()).second)stats_.temporaryProtectedBytes+=found->second.bytes;
    // Reuse authenticated resident backings under a new demand without claiming
    // a new upload operation. Their physical commit evidence lives in the owner.
    for(auto& b:backings_)if(b.second.committed&&(!b.second.announcedScope||*b.second.announcedScope!=input_->scope)) {
        const bool desiredTint=b.first.key==QStringLiteral("@terrain/tint")&&b.first.contentKey==input_->tintContentKey;
        const bool desired=desiredTint||std::any_of(input_->resources.begin(),input_->resources.end(),[&](const auto& r){return idOf(r.resource)==b.first;});
        if(!desired)continue;b.second.announcedScope=input_->scope;
        if(desiredTint)newlyCommittedTint_=b.first.contentKey;else newlyCommitted_.push_back(b.second.data.resource);
    }
    // Mandatory base/tint precede detail and prefetch and remain independent of
    // viewport/horizon draw culling. A probe drives every new allocation.
    for(const auto& id:input_->baseResources) {
        const auto* data=resource(id);if(data&&!backings_.count(id)&&allocate(*data,true))break;
    }
    if(!input_->tint.isNull())allocate(tintResource(*input_),true);
    if(bootstrapReady()) {
        for(const auto& r:input_->resources) {
            if(r.prefetch)continue;allocate(r,std::find(input_->baseResources.begin(),input_->baseResources.end(),idOf(r.resource))!=input_->baseResources.end());
        }
        if(!input_->inputActive&&!stats_.memoryPressure)for(const auto& r:input_->resources)if(r.prefetch)allocate(r,false);
    }
    trim();candidateSelected_=candidateReady();receiptNeeded_=candidateSelected_;
    if(candidateSelected_) {
        if(adoptedReceipt_&&adoptedReceipt_->scope==input_->scope&&adoptedReceipt_->candidateId==input_->candidate->id)
            receiptNeeded_=false;
        for(const auto& pending:inFlight_)if(pending.second.receipt.scope==input_->scope&&pending.second.receipt.candidateId==input_->candidate->id)
            receiptNeeded_=false;
    }
    if(candidateSelected_)drawObserved_.assign(input_->candidate->draws.size(),false);
}
const TerrainRenderResource* TerrainRenderOwner::resource(const TerrainDisplayResourceId& id) const {
    const auto old=backings_.find(id);if(old!=backings_.end())return &old->second.data;
    if(input_)for(const auto& resource:input_->resources)if(idOf(resource.resource)==id)return &resource;
    return nullptr;
}
QSGTexture* TerrainRenderOwner::texture(const TerrainDisplayResourceId& id) const {
    const auto found=backings_.find(id);return found==backings_.end()?nullptr:found->second.texture.get();
}
QSGTexture* TerrainRenderOwner::tintTexture() const {
    return input_?texture({QStringLiteral("@terrain/tint"),input_->tintContentKey}):nullptr;
}
QSGTexture* TerrainRenderOwner::tintTextureFor(const TerrainRenderResource& resource) const {
    if(resource.frame.tint.isNull())return nullptr;
    return texture({QStringLiteral("@terrain/tint"),resource.tintContentKey});
}
bool TerrainRenderOwner::bootstrapReady() const {
    if(!input_||input_->baseResources.size()!=2)return false;
    std::vector<TerrainDisplayDraw> reserve;
    for(const auto& id:input_->baseResources) {
        const auto found=backings_.find(id);if(found==backings_.end()||!found->second.committed||id.contentKey.isEmpty())return false;
        const auto& r=found->second.data.resource;if(r.level!=0)return false;
        reserve.push_back({r,r.interior,0});
    }
    if(!TerrainDisplayState::coverageGaps(reserve,{{-180,90,180,-90}}).empty())return false;
    if(input_->requiresTint&&(input_->tint.isNull()||input_->tintContentKey.isEmpty()))return false;
    if(!input_->tint.isNull()) {
        const auto found=backings_.find({QStringLiteral("@terrain/tint"),input_->tintContentKey});
        if(found==backings_.end()||!found->second.committed)return false;
    }
    return true;
}
bool TerrainRenderOwner::candidateReady() const {
    if(!scopeCurrent()||!bootstrapReady()||!input_->candidate||input_->candidate->scope!=input_->scope)return false;
    for(const auto& draw:input_->candidate->draws) {
        const auto found=backings_.find(idOf(draw.resource));
        if(found==backings_.end()||!found->second.committed||!(found->second.data.resource==draw.resource))return false;
    }
    return true;
}
std::vector<TerrainDisplayResourceId> TerrainRenderOwner::pendingProbes() const {
    std::vector<TerrainDisplayResourceId> result;
    std::set<QSGTexture*> seen;
    for(const auto& b:backings_)if(!b.second.committed&&!b.second.failed&&seen.insert(b.second.texture.get()).second)result.push_back(b.first);return result;
}
const std::vector<TerrainDisplayDraw>& TerrainRenderOwner::draws() const {
    return candidateSelected_?input_->candidate->draws:fallbackDraws_;
}
bool TerrainRenderOwner::needsFallbackUnderlay() const {
    return !fallbackDraws_.empty()&&(!candidateSelected_||!adoptedReceipt_||
        adoptedReceipt_->scope!=input_->scope||adoptedReceipt_->candidateId!=input_->candidate->id);
}
bool TerrainRenderOwner::hasRunnablePreparation() const {
    if(!input_||!input_->enabled)return false;
    for(const auto& id:input_->baseResources)if(!backings_.count(id))
        if(const auto* r=resource(id);r&&!r->frame.image.isNull()&&r->frame.sourceEpoch==input_->scope.sourceEpoch)return true;
    if(!input_->tint.isNull()&&!input_->tintContentKey.isEmpty()&&
       !backings_.count({QStringLiteral("@terrain/tint"),input_->tintContentKey}))return true;
    if(!bootstrapReady()||input_->inputActive)return false;
    std::set<QSGTexture*> seen;quint64 protectedBytes=0;
    for(const auto& id:protectedIds())if(const auto found=backings_.find(id);found!=backings_.end()&&seen.insert(found->second.texture.get()).second)
        protectedBytes+=found->second.bytes;
    for(const auto& r:input_->resources)if(!r.frame.image.isNull()&&!backings_.count(idOf(r.resource))) {
        const auto bytes=quint64(r.frame.image.width())*quint64(r.frame.image.height())*4;
        if(bytes<=input_->budgetBytes&&protectedBytes<=input_->budgetBytes-bytes)return true;
    }
    return false;
}
void TerrainRenderOwner::noteTextureCommit(QSGTexture* texture,double elapsed,bool valid) {
    if(!owner_.alive||!texture)return;
    bool measured=false;
    for(auto& b:backings_)if(b.second.texture.get()==texture&&!b.second.committed&&!b.second.failed) {
        if(!valid||texture->textureSize()!=b.second.data.frame.image.size()) {
            b.second.failed=true;b.second.announcedScope=input_->scope;frameResourceFailure_=true;stats_.memoryPressure=true;
            if(!measured){++stats_.allocationFailures;if(texture->textureSize()!=b.second.data.frame.image.size())++stats_.textureSizeChanges;measured=true;}
            failureDiagnostic_=QStringLiteral("Terrain Qt backing allocation failed or changed immutable pixel dimensions");
            schedule_.abandon();continue;
        }
        const auto oversizedBefore=schedule_.oversizedOperations();
        b.second.committed=true;b.second.lastUse=++tick_;
        if(!measured) {
            schedule_.committed(b.second.bytes,elapsed);stats_.oversizedOperations+=schedule_.oversizedOperations()-oversizedBefore;
            ++stats_.uploadOperations;stats_.uploadBytes+=b.second.bytes;stats_.stagingNominalBytes-=b.second.bytes;measured=true;
        }
        if(scopeCurrent()) {
            b.second.announcedScope=input_->scope;
            if(b.first.key==QStringLiteral("@terrain/tint"))newlyCommittedTint_=b.first.contentKey;
            else newlyCommitted_.push_back(b.second.data.resource);
        }
    }
    if(!valid){frameResourceFailure_=true;stats_.memoryPressure=true;}
    stats_.frameUploadOperations=schedule_.operations();stats_.frameUploadBytes=schedule_.bytes();
    stats_.frameUploadMillis=schedule_.millis();
}
void TerrainRenderOwner::beforeFrameBegin() {
    ++frame_;swapObserved_=false;frameEnded_=false;frameSubmitted_.reset();
    frameResourceFailure_=false;
    std::fill(drawObserved_.begin(),drawObserved_.end(),false);
    if(input_)schedule_.beginFrame(input_->uploadBytesPerFrame,input_->uploadMillisPerFrame);
    stats_.frameUploadBytes=stats_.frameUploadOperations=0;stats_.frameUploadMillis=0;
}
void TerrainRenderOwner::noteDraw(std::size_t index,const TerrainDisplayDraw& draw) {
    if(!scopeCurrent()||!candidateSelected_||index>=drawObserved_.size()||!(input_->candidate->draws[index]==draw))return;
    drawObserved_[index]=true;
}
void TerrainRenderOwner::afterFrameEnd() {
    if(!scopeCurrent())return;
    if(!newlyCommitted_.empty()||!newlyCommittedTint_.isEmpty()) {
        TerrainRenderObservation observation;observation.kind=TerrainRenderObservation::UploadSubmitted;
        observation.resources=newlyCommitted_;observation.tintContentKey=newlyCommittedTint_;publish(std::move(observation));
        newlyCommitted_.clear();newlyCommittedTint_.clear();
    }
    if(!frameResourceFailure_&&receiptNeeded_&&candidateSelected_&&!drawObserved_.empty()&&std::all_of(drawObserved_.begin(),drawObserved_.end(),[](bool seen){return seen;})) {
        frameSubmitted_=TerrainDisplayReceipt{input_->scope,input_->candidate->id,frame_,input_->candidate->draws};
        inFlight_[frame_]=SubmittedFrame{*frameSubmitted_,input_->baseResources,input_->tintContentKey};TerrainRenderObservation observation;
        observation.kind=TerrainRenderObservation::CandidateSubmitted;observation.receipt=frameSubmitted_;publish(std::move(observation));
        receiptNeeded_=false;
    }
    frameEnded_=true;
    // Qt's threaded loop presents before afterFrameEnd. Both observations
    // must belong to this actual frame; redirected rendering has no swap.
    if(swapObserved_&&frameSubmitted_) {
        publishCompletedFrame();
    }
    TerrainRenderObservation pressure;pressure.kind=TerrainRenderObservation::Pressure;publish(std::move(pressure));
    if(bridge_&&(!pendingProbes().empty()||hasRunnablePreparation())) {
        QMetaObject::invokeMethod(bridge_,[bridge=bridge_]{if(bridge)emit bridge->renderInputChanged();},Qt::QueuedConnection);
    }
}
void TerrainRenderOwner::frameSwapped() {
    swapObserved_=true;
    if(!frameEnded_||!scopeCurrent()||!frameSubmitted_||frameSubmitted_->scope!=input_->scope)return;
    publishCompletedFrame();
}
void TerrainRenderOwner::publishCompletedFrame() {
    if(!frameSubmitted_||!swapObserved_||!frameEnded_)return;
    // A newer actually submitted and swapped complete frame supersedes older
    // submitted leases. GUI acknowledgement can lag or reject stale scopes;
    // retain the latest receipt and the separately adopted fallback, rather
    // than accumulating a viewport's entire completed history.
    for(auto it=inFlight_.begin();it!=inFlight_.end();) {
        if(it->first<frameSubmitted_->frameSequence)it=inFlight_.erase(it);
        else ++it;
    }
    TerrainRenderObservation observation;observation.kind=TerrainRenderObservation::DisplayReceipt;
    observation.receipt=frameSubmitted_;publish(std::move(observation));frameSubmitted_.reset();
}

#pragma once
#include <QVariantList>
#include <QString>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

// Full demand identity, plus separate candidate and submitted frame identities.
struct TerrainDisplayScope {
    std::uint64_t sourceEpoch=0,windowEpoch=0,contextEpoch=0,projectGeneration=0;
    std::uint64_t viewGeneration=0,maskGeneration=0,candidateSequence=0,requestSequence=0;
    bool operator==(const TerrainDisplayScope& other) const {
        return sourceEpoch==other.sourceEpoch&&windowEpoch==other.windowEpoch&&contextEpoch==other.contextEpoch&&
            projectGeneration==other.projectGeneration&&viewGeneration==other.viewGeneration&&
            maskGeneration==other.maskGeneration&&candidateSequence==other.candidateSequence&&requestSequence==other.requestSequence;
    }
    bool operator!=(const TerrainDisplayScope& other) const{return !(*this==other);}
};
struct TerrainDisplayRect {
    double west=0,north=0,east=0,south=0;
    bool operator==(const TerrainDisplayRect& other) const {
        return west==other.west&&north==other.north&&east==other.east&&south==other.south;
    }
};
struct TerrainDisplayResource {
    // key identifies the render backing: canonical asset + /raw (DEM),
    // /raster-color or /raster-gray. Fetch/decode adapters separately retain
    // canonical asset keys. contentKey identifies the actual immutable uploaded
    // backing (for example SHA256 of actual RGBA plus source identity), not its URL.
    QString key,contentKey;
    int level=0;
    TerrainDisplayRect interior;
    bool operator==(const TerrainDisplayResource& other) const {
        return key==other.key&&contentKey==other.contentKey&&level==other.level&&interior==other.interior;
    }
};
struct TerrainDisplayResourceId {
    QString key,contentKey;
    bool operator==(const TerrainDisplayResourceId& other) const{return key==other.key&&contentKey==other.contentKey;}
    bool operator<(const TerrainDisplayResourceId& other) const {
        return key<other.key||(key==other.key&&contentKey<other.contentKey);
    }
};
struct TerrainDisplayDraw {
    TerrainDisplayResource resource;
    TerrainDisplayRect bounds;
    double worldOffset=0;
    bool operator==(const TerrainDisplayDraw& other) const {
        return resource==other.resource&&bounds==other.bounds&&worldOffset==other.worldOffset;
    }
};
struct TerrainDisplayDemand {
    TerrainDisplayScope scope;
    // Demand contentKey may be empty until decode identifies the backing.
    // CPU and upload observations must provide nonempty immutable content keys.
    std::vector<TerrainDisplayResource> base,target,prefetch;
    // Dateline domains are unwrapped. Replicas shift draw bounds only; gutters
    // do not enlarge the canonical interior or duplicate resource identity.
    std::vector<TerrainDisplayRect> domains;
    std::vector<double> worldOffsets{0};
};
struct TerrainDisplayCandidate {
    TerrainDisplayScope scope;
    std::uint64_t id=0;
    std::vector<TerrainDisplayDraw> draws;
};
struct TerrainDisplayReceipt {
    TerrainDisplayScope scope;
    std::uint64_t candidateId=0,frameSequence=0;
    std::vector<TerrainDisplayDraw> draws;
};
struct TerrainDisplaySnapshot {
    TerrainDisplayScope scope;
    std::vector<QString> requestedKeys,cpuReadyKeys,uploadSubmittedKeys,protectedKeys,releasedKeys;
    // Exact backing leases. Canonical key inventories above deduplicate copies
    // and contents; owners must use these identities for retirement decisions.
    std::vector<TerrainDisplayResourceId> cpuReadyResources,uploadSubmittedResources,protectedResources,releasedResources;
    std::vector<TerrainDisplayDraw> displayed;
    std::optional<TerrainDisplayCandidate> candidate;
    std::optional<TerrainDisplayReceipt> submitted;
    bool receiptAccepted=false,cancelled=false;
    std::uint64_t rejectedEvents=0;
};

// GUI-thread pure coverage/protection decisions: no QSGTexture, QImage or
// QObject ownership. Render owners authenticate upload enqueue and frameSwapped
// inventories; neither observation denotes a measured GPU fence.
class TerrainDisplayState {
public:
    void beginDemand(TerrainDisplayDemand demand) {
        validateDemand(demand);
        const bool sameOwner=demand_&&ownerMatches(demand_->scope,demand.scope);
        if(demand_&&demand.scope==demand_->scope) {
            if(cancelled_)throw std::invalid_argument("cancelled terrain demand needs a new scope");
            if(demand.base!=demand_->base||demand.target!=demand_->target||demand.prefetch!=demand_->prefetch||
               demand.domains!=demand_->domains||demand.worldOffsets!=demand_->worldOffsets)
                throw std::invalid_argument("terrain demand content changed without a new scope");
            return; // Idempotent repetition must not reset accepted receipts.
        }
        if(sameOwner&&(demand.scope.requestSequence<=demand_->scope.requestSequence||
                       demand.scope.candidateSequence<demand_->scope.candidateSequence))
            throw std::invalid_argument("stale terrain demand sequence");
        const auto oldProtected=protectedResources();
        if(!sameOwner) {
            cpu_.clear();uploaded_.clear();displayed_.clear();selected_.clear();uploadOrder_.clear();acceptedFrame_=0;
        }
        demand_=std::move(demand);candidate_.reset();submitted_.reset();receiptAccepted_=false;cancelled_=false;
        observed_.clear();released_.clear();
        if(!sameOwner)released_=oldProtected;
        for(const auto* specs:{&demand_->base,&demand_->target,&demand_->prefetch})for(const auto& spec:*specs)
            if(!spec.contentKey.isEmpty())selected_[spec.key]=spec.contentKey;
        // Submitted immutable resources survive view/mask/request changes.
        // Asynchronous observations must still match the entire current scope.
        for(auto it=cpu_.begin();it!=cpu_.end();) {
            if(!uploaded_.count(it->first))it=cpu_.erase(it);else ++it;
        }
    }
    bool cpuReady(const TerrainDisplayScope& scope,const TerrainDisplayResource& resource) {
        startEvent();if(!current(scope)||!demandResourceMatches(resource))return reject();
        const auto observed=observed_.find(resource.key);
        if(observed!=observed_.end()&&observed->second!=resource.contentKey)return reject();
        const auto id=identity(resource);
        const auto old=uploaded_.find(id);
        if(old!=uploaded_.end()&&!(old->second==resource))return reject();
        observed_[resource.key]=resource.contentKey;selected_[resource.key]=resource.contentKey;
        cpu_[id]=resource;return true;
    }
    bool uploadSubmitted(const TerrainDisplayScope& scope,const TerrainDisplayResource& resource) {
        startEvent();if(!current(scope)||!demandResourceMatches(resource))return reject();
        const auto id=identity(resource);const auto cpu=cpu_.find(id);
        if(cpu==cpu_.end()||!(cpu->second==resource))return reject();
        const auto old=uploaded_.find(id);
        if(old!=uploaded_.end()&&!(old->second==resource))return reject();
        uploaded_[id]=resource;uploadOrder_[id]=++uploadSerial_;return true;
    }
    std::optional<TerrainDisplayCandidate> buildCandidate() {
        startEvent();if(!demand_||cancelled_)return {};
        for(const auto& base:demand_->base)if(!resident(base))return {};
        std::vector<TerrainDisplayResource> readyTarget;
        for(const auto& target:demand_->target)if(const auto* ready=resident(target))readyTarget.push_back(*ready);
        const auto targetDraws=replicas(readyTarget);
        std::vector<TerrainDisplayResource> missingTarget;
        for(const auto& target:demand_->target)if(!resident(target))missingTarget.push_back(target);
        const auto missingDraws=replicas(missingTarget);
        // Fixed Web scans every submitted resident non-base/non-target tile,
        // including prefetch that has never been displayed. Only interiors that
        // overlap a missing target can become fallback, never arbitrary cache.
        std::map<QString,TerrainDisplayResource> fallbackByKey;
        for(const auto& entry:uploaded_) {
            const auto& resource=entry.second;
            if(isBaseLevel(resource.level)||isCurrentTarget(resource))continue;
            bool needed=false;
            for(const auto& replica:replicas({resource}))for(const auto& missing:missingDraws)
                if(overlaps(replica.bounds,missing.bounds))needed=true;
            if(!needed)continue;
            const auto old=fallbackByKey.find(resource.key);
            if(old==fallbackByKey.end()||preferFallback(resource,old->second))fallbackByKey[resource.key]=resource;
        }
        std::vector<TerrainDisplayResource> fallback;
        for(const auto& entry:fallbackByKey)fallback.push_back(entry.second);
        std::sort(fallback.begin(),fallback.end(),[](const auto& a,const auto& b) {
            if(a.level!=b.level)return a.level<b.level;
            if(a.interior.north!=b.interior.north)return a.interior.north>b.interior.north;
            if(a.interior.west!=b.interior.west)return a.interior.west<b.interior.west;
            return a.key<b.key;
        });
        std::vector<TerrainDisplayDraw> draws;
        if(!missingTarget.empty()) {
            std::vector<TerrainDisplayResource> base;
            for(const auto& spec:demand_->base)base.push_back(*resident(spec));
            draws=replicas(base);
        }
        const auto fallbackDraws=replicas(fallback);
        draws.insert(draws.end(),fallbackDraws.begin(),fallbackDraws.end());
        draws.insert(draws.end(),targetDraws.begin(),targetDraws.end());
        if(!coverageGaps(draws,demand_->domains).empty())return {};
        TerrainDisplayCandidate candidate{demand_->scope,++candidateId_,std::move(draws)};
        candidate_=candidate;return candidate;
    }
    bool submitCandidate(const TerrainDisplayCandidate& candidate,std::uint64_t frameSequence,
                         const std::vector<TerrainDisplayDraw>& authenticatedInventory) {
        startEvent();
        if(!current(candidate.scope)||!candidate_||candidate.id!=candidate_->id||candidate.draws!=candidate_->draws||
           authenticatedInventory!=candidate.draws||!frameSequence||frameSequence<=acceptedFrame_||
           (submitted_&&frameSequence<=submitted_->frameSequence))return reject();
        for(const auto& draw:authenticatedInventory) {
            const auto found=uploaded_.find(identity(draw.resource));
            if(found==uploaded_.end()||!(found->second==draw.resource))return reject();
        }
        submitted_=TerrainDisplayReceipt{candidate.scope,candidate.id,frameSequence,authenticatedInventory};return true;
    }
    bool acceptDisplayReceipt(const TerrainDisplayReceipt& receipt) {
        startEvent();
        if(!current(receipt.scope)||!submitted_||receipt.candidateId!=submitted_->candidateId||
           receipt.frameSequence!=submitted_->frameSequence||receipt.frameSequence<=acceptedFrame_||
           receipt.draws!=submitted_->draws)return reject();
        const auto before=protectedResources();
        displayed_=receipt.draws;acceptedFrame_=receipt.frameSequence;receiptAccepted_=true;
        const auto after=protectedResources();
        std::set_difference(before.begin(),before.end(),after.begin(),after.end(),std::back_inserter(released_));
        return true;
    }
    // Only after actual resource-owner retirement, not merely lease release.
    bool retireResource(const TerrainDisplayScope& scope,const QString& key,const QString& contentKey) {
        startEvent();const TerrainDisplayResourceId id{key,contentKey};
        // Retirement is tied to actual owner/backing lifetime, not demand
        // activity. A cancelled or older view may acknowledge an unprotected
        // texture's destruction, so it cannot later be reused as resident.
        if(!demand_||!ownerMatches(scope,demand_->scope))return reject();
        if(isProtected(id)) {
            // A queued older-owner acknowledgement can arrive after a new
            // demand tentatively selected the already destroyed backing. Only
            // unsubmitted candidate/target leases may yield to that evidence.
            if(scope.requestSequence>=demand_->scope.requestSequence||
               std::any_of(displayed_.begin(),displayed_.end(),[&](const auto& draw){return identity(draw.resource)==id;})||
               (submitted_&&std::any_of(submitted_->draws.begin(),submitted_->draws.end(),[&](const auto& draw){return identity(draw.resource)==id;}))||
               std::any_of(demand_->base.begin(),demand_->base.end(),[&](const auto& base){const auto* r=resident(base);return r&&identity(*r)==id;}))return reject();
            if(candidate_&&std::any_of(candidate_->draws.begin(),candidate_->draws.end(),[&](const auto& draw){return identity(draw.resource)==id;}))candidate_.reset();
        }
        const auto found=uploaded_.find(id);
        if(found==uploaded_.end())return reject();
        uploaded_.erase(found);cpu_.erase(id);uploadOrder_.erase(id);
        const auto selected=selected_.find(key);
        if(selected!=selected_.end()&&selected->second==contentKey)selected_.erase(selected);
        return true;
    }
    bool cancel(const TerrainDisplayScope& scope) {
        startEvent();if(!current(scope))return reject();
        const auto before=protectedResources();
        cancelled_=true;receiptAccepted_=false;candidate_.reset();
        // Preserve displayed/base and an actually submitted inventory; mere
        // undisplayed target residency no longer holds a cancellation lease.
        for(auto it=cpu_.begin();it!=cpu_.end();) {
            if(!uploaded_.count(it->first))it=cpu_.erase(it);else ++it;
        }
        const auto after=protectedResources();
        std::set_difference(before.begin(),before.end(),after.begin(),after.end(),std::back_inserter(released_));
        return true;
    }
    // Render owner confirms that a submission no longer holds in-flight leases.
    // This is abandonment/retirement evidence, never display adoption.
    bool discardSubmission(const TerrainDisplayScope& scope,std::uint64_t candidateId,std::uint64_t frameSequence) {
        startEvent();
        if(!demand_||!ownerMatches(scope,demand_->scope)||!submitted_||submitted_->scope!=scope||
           submitted_->candidateId!=candidateId||submitted_->frameSequence!=frameSequence)return reject();
        const auto before=protectedResources();submitted_.reset();const auto after=protectedResources();
        std::set_difference(before.begin(),before.end(),after.begin(),after.end(),std::back_inserter(released_));
        return true;
    }
    TerrainDisplaySnapshot snapshot() const {
        TerrainDisplaySnapshot result;
        if(demand_) {
            result.scope=demand_->scope;
            std::set<QString> requested;
            for(const auto* specs:{&demand_->base,&demand_->target,&demand_->prefetch})
                for(const auto& spec:*specs)requested.insert(spec.key);
            result.requestedKeys.assign(requested.begin(),requested.end());
        }
        for(const auto& resource:cpu_)result.cpuReadyResources.push_back(resource.first);
        for(const auto& resource:uploaded_)result.uploadSubmittedResources.push_back(resource.first);
        result.cpuReadyKeys=canonicalKeys(result.cpuReadyResources);
        result.uploadSubmittedKeys=canonicalKeys(result.uploadSubmittedResources);
        result.protectedResources=protectedResources();result.protectedKeys=canonicalKeys(result.protectedResources);
        result.releasedResources=released_;
        // Releasing K/A must not be reported as retiring every backing of K
        // when the current K/B remains protected.
        const auto releasedKeys=canonicalKeys(released_);
        std::set_difference(releasedKeys.begin(),releasedKeys.end(),result.protectedKeys.begin(),result.protectedKeys.end(),
                            std::back_inserter(result.releasedKeys));
        result.displayed=displayed_;
        result.candidate=candidate_;result.submitted=submitted_;result.receiptAccepted=receiptAccepted_;
        result.cancelled=cancelled_;result.rejectedEvents=rejectedEvents_;
        return result;
    }
    void reset() {
        demand_.reset();cpu_.clear();uploaded_.clear();displayed_.clear();candidate_.reset();submitted_.reset();
        selected_.clear();observed_.clear();uploadOrder_.clear();
        released_.clear();lastComplete_.clear();receiptAccepted_=false;cancelled_=false;acceptedFrame_=0;
    }
    // Deprecated CPU-only compatibility. Never authenticates typed display
    // receipts, candidates or resource protection; root removes its callers.
    QVariantList publish(QVariantList ready,bool complete) {
        if(complete)lastComplete_=ready;
        else for(const auto& old:lastComplete_)if(!ready.contains(old))ready.push_back(old);
        return ready;
    }
    static std::vector<TerrainDisplayRect> coverageGaps(const std::vector<TerrainDisplayDraw>& draws,
                                                      const std::vector<TerrainDisplayRect>& domains) {
        std::vector<TerrainDisplayRect> gaps;
        for(const auto& domain:domains) {
            if(!validRect(domain))throw std::invalid_argument("invalid terrain coverage domain");
            std::vector<TerrainDisplayRect> clipped;
            std::vector<double> xs{domain.west,domain.east};
            for(const auto& draw:draws) {
                if(!validRect(draw.bounds))throw std::invalid_argument("invalid terrain draw bounds");
                const TerrainDisplayRect rect{std::max(draw.bounds.west,domain.west),std::min(draw.bounds.north,domain.north),
                    std::min(draw.bounds.east,domain.east),std::max(draw.bounds.south,domain.south)};
                if(validRect(rect)){clipped.push_back(rect);xs.push_back(rect.west);xs.push_back(rect.east);}
            }
            std::sort(xs.begin(),xs.end());xs.erase(std::unique(xs.begin(),xs.end()),xs.end());
            for(std::size_t x=0;x+1<xs.size();++x) {
                std::vector<std::pair<double,double>> ys;
                for(const auto& rect:clipped)if(rect.west<=xs[x]&&rect.east>=xs[x+1])ys.emplace_back(rect.south,rect.north);
                std::sort(ys.begin(),ys.end());double covered=domain.south;
                for(const auto& interval:ys) {
                    if(interval.first>covered)gaps.push_back({xs[x],interval.first,xs[x+1],covered});
                    covered=std::max(covered,interval.second);
                }
                if(covered<domain.north)gaps.push_back({xs[x],domain.north,xs[x+1],covered});
            }
        }
        return gaps;
    }
private:
    static bool validRect(const TerrainDisplayRect& rect) {
        return std::isfinite(rect.west)&&std::isfinite(rect.north)&&std::isfinite(rect.east)&&std::isfinite(rect.south)&&
            rect.west<rect.east&&rect.south<rect.north;
    }
    static bool overlaps(const TerrainDisplayRect& a,const TerrainDisplayRect& b) {
        return a.west<b.east&&a.east>b.west&&a.south<b.north&&a.north>b.south;
    }
    static bool ownerMatches(const TerrainDisplayScope& a,const TerrainDisplayScope& b) {
        return a.sourceEpoch==b.sourceEpoch&&a.windowEpoch==b.windowEpoch&&a.contextEpoch==b.contextEpoch&&
            a.projectGeneration==b.projectGeneration;
    }
    static void validateDemand(const TerrainDisplayDemand& demand) {
        if(demand.domains.empty()||demand.worldOffsets.empty()||demand.base.empty())
            throw std::invalid_argument("terrain demand needs domains, replicas and world reserve");
        for(const auto& domain:demand.domains)if(!validRect(domain)||domain.north>90||domain.south< -90)
            throw std::invalid_argument("invalid terrain demand domain");
        std::set<double> offsets;
        for(const auto offset:demand.worldOffsets)if(!std::isfinite(offset)||std::fmod(offset,360)!=0||!offsets.insert(offset).second)
            throw std::invalid_argument("invalid terrain world replica");
        std::map<QString,TerrainDisplayResource> specs;
        for(const auto* collection:{&demand.base,&demand.target,&demand.prefetch})for(const auto& spec:*collection) {
            if(spec.key.isEmpty()||spec.level<0||!validRect(spec.interior)||spec.interior.west< -180||
               spec.interior.east>180||spec.interior.north>90||spec.interior.south< -90)
                throw std::invalid_argument("invalid canonical terrain interior");
            const auto old=specs.find(spec.key);
            if(old!=specs.end()&&!(old->second==spec))throw std::invalid_argument("conflicting terrain resource identity");
            specs[spec.key]=spec;
        }
        std::vector<TerrainDisplayDraw> base;
        for(const auto& spec:demand.base)base.push_back({spec,spec.interior,0});
        if(!coverageGaps(base,{{-180,90,180,-90}}).empty())throw std::invalid_argument("incomplete terrain world reserve");
    }
    bool current(const TerrainDisplayScope& scope) const{return demand_&&!cancelled_&&demand_->scope==scope;}
    bool demandResourceMatches(const TerrainDisplayResource& resource) const {
        if(!demand_||resource.contentKey.isEmpty())return false;
        for(const auto* specs:{&demand_->base,&demand_->target,&demand_->prefetch})for(const auto& spec:*specs)
            if(spec.key==resource.key&&spec.level==resource.level&&spec.interior==resource.interior&&
               (spec.contentKey.isEmpty()||spec.contentKey==resource.contentKey))return true;
        return false;
    }
    const TerrainDisplayResource* resident(const TerrainDisplayResource& spec) const {
        auto content=spec.contentKey;
        if(content.isEmpty()) {
            const auto selected=selected_.find(spec.key);if(selected==selected_.end())return nullptr;
            content=selected->second;
        }
        const auto found=uploaded_.find({spec.key,content});
        if(found==uploaded_.end())return nullptr;
        const auto& resource=found->second;
        return resource.level==spec.level&&resource.interior==spec.interior&&
            (spec.contentKey.isEmpty()||resource.contentKey==spec.contentKey)?&resource:nullptr;
    }
    bool isBaseLevel(int level) const {
        return demand_&&std::any_of(demand_->base.begin(),demand_->base.end(),[&](const auto& spec){return spec.level==level;});
    }
    bool isCurrentTarget(const TerrainDisplayResource& resource) const {
        if(!demand_)return false;
        for(const auto& spec:demand_->target)if(const auto* ready=resident(spec))
            if(*ready==resource)return true;
        return false;
    }
    std::vector<TerrainDisplayDraw> replicas(const std::vector<TerrainDisplayResource>& resources) const {
        std::vector<TerrainDisplayDraw> result;if(!demand_)return result;
        for(const auto offset:demand_->worldOffsets)for(const auto& resource:resources) {
            auto bounds=resource.interior;bounds.west+=offset;bounds.east+=offset;
            if(std::any_of(demand_->domains.begin(),demand_->domains.end(),[&](const auto& domain){return overlaps(bounds,domain);}))
                result.push_back({resource,bounds,offset});
        }
        return result;
    }
    static TerrainDisplayResourceId identity(const TerrainDisplayResource& resource) {
        return {resource.key,resource.contentKey};
    }
    static std::vector<QString> canonicalKeys(const std::vector<TerrainDisplayResourceId>& ids) {
        std::set<QString> keys;for(const auto& id:ids)keys.insert(id.key);return {keys.begin(),keys.end()};
    }
    std::vector<TerrainDisplayResourceId> protectedResources() const {
        std::set<TerrainDisplayResourceId> result;
        if(demand_) {
            for(const auto& spec:demand_->base)if(const auto* resource=resident(spec))result.insert(identity(*resource));
            if(!cancelled_)for(const auto& spec:demand_->target)
                if(const auto* resource=resident(spec))result.insert(identity(*resource));
        }
        for(const auto& draw:displayed_)result.insert(identity(draw.resource));
        if(candidate_)for(const auto& draw:candidate_->draws)result.insert(identity(draw.resource));
        if(submitted_)for(const auto& draw:submitted_->draws)result.insert(identity(draw.resource));
        return {result.begin(),result.end()};
    }
    bool isProtected(const TerrainDisplayResourceId& id) const {
        const auto ids=protectedResources();return std::binary_search(ids.begin(),ids.end(),id);
    }
    bool preferFallback(const TerrainDisplayResource& resource,const TerrainDisplayResource& old) const {
        const auto displayed=[&](const auto& value) {
            return std::any_of(displayed_.begin(),displayed_.end(),[&](const auto& draw){return draw.resource==value;});
        };
        const bool currentDisplayed=displayed(resource),oldDisplayed=displayed(old);
        if(currentDisplayed!=oldDisplayed)return currentDisplayed;
        return uploadOrder_.at(identity(resource))>uploadOrder_.at(identity(old));
    }
    // Acceptance is current-demand display state, not a one-event pulse.
    // Readiness, candidate preparation and rejected events cannot revoke an
    // actually presented frame. beginDemand/cancel/reset invalidate it.
    void startEvent(){released_.clear();}
    bool reject(){++rejectedEvents_;return false;}
    std::optional<TerrainDisplayDemand> demand_;
    std::map<TerrainDisplayResourceId,TerrainDisplayResource> cpu_,uploaded_;
    std::map<QString,QString> selected_,observed_;
    std::map<TerrainDisplayResourceId,std::uint64_t> uploadOrder_;
    std::vector<TerrainDisplayDraw> displayed_;
    std::optional<TerrainDisplayCandidate> candidate_;
    std::optional<TerrainDisplayReceipt> submitted_;
    std::vector<TerrainDisplayResourceId> released_;
    QVariantList lastComplete_;
    std::uint64_t candidateId_=0,acceptedFrame_=0,rejectedEvents_=0,uploadSerial_=0;
    bool receiptAccepted_=false,cancelled_=false;
};

#include <pandoeditor/jobs.h>
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace pandoeditor {
namespace {
JobDisposition disposition(JobStop reason) noexcept {
    switch(reason) {
    case JobStop::None: return JobDisposition::Accepted;
    case JobStop::Cancelled: return JobDisposition::Cancelled;
    case JobStop::Coalesced: return JobDisposition::Coalesced;
    case JobStop::Aborted: return JobDisposition::Aborted;
    case JobStop::Stale: return JobDisposition::Stale;
    case JobStop::Closed: return JobDisposition::Closed;
    }
    return JobDisposition::Unknown;
}
}
bool JobToken::cancelled() const noexcept { return reason()!=JobStop::None; }
JobStop JobToken::reason() const noexcept { return static_cast<JobStop>(state_->value.load(std::memory_order_relaxed)>>8); }
int JobToken::progress() const noexcept { return static_cast<int>(state_->value.load(std::memory_order_relaxed)&255)-1; }
void JobToken::reportProgress(int value) const noexcept {
    if(value<0 || value>100) return;
    auto old=state_->value.load(std::memory_order_relaxed);
    while(!(old>>8) && static_cast<int>(old&255)<value+1)
        if(state_->value.compare_exchange_weak(old,static_cast<std::uint32_t>(value+1),std::memory_order_relaxed)) return;
}
bool JobToken::stop(JobStop reason) const noexcept {
    if(reason==JobStop::None) return false;
    auto old=state_->value.load(std::memory_order_relaxed);
    while(!(old>>8))
        if(state_->value.compare_exchange_weak(old,old|(static_cast<std::uint32_t>(reason)<<8),std::memory_order_relaxed)) return true;
    return false;
}
std::uint64_t JobTicket::id() const noexcept { return data_->id; }
const std::string& JobTicket::key() const noexcept { return data_->key; }
const ProjectSnapshot& JobTicket::snapshot() const noexcept { return data_->snapshot; }
const JobToken& JobTicket::token() const noexcept { return data_->token; }
const char* jobDispositionCode(JobDisposition value) noexcept {
    switch(value) {
    case JobDisposition::Accepted: return "accepted";
    case JobDisposition::Cancelled: return "cancelled";
    case JobDisposition::Coalesced: return "coalesced";
    case JobDisposition::Aborted: return "aborted";
    case JobDisposition::Stale: return "stale";
    case JobDisposition::Closed: return "closed";
    case JobDisposition::Unknown: return "unknown";
    }
    return "unknown";
}
JobTicket JobScheduler::enqueue(ProjectSnapshot snapshot,std::string key,int priority) {
    if(closed_) throw std::logic_error("JOBS_CLOSED");
    if(sequence_==std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("JOB_ID_OVERFLOW");
    if(key.empty()) key="default";
    JobTicket ticket(std::make_shared<const JobTicket::Data>(JobTicket::Data{
        sequence_+1,std::move(key),priority,std::move(snapshot),JobToken(std::make_shared<JobToken::State>())}));
    // Allocate the new node before superseding anything. Submission allocation
    // failure must not silently cancel the previous user's accepted request.
    queue_.push_back(ticket);
    ++sequence_; ++stats_.submitted;
    for(auto it=queue_.begin();it!=queue_.end();) {
        if(it->data_!=ticket.data_ && it->key()==ticket.key()) { stop(*it,JobStop::Coalesced); it=queue_.erase(it); }
        else ++it;
    }
    if(running_ && running_->key()==ticket.key()) stop(*running_,JobStop::Coalesced);
    stats_.maxQueueDepth=std::max(stats_.maxQueueDepth,queue_.size());
    return ticket;
}
std::optional<JobTicket> JobScheduler::takeNext() {
    if(closed_ || running_ || queue_.empty()) return {};
    auto next=std::max_element(queue_.begin(),queue_.end(),[](const auto& a,const auto& b){ return a.data_->priority<b.data_->priority; });
    running_=*next; queue_.erase(next); ++stats_.started;
    return running_;
}
bool JobScheduler::stop(const JobTicket& ticket,JobStop reason) noexcept {
    if(!ticket.token().stop(reason)) return false;
    if(reason==JobStop::Coalesced) ++stats_.coalesced;
    else if(reason==JobStop::Stale) ++stats_.staleDiscarded;
    else ++stats_.cancelled;
    return true;
}
JobDisposition JobScheduler::finish(const JobTicket& ticket,const Project& current) noexcept {
    // IDs are scheduler-local. Identity also rejects foreign and duplicate results.
    if(!running_ || running_->data_!=ticket.data_) return JobDisposition::Unknown;
    running_.reset();
    if(!ticket.token().cancelled() && !ticket.snapshot().matches(current)) stop(ticket,JobStop::Stale);
    return disposition(ticket.token().reason());
}
bool JobScheduler::cancel(std::uint64_t id,JobStop reason) noexcept {
    if(reason==JobStop::None) return false;
    auto it=std::find_if(queue_.begin(),queue_.end(),[&](const auto& t){return t.id()==id;});
    if(it!=queue_.end()) { const bool changed=stop(*it,reason); queue_.erase(it); return changed; }
    return running_ && running_->id()==id && stop(*running_,reason);
}
bool JobScheduler::cancelKey(const std::string& key,JobStop reason) noexcept {
    if(reason==JobStop::None) return false;
    bool changed=false;
    for(auto it=queue_.begin();it!=queue_.end();) {
        if(it->key()==key || (key.empty() && it->key()=="default")) { changed=stop(*it,reason)||changed; it=queue_.erase(it); }
        else ++it;
    }
    if(running_ && (running_->key()==key || (key.empty() && running_->key()=="default"))) changed=stop(*running_,reason)||changed;
    return changed;
}
void JobScheduler::cancelAll(JobStop reason) noexcept {
    if(reason==JobStop::None) return;
    for(const auto& t:queue_) stop(t,reason);
    queue_.clear(); if(running_) stop(*running_,reason);
}
void JobScheduler::close() noexcept {
    if(closed_) return;
    closed_=true; cancelAll(JobStop::Closed);
}
}

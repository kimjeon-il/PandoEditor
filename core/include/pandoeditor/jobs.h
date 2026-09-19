#pragma once
#include <pandoeditor/project.h>
#include <atomic>
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <string>
namespace pandoeditor {
enum class JobStop { None, Cancelled, Coalesced, Aborted, Stale, Closed };
enum class JobDisposition { Accepted, Cancelled, Coalesced, Aborted, Stale, Closed, Unknown };
const char* jobDispositionCode(JobDisposition) noexcept;
// Only this shared cancellation/progress state crosses threads mutably.
class JobToken {
public:
    bool cancelled() const noexcept;
    JobStop reason() const noexcept;
    int progress() const noexcept;
    void reportProgress(int value) const noexcept;
private:
    // One atomic word makes stop + last visible progress an indivisible boundary.
    struct State { std::atomic<std::uint32_t> value{0}; };
    friend class JobScheduler;
    explicit JobToken(std::shared_ptr<State> state):state_(std::move(state)) {}
    bool stop(JobStop reason) const noexcept;
    std::shared_ptr<State> state_;
};
class JobTicket {
public:
    std::uint64_t id() const noexcept;
    const std::string& key() const noexcept;
    const ProjectSnapshot& snapshot() const noexcept;
    const JobToken& token() const noexcept;
private:
    friend class JobScheduler;
    struct Data {
        std::uint64_t id;
        std::string key;
        int priority;
        ProjectSnapshot snapshot;
        JobToken token;
    };
    explicit JobTicket(std::shared_ptr<const Data> data):data_(std::move(data)) {}
    std::shared_ptr<const Data> data_;
};
struct JobStats { std::uint64_t submitted=0, started=0, cancelled=0, coalesced=0, staleDiscarded=0; std::size_t maxQueueDepth=0; };
// Owner-thread scheduler; no Project mutation and no executor ownership.
// One physical running task, priority then FIFO, latest queued successor/key.
class JobScheduler {
public:
    JobScheduler()=default;
    ~JobScheduler() { close(); }
    JobScheduler(const JobScheduler&)=delete;
    JobScheduler& operator=(const JobScheduler&)=delete;
    JobScheduler(JobScheduler&&)=delete;
    JobScheduler& operator=(JobScheduler&&)=delete;
    JobTicket enqueue(ProjectSnapshot snapshot,std::string key,int priority=0);
    std::optional<JobTicket> takeNext();
    JobDisposition finish(const JobTicket&,const Project& current) noexcept;
    bool cancel(std::uint64_t id,JobStop reason=JobStop::Cancelled) noexcept;
    bool cancelKey(const std::string& key,JobStop reason=JobStop::Cancelled) noexcept;
    void cancelAll(JobStop reason=JobStop::Cancelled) noexcept;
    void close() noexcept;
    bool closed() const noexcept { return closed_; }
    std::size_t queueDepth() const noexcept { return queue_.size(); }
    std::size_t runningCount() const noexcept { return running_?1:0; }
    JobStats stats() const noexcept { return stats_; }
private:
    bool stop(const JobTicket&,JobStop) noexcept;
    std::list<JobTicket> queue_;
    std::optional<JobTicket> running_;
    JobStats stats_;
    std::uint64_t sequence_=0;
    bool closed_=false;
};
}

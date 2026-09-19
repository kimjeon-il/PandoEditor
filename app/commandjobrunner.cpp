#include "commandjobrunner.h"
#include <QFutureWatcher>
#include <QPointer>
#include <QThread>
#include <QtConcurrentRun>
#include <stdexcept>
#include <algorithm>
using namespace pandoeditor;
namespace {
PrepareResult failed() { PrepareResult r; r.error=CommandError::PrepareFailed; return r; }
JobDisposition stopped(JobStop s) {
    switch(s) {
    case JobStop::Coalesced: return JobDisposition::Coalesced;
    case JobStop::Aborted: return JobDisposition::Aborted;
    case JobStop::Stale: return JobDisposition::Stale;
    case JobStop::Closed: return JobDisposition::Closed;
    default: return JobDisposition::Cancelled;
    }
}
}
CommandJobRunner::CommandJobRunner(std::function<const Project&()> current,QObject* parent)
    :QObject(parent),current_(std::move(current))
{
    if(!current_) throw std::invalid_argument("current project provider is required");
    progressTimer_.setInterval(40);
    connect(&progressTimer_,&QTimer::timeout,this,&CommandJobRunner::changed);
}
CommandJobRunner::~CommandJobRunner()
{
    scheduler_.close(); entries_.clear();
    // QFutureWatchers are QObject children. Their receiver-bound connections
    // disappear on destruction; workers retain only snapshots, tokens and values.
}
JobTicket CommandJobRunner::submit(ProjectSnapshot snapshot,std::string key,Task task,Completion completion,int priority)
{
    Q_ASSERT(QThread::currentThread()==thread());
    if(!task) throw std::invalid_argument("worker task is required");
    // Allocate callback storage before enqueue supersedes any existing entry.
    std::map<std::uint64_t,Entry> staged;
    staged.emplace(0,Entry{std::nullopt,std::move(task),std::move(completion)});
    auto ticket=scheduler_.enqueue(std::move(snapshot),std::move(key),priority);
    auto node=staged.extract(0); node.key()=ticket.id(); node.mapped().ticket=ticket; entries_.insert(std::move(node));
    QPointer<CommandJobRunner> guard(this);
    flushStopped(); if(!guard) return ticket;
    pump(); if(!guard) return ticket;
    progressTimer_.start(); emit changed();
    return ticket;
}
void CommandJobRunner::flushStopped()
{
    for(;;) {
        auto it=std::find_if(entries_.begin(),entries_.end(),[](const auto& e){return e.second.ticket->token().cancelled();});
        if(it==entries_.end()) break;
        auto entry=std::move(it->second); entries_.erase(it);
        if(entry.completion) {
            const auto id=entry.ticket->id(); const auto reason=stopped(entry.ticket->token().reason());
            // Like Promise rejection in the web scheduler, notify on a later
            // event-loop turn; never reenter the submitter before its ID returns.
            QMetaObject::invokeMethod(this,[completion=std::move(entry.completion),id,reason]() mutable {
                completion(id,reason,{});
            },Qt::QueuedConnection);
        }
    }
    if(entries_.empty()) progressTimer_.stop();
}
void CommandJobRunner::cancel(std::uint64_t id)
{
    Q_ASSERT(QThread::currentThread()==thread());
    scheduler_.cancel(id); QPointer<CommandJobRunner> guard(this); flushStopped();
    if(guard) emit changed();
}
void CommandJobRunner::cancelAll()
{
    Q_ASSERT(QThread::currentThread()==thread());
    scheduler_.cancelAll(); QPointer<CommandJobRunner> guard(this); flushStopped();
    if(guard) emit changed();
}
void CommandJobRunner::pump()
{
    Q_ASSERT(QThread::currentThread()==thread());
    auto next=scheduler_.takeNext(); if(!next) return;
    const auto ticket=*next;
    auto it=entries_.find(ticket.id());
    if(it==entries_.end()) { scheduler_.cancel(ticket.id()); scheduler_.finish(ticket,current_()); return; }
    auto task=std::move(it->second.task);
    try {
        auto watcher=std::make_unique<QFutureWatcher<PrepareResult>>(this);
        auto* receiver=watcher.get();
        connect(receiver,&QFutureWatcher<PrepareResult>::finished,this,[this,receiver,ticket]() {
            PrepareResult result;
            try { result=receiver->future().takeResult(); } catch(...) { result=failed(); }
            receiver->deleteLater(); complete(ticket,std::move(result));
        });
        receiver->setFuture(QtConcurrent::run([ticket,task=std::move(task)]() mutable {
            if(ticket.token().cancelled()) return PrepareResult{};
            try { return task(ticket.snapshot(),ticket.token()); } catch(...) { return failed(); }
        }));
        watcher.release();
    } catch(...) {
        // Complete on a later owner-thread turn, after submit has returned its ID.
        QMetaObject::invokeMethod(this,[this,ticket](){complete(ticket,failed());},Qt::QueuedConnection);
    }
}
void CommandJobRunner::complete(const JobTicket& ticket,PrepareResult result)
{
    Q_ASSERT(QThread::currentThread()==thread());
    const auto disposition=scheduler_.finish(ticket,current_());
    Completion completion;
    auto it=entries_.find(ticket.id());
    if(it!=entries_.end()) { completion=std::move(it->second.completion); entries_.erase(it); }
    if(disposition!=JobDisposition::Accepted) result={}; // destroy obsolete candidate
    // Schedule first; a completion may reenter or destroy the editor/runner.
    QMetaObject::invokeMethod(this,[this](){pump();},Qt::QueuedConnection);
    QPointer<CommandJobRunner> guard(this);
    if(completion) completion(ticket.id(),disposition,std::move(result));
    if(!guard) return;
    if(entries_.empty()) progressTimer_.stop();
    emit changed();
}

#include "commandjobrunner.h"
#include <QFutureWatcher>
#include <QPointer>
#include <QThread>
#include <QtConcurrentRun>
#include <stdexcept>
#include <algorithm>
#include <type_traits>
using namespace pandoeditor;
namespace {
std::string exceptionDetail() {
    try { throw; }
    catch(const std::exception& error) { return error.what(); }
    catch(...) { return "Worker calculation failed with a non-standard exception"; }
}
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
    if(!task) throw std::invalid_argument("worker task is required");
    return submitWork(std::move(snapshot),std::move(key),std::move(task),std::move(completion),priority);
}
JobTicket CommandJobRunner::submitGeometry(ProjectSnapshot snapshot,std::string key,GeometryTask task,GeometryCompletion completion,int priority)
{
    if(!task) throw std::invalid_argument("worker task is required");
    return submitWork(std::move(snapshot),std::move(key),std::move(task),std::move(completion),priority);
}
CommandJobRunner::Result CommandJobRunner::emptyResult(std::size_t kind)
{
    if(kind==1) return GeometryJobResult{};
    return PrepareResult{};
}
CommandJobRunner::Result CommandJobRunner::failedResult(std::size_t kind,std::string detail)
{
    if(kind==1) return GeometryJobResult{GeometryJobFailure{CommandError::PrepareFailed,std::move(detail)}};
    PrepareResult result; result.error=CommandError::PrepareFailed; return result;
}
void CommandJobRunner::invokeCompletion(Callback completion,std::uint64_t id,JobDisposition disposition,Result result)
{
    std::visit([&](auto& callback) {
        if(!callback) return;
        using Value=std::conditional_t<std::is_same_v<std::decay_t<decltype(callback)>,Completion>,PrepareResult,GeometryJobResult>;
        callback(id,disposition,std::move(std::get<Value>(result)));
    },completion);
}
JobTicket CommandJobRunner::submitWork(ProjectSnapshot snapshot,std::string key,Worker task,Callback completion,int priority)
{
    Q_ASSERT(QThread::currentThread()==thread());
    Q_ASSERT(task.index()==completion.index());
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
        const auto id=entry.ticket->id(); const auto reason=stopped(entry.ticket->token().reason());
        const auto kind=entry.completion.index();
        // Like Promise rejection in the web scheduler, notify on a later
        // event-loop turn; never reenter the submitter before its ID returns.
        QMetaObject::invokeMethod(this,[completion=std::move(entry.completion),id,reason,kind]() mutable {
            invokeCompletion(std::move(completion),id,reason,emptyResult(kind));
        },Qt::QueuedConnection);
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
void CommandJobRunner::cancelKey(const std::string& key)
{
    Q_ASSERT(QThread::currentThread()==thread());
    scheduler_.cancelKey(key); QPointer<CommandJobRunner> guard(this); flushStopped();
    if(guard) emit changed();
}
pandoeditor::ProjectSnapshot CommandJobRunner::currentSnapshot() const
{
    Q_ASSERT(QThread::currentThread()==thread());
    return current_().snapshot();
}
void CommandJobRunner::pump()
{
    Q_ASSERT(QThread::currentThread()==thread());
    auto next=scheduler_.takeNext(); if(!next) return;
    const auto ticket=*next;
    auto it=entries_.find(ticket.id());
    if(it==entries_.end()) { scheduler_.cancel(ticket.id()); scheduler_.finish(ticket,current_()); return; }
    auto task=std::move(it->second.task);
    const auto kind=task.index();
    try {
        auto watcher=std::make_unique<QFutureWatcher<Result>>(this);
        auto* receiver=watcher.get();
        connect(receiver,&QFutureWatcher<Result>::finished,this,[this,receiver,ticket,kind]() {
            Result result;
            try { result=receiver->future().takeResult(); } catch(...) { result=failedResult(kind,exceptionDetail()); }
            receiver->deleteLater(); complete(ticket,std::move(result));
        });
        receiver->setFuture(QtConcurrent::run([ticket,task=std::move(task),kind]() mutable ->Result {
            if(ticket.token().cancelled()) return emptyResult(kind);
            try {
                return std::visit([&](auto& worker) ->Result {return worker(ticket.snapshot(),ticket.token());},task);
            } catch(...) { return failedResult(kind,exceptionDetail()); }
        }));
        watcher.release();
    } catch(...) {
        // Complete on a later owner-thread turn, after submit has returned its ID.
        QMetaObject::invokeMethod(this,[this,ticket,kind,detail=exceptionDetail()](){complete(ticket,failedResult(kind,detail));},Qt::QueuedConnection);
    }
}
void CommandJobRunner::complete(const JobTicket& ticket,Result result)
{
    Q_ASSERT(QThread::currentThread()==thread());
    const auto disposition=scheduler_.finish(ticket,current_());
    Callback completion;
    auto it=entries_.find(ticket.id());
    if(it!=entries_.end()) { completion=std::move(it->second.completion); entries_.erase(it); }
    if(disposition!=JobDisposition::Accepted) result=emptyResult(result.index()); // destroy obsolete candidate/calculation
    // Schedule first; a completion may reenter or destroy the editor/runner.
    QMetaObject::invokeMethod(this,[this](){pump();},Qt::QueuedConnection);
    QPointer<CommandJobRunner> guard(this);
    invokeCompletion(std::move(completion),ticket.id(),disposition,std::move(result));
    if(!guard) return;
    if(entries_.empty()) progressTimer_.stop();
    emit changed();
}

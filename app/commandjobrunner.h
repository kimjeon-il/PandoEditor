#pragma once
#include "geometryjobresult.h"
#include <pandoeditor/jobs.h>
#include <QObject>
#include <QTimer>
#include <functional>
#include <map>

// Scheduling/completion are owner-thread-only. Task receives no Project or
// QObject; capture value inputs only. Destruction never waits for worker code.
class CommandJobRunner : public QObject {
    Q_OBJECT
public:
    using Task=std::function<pandoeditor::PrepareResult(const pandoeditor::ProjectSnapshot&,const pandoeditor::JobToken&)>;
    using Completion=std::function<void(std::uint64_t,pandoeditor::JobDisposition,pandoeditor::PrepareResult)>;
    using GeometryTask=std::function<GeometryJobResult(const pandoeditor::ProjectSnapshot&,const pandoeditor::JobToken&)>;
    using GeometryCompletion=std::function<void(std::uint64_t,pandoeditor::JobDisposition,GeometryJobResult)>;
    explicit CommandJobRunner(std::function<const pandoeditor::Project&()> current,QObject* parent=nullptr);
    ~CommandJobRunner() override;
    pandoeditor::JobTicket submit(pandoeditor::ProjectSnapshot,std::string key,Task,Completion,int priority=0);
    pandoeditor::JobTicket submitGeometry(pandoeditor::ProjectSnapshot,std::string key,GeometryTask,GeometryCompletion,int priority=0);
    void cancel(std::uint64_t id);
    void cancelKey(const std::string& key);
    void cancelAll();
    pandoeditor::ProjectSnapshot currentSnapshot() const;
    std::size_t runningCount() const { return scheduler_.runningCount(); }
    std::size_t queueDepth() const { return scheduler_.queueDepth(); }
signals:
    void changed();
    void operationMeasured(qulonglong jobId,QString operation,double milliseconds,int disposition);
private:
    using Result=std::variant<pandoeditor::PrepareResult,GeometryJobResult>;
    using Worker=std::variant<Task,GeometryTask>;
    using Callback=std::variant<Completion,GeometryCompletion>;
    struct Entry { std::optional<pandoeditor::JobTicket> ticket; Worker task; Callback completion; };
    pandoeditor::JobTicket submitWork(pandoeditor::ProjectSnapshot,std::string,Worker,Callback,int);
    static Result emptyResult(std::size_t kind);
    static Result failedResult(std::size_t kind,std::string detail);
    static void invokeCompletion(Callback,std::uint64_t,pandoeditor::JobDisposition,Result);
    void pump();
    void flushStopped();
    void complete(const pandoeditor::JobTicket&,Result,std::optional<double> milliseconds=std::nullopt);
    std::function<const pandoeditor::Project&()> current_;
    pandoeditor::JobScheduler scheduler_;
    std::map<std::uint64_t,Entry> entries_;
    QTimer progressTimer_;
};

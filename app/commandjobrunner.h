#pragma once
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
    explicit CommandJobRunner(std::function<const pandoeditor::Project&()> current,QObject* parent=nullptr);
    ~CommandJobRunner() override;
    pandoeditor::JobTicket submit(pandoeditor::ProjectSnapshot,std::string key,Task,Completion,int priority=0);
    void cancel(std::uint64_t id);
    void cancelAll();
    std::size_t runningCount() const { return scheduler_.runningCount(); }
    std::size_t queueDepth() const { return scheduler_.queueDepth(); }
signals:
    void changed();
private:
    struct Entry { std::optional<pandoeditor::JobTicket> ticket; Task task; Completion completion; };
    void pump();
    void flushStopped();
    void complete(const pandoeditor::JobTicket&,pandoeditor::PrepareResult);
    std::function<const pandoeditor::Project&()> current_;
    pandoeditor::JobScheduler scheduler_;
    std::map<std::uint64_t,Entry> entries_;
    QTimer progressTimer_;
};

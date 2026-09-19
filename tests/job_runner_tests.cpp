#include "commandjobrunner.h"
#include <QSemaphore>
#include <QSignalSpy>
#include <QThread>
#include <QtTest>
#include <atomic>
using namespace pandoeditor;
namespace {
Project sample() { Project p; p.replace(std::vector<Country>{{"A","Alpha",{{{{0,0},{1,0},{1,1},{0,1},{0,0}}}},0x123456}}); return p; }
CommandRequest request(const Project& p) { CommandArguments a; a.action=SetCountryColor{"A",0x102030}; return CommandProcessor::makeRequest(p,"country.color",a); }
struct Gate { QSemaphore started,release,finished; };
// The guard is separate from the shared worker gate: any early test return
// releases the worker before Qt's global pool can wait during shutdown.
struct Release { std::shared_ptr<Gate> gate; ~Release(){gate->release.release();} };
CommandJobRunner::Task work(std::shared_ptr<Gate> gate,CommandRequest req,std::shared_ptr<std::atomic<bool>> offThread) {
    auto* owner=QThread::currentThread();
    return [gate,req=std::move(req),offThread,owner](const ProjectSnapshot& s,const JobToken& token) {
        offThread->store(QThread::currentThread()!=owner); token.reportProgress(30); gate->started.release(); gate->release.acquire();
        auto result=CommandProcessor::prepare(s,req); token.reportProgress(100); gate->finished.release(); return result;
    };
}
}
class JobRunnerTests : public QObject {
    Q_OBJECT
private slots:
    void realWorkerAndGuiCompletion() {
        auto p=sample(); CommandJobRunner r([&]() ->const Project& {return p;});
        auto gate=std::make_shared<Gate>(); Release release{gate}; auto off=std::make_shared<std::atomic<bool>>(false);
        bool completed=false,gui=false; auto ticket=r.submit(p.snapshot(),"one",work(gate,request(p),off),[&](auto,auto status,PrepareResult result){
            gui=QThread::currentThread()==r.thread(); completed=true; QCOMPARE(status,JobDisposition::Accepted); QVERIFY(result.preview);
            QVERIFY(CommandProcessor::confirm(p,*result.preview).changed());
        });
        QTRY_VERIFY(gate->started.available()); QVERIFY(off->load()); QCOMPARE(p.revision(),std::uint64_t(0)); QCOMPARE(ticket.token().progress(),30);
        gate->release.release(); QTRY_VERIFY(completed); QVERIFY(gui); QCOMPARE(p.revision(),std::uint64_t(1));
    }
    void latestAndLateCancelCannotPublish() {
        auto p=sample(); CommandJobRunner r([&]() ->const Project& {return p;});
        auto gate=std::make_shared<Gate>(); Release release{gate}; auto off=std::make_shared<std::atomic<bool>>(false);
        int cancelled=0,accepted=0,queuedExecutions=0;
        auto done=[&](auto,auto status,PrepareResult result){ if(status==JobDisposition::Coalesced) ++cancelled; else if(status==JobDisposition::Accepted) { ++accepted; QVERIFY(result.preview); } };
        auto a=r.submit(p.snapshot(),"same",work(gate,request(p),off),done); QTRY_VERIFY(gate->started.available());
        auto task=[&,req=request(p)](const ProjectSnapshot& s,const JobToken&) { ++queuedExecutions; return CommandProcessor::prepare(s,req); };
        r.submit(p.snapshot(),"same",task,done); r.submit(p.snapshot(),"same",task,done);
        QTRY_COMPARE(cancelled,2); QCOMPARE(r.runningCount(),std::size_t(1)); QCOMPARE(r.queueDepth(),std::size_t(1));
        gate->release.release(); QTRY_COMPARE(accepted,1); QCOMPARE(queuedExecutions,1); QCOMPARE(p.revision(),std::uint64_t(0));
    }
    void staleAndThrownTaskKeepRedo() {
        auto p=sample(); QVERIFY(p.setMemo("A","redo")); QVERIFY(p.undo()); CommandJobRunner r([&]() ->const Project& {return p;});
        auto gate=std::make_shared<Gate>(); Release release{gate}; auto off=std::make_shared<std::atomic<bool>>(false);
        bool stale=false; r.submit(p.snapshot(),"stale",work(gate,request(p),off),[&](auto,auto status,PrepareResult result){ stale=status==JobDisposition::Stale; QVERIFY(!result.preview); });
        QTRY_VERIFY(gate->started.available()); QVERIFY(p.redo()); gate->release.release(); QTRY_VERIFY(stale); QCOMPARE(p.revision(),std::uint64_t(3));
        QVERIFY(p.undo()); bool failed=false;
        r.submit(p.snapshot(),"throws",[](const ProjectSnapshot&,const JobToken&) ->PrepareResult {throw std::runtime_error("worker failed");},
            [&](auto,auto status,PrepareResult result){ QCOMPARE(status,JobDisposition::Accepted); failed=result.error==CommandError::PrepareFailed; });
        QTRY_VERIFY(failed); QVERIFY(p.canRedo()); QCOMPARE(p.revision(),std::uint64_t(4));
    }
    void destructionDoesNotCaptureOwner() {
        auto p=sample(); auto gate=std::make_shared<Gate>(); Release release{gate}; auto off=std::make_shared<std::atomic<bool>>(false);
        bool called=false;
        auto r=std::make_unique<CommandJobRunner>([&]() ->const Project& {return p;});
        auto ticket=r->submit(p.snapshot(),"destroy",work(gate,request(p),off),[&](auto,auto,PrepareResult){called=true;});
        QTRY_VERIFY(gate->started.available()); r.reset(); QVERIFY(ticket.token().cancelled());
        p.replace(sample().document()); gate->release.release(); QTRY_VERIFY(gate->finished.available()); QCoreApplication::processEvents();
        QVERIFY(!called); QVERIFY(!p.canUndo()); QCOMPARE(ticket.snapshot().country("A")->name,std::string("Alpha"));
    }
};
QTEST_GUILESS_MAIN(JobRunnerTests)
#include "job_runner_tests.moc"

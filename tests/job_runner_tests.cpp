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
    void geometryResultsAreTypedAndCompleteOnOwnerThread() {
        auto p=sample(); CommandJobRunner r([&]() ->const Project& {return p;});
        const auto before=p.snapshot();
        auto* owner=QThread::currentThread();
        auto off=std::make_shared<std::atomic<bool>>(false);
        bool completed=false;
        r.submitGeometry(p.snapshot(),"draft",[owner,off](const ProjectSnapshot& snapshot,const JobToken& token) ->GeometryJobResult {
            off->store(QThread::currentThread()!=owner);
            token.reportProgress(100);
            TerritorySelectionDraftResult result;
            Geometry geometry; geometry.polygons=snapshot.country("A")->polygons;
            result.candidates.push_back({snapshot.country("A")->id,std::move(geometry),1.0});
            return result;
        },[&](auto,auto status,GeometryJobResult result) {
            QCOMPARE(QThread::currentThread(),owner); QCOMPARE(status,JobDisposition::Accepted);
            const auto* draft=std::get_if<TerritorySelectionDraftResult>(&result); QVERIFY(draft);
            QCOMPARE(draft->candidates.size(),std::size_t(1)); QCOMPARE(draft->candidates.front().id,std::string("A"));
            completed=true;
        });
        QTRY_VERIFY(completed); QVERIFY(off->load()); QVERIFY(before.matches(p)); QVERIFY(!p.canUndo());

        bool derived=false,annex=false;
        r.submitGeometry(p.snapshot(),"derived",[](const ProjectSnapshot&,const JobToken&) ->GeometryJobResult {
            return TerritorySelectionDerivedResult{};
        },[&](auto,auto status,GeometryJobResult result) {
            QCOMPARE(status,JobDisposition::Accepted); derived=std::holds_alternative<TerritorySelectionDerivedResult>(result);
        });
        r.submitGeometry(p.snapshot(),"annex",[](const ProjectSnapshot&,const JobToken&) ->GeometryJobResult {
            return AnnexGeometryPreviewResult{};
        },[&](auto,auto status,GeometryJobResult result) {
            QCOMPARE(status,JobDisposition::Accepted); annex=std::holds_alternative<AnnexGeometryPreviewResult>(result);
        });
        QTRY_VERIFY(derived && annex); QVERIFY(before.matches(p)); QVERIFY(!p.canUndo());
    }
    void geometryAndCommandsShareLatestKeyAndPhysicalWorker() {
        auto p=sample(); CommandJobRunner r([&]() ->const Project& {return p;});
        auto gate=std::make_shared<Gate>(); Release release{gate};
        int coalesced=0,accepted=0; auto commandRuns=std::make_shared<std::atomic<int>>(0);
        const auto done=[&](auto,auto status,GeometryJobResult result) {
            if(status==JobDisposition::Coalesced) { ++coalesced; QVERIFY(std::holds_alternative<std::monostate>(result)); }
            else { QCOMPARE(status,JobDisposition::Accepted); ++accepted; QVERIFY(std::holds_alternative<TerritorySelectionDraftResult>(result)); }
        };
        const auto first=r.submitGeometry(p.snapshot(),"shared",[gate](const ProjectSnapshot&,const JobToken&) ->GeometryJobResult {
            gate->started.release(); gate->release.acquire(); gate->finished.release(); return TerritorySelectionDraftResult{};
        },done);
        QTRY_VERIFY(gate->started.available());
        r.submit(p.snapshot(),"shared",[commandRuns,req=request(p)](const ProjectSnapshot& snapshot,const JobToken&) {
            ++*commandRuns; return CommandProcessor::prepare(snapshot,req);
        },[&](auto,auto status,PrepareResult result) {
            QCOMPARE(status,JobDisposition::Coalesced); ++coalesced; QVERIFY(!result.preview);
        });
        r.submitGeometry(p.snapshot(),"shared",[](const ProjectSnapshot&,const JobToken&) ->GeometryJobResult {
            return TerritorySelectionDraftResult{};
        },done);
        QVERIFY(first.token().cancelled()); QCOMPARE(r.runningCount(),std::size_t(1)); QCOMPARE(r.queueDepth(),std::size_t(1));
        QTRY_COMPARE(coalesced,2); QCOMPARE(accepted,0); QCOMPARE(commandRuns->load(),0);
        gate->release.release(); QTRY_COMPARE(accepted,1); QCOMPARE(commandRuns->load(),0); QVERIFY(!p.canUndo());
    }
    void cancelledGeometryDiscardsLatePayloadAndUnblocksCommand() {
        auto p=sample(); CommandJobRunner r([&]() ->const Project& {return p;});
        auto gate=std::make_shared<Gate>(); Release release{gate}; int callbacks=0; bool commandDone=false;
        const auto ticket=r.submitGeometry(p.snapshot(),"cancel",[gate](const ProjectSnapshot&,const JobToken&) ->GeometryJobResult {
            gate->started.release(); gate->release.acquire(); gate->finished.release(); return AnnexGeometryPreviewResult{};
        },[&](auto,auto status,GeometryJobResult result) {
            ++callbacks; QCOMPARE(status,JobDisposition::Cancelled); QVERIFY(std::holds_alternative<std::monostate>(result));
        });
        QTRY_VERIFY(gate->started.available()); r.cancel(ticket.id()); QTRY_COMPARE(callbacks,1);
        r.submit(p.snapshot(),"next",[req=request(p)](const ProjectSnapshot& snapshot,const JobToken&) {
            return CommandProcessor::prepare(snapshot,req);
        },[&](auto,auto status,PrepareResult result) {
            QCOMPARE(status,JobDisposition::Accepted); QVERIFY(result.preview); commandDone=true;
        });
        QCOMPARE(r.runningCount(),std::size_t(1)); QVERIFY(!commandDone);
        gate->release.release(); QTRY_VERIFY(commandDone); QCOMPARE(callbacks,1); QVERIFY(!p.canUndo());
    }
    void staleGeometryAndExceptionsKeepCanonicalHistory() {
        auto p=sample(); QVERIFY(p.setMemo("A","redo")); QVERIFY(p.undo());
        CommandJobRunner r([&]() ->const Project& {return p;});
        auto gate=std::make_shared<Gate>(); Release release{gate}; bool stale=false;
        r.submitGeometry(p.snapshot(),"stale",[gate](const ProjectSnapshot&,const JobToken&) ->GeometryJobResult {
            gate->started.release(); gate->release.acquire(); return TerritorySelectionDerivedResult{};
        },[&](auto,auto status,GeometryJobResult result) {
            QCOMPARE(status,JobDisposition::Stale); QVERIFY(std::holds_alternative<std::monostate>(result)); stale=true;
        });
        QTRY_VERIFY(gate->started.available()); QVERIFY(p.redo()); gate->release.release(); QTRY_VERIFY(stale);
        QVERIFY(p.undo()); const auto before=p.snapshot(); bool failed=false,unknownFailed=false;
        r.submitGeometry(p.snapshot(),"throws",[](const ProjectSnapshot&,const JobToken&) ->GeometryJobResult {
            throw std::runtime_error("geometry worker failed");
        },[&](auto,auto status,GeometryJobResult result) {
            QCOMPARE(status,JobDisposition::Accepted); const auto* failure=std::get_if<GeometryJobFailure>(&result); QVERIFY(failure);
            QCOMPARE(failure->error,CommandError::PrepareFailed); QCOMPARE(failure->detail,std::string("geometry worker failed")); failed=true;
        });
        r.submitGeometry(p.snapshot(),"unknown",[](const ProjectSnapshot&,const JobToken&) ->GeometryJobResult {throw 7;},
            [&](auto,auto status,GeometryJobResult result) {
                QCOMPARE(status,JobDisposition::Accepted); const auto* failure=std::get_if<GeometryJobFailure>(&result); QVERIFY(failure);
                QCOMPARE(failure->error,CommandError::PrepareFailed); QVERIFY(!failure->detail.empty()); unknownFailed=true;
            });
        QTRY_VERIFY(failed && unknownFailed); QVERIFY(before.matches(p)); QVERIFY(p.canRedo());
    }
    void geometryDestructionDoesNotCaptureOwner() {
        auto p=sample(); auto gate=std::make_shared<Gate>(); Release release{gate}; bool called=false;
        auto r=std::make_unique<CommandJobRunner>([&]() ->const Project& {return p;});
        const auto ticket=r->submitGeometry(p.snapshot(),"destroy",[gate](const ProjectSnapshot& snapshot,const JobToken&) ->GeometryJobResult {
            gate->started.release(); gate->release.acquire();
            TerritorySelectionDraftResult result; result.candidates.push_back({snapshot.country("A")->id,{},std::nullopt});
            gate->finished.release(); return result;
        },[&](auto,auto,GeometryJobResult){called=true;});
        QTRY_VERIFY(gate->started.available()); r.reset(); QVERIFY(ticket.token().cancelled());
        p.replace(sample().document()); gate->release.release(); QTRY_VERIFY(gate->finished.available()); QCoreApplication::processEvents();
        QVERIFY(!called); QVERIFY(!p.canUndo()); QCOMPARE(ticket.snapshot().country("A")->name,std::string("Alpha"));
    }
    void geometryCompletionMayDestroyRunner() {
        auto p=sample(); auto r=std::make_unique<CommandJobRunner>([&]() ->const Project& {return p;}); bool completed=false;
        r->submitGeometry(p.snapshot(),"destroy-in-completion",[](const ProjectSnapshot&,const JobToken&) ->GeometryJobResult {
            return TerritorySelectionDerivedResult{};
        },[&](auto,auto status,GeometryJobResult result) {
            QCOMPARE(status,JobDisposition::Accepted); QVERIFY(std::holds_alternative<TerritorySelectionDerivedResult>(result));
            r.reset(); completed=true;
        });
        QTRY_VERIFY(completed); QVERIFY(!r); QCoreApplication::processEvents(); QVERIFY(!p.canUndo());
    }
};
QTEST_GUILESS_MAIN(JobRunnerTests)
#include "job_runner_tests.moc"

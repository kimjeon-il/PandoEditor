#include <pandoeditor/jobs.h>
#include <pandoeditor/project.h>
#include <future>
#include <iostream>
#include <stdexcept>
#include <functional>
#include <vector>
using namespace pandoeditor;
#define CHECK(x) do { if(!(x)) throw std::runtime_error(#x); } while(false)
Project sample() {
    Project p; p.replace(std::vector<Country>{{"A","Alpha",{{{{0,0},{2,0},{2,2},{0,2},{0,0}}}},0x123456}}); return p;
}
CommandRequest rename(const Project& p,const std::string& name) {
    CommandArguments a; a.properties.countries.push_back({"A",{name,"notes",0x102030,0.5,"countries"}});
    return CommandProcessor::makeRequest(p,"edit.properties",std::move(a));
}
void latestKey() {
    auto p=sample(); JobScheduler q;
    auto a=q.enqueue(p.snapshot(),"mesh:country:A"); auto first=q.takeNext(); CHECK(first && first->id()==a.id());
    auto b=q.enqueue(p.snapshot(),"mesh:country:A"); auto c=q.enqueue(p.snapshot(),"mesh:country:A");
    CHECK(a.token().reason()==JobStop::Coalesced); CHECK(b.token().reason()==JobStop::Coalesced);
    CHECK(q.runningCount()==1 && q.queueDepth()==1); CHECK(!q.takeNext());
    CHECK(q.finish(a,p)==JobDisposition::Coalesced); auto last=q.takeNext(); CHECK(last && last->id()==c.id());
    CHECK(q.finish(c,p)==JobDisposition::Accepted); CHECK(q.stats().coalesced==2); CHECK(q.stats().maxQueueDepth==1);
    CHECK(!p.dirty() && !p.canUndo() && p.revision()==0);
}
void revisionChanged() {
    auto p=sample(); JobScheduler q; auto a=q.enqueue(p.snapshot(),"validate:A"); CHECK(q.takeNext());
    auto result=CommandProcessor::prepare(a.snapshot(),rename(p,"candidate")); CHECK(result.preview);
    CHECK(p.setMemo("A","live")); CHECK(q.finish(a,p)==JobDisposition::Stale);
    CHECK(CommandProcessor::confirm(p,*result.preview).error==CommandError::StaleRevision);
    CHECK(p.country("A")->name=="Alpha" && p.country("A")->memo=="live" && p.revision()==1);
    CHECK(q.stats().staleDiscarded==1);
}
void queuedAbort() {
    auto p=sample(); JobScheduler q; auto a=q.enqueue(p.snapshot(),"active"); CHECK(q.takeNext());
    auto b=q.enqueue(p.snapshot(),"queued"); CHECK(q.cancel(b.id(),JobStop::Aborted));
    CHECK(b.token().reason()==JobStop::Aborted); CHECK(q.queueDepth()==0);
    CHECK(q.finish(a,p)==JobDisposition::Accepted); CHECK(!q.takeNext()); CHECK(q.stats().cancelled==1);
}
void priorityAndFifo() {
    auto p=sample(); JobScheduler q; auto a=q.enqueue(p.snapshot(),"active"); CHECK(q.takeNext());
    auto low=q.enqueue(p.snapshot(),"low",1); auto high=q.enqueue(p.snapshot(),"high",5); auto equal=q.enqueue(p.snapshot(),"equal",5);
    CHECK(q.finish(a,p)==JobDisposition::Accepted);
    for(const auto& t:{high,equal,low}) { auto next=q.takeNext(); CHECK(next && next->id()==t.id()); CHECK(q.finish(*next,p)==JobDisposition::Accepted); }
}
void lateCancelAndClose() {
    auto p=sample(); JobScheduler q; auto a=q.enqueue(p.snapshot(),"active"); CHECK(q.takeNext());
    auto r=CommandProcessor::prepare(a.snapshot(),rename(p,"never commit")); CHECK(r.preview);
    CHECK(q.cancel(a.id())); CHECK(!q.cancel(a.id())); CHECK(q.runningCount()==1);
    CHECK(q.finish(a,p)==JobDisposition::Cancelled); CHECK(q.finish(a,p)==JobDisposition::Unknown);
    auto b=q.enqueue(p.snapshot(),"b"); CHECK(q.takeNext()); auto c=q.enqueue(p.snapshot(),"c");
    q.close(); q.close(); CHECK(b.token().cancelled() && c.token().cancelled()); CHECK(q.queueDepth()==0);
    CHECK(q.finish(b,p)==JobDisposition::Closed); CHECK(!q.takeNext());
    bool rejected=false; try { q.enqueue(p.snapshot(),"closed"); } catch(const std::exception&) { rejected=true; } CHECK(rejected);
    CHECK(!p.dirty() && p.revision()==0 && !p.canUndo());
}
void undoRedoAndReopen() {
    auto p=sample(); const auto doc=p.document(); CHECK(p.setMemo("A","change"));
    JobScheduler q; auto a=q.enqueue(p.snapshot(),"a"); CHECK(q.takeNext()); CHECK(p.undo()); CHECK(p.redo());
    CHECK(q.finish(a,p)==JobDisposition::Stale);
    auto b=q.enqueue(p.snapshot(),"b"); CHECK(q.takeNext()); p.replace(doc); CHECK(q.finish(b,p)==JobDisposition::Stale);
    CHECK(p.revision()==0 && !p.dirty() && !p.canUndo());
}
void detachedSnapshotAndGeometry() {
    auto p=sample(); auto snapshot=p.snapshot(); auto request=rename(p,"worker result");
    auto geometry=snapshot.document().geometries.get(snapshot.document().units[0].geometry);
    const auto* oldName=&snapshot.country("A")->name;
    std::promise<void> start; auto gate=start.get_future();
    auto worker=std::async(std::launch::async,[snapshot,request,gate=std::move(gate)]() mutable { gate.wait(); return CommandProcessor::prepare(snapshot,request); });
    p.replace(sample().document()); start.set_value(); auto r=worker.get(); CHECK(r.preview);
    CHECK(snapshot.country("A")->name=="Alpha" && oldName==&snapshot.document().units[0].name);
    CHECK(r.preview->change().after().geometries.get(snapshot.document().units[0].geometry)==geometry);
    CHECK(CommandProcessor::confirm(p,*r.preview).error==CommandError::ProjectMismatch); CHECK(!p.canUndo());
}
void acceptedOneUndoAndNoOp() {
    auto p=sample(); auto req=rename(p,"worker"); JobScheduler q; auto a=q.enqueue(p.snapshot(),"apply"); CHECK(q.takeNext());
    auto r=CommandProcessor::prepare(a.snapshot(),req); CHECK(r.preview);
    CHECK(q.finish(a,p)==JobDisposition::Accepted); CHECK(CommandProcessor::confirm(p,*r.preview).changed());
    CHECK(p.revision()==1 && p.country("A")->memo=="notes"); CHECK(p.undo()); CHECK(!p.dirty() && !p.canUndo() && p.canRedo());
    CommandArguments args; auto no=CommandProcessor::makeRequest(p,"edit.properties",args);
    auto b=q.enqueue(p.snapshot(),"no-op"); CHECK(q.takeNext()); auto result=CommandProcessor::prepare(b.snapshot(),no);
    CHECK(q.finish(b,p)==JobDisposition::Accepted && result.status==CommandStatus::NoOp && !result.preview);
    CHECK(p.revision()==2 && p.canRedo());
}
void failuresAndRecheckAfterReady() {
    auto p=sample(); CHECK(p.setMemo("A","branch")); CHECK(p.undo()); auto before=p.snapshot();
    JobScheduler q; auto a=q.enqueue(before,"validation"); CHECK(q.takeNext()); CommandArguments args; args.action=RemoveLayer{"countries"};
    auto req=CommandProcessor::makeRequest(p,"layer.remove",args); auto r=CommandProcessor::prepare(before,req);
    CHECK(r.error==CommandError::ValidationFailed); CHECK(q.finish(a,p)==JobDisposition::Accepted); CHECK(p.canRedo() && p.revision()==2);
    auto b=q.enqueue(p.snapshot(),"ready"); CHECK(q.takeNext()); auto ready=CommandProcessor::prepare(b.snapshot(),rename(p,"ready")); CHECK(ready.preview);
    CHECK(q.finish(b,p)==JobDisposition::Accepted); CHECK(p.redo()); CHECK(CommandProcessor::confirm(p,*ready.preview).error==CommandError::StaleRevision);
    CHECK(p.country("A")->name=="Alpha");
}
void schedulerDestructionClosesToken() {
    auto p=sample(); std::optional<JobTicket> ticket;
    { JobScheduler q; ticket=q.enqueue(p.snapshot(),"lifetime"); CHECK(q.takeNext()); }
    CHECK(ticket->token().reason()==JobStop::Closed);
}
void cancellationProgressAndIsolation() {
    auto p=sample(); JobScheduler a,b; auto x=a.enqueue(p.snapshot(),"x"); auto y=b.enqueue(p.snapshot(),"x");
    CHECK(a.takeNext()); CHECK(b.takeNext()); CHECK(a.finish(y,p)==JobDisposition::Unknown); CHECK(a.runningCount()==1);
    x.token().reportProgress(40); x.token().reportProgress(20); CHECK(x.token().progress()==40);
    CHECK(a.cancelKey("x")); x.token().reportProgress(100); CHECK(x.token().progress()==40);
    CHECK(a.finish(x,p)==JobDisposition::Cancelled); CHECK(b.finish(y,p)==JobDisposition::Accepted);
}
int main() {
    const std::vector<std::pair<const char*,std::function<void()>>> tests={
        {"web latest-key",latestKey},{"web stale result",revisionChanged},{"web queued abort",queuedAbort},
        {"priority/FIFO",priorityAndFifo},{"cancel/close/one-shot",lateCancelAndClose},
        {"Undo/Redo/reopen stale",undoRedoAndReopen},{"worker snapshot lifetime",detachedSnapshotAndGeometry},
        {"single Undo/NoOp",acceptedOneUndoAndNoOp},{"failure/confirm recheck",failuresAndRecheckAfterReady},
        {"progress/foreign ticket",cancellationProgressAndIsolation},{"scheduler destruction",schedulerDestructionClosesToken}};
    int failures=0; for(const auto& [name,run]:tests) { try { run(); std::cout<<"PASS "<<name<<'\n'; } catch(const std::exception& e) { ++failures; std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n'; } }
    return failures?1:0;
}

#include <pandoeditor/project.h>
#include <pandoeditor/timeline-resolver.h>
#include <iostream>

using namespace pandoeditor;

int main() {
    int failures=0;
    const auto check=[&](bool okay,const char* detail){if(!okay){++failures;std::cerr<<detail<<'\n';}};
    ProjectDocument document({
        {"A","Alpha",{{{{0,0},{10,0},{10,10},{0,10},{0,0}}}},0x336699},
        {"B","Beta",{{{{2,2},{5,2},{5,5},{2,5},{2,2}}}},0x993366}},
        {{"countries","Countries"}});
    auto old=staticGeometryBinding(document,"A");
    auto second=*document.geometries.get(old.geometryRef);
    second.polygons.front().front()[1].x=11;
    const GeometryRef secondRef{old.geometryRef.id,2};
    document.geometries.insert(secondRef,second);
    staticGeometryBinding(document,"A").validity.to="1914-06";
    document.timelineRecords.geometryBindings.push_back({"A:1914-07","A",{{"1914-07"},{}},secondRef});
    Project project;project.replace(document);project.setTimelineCursor("1915-01");
    const auto baseline=project.revision();
    auto changed=second;changed.polygons.front().front()[1].x=12;
    CommandArguments geometryArgs;geometryArgs.action=TimelineGeometryEdit{"A","1915-01",changed};
    auto geometryRequest=CommandProcessor::makeRequest(project,"timeline.geometry",geometryArgs);
    auto geometry=CommandProcessor::prepare(project,geometryRequest);
    check(geometry.status==CommandStatus::Prepared&&geometry.preview.has_value(),"dated geometry prepare");
    if(!geometry.preview)return 1;
    check(CommandProcessor::confirm(project,*geometry.preview).changed(),"dated geometry confirm");
    check(project.revision()==baseline+1&&project.canUndo()&&project.dirty(),"dated geometry history/dirty");
    check(project.resolveWorld("1914-07").find("A")->geometryRef==secondRef,"past geometry preserved");
    check(project.resolveWorld("1915-01").find("A")->geometryRef.version==3,"new geometry version at cursor");
    check(project.snapshotForView().document().geometries.get(
        staticGeometryBinding(project.snapshotForView().document(),"A").geometryRef)->polygons.front().front()[1].x==12,
        "resolved geometry view updated");
    check(project.undo()&&project.resolveWorld("1915-01").find("A")->geometryRef==secondRef,
        "dated geometry undo");
    check(project.redo()&&project.resolveWorld("1915-01").find("A")->geometryRef.version==3,
        "dated geometry redo");
    project.markSaved();
    auto noOp=CommandProcessor::prepare(project,CommandProcessor::makeRequest(project,"timeline.geometry",geometryArgs));
    check(noOp.status==CommandStatus::NoOp&&!noOp.preview&&!project.dirty(),"dated geometry no-op");

    CommandArguments parentArgs;parentArgs.action=TimelineParentEdit{"B","1915-01","A","partition"};
    auto parent=CommandProcessor::prepare(project,CommandProcessor::makeRequest(project,"timeline.parent",parentArgs));
    check(parent.status==CommandStatus::Prepared&&parent.preview.has_value(),"dated parent prepare");
    if(!parent.preview)return 1;
    check(CommandProcessor::confirm(project,*parent.preview).changed(),"dated parent confirm");
    check(project.resolveWorld("1914-12").find("B")->parentId.empty()&&
          project.resolveWorld("1915-01").find("B")->parentId=="A","dated parent split");
    CommandArguments cycleArgs;cycleArgs.action=TimelineParentEdit{"A","1915-01","B","partition"};
    auto cycle=CommandProcessor::prepare(project,CommandProcessor::makeRequest(project,"timeline.parent",cycleArgs));
    check(cycle.status==CommandStatus::Rejected&&cycle.error==CommandError::ValidationFailed,
        "dated parent cycle rejected");
    project.undo();
    check(project.resolveWorld("1915-01").find("B")->parentId.empty(),"dated parent undo");
    project.redo();
    check(project.resolveWorld("1915-01").find("B")->parentId=="A","dated parent redo");
    auto stale=CommandProcessor::prepare(project,CommandProcessor::makeRequest(project,"timeline.geometry",geometryArgs));
    check(stale.status==CommandStatus::NoOp,"same geometry is no-op before month switch");
    auto pendingArgs=geometryArgs;std::get<TimelineGeometryEdit>(pendingArgs.action).geometry=second;
    auto pending=CommandProcessor::prepare(project,CommandProcessor::makeRequest(project,"timeline.geometry",pendingArgs));
    check(pending.preview.has_value(),"prepare pending geometry");
    project.setTimelineCursor("1915-02");
    if(pending.preview)check(CommandProcessor::confirm(project,*pending.preview).error==CommandError::StaleRevision,
        "month switch rejects stale preview");
    CreateTerritorialIntent creation;creation.kind=UnitKind::General;creation.id="C";creation.name="Gamma";
    creation.geometry=*document.geometries.get(staticGeometryBinding(document,"B").geometryRef);
    creation.parent=territorialRef("A");
    CommandArguments createArgs;createArgs.action=TimelineCreate{creation,"1915-02"};
    auto created=CommandProcessor::prepare(project,CommandProcessor::makeRequest(project,"timeline.create",createArgs));
    check(created.status==CommandStatus::Prepared&&created.preview.has_value(),"dated creation prepare");
    if(created.preview)check(CommandProcessor::confirm(project,*created.preview).changed(),"dated creation confirm");
    check(project.resolveWorld("1915-01").find("C")==nullptr&&
          project.resolveWorld("1915-02").find("C")!=nullptr,"dated creation lifetime");
    auto duplicate=CommandProcessor::prepare(project,CommandProcessor::makeRequest(project,"timeline.create",createArgs));
    check(duplicate.status==CommandStatus::Rejected,"dated creation duplicate rejected");
    project.setTimelineCursor("1916-01");
    CommandArguments deleteArgs;deleteArgs.action=TimelineDelete{{territorialRef("C")},"1916-01"};
    auto deleted=CommandProcessor::prepare(project,CommandProcessor::makeRequest(project,"timeline.delete",deleteArgs));
    check(deleted.status==CommandStatus::Prepared&&deleted.preview.has_value(),"dated deletion prepare");
    if(deleted.preview)check(CommandProcessor::confirm(project,*deleted.preview).changed(),"dated deletion confirm");
    check(project.resolveWorld("1915-12").find("C")!=nullptr&&
          project.resolveWorld("1916-01").find("C")==nullptr,"dated deletion preserves past");
    check(project.undo()&&project.resolveWorld("1916-01").find("C")!=nullptr,"dated deletion undo");
    check(project.redo()&&project.resolveWorld("1916-01").find("C")==nullptr,"dated deletion redo");
    project.setTimelineCursor("1916-02");
    auto planned=CommandProcessor::planTerritorial(project.snapshotForView(),
        DeleteTerritorialIntent{{territorialRef("B")}});
    check(planned.ok()&&planned.plan.has_value(),"projected territorial plan");
    if(planned.plan) {
        CommandArguments mutationArgs;
        mutationArgs.action=TimelineTerritorialMutation{
            ApplyTerritorialMutation{*planned.plan,std::nullopt},"1916-02"};
        auto mutation=CommandProcessor::prepare(project,
            CommandProcessor::makeRequest(project,"timeline.territorial",mutationArgs));
        check(mutation.status==CommandStatus::Prepared&&mutation.preview.has_value(),
            "dated territorial mutation prepare");
        if(mutation.preview)check(CommandProcessor::confirm(project,*mutation.preview).changed(),
            "dated territorial mutation confirm");
        check(project.resolveWorld("1916-01").find("B")!=nullptr&&
              project.resolveWorld("1916-02").find("B")==nullptr,
              "dated territorial mutation preserves earlier month");
    }
    check(!failures,"timeline commands");
    return failures?1:0;
}

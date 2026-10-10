#include <pandoeditor/timeline-resolver.h>
#include <pandoeditor/timeline-edit.h>
#include <pandoeditor/project.h>
#include <iostream>

using namespace pandoeditor;

int main() {
    TimelineRecords records;
    records.lifetimes={{"life:A","A",{}},{"life:B","B",{{"1910-01-02"},{"1920-03"}}}};
    records.geometryBindings={{"A:old","A",{{},{"1914-06"}},{"shape",1}},
        {"A:new","A",{{"1914-07"},{}},{"shape",2}},
        {"B:shape","B",{{"1910-01-02"},{"1920-03"}},{"shape",1}}};
    records.parentRelations={{"parent:A","A",{},"","explicit"},
        {"parent:B","B",{{"1910-01-02"},{"1920-03"}},"A","partition"}};
    const TimelineValidationContext context{{{"A","general"},{"B","general"}},
        [](const GeometryRef& ref){return ref.id=="shape"&&(ref.version==1||ref.version==2);}};
    const auto june=resolveWorld(records,context,"1914-06");
    const auto july=resolveWorld(records,context,"1914-07");
    const auto before=resolveWorld(records,context,"1909-12");
    const auto januaryStart=resolveWorld(records,context,"1910-01");
    auto endedMidMonth=records;
    endedMidMonth.lifetimes.at(1).validity.to="1910-01-15";
    endedMidMonth.geometryBindings.at(2).validity.to="1910-01-15";
    endedMidMonth.parentRelations.at(1).validity.to="1910-01-15";
    const auto januaryAfterEnd=resolveWorld(endedMidMonth,context,"1910-01");
    auto exactEndpointRecords=records;
    exactEndpointRecords.geometryBindings.at(1).validity.to="1915-12-24";
    const auto exactEndpointChanged=replaceGeometryBindingAtMonth(exactEndpointRecords,"A","1915-01",{"shape",3});
    const auto changed=replaceGeometryBindingAtMonth(records,"A","1914-06",{"shape",3});
    const auto reparented=replaceParentRelationAtMonth(records,"B","1914-07","","explicit");
    const auto ended=truncateTimelineEntityAtMonth(records,"A","1914-07");
    const auto initial=initialTimelineMonth(records,"2026-10");
    Project project;
    const auto emptyProjectWorld=project.resolveWorld("1914-07");
    const auto originalRevision=project.revision();
    const bool cursorMoved=project.setTimelineCursor("1914-07");
    const bool cursorNoOp=!project.setTimelineCursor("1914-07");
    const bool good=july.month=="1914-07"&&june.entities.size()==2&&june.entities.at(0).geometryRef.version==1
        &&july.entities.at(0).geometryRef.version==2&&july.entities.at(1).parentId=="A"
        &&july.entities.at(1).rootId=="A"&&july.entities.at(1).ancestors==std::vector<std::string>{"A"}
        &&before.entities.size()==1&&januaryStart.entities.size()==2&&januaryAfterEnd.entities.size()==1
        &&exactEndpointChanged.geometryBindings.at(1).validity.to=="1914-12"
        &&exactEndpointChanged.geometryBindings.at(2).validity.to=="1915-12-24"
        &&changed.geometryBindings.size()==4
        &&changed.geometryBindings.front().validity.to=="1914-05"
        &&changed.geometryBindings.at(1).validity.from=="1914-06"
        &&changed.geometryBindings.at(1).geometryRef.version==3
        &&records.geometryBindings.front().geometryRef.version==1&&initial=="1914-07"
        &&reparented.parentRelations.size()==3&&reparented.parentRelations.at(1).validity.to=="1914-06"
        &&reparented.parentRelations.at(2).parentId.empty()
        &&ended.lifetimes.front().validity.to=="1914-06"
        &&ended.geometryBindings.size()==2
        &&ended.geometryBindings.front().validity.to=="1914-06"
        &&emptyProjectWorld.entities.empty()
        &&cursorMoved&&cursorNoOp&&project.timelineCursor()=="1914-07"
        &&project.revision()==originalRevision&&!project.dirty()&&!project.canUndo();
    if(!good)std::cerr<<"Timeline month resolution mismatch\n";
    return good?0:1;
}
